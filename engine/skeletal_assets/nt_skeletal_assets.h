#ifndef NT_SKELETAL_ASSETS_H
#define NT_SKELETAL_ASSETS_H

#include <stdint.h>

#include "resource/nt_resource.h"
#include "skeletal/nt_skeletal.h"

/*
 * nt_skeletal_assets — the NSKL / NSKN / NANM adapters.
 *
 * Each activator validates the structure of the whole pack payload, copies it
 * into one allocation and points a runtime view at the copy: the wire layout is
 * the runtime layout (spec §16), so nothing is transposed or re-indexed. The
 * pack blob is never read again, so any blob policy may drop it after
 * activation. A structurally broken payload logs one warning and returns 0,
 * which leaves the asset FAILED: a header rejection allocates nothing, a
 * joint-table rejection takes and releases a slot. Structure is what a view
 * needs to address memory (header, magic, version, exact size, counts, table
 * indices, preorder, the grid's sample_count and duration); values inside a
 * sound structure (finite samples, unit quaternions, radii) are the builder's
 * contract, the pack CRC32 and the kernels' NT_SKELETAL_CHECKS, never checked
 * here. Payload arrays are read in place, which the 4-aligned start of every
 * pack asset allows.
 *
 * The module registers nothing itself. An application that links animation
 * registers the three pairs like any other type:
 *
 *   nt_resource_register_type(NT_ASSET_SKELETON,
 *       &(nt_resource_type_desc_t){.activate = nt_skeletal_assets_activate_skeleton,
 *                                 .deactivate = nt_skeletal_assets_deactivate_skeleton});
 *
 * Views are borrowed and live until their asset is deactivated (unmount,
 * reload, shutdown), so the game refetches them after nt_resource_step (§15).
 * rig_compat_id is not cross-checked here — no second asset exists at
 * activation; the game asserts equal rig_compat_id once, when it pairs a
 * skeleton, a binding and a clip.
 */

/* One fixed pool for all three types, allocated once: the game decides how many
 * skeletal assets may be live at a time, exactly like nt_font's max_fonts.
 * Activating past the capacity is a programming error, not a load failure.
 *
 * The capacity counts every *activated* asset, not every published one: when
 * one resource id sits in two mounted packs both copies activate and take a
 * slot, only the winner is published, and the loser is released when its own
 * pack unmounts. Size the pool for the peak mounted set, overlaps included.
 *
 * A second init without shutdown asserts. Call after nt_resource_init and
 * before the first mount that carries skeletal assets. */
void nt_skeletal_assets_init(uint16_t max_assets);
/* Frees every still-live asset and the pool itself; every view published by
 * this module is dangling afterwards. Shut the resource system down first: a
 * deactivate callback arriving after this call asserts. */
void nt_skeletal_assets_shutdown(void);

/* ---- Activators (nt_activate_fn / nt_deactivate_fn, registered by the app) ---- */

uint32_t nt_skeletal_assets_activate_skeleton(const uint8_t *data, uint32_t size);
void nt_skeletal_assets_deactivate_skeleton(uint32_t runtime_handle);
uint32_t nt_skeletal_assets_activate_skin_binding(const uint8_t *data, uint32_t size);
void nt_skeletal_assets_deactivate_skin_binding(uint32_t runtime_handle);
uint32_t nt_skeletal_assets_activate_clip(const uint8_t *data, uint32_t size);
void nt_skeletal_assets_deactivate_clip(uint32_t runtime_handle);

/* ---- Views ----
 *
 * The handle must name a ready resource of that asset type (both asserted).
 * The returned view stays valid until the asset is deactivated. */

const nt_skeletal_skeleton_t *nt_skeletal_assets_skeleton(nt_resource_t skeleton);
const nt_skin_binding_t *nt_skeletal_assets_skin_binding(nt_resource_t binding);
const nt_skeletal_clip_t *nt_skeletal_assets_clip(nt_resource_t clip);

#endif /* NT_SKELETAL_ASSETS_H */
