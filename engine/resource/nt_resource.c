#include "resource/nt_resource.h"

#include "resource/nt_resource_internal.h"

#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "core/nt_assert.h"
#include "fs/nt_fs.h"
#include "hash/nt_hash.h"
#include "http/nt_http.h"
#include "log/nt_log.h"
#include "nt_blob_format.h"
#include "nt_crc32.h"
#include "nt_pack_format.h"
#include "time/nt_time.h"

/* ---- Slot map: resource_id -> slot index, open-addressing hash table ---- */

#define NT_SLOT_MAP_SIZE (NT_RESOURCE_MAX_SLOTS * 2)
#define NT_RESOURCE_MAX_RESOLVE_PASSES 4U

/* ---- Module state ---- */

static struct {
    NtPackMeta packs[NT_RESOURCE_MAX_PACKS];
    NtAssetMeta assets[NT_RESOURCE_MAX_ASSETS];
    NtResourceSlot slots[NT_RESOURCE_MAX_SLOTS + 1]; /* index 0 reserved */
    NtActivatorEntry activators[NT_RESOURCE_MAX_ASSET_TYPES];
    uint16_t free_queue[NT_RESOURCE_MAX_SLOTS];
    uint16_t slot_map[NT_SLOT_MAP_SIZE];
    uint16_t queue_top;
    uint16_t next_mount_seq;       /* monotonic counter for mount order tiebreak */
    uint32_t asset_hwm;            /* high-water mark in assets[] */
    uint64_t placeholder_texture;  /* resource_id for fallback texture, 0 = none */
    float activate_time_budget_ms; /* per-step time limit, 0 = unlimited */
    /* Retry policy (global) */
    uint32_t retry_max_attempts; /* 0 = infinite */
    uint32_t retry_base_delay_ms;
    uint32_t retry_max_delay_ms;
    bool needs_resolve;
    bool initialized;
} s_resource;

/* ---- Pack lookup ---- */

static int16_t find_pack(uint32_t pack_id) {
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 1 && s_resource.packs[i].pack_id == pack_id) {
            return (int16_t)i;
        }
    }
    return -1;
}

/* ---- Slot map helpers ---- */

static uint16_t slot_map_find(uint64_t resource_id) {
    uint32_t idx = (uint32_t)(resource_id % (uint64_t)NT_SLOT_MAP_SIZE);
    for (uint32_t i = 0; i < NT_SLOT_MAP_SIZE; i++) {
        uint32_t probe = (idx + i) % NT_SLOT_MAP_SIZE;
        uint16_t si = s_resource.slot_map[probe];
        if (si == 0) {
            return 0;
        }
        if (s_resource.slots[si].resource_id == resource_id) {
            return si;
        }
    }
    return 0;
}

static void slot_map_insert(uint64_t resource_id, uint16_t slot_index) {
    uint32_t idx = (uint32_t)(resource_id % (uint64_t)NT_SLOT_MAP_SIZE);
    for (uint32_t i = 0; i < NT_SLOT_MAP_SIZE; i++) {
        uint32_t probe = (idx + i) % NT_SLOT_MAP_SIZE;
        if (s_resource.slot_map[probe] == 0) {
            s_resource.slot_map[probe] = slot_index;
            return;
        }
    }
}

/* ---- Asset slot allocation (reuses holes before appending) ---- */

static uint32_t asset_alloc(void) {
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == 0) {
            return i;
        }
    }
    if (s_resource.asset_hwm >= NT_RESOURCE_MAX_ASSETS) {
        return UINT32_MAX;
    }
    return s_resource.asset_hwm++;
}

/* ---- Time helper ---- */

static uint32_t resource_get_time_ms(void) { return (uint32_t)(nt_time_now() * 1000.0); }

static inline nt_resource_t resource_make(uint16_t index, uint16_t gen) { return (nt_resource_t){.id = ((uint32_t)gen << 16) | index}; }

static bool slot_user_data_synced_for(const NtResourceSlot *slot, uint16_t asset_idx) { return slot->user_data != NULL && slot->user_data_asset_idx == asset_idx; }

static bool asset_blob_resident(const NtAssetMeta *meta) {
    NT_ASSERT(meta->pack_index < NT_RESOURCE_MAX_PACKS);
    const NtPackMeta *pack = &s_resource.packs[meta->pack_index];
    return pack->pack_type == NT_PACK_VIRTUAL || pack->blob != NULL;
}

static uint32_t asset_effective_runtime_handle(uint32_t asset_index, const NtAssetMeta *meta) { return (meta->asset_type == NT_ASSET_BLOB) ? asset_index : meta->runtime_handle; }

static bool asset_is_publishable(const NtResourceSlot *slot, const NtAssetMeta *meta, uint16_t asset_index, uint8_t behavior_flags) {
    if ((behavior_flags & NT_RESOURCE_BEHAVIOR_PUBLISH_REQUIRES_AUX) == 0) {
        return true;
    }
    return slot_user_data_synced_for(slot, asset_index) || asset_blob_resident(meta);
}

static const uint8_t *asset_data_ptr(const NtAssetMeta *meta, uint32_t *out_size) {
    const NtPackMeta *pack = &s_resource.packs[meta->pack_index];
    if (pack->blob == NULL) {
        if (out_size != NULL) {
            *out_size = 0;
        }
        return NULL;
    }
    if (out_size != NULL) {
        *out_size = meta->size;
    }
    return pack->blob + meta->offset;
}

static void schedule_pack_redownload_if_needed(NtPackMeta *pack) {
    if (!pack->mounted || pack->pack_type == NT_PACK_VIRTUAL || pack->io_type == NT_IO_NONE) {
        return;
    }
    if (pack->blob != NULL || pack->pack_state != NT_PACK_STATE_READY) {
        return;
    }
    pack->pack_state = NT_PACK_STATE_NONE;
    pack->retry_delay_ms = 0;
    pack->retry_time_ms = 1; /* non-zero: retry logic re-issues I/O on next step */
    pack->attempt_count = 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static void resource_resolve_pass(void) {
    /* D.1: Prepare per-pass target/publishable candidates */
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->resource_id == 0) {
            continue;
        }
        slot->prev_resolve_asset_idx = slot->resolve_asset_idx;
        slot->prev_runtime_handle = slot->runtime_handle;
        slot->target_runtime_handle = 0;
        slot->candidate_runtime_handle = 0;
        slot->target_prio = INT16_MIN;
        slot->candidate_prio = INT16_MIN;
        slot->target_seq = 0;
        slot->candidate_seq = 0;
        slot->target_asset_idx = UINT16_MAX;
        slot->candidate_asset_idx = UINT16_MAX;
        slot->scan_state = NT_ASSET_STATE_REGISTERED;
        slot->resolve_pending = 0;
        slot->post_resolve_pending = 0;
    }

    /* D.2: Single pass over assets -- O(A) via slot_map lookup */
    for (uint32_t ai = 0; ai < s_resource.asset_hwm; ai++) {
        NtAssetMeta *meta = &s_resource.assets[ai];
        if (meta->resource_id == 0) {
            continue;
        }
        if (meta->pack_index >= NT_RESOURCE_MAX_PACKS || s_resource.packs[meta->pack_index].mounted == 0) {
            continue;
        }

        uint16_t si = slot_map_find(meta->resource_id);
        if (si == 0) {
            continue;
        }

        NtResourceSlot *slot = &s_resource.slots[si];

        /* Skip type mismatch (malformed pack or hash collision) */
        if (meta->asset_type != slot->asset_type) {
            continue;
        }

        /* Track best available state from any matching entry */
        if (meta->state > slot->scan_state && slot->scan_state != NT_ASSET_STATE_READY) {
            slot->scan_state = meta->state;
        }

        if (meta->state != NT_ASSET_STATE_READY) {
            continue;
        }

        int16_t prio = s_resource.packs[meta->pack_index].priority;
        uint16_t seq = s_resource.packs[meta->pack_index].mount_seq;
        uint32_t runtime_handle = asset_effective_runtime_handle(ai, meta);
        uint8_t behavior_flags = s_resource.activators[slot->asset_type].behavior_flags;

        /* Target winner: highest-priority READY asset, even if it is not yet publishable. */
        if (prio > slot->target_prio || (prio == slot->target_prio && seq >= slot->target_seq)) {
            slot->target_runtime_handle = runtime_handle;
            slot->target_prio = prio;
            slot->target_seq = seq;
            NT_ASSERT(ai <= UINT16_MAX && "asset index exceeds uint16 -- raise resolve_asset_idx to uint32");
            slot->target_asset_idx = (uint16_t)ai;
        }

        /* Published winner: highest-priority READY asset that is usable now. */
        if (asset_is_publishable(slot, meta, (uint16_t)ai, behavior_flags) && (prio > slot->candidate_prio || (prio == slot->candidate_prio && seq >= slot->candidate_seq))) {
            slot->candidate_runtime_handle = runtime_handle;
            slot->candidate_prio = prio;
            slot->candidate_seq = seq;
            NT_ASSERT(ai <= UINT16_MAX && "asset index exceeds uint16 -- raise resolve_asset_idx to uint32");
            slot->candidate_asset_idx = (uint16_t)ai;
        }
    }

    /* D.3: Texture placeholder fallback -- publish placeholder only if no READY asset exists */
    if (s_resource.placeholder_texture != 0) {
        uint16_t ph_si = slot_map_find(s_resource.placeholder_texture);
        uint32_t ph_handle = 0;
        if (ph_si != 0) {
            const NtResourceSlot *ph_slot = &s_resource.slots[ph_si];
            ph_handle = (ph_slot->candidate_asset_idx < s_resource.asset_hwm || ph_slot->candidate_runtime_handle != 0) ? ph_slot->candidate_runtime_handle : ph_slot->runtime_handle;
        }

        if (ph_handle != 0) {
            for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
                NtResourceSlot *slot = &s_resource.slots[si];
                if (slot->resource_id == 0) {
                    continue;
                }
                if (slot->scan_state == NT_ASSET_STATE_READY) {
                    continue;
                }
                if (slot->asset_type == NT_ASSET_TEXTURE) {
                    slot->candidate_runtime_handle = ph_handle;
                }
            }
        }
    }

    // #region D.4: Publish the best usable winner and run resolve/cleanup hooks
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->resource_id == 0) {
            continue;
        }

        uint8_t atype = slot->asset_type;
        NtActivatorEntry *entry = &s_resource.activators[atype];
        uint8_t behavior_flags = entry->behavior_flags;
        const bool requires_aux = (behavior_flags & NT_RESOURCE_BEHAVIOR_PUBLISH_REQUIRES_AUX) != 0;
        const bool auto_reload_on_aux_miss = (behavior_flags & NT_RESOURCE_BEHAVIOR_AUTO_RELOAD_ON_AUX_MISS) != 0;

        NT_ASSERT(!requires_aux || (entry->on_resolve != NULL && entry->on_cleanup != NULL));

        const bool next_has_real_winner = slot->candidate_asset_idx < s_resource.asset_hwm;
        const uint16_t next_asset_idx = slot->candidate_asset_idx;
        const uint32_t next_handle = slot->candidate_runtime_handle;
        const bool next_changed = next_has_real_winner && (next_asset_idx != slot->prev_resolve_asset_idx || next_handle != slot->prev_runtime_handle);
        const bool needs_aux_sync = next_has_real_winner && requires_aux && !slot_user_data_synced_for(slot, next_asset_idx);

        bool target_missing_aux = false;
        if (requires_aux && slot->target_asset_idx < s_resource.asset_hwm) {
            const NtAssetMeta *target = &s_resource.assets[slot->target_asset_idx];
            target_missing_aux = !slot_user_data_synced_for(slot, slot->target_asset_idx) && !asset_blob_resident(target);
            if (target_missing_aux && auto_reload_on_aux_miss) {
                schedule_pack_redownload_if_needed(&s_resource.packs[target->pack_index]);
            }
        }

        if (next_has_real_winner && entry->on_resolve != NULL && (next_changed || needs_aux_sync)) {
            const NtAssetMeta *winner = &s_resource.assets[next_asset_idx];
            uint32_t size = 0;
            const uint8_t *data = asset_data_ptr(winner, &size);
            entry->on_resolve(data, size, next_handle, &slot->user_data);
            slot->resolve_pending = 1;
            slot->post_resolve_pending = 1;
            if (requires_aux) {
                NT_ASSERT(slot->user_data != NULL && "aux-backed asset must populate user_data before publication");
                slot->user_data_asset_idx = next_asset_idx;
            }
        } else if (next_has_real_winner && (next_changed || needs_aux_sync)) {
            slot->post_resolve_pending = 1;
        }

        if (!next_has_real_winner && slot->user_data != NULL) {
            if (entry->on_cleanup != NULL) {
                entry->on_cleanup(slot->user_data);
            }
            slot->user_data = NULL;
            slot->user_data_asset_idx = UINT16_MAX;
        }

        slot->resolve_asset_idx = next_has_real_winner ? next_asset_idx : UINT16_MAX;
        slot->runtime_handle = next_handle;
        slot->resolve_prio = (int16_t)(next_has_real_winner ? slot->candidate_prio : INT16_MIN);
        slot->resolve_seq = next_has_real_winner ? slot->candidate_seq : 0;
        if (next_has_real_winner) {
            slot->state = NT_ASSET_STATE_READY;
        } else if (target_missing_aux && next_handle == 0) {
            slot->state = NT_ASSET_STATE_LOADING;
        } else {
            slot->state = slot->scan_state;
        }
    }
    // #endregion

    // #region D.5: Fire on_post_resolve callbacks after the resolve iteration
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->resource_id == 0 || !slot->post_resolve_pending || slot->resolve_asset_idx >= s_resource.asset_hwm) {
            continue;
        }

        uint8_t atype = slot->asset_type;
        if (atype >= NT_RESOURCE_MAX_ASSET_TYPES || s_resource.activators[atype].on_post_resolve == NULL) {
            continue;
        }

        NtAssetMeta *winner = &s_resource.assets[slot->resolve_asset_idx];
        uint32_t size = 0;
        const uint8_t *data = asset_data_ptr(winner, &size);
        s_resource.activators[atype].on_post_resolve(data, size, resource_make(si, slot->generation), slot->runtime_handle, slot->user_data);
    }
    // #endregion
}

/* ---- I/O issue helper (shared by load + retry) ---- */

static uint32_t resource_io_issue(const char *path, uint8_t io_type) {
    if (io_type == NT_IO_HTTP) {
        return nt_http_request(path).id;
    }
    if (io_type == NT_IO_FS) {
        return nt_fs_read_file(path).id;
    }
    return 0;
}

/* ---- Load failure / retry handler ---- */

static void resource_handle_load_failure(NtPackMeta *pack) {
    /* Check retry policy */
    if (s_resource.retry_max_attempts == 1 || (s_resource.retry_max_attempts > 1 && pack->attempt_count >= s_resource.retry_max_attempts)) {
        /* Max attempts reached -- FAILED permanently */
        pack->pack_state = NT_PACK_STATE_FAILED;
        NT_LOG_ERROR("pack 0x%08X FAILED after %d attempts", pack->pack_id, pack->attempt_count);
        return;
    }
    /* Compute backoff delay */
    if (pack->retry_delay_ms == 0) {
        pack->retry_delay_ms = s_resource.retry_base_delay_ms;
    } else {
        pack->retry_delay_ms = (uint32_t)((float)pack->retry_delay_ms * 1.5F);
        if (pack->retry_delay_ms > s_resource.retry_max_delay_ms) {
            pack->retry_delay_ms = s_resource.retry_max_delay_ms;
        }
    }
    pack->retry_time_ms = resource_get_time_ms() + pack->retry_delay_ms;
    pack->pack_state = NT_PACK_STATE_NONE; /* will be retried */
    /* NOTE: io_type is NOT cleared -- preserved for retry re-issue */
    NT_LOG_INFO("pack 0x%08X attempt %d, retry in %ums", pack->pack_id, pack->attempt_count, pack->retry_delay_ms);
}

/* ---- Activation time helpers ---- */

/* ---- Lifecycle ---- */

nt_result_t nt_resource_init(const nt_resource_desc_t *desc) {
    (void)desc;

    if (s_resource.initialized) {
        return NT_ERR_INIT_FAILED;
    }

    memset(&s_resource, 0, sizeof(s_resource));

    /* Fill free queue: stack with lowest index on top (first alloc gets 1) */
    s_resource.queue_top = NT_RESOURCE_MAX_SLOTS;
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_SLOTS; i++) {
        s_resource.free_queue[i] = (uint16_t)(NT_RESOURCE_MAX_SLOTS - i); /* top has lowest */
    }

    s_resource.activate_time_budget_ms = NT_RESOURCE_ACTIVATE_TIME_BUDGET_MS;
    s_resource.retry_max_attempts = 0; /* infinite by default */
    s_resource.retry_base_delay_ms = 500;
    s_resource.retry_max_delay_ms = 10000;

    s_resource.initialized = true;
    return NT_OK;
}

void nt_resource_shutdown(void) {
    /* Unmount all packs: deactivates GPU resources, frees owned blobs, cancels I/O */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted) {
            nt_resource_unmount((nt_hash32_t){s_resource.packs[i].pack_id});
        }
    }
    // #region Cleanup remaining user_data (unmount doesn't trigger resolve)
    for (uint16_t si = 1; si <= NT_RESOURCE_MAX_SLOTS; si++) {
        NtResourceSlot *slot = &s_resource.slots[si];
        if (slot->user_data != NULL) {
            uint8_t atype = slot->asset_type;
            if (atype < NT_RESOURCE_MAX_ASSET_TYPES && s_resource.activators[atype].on_cleanup) {
                s_resource.activators[atype].on_cleanup(slot->user_data);
            }
            slot->user_data = NULL;
        }
    }
    // #endregion
    memset(&s_resource, 0, sizeof(s_resource));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_resource_step(void) {
    if (!s_resource.initialized) {
        return;
    }

    /* ===================================================
     *  Phase A: Poll I/O for loading packs + retry
     * =================================================== */
    for (uint16_t pi = 0; pi < NT_RESOURCE_MAX_PACKS; pi++) {
        NtPackMeta *pack = &s_resource.packs[pi];
        if (!pack->mounted) {
            continue;
        }

        /* Retry check: packs in NONE state with pending retry */
        if (pack->pack_state == NT_PACK_STATE_NONE && pack->retry_time_ms > 0) {
            uint32_t now = resource_get_time_ms();
            if (now >= pack->retry_time_ms) {
                pack->retry_time_ms = 0;
                pack->attempt_count++;
                pack->io_request_id = resource_io_issue(pack->load_path, pack->io_type);
                pack->pack_state = (pack->io_request_id != 0) ? NT_PACK_STATE_REQUESTED : NT_PACK_STATE_FAILED;
            }
        }

        if (pack->pack_state == NT_PACK_STATE_REQUESTED || pack->pack_state == NT_PACK_STATE_DOWNLOADING) {
            uint8_t *loaded_blob = NULL;
            uint32_t loaded_size = 0;
            bool io_done = false;
            bool io_failed = false;

            if (pack->io_type == NT_IO_HTTP) {
                nt_http_request_t req = {.id = pack->io_request_id};
                nt_http_state_t st = nt_http_state(req);
                if (st == NT_HTTP_STATE_DOWNLOADING) {
                    pack->pack_state = NT_PACK_STATE_DOWNLOADING;
                    nt_http_progress(req, &pack->bytes_received, &pack->bytes_total);
                } else if (st == NT_HTTP_STATE_DONE) {
                    loaded_blob = nt_http_take_data(req, &loaded_size);
                    nt_http_free(req);
                    pack->io_request_id = 0;
                    io_done = true;
                } else if (st == NT_HTTP_STATE_FAILED) {
                    nt_http_free(req);
                    pack->io_request_id = 0;
                    io_failed = true;
                }
            } else if (pack->io_type == NT_IO_FS) {
                nt_fs_request_t req = {.id = pack->io_request_id};
                nt_fs_state_t st = nt_fs_state(req);
                if (st == NT_FS_STATE_DONE) {
                    loaded_blob = nt_fs_take_data(req, &loaded_size);
                    nt_fs_free(req);
                    pack->io_request_id = 0;
                    io_done = true;
                } else if (st == NT_FS_STATE_FAILED) {
                    nt_fs_free(req);
                    pack->io_request_id = 0;
                    io_failed = true;
                }
            }

            if (io_done && loaded_blob != NULL) {
                NT_LOG_INFO("pack 0x%08X loaded (%u bytes)", pack->pack_id, loaded_size);

                /* Check if asset entries already exist (re-download after blob eviction).
                 * If so, skip parse_pack -- entries have correct offsets/sizes already.
                 * Just restore the blob pointer and let the activation loop re-activate. */
                bool has_existing_assets = false;
                for (uint32_t ai = 0; ai < s_resource.asset_hwm; ai++) {
                    if (s_resource.assets[ai].pack_index == pi && s_resource.assets[ai].resource_id != 0) {
                        has_existing_assets = true;
                        break;
                    }
                }

                if (has_existing_assets) {
                    /* Re-download after blob eviction: restore blob, skip re-parse.
                     * Assumes pack content is immutable -- same URL/path always returns
                     * identical data. If hot-update is ever needed, validate CRC32 here
                     * and fall through to full re-parse on mismatch. */
                    NT_ASSERT(loaded_size == pack->blob_size);
                    pack->blob = loaded_blob;
                    pack->blob_size = loaded_size;
                    pack->pack_state = NT_PACK_STATE_READY;
                    pack->blob_last_access_ms = resource_get_time_ms();
                    s_resource.needs_resolve = true;
                } else {
                    /* First load: full parse */
                    nt_result_t res = nt_resource_parse_pack((nt_hash32_t){pack->pack_id}, loaded_blob, loaded_size);
                    if (res == NT_OK) {
                        pack->pack_state = NT_PACK_STATE_READY;
                        pack->blob_last_access_ms = resource_get_time_ms();
                    } else {
                        free(loaded_blob);
                        pack->pack_state = NT_PACK_STATE_FAILED;
                        NT_LOG_ERROR("pack 0x%08X parse failed", pack->pack_id);
                    }
                }
            } else if (io_failed) {
                resource_handle_load_failure(pack);
            }
        }
    }

    /* ===================================================
     *  Phase B: Activate assets within time budget
     * =================================================== */
    {
        double t_start = nt_time_now();
        float budget_ms = s_resource.activate_time_budget_ms;
        bool activated_any = false;
        uint32_t activated_count = 0;

        for (uint16_t pi = 0; pi < NT_RESOURCE_MAX_PACKS; pi++) {
            NtPackMeta *pack = &s_resource.packs[pi];
            if (!pack->mounted || pack->pack_state != NT_PACK_STATE_READY) {
                continue;
            }
            if (pack->blob == NULL) {
                continue; /* blob evicted */
            }

            for (uint32_t ai = 0; ai < s_resource.asset_hwm; ai++) {
                NtAssetMeta *meta = &s_resource.assets[ai];
                if (meta->resource_id == 0) {
                    continue;
                }
                if (meta->pack_index != pi) {
                    continue;
                }
                if (meta->state != NT_ASSET_STATE_REGISTERED) {
                    continue;
                }

                uint8_t atype = meta->asset_type;
                if (atype >= NT_RESOURCE_MAX_ASSET_TYPES) {
                    continue;
                }
                if (!s_resource.activators[atype].activate) {
                    continue;
                }

                /* Check time budget (0 = unlimited).
                 * Guarantee at least 1 activation per step — prevents starvation. */
                if (activated_any && budget_ms > 0.0F) {
                    double elapsed_ms = (nt_time_now() - t_start) * 1000.0;
                    if (elapsed_ms >= (double)budget_ms) {
                        goto budget_exhausted;
                    }
                }

                /* Deduplicate: if marked as dedup, find the original (same pack+offset+size, already READY) */
                const uint8_t *asset_data = pack->blob + meta->offset;
                uint32_t handle = 0;
                if (meta->is_dedup) {
                    for (uint32_t di = 0; di < s_resource.asset_hwm; di++) {
                        const NtAssetMeta *other = &s_resource.assets[di];
                        if (di != ai && other->state == NT_ASSET_STATE_READY && other->pack_index == pi && other->offset == meta->offset && other->size == meta->size) {
                            handle = other->runtime_handle;
                            break;
                        }
                    }
                }
                if (handle == 0) {
                    handle = s_resource.activators[atype].activate(asset_data, meta->size);
                }
                if (handle != 0) {
                    meta->state = NT_ASSET_STATE_READY;
                    meta->runtime_handle = handle;
                    s_resource.needs_resolve = true;
                } else {
                    meta->state = NT_ASSET_STATE_FAILED;
                }
                pack->blob_last_access_ms = resource_get_time_ms();
                activated_any = true;
                activated_count++;
            }
        }
    budget_exhausted:
        if (activated_count > 0) {
            double elapsed_ms = (nt_time_now() - t_start) * 1000.0;
            NT_LOG_INFO("activated %u assets (%.1fms / %.1fms budget)", activated_count, elapsed_ms, (double)budget_ms);
        }
    }

    /* ===================================================
     *  Phase C: Blob eviction (NT_BLOB_AUTO)
     * =================================================== */
    {
        uint32_t now_ms = resource_get_time_ms();
        for (uint16_t pi = 0; pi < NT_RESOURCE_MAX_PACKS; pi++) {
            NtPackMeta *pack = &s_resource.packs[pi];
            if (!pack->mounted || pack->blob == NULL) {
                continue;
            }
            if (pack->blob_policy != NT_BLOB_AUTO) {
                continue;
            }
            if (pack->blob_ttl_ms == 0) {
                continue;
            }
            if (now_ms - pack->blob_last_access_ms >= pack->blob_ttl_ms) {
                /* Only free blobs owned by resource system (loaded via I/O).
                 * Caller-owned blobs (parse_pack direct) have io_type == NT_IO_NONE. */
                if (pack->io_type != NT_IO_NONE) {
                    free((void *)pack->blob);
                }
                pack->blob = NULL;
                /* Keep blob_size -- used to validate re-download returns same data */
            }
        }
    }

    /* ===================================================
     *  Phase D: Resolve slots (priority-based winner)
     * =================================================== */

    if (!s_resource.needs_resolve) {
        return; /* O(1) fast path when nothing changed */
    }

    for (uint32_t pass = 0; pass < NT_RESOURCE_MAX_RESOLVE_PASSES; pass++) {
        if (!s_resource.needs_resolve) {
            break;
        }
        s_resource.needs_resolve = false;
        resource_resolve_pass();
    }

    NT_ASSERT(!s_resource.needs_resolve && "resource resolve pass limit exceeded");
}

/* ---- Slot allocation helpers ---- */

static nt_resource_t slot_alloc(uint64_t resource_id, uint8_t asset_type) {
    NT_ASSERT(s_resource.queue_top > 0); /* slot pool full -- raise MAX_SLOTS */

    s_resource.queue_top--;
    uint16_t index = s_resource.free_queue[s_resource.queue_top];

    NtResourceSlot *slot = &s_resource.slots[index];

    /* Increment generation (skip 0: reserved for invalid handles) */
    slot->generation++;
    if (slot->generation == 0) {
        slot->generation = 1;
    }

    slot->resource_id = resource_id;
    slot->runtime_handle = 0;
    slot->target_runtime_handle = 0;
    slot->candidate_runtime_handle = 0;
    slot->resolve_prio = INT16_MIN;
    slot->target_prio = INT16_MIN;
    slot->candidate_prio = INT16_MIN;
    slot->resolve_seq = 0;
    slot->target_seq = 0;
    slot->candidate_seq = 0;
    slot->resolve_asset_idx = UINT16_MAX;
    slot->target_asset_idx = UINT16_MAX;
    slot->candidate_asset_idx = UINT16_MAX;
    slot->prev_resolve_asset_idx = UINT16_MAX;
    slot->user_data_asset_idx = UINT16_MAX;
    slot->prev_runtime_handle = 0;
    slot->asset_type = asset_type;
    slot->state = NT_ASSET_STATE_REGISTERED;
    slot->scan_state = NT_ASSET_STATE_REGISTERED;
    slot->resolve_pending = 0;
    slot->post_resolve_pending = 0;
    slot->user_data = NULL;

    slot_map_insert(resource_id, index);

    return resource_make(index, slot->generation);
}

/* ---- Pack management ---- */

static nt_result_t pack_alloc(uint32_t pack_id, int16_t priority, uint8_t pack_type) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    if (find_pack(pack_id) >= 0) {
        NT_LOG_ERROR("pack already mounted");
        return NT_ERR_INVALID_ARG;
    }

    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        if (s_resource.packs[i].mounted == 0) {
            memset(&s_resource.packs[i], 0, sizeof(NtPackMeta));
            s_resource.packs[i].pack_id = pack_id;
            s_resource.packs[i].priority = priority;
            s_resource.packs[i].pack_type = pack_type;
            s_resource.packs[i].mounted = 1;
            s_resource.packs[i].mount_seq = s_resource.next_mount_seq++;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    NT_ASSERT(0);              /* pack slots full -- raise MAX_PACKS */
    return NT_ERR_INVALID_ARG; /* unreachable, satisfies compiler */
}

nt_result_t nt_resource_mount(nt_hash32_t pack_id, int16_t priority) { return pack_alloc(pack_id.value, priority, NT_PACK_FILE); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_resource_unmount(nt_hash32_t pack_id) {
    if (!s_resource.initialized) {
        return;
    }

    int16_t pack_idx = find_pack(pack_id.value);
    if (pack_idx < 0) {
        return;
    }

    NtPackMeta *pack = &s_resource.packs[pack_idx];

    /* Deactivate READY assets and clear all assets belonging to this pack */
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].pack_index == (uint16_t)pack_idx && s_resource.assets[i].resource_id != 0) {
            /* Deactivate if file pack asset is READY with runtime handle.
             * Skip dedup assets — they share the original's handle, not their own. */
            if (s_resource.assets[i].state == NT_ASSET_STATE_READY && s_resource.assets[i].runtime_handle != 0 && pack->pack_type == NT_PACK_FILE && !s_resource.assets[i].is_dedup) {
                uint8_t atype = s_resource.assets[i].asset_type;
                if (atype < NT_RESOURCE_MAX_ASSET_TYPES && s_resource.activators[atype].deactivate) {
                    s_resource.activators[atype].deactivate(s_resource.assets[i].runtime_handle);
                }
            }
            s_resource.assets[i].resource_id = 0;
        }
    }

    /* Cancel any pending I/O */
    if (pack->io_request_id != 0) {
        if (pack->io_type == NT_IO_HTTP) {
            nt_http_free((nt_http_request_t){.id = pack->io_request_id});
        } else if (pack->io_type == NT_IO_FS) {
            nt_fs_free((nt_fs_request_t){.id = pack->io_request_id});
        }
    }

    /* Free blob if it was loaded via I/O (resource system owns it).
     * Blobs provided directly via parse_pack are caller-owned. */
    if (pack->blob != NULL && pack->io_type != NT_IO_NONE) {
        free((void *)pack->blob);
    }

    /* Free resident metadata */
    free(pack->meta_data);

    memset(&s_resource.packs[pack_idx], 0, sizeof(NtPackMeta));
    s_resource.needs_resolve = true;
}

nt_result_t nt_resource_set_priority(nt_hash32_t pack_id, int16_t new_priority) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t idx = find_pack(pack_id.value);
    if (idx < 0) {
        NT_LOG_ERROR("set_priority pack not found");
        return NT_ERR_INVALID_ARG;
    }

    s_resource.packs[idx].priority = new_priority;
    s_resource.needs_resolve = true;
    return NT_OK;
}

/* ---- Pack parsing ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_resource_parse_pack(nt_hash32_t pack_id, const uint8_t *blob, uint32_t blob_size) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t pack_idx = find_pack(pack_id.value);
    if (pack_idx < 0) {
        NT_LOG_ERROR("parse_pack not mounted");
        return NT_ERR_INVALID_ARG;
    }

    if (s_resource.packs[pack_idx].blob != NULL) {
        NT_LOG_ERROR("pack already parsed");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate blob size */
    if (blob_size < sizeof(NtPackHeader)) {
        NT_LOG_ERROR("blob too small");
        return NT_ERR_INVALID_ARG;
    }

    const NtPackHeader *h = (const NtPackHeader *)blob;

    /* Validate magic */
    if (h->magic != NT_PACK_MAGIC) {
        NT_LOG_ERROR("bad magic");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate version -- no backwards compat, packs must match exactly */
    NT_ASSERT(h->version == NT_PACK_VERSION && "pack version mismatch -- rebuild packs");

    /* Validate header_size */
    if (h->header_size > blob_size) {
        NT_LOG_ERROR("header_size exceeds blob");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate total_size */
    if (h->total_size != blob_size) {
        NT_LOG_ERROR("total_size mismatch");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate asset entries fit in header region */
    uint32_t entries_end = (uint32_t)sizeof(NtPackHeader) + ((uint32_t)h->asset_count * (uint32_t)sizeof(NtAssetEntry));
    if (entries_end > h->header_size) {
        NT_LOG_ERROR("entries overflow header region");
        return NT_ERR_INVALID_ARG;
    }

    /* Validate CRC32 */
    uint32_t data_size = blob_size - h->header_size;
    uint32_t computed = nt_crc32(blob + h->header_size, data_size);
    if (computed != h->checksum) {
        NT_LOG_ERROR("CRC32 mismatch");
        return NT_ERR_INVALID_ARG;
    }

    /* Parse asset entries */
    const NtAssetEntry *entries = (const NtAssetEntry *)(blob + sizeof(NtPackHeader));

    /* Meta section start from header (no scan needed) */
    uint32_t meta_section_start = h->meta_offset;

    for (uint16_t i = 0; i < h->asset_count; i++) {
        /* Validate entry offset is in data region and data fits within blob */
        if (entries[i].offset < h->header_size || entries[i].size > blob_size || entries[i].offset > blob_size - entries[i].size) {
            NT_LOG_ERROR("entry data outside data region");
            return NT_ERR_INVALID_ARG;
        }

        uint32_t idx = asset_alloc();
        NT_ASSERT(idx != UINT32_MAX); /* asset array full -- raise limits */

        NtAssetMeta *meta = &s_resource.assets[idx];
        meta->resource_id = entries[i].resource_id;
        meta->asset_type = entries[i].asset_type;
        meta->format_version = entries[i].format_version;
        meta->pack_index = (uint16_t)pack_idx;
        meta->offset = entries[i].offset;
        meta->size = entries[i].size;
        meta->runtime_handle = 0;
        /* Convert absolute meta_offset to meta_data-relative in one pass */
        if (entries[i].meta_offset != 0 && entries[i].meta_offset >= meta_section_start) {
            meta->meta_offset = entries[i].meta_offset - meta_section_start;
        } else {
            meta->meta_offset = NT_NO_METADATA;
        }

        /* Detect dedup: check if a previous entry in this pack has same offset+size */
        meta->is_dedup = 0;
        for (uint16_t j = 0; j < i; j++) {
            if (entries[j].offset == entries[i].offset && entries[j].size == entries[i].size) {
                meta->is_dedup = 1;
                break;
            }
        }

        /* Blob assets auto-transition to READY (no GPU activation needed) */
        if (entries[i].asset_type == NT_ASSET_BLOB) {
            meta->state = NT_ASSET_STATE_READY;
        } else {
            meta->state = NT_ASSET_STATE_REGISTERED;
        }
    }

    /* Parse metadata section */
    NtPackMeta *pack = &s_resource.packs[pack_idx];

    if (h->meta_count > 0 && meta_section_start != 0 && meta_section_start < blob_size) {
        uint32_t meta_section_size = blob_size - meta_section_start;
        /* Copy meta section to resident memory (survives blob eviction) */
        pack->meta_data = (uint8_t *)malloc(meta_section_size);
        NT_ASSERT(pack->meta_data && "meta section malloc failed");
        memcpy(pack->meta_data, blob + meta_section_start, meta_section_size);
        pack->meta_size = meta_section_size;
        pack->meta_count = h->meta_count;
    }

    /* Update pack metadata */
    pack->blob = blob;
    pack->blob_size = blob_size;
    if (pack->pack_state != NT_PACK_STATE_READY) {
        pack->pack_state = NT_PACK_STATE_READY;
    }

    s_resource.needs_resolve = true;

    return NT_OK;
}

/* ---- Resource access ---- */

nt_resource_t nt_resource_request(nt_hash64_t resource_id, uint8_t asset_type) {
    if (!s_resource.initialized) {
        return NT_RESOURCE_INVALID;
    }

    /* O(1) lookup via slot map (idempotent) */
    uint16_t existing = slot_map_find(resource_id.value);
    if (existing != 0) {
        NT_ASSERT(s_resource.slots[existing].asset_type == asset_type);
        return resource_make(existing, s_resource.slots[existing].generation);
    }

    /* Not found: allocate new slot */
    nt_resource_t handle = slot_alloc(resource_id.value, asset_type);
    if (handle.id != 0) {
        s_resource.needs_resolve = true;
    }
    return handle;
}

nt_resource_t nt_resource_find(nt_hash64_t resource_id) {
    if (!s_resource.initialized) {
        return NT_RESOURCE_INVALID;
    }
    uint16_t existing = slot_map_find(resource_id.value);
    if (existing != 0) {
        return resource_make(existing, s_resource.slots[existing].generation);
    }
    return NT_RESOURCE_INVALID;
}

uint32_t nt_resource_get(nt_resource_t handle) {
    if (handle.id == 0) {
        return 0;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return 0;
    }
    if (s_resource.slots[index].generation != gen) {
        return 0; /* stale handle */
    }

    return s_resource.slots[index].runtime_handle;
}

void *nt_resource_get_user_data(nt_resource_t handle) {
    if (handle.id == 0) {
        return NULL;
    }
    uint16_t idx = nt_resource_slot_index(handle);
    if (idx == 0 || idx > NT_RESOURCE_MAX_SLOTS) {
        return NULL;
    }
    NtResourceSlot *slot = &s_resource.slots[idx];
    if (slot->generation != nt_resource_generation(handle)) {
        return NULL;
    }
    return slot->user_data;
}

bool nt_resource_is_ready(nt_resource_t handle) {
    if (handle.id == 0) {
        return false;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return false;
    }
    if (s_resource.slots[index].generation != gen) {
        return false; /* stale handle */
    }

    return s_resource.slots[index].state == NT_ASSET_STATE_READY;
}

uint8_t nt_resource_get_state(nt_resource_t handle) {
    if (handle.id == 0) {
        return NT_ASSET_STATE_FAILED;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return NT_ASSET_STATE_FAILED;
    }
    if (s_resource.slots[index].generation != gen) {
        return NT_ASSET_STATE_FAILED; /* stale handle */
    }

    return s_resource.slots[index].state;
}

const uint8_t *nt_resource_get_blob(nt_resource_t handle, uint32_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    if (handle.id == 0) {
        return NULL;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return NULL;
    }
    if (s_resource.slots[index].generation != gen) {
        return NULL; /* stale handle */
    }
    if (s_resource.slots[index].asset_type != NT_ASSET_BLOB) {
        return NULL; /* not a blob */
    }

    /* Slot runtime_handle stores the winning asset index (set by resolve Phase D) */
    const NtResourceSlot *slot = &s_resource.slots[index];
    if (slot->state != NT_ASSET_STATE_READY) {
        return NULL;
    }
    uint32_t ai = slot->runtime_handle;
    if (ai >= s_resource.asset_hwm) {
        return NULL;
    }
    const NtAssetMeta *meta = &s_resource.assets[ai];
    if (meta->resource_id == 0 || meta->state != NT_ASSET_STATE_READY) {
        return NULL;
    }
    if (meta->pack_index >= NT_RESOURCE_MAX_PACKS) {
        return NULL;
    }
    const NtPackMeta *pack = &s_resource.packs[meta->pack_index];
    if (!pack->mounted || pack->blob == NULL) {
        return NULL;
    }
    if (meta->size <= sizeof(NtBlobAssetHeader)) {
        return NULL;
    }
    if (out_size) {
        *out_size = meta->size - (uint32_t)sizeof(NtBlobAssetHeader);
    }
    return pack->blob + meta->offset + sizeof(NtBlobAssetHeader);
}

/* ---- Metadata query ---- */

const void *nt_resource_get_meta(nt_resource_t handle, uint64_t kind, uint32_t *out_size) {
    if (out_size) {
        *out_size = 0;
    }
    if (handle.id == 0) {
        return NULL;
    }

    uint16_t index = nt_resource_slot_index(handle);
    uint16_t gen = nt_resource_generation(handle);

    if (index == 0 || index > NT_RESOURCE_MAX_SLOTS) {
        return NULL;
    }
    if (s_resource.slots[index].generation != gen) {
        return NULL; /* stale handle */
    }

    const NtResourceSlot *slot = &s_resource.slots[index];

    /* Get the resolved asset index */
    uint16_t ai = slot->resolve_asset_idx;
    if (ai >= s_resource.asset_hwm) {
        return NULL;
    }

    const NtAssetMeta *ameta = &s_resource.assets[ai];
    if (ameta->resource_id != slot->resource_id) {
        return NULL; /* not yet resolved, or stale index */
    }
    if (ameta->meta_offset == NT_NO_METADATA) {
        return NULL; /* no metadata for this asset */
    }

    /* Find pack's resident meta data */
    if (ameta->pack_index >= NT_RESOURCE_MAX_PACKS) {
        return NULL;
    }
    const NtPackMeta *pack = &s_resource.packs[ameta->pack_index];
    if (!pack->mounted || pack->meta_data == NULL) {
        return NULL;
    }

    /* Scan NtMetaEntryHeader records from the asset's meta_offset */
    uint32_t scan_offset = ameta->meta_offset;
    if (scan_offset >= pack->meta_size) {
        return NULL;
    }

    while (scan_offset + sizeof(NtMetaEntryHeader) <= pack->meta_size) {
        NtMetaEntryHeader mh;
        memcpy(&mh, pack->meta_data + scan_offset, sizeof(NtMetaEntryHeader));

        if (mh.resource_id != ameta->resource_id) {
            break; /* end of entries for this asset */
        }

        uint32_t payload_offset = scan_offset + (uint32_t)sizeof(NtMetaEntryHeader);

        /* Guard against corrupted mh.size causing overflow/infinite loop */
        if (mh.size > pack->meta_size || payload_offset + mh.size > pack->meta_size) {
            return NULL; /* corrupt */
        }

        if (mh.kind == kind) {
            if (out_size) {
                *out_size = mh.size;
            }
            return pack->meta_data + payload_offset;
        }

        /* Move to next entry */
        uint32_t padded_size = (mh.size + (NT_PACK_ASSET_ALIGN - 1U)) & ~(NT_PACK_ASSET_ALIGN - 1U);
        scan_offset = payload_offset + padded_size;
    }

    return NULL; /* kind not found */
}

/* ---- Virtual packs ---- */

nt_result_t nt_resource_create_pack(nt_hash32_t pack_id, int16_t priority) { return pack_alloc(pack_id.value, priority, NT_PACK_VIRTUAL); }

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
nt_result_t nt_resource_register(nt_hash32_t pack_id, nt_hash64_t resource_id, uint8_t asset_type, uint32_t runtime_handle) {
    if (!s_resource.initialized) {
        return NT_ERR_INVALID_ARG;
    }

    int16_t pack_idx = find_pack(pack_id.value);
    if (pack_idx < 0) {
        NT_LOG_ERROR("register pack not found");
        return NT_ERR_INVALID_ARG;
    }
    if (s_resource.packs[pack_idx].pack_type != NT_PACK_VIRTUAL) {
        NT_LOG_ERROR("register on non-virtual pack");
        return NT_ERR_INVALID_ARG;
    }

    /* Check for duplicate: update existing entry */
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id.value && s_resource.assets[i].pack_index == (uint16_t)pack_idx) {
            s_resource.assets[i].runtime_handle = runtime_handle;
            s_resource.assets[i].state = NT_ASSET_STATE_READY;
            s_resource.needs_resolve = true;
            return NT_OK;
        }
    }

    uint32_t idx = asset_alloc();
    NT_ASSERT(idx != UINT32_MAX); /* asset array full -- raise limits */

    NtAssetMeta *meta = &s_resource.assets[idx];
    meta->resource_id = resource_id.value;
    meta->asset_type = asset_type;
    meta->state = NT_ASSET_STATE_READY;
    meta->format_version = 0;
    meta->pack_index = (uint16_t)pack_idx;
    meta->is_dedup = 0;
    meta->_pad = 0;
    meta->offset = 0;
    meta->size = 0;
    meta->runtime_handle = runtime_handle;

    s_resource.needs_resolve = true;

    return NT_OK;
}

void nt_resource_unregister(nt_hash32_t pack_id, nt_hash64_t resource_id) {
    if (!s_resource.initialized) {
        return;
    }

    int16_t pack_idx = find_pack(pack_id.value);
    if (pack_idx < 0) {
        return;
    }

    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id.value && s_resource.assets[i].pack_index == (uint16_t)pack_idx) {
            s_resource.assets[i].resource_id = 0; /* mark as free */
            s_resource.needs_resolve = true;
            return;
        }
    }
}

/* ---- Pack loading ---- */

static nt_result_t resource_load(uint32_t pack_id, const char *path, uint8_t io_type) {
    int16_t idx = find_pack(pack_id);
    NT_ASSERT(idx >= 0); /* programmer error: load called on unmounted pack */

    NtPackMeta *pack = &s_resource.packs[idx];
    NT_ASSERT(pack->pack_state == NT_PACK_STATE_NONE); /* not already loading */

    strncpy(pack->load_path, path, 255);
    pack->load_path[255] = '\0';

    uint32_t req_id = resource_io_issue(path, io_type);
    if (req_id == 0) {
        pack->pack_state = NT_PACK_STATE_FAILED;
        NT_LOG_ERROR("load failed to issue I/O request");
        return NT_ERR_INVALID_ARG;
    }

    pack->io_request_id = req_id;
    pack->io_type = io_type;
    pack->pack_state = NT_PACK_STATE_REQUESTED;
    pack->attempt_count = 1;

    NT_LOG_INFO("loading pack 0x%08X from %s", pack_id, path);

    return NT_OK;
}

nt_result_t nt_resource_load_file(nt_hash32_t pack_id, const char *path) { return resource_load(pack_id.value, path, NT_IO_FS); }

nt_result_t nt_resource_load_url(nt_hash32_t pack_id, const char *url) { return resource_load(pack_id.value, url, NT_IO_HTTP); }

nt_result_t nt_resource_load_auto(nt_hash32_t pack_id, const char *path) {
#ifdef NT_PLATFORM_WEB
    return nt_resource_load_url(pack_id, path);
#else
    return nt_resource_load_file(pack_id, path);
#endif
}

nt_pack_state_t nt_resource_pack_state(nt_hash32_t pack_id) {
    int16_t idx = find_pack(pack_id.value);
    if (idx < 0) {
        return NT_PACK_STATE_NONE;
    }
    return (nt_pack_state_t)s_resource.packs[idx].pack_state;
}

void nt_resource_pack_progress(nt_hash32_t pack_id, uint32_t *received, uint32_t *total) {
    int16_t idx = find_pack(pack_id.value);
    if (idx < 0) {
        if (received) {
            *received = 0;
        }
        if (total) {
            *total = 0;
        }
        return;
    }
    if (received) {
        *received = s_resource.packs[idx].bytes_received;
    }
    if (total) {
        *total = s_resource.packs[idx].bytes_total;
    }
}

/* ---- Activator registration ---- */

void nt_resource_set_activator(uint8_t asset_type, nt_activate_fn activate, nt_deactivate_fn deactivate) {
    NT_ASSERT(asset_type < NT_RESOURCE_MAX_ASSET_TYPES);
    NT_ASSERT((s_resource.activators[asset_type].activate == NULL || s_resource.activators[asset_type].activate == activate) && "activator already registered");
    NT_ASSERT((s_resource.activators[asset_type].deactivate == NULL || s_resource.activators[asset_type].deactivate == deactivate) && "deactivator already registered");
    s_resource.activators[asset_type].activate = activate;
    s_resource.activators[asset_type].deactivate = deactivate;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_resource_set_resolve_callbacks(uint8_t asset_type, nt_resolve_fn on_resolve, nt_cleanup_fn on_cleanup) {
    NT_ASSERT(asset_type < NT_RESOURCE_MAX_ASSET_TYPES);
    NT_ASSERT((s_resource.activators[asset_type].on_resolve == NULL || s_resource.activators[asset_type].on_resolve == on_resolve) && "resolve callbacks already registered");
    NT_ASSERT((s_resource.activators[asset_type].on_cleanup == NULL || s_resource.activators[asset_type].on_cleanup == on_cleanup) && "cleanup callback already registered");
    NT_ASSERT((on_resolve == NULL || on_cleanup != NULL) && "on_resolve requires on_cleanup to avoid user_data leak");
    s_resource.activators[asset_type].on_resolve = on_resolve;
    s_resource.activators[asset_type].on_cleanup = on_cleanup;
}

void nt_resource_set_post_resolve_callback(uint8_t asset_type, nt_post_resolve_fn on_post_resolve) {
    NT_ASSERT(asset_type < NT_RESOURCE_MAX_ASSET_TYPES);
    NT_ASSERT((s_resource.activators[asset_type].on_post_resolve == NULL || s_resource.activators[asset_type].on_post_resolve == on_post_resolve) && "post-resolve callback already registered");
    s_resource.activators[asset_type].on_post_resolve = on_post_resolve;
}

void nt_resource_set_behavior_flags(uint8_t asset_type, uint8_t behavior_flags) {
    NT_ASSERT(asset_type < NT_RESOURCE_MAX_ASSET_TYPES);
    NT_ASSERT((behavior_flags & NT_RESOURCE_BEHAVIOR_AUTO_RELOAD_ON_AUX_MISS) == 0 || (behavior_flags & NT_RESOURCE_BEHAVIOR_PUBLISH_REQUIRES_AUX) != 0);
    s_resource.activators[asset_type].behavior_flags = behavior_flags;
}

/* ---- Activation time budget ---- */

void nt_resource_set_activate_time_budget(float max_ms) { s_resource.activate_time_budget_ms = max_ms; }

/* ---- Retry policy ---- */

void nt_resource_set_retry_policy(uint32_t max_attempts, uint32_t base_delay_ms, uint32_t max_delay_ms) {
    s_resource.retry_max_attempts = max_attempts;
    s_resource.retry_base_delay_ms = base_delay_ms;
    s_resource.retry_max_delay_ms = max_delay_ms;
}

/* ---- Blob policy ---- */

void nt_resource_set_blob_policy(nt_hash32_t pack_id, uint8_t policy, uint32_t ttl_ms) {
    int16_t idx = find_pack(pack_id.value);
    if (idx < 0) {
        return;
    }
    s_resource.packs[idx].blob_policy = policy;
    s_resource.packs[idx].blob_ttl_ms = ttl_ms;
}

/* ---- Context loss recovery ---- */

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void nt_resource_invalidate(uint8_t asset_type) {
    /* Pass 1: Deactivate and mark assets back to REGISTERED */
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        NtAssetMeta *meta = &s_resource.assets[i];
        if (meta->resource_id == 0) {
            continue;
        }
        if (meta->asset_type != asset_type) {
            continue;
        }
        /* Skip virtual pack assets -- only file pack assets are invalidated */
        if (meta->pack_index >= NT_RESOURCE_MAX_PACKS) {
            continue;
        }
        if (s_resource.packs[meta->pack_index].pack_type == NT_PACK_VIRTUAL) {
            continue;
        }
        /* Deactivate if READY with runtime handle.
         * Skip dedup assets — they share the original's handle. */
        if (meta->state == NT_ASSET_STATE_READY && meta->runtime_handle != 0 && !meta->is_dedup) {
            uint8_t atype = meta->asset_type;
            if (atype < NT_RESOURCE_MAX_ASSET_TYPES && s_resource.activators[atype].deactivate) {
                s_resource.activators[atype].deactivate(meta->runtime_handle);
            }
        }
        meta->state = NT_ASSET_STATE_REGISTERED;
        meta->runtime_handle = 0;
    }

    /* Pass 2: Check file packs for blob eviction + re-download trigger */
    for (uint16_t i = 0; i < NT_RESOURCE_MAX_PACKS; i++) {
        NtPackMeta *pack = &s_resource.packs[i];
        if (!pack->mounted || pack->pack_type == NT_PACK_VIRTUAL) {
            continue;
        }
        if (pack->pack_state != NT_PACK_STATE_READY) {
            continue;
        }
        if (pack->blob != NULL) {
            continue; /* blob still available, resource_step will re-activate from it */
        }
        /* Blob was evicted -- need to re-download to re-activate assets.
         * Set pack_state to NONE so resource_step's retry logic re-issues the download.
         * io_type and load_path are preserved from the original load call. */
        pack->pack_state = NT_PACK_STATE_NONE;
        pack->retry_delay_ms = 0; /* immediate, not exponential backoff */
        pack->retry_time_ms = 1;  /* non-zero triggers retry on next resource_step() */
        pack->attempt_count = 0;  /* reset attempt count for re-download */
    }

    s_resource.needs_resolve = true;
}

/* ---- Placeholder ---- */

void nt_resource_set_placeholder_texture(nt_hash64_t resource_id) {
    if (!s_resource.initialized) {
        return;
    }
    s_resource.placeholder_texture = resource_id.value;

    /* Ensure placeholder has a slot so step() can resolve its handle */
    if (resource_id.value != 0 && slot_map_find(resource_id.value) == 0) {
        nt_resource_request(resource_id, NT_ASSET_TEXTURE);
    }
    s_resource.needs_resolve = true;
}

/* ---- Debug: dump loaded pack contents to log ---- */

void nt_resource_dump_pack(nt_hash32_t pack_id) {
    int16_t idx = find_pack(pack_id.value);
    if (idx < 0) {
        NT_LOG_ERROR("dump_pack: not mounted");
        return;
    }

    NtPackMeta *pack = &s_resource.packs[idx];
    NT_LOG_INFO("  Pack 0x%08X  prio=%d  state=%d  blob=%s (%u bytes)", pack->pack_id, (int)pack->priority, (int)pack->pack_state, pack->blob ? "yes" : "no", pack->blob_size);

    if (!pack->blob || pack->blob_size < sizeof(NtPackHeader)) {
        NT_LOG_INFO("  (no blob data to dump)");
        return;
    }

    const NtPackHeader *h = (const NtPackHeader *)pack->blob;
    const NtAssetEntry *entries = (const NtAssetEntry *)(pack->blob + sizeof(NtPackHeader));
    uint32_t max_entries = (h->header_size - (uint32_t)sizeof(NtPackHeader)) / (uint32_t)sizeof(NtAssetEntry);
    if (h->asset_count < max_entries) {
        max_entries = h->asset_count;
    }

    for (uint32_t i = 0; i < max_entries; i++) {
        const char *tname = "???";
        switch (entries[i].asset_type) {
        case NT_ASSET_MESH:
            tname = "mesh";
            break;
        case NT_ASSET_TEXTURE:
            tname = "texture";
            break;
        case NT_ASSET_SHADER_CODE:
            tname = "shader";
            break;
        default:
            break;
        }
        NT_LOG_INFO("    [%u] 0x%016" PRIX64 "  %-8s  %u bytes", i, entries[i].resource_id, tname, entries[i].size);
    }
}

/* ---- Test access (test-only) ---- */

#ifdef NT_RESOURCE_TEST_ACCESS

void nt_resource_test_set_asset_state(nt_hash64_t resource_id, uint16_t pack_index, uint8_t state, uint32_t runtime_handle) {
    for (uint32_t i = 0; i < s_resource.asset_hwm; i++) {
        if (s_resource.assets[i].resource_id == resource_id.value && s_resource.assets[i].pack_index == pack_index) {
            s_resource.assets[i].state = state;
            s_resource.assets[i].runtime_handle = runtime_handle;
            s_resource.needs_resolve = true;
            return;
        }
    }
}

#endif /* NT_RESOURCE_TEST_ACCESS */
