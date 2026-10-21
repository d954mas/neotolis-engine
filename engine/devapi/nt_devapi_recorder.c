#include "app/nt_app.h"
#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "recorder/nt_recorder.h"

/* record.* group: a bot starts and stops nt_recorder on game time. The host calls nt_recorder_frame
   every frame; the web output is the 'nt-recorder' event. */

#ifdef NT_DEVAPI_GROUP_RECORDER

static void set_bad_params(nt_devapi_error *err, const char *message) {
    err->code = NT_DEVAPI_ERR_BAD_PARAMS;
    err->message = message;
}

static bool cmd_record_start(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    if (!cJSON_IsString(name) || name->valuestring[0] == '\0') {
        set_bad_params(err, "record.start: name must be a non-empty string");
        return false;
    }
    const char *bad_fps = "record.start: fps must be an integer 1..120";
    uint32_t fps = 0;
    if (!nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "fps"), 120, err, set_bad_params, bad_fps, &fps)) {
        return false;
    }
    if (fps == 0) {
        set_bad_params(err, bad_fps);
        return false;
    }
    if (nt_recorder_active()) {
        err->code = "busy";
        err->message = "record.start: a recording is active";
        return false;
    }
    if (!nt_recorder_start(name->valuestring, fps, g_nt_app.time)) {
        err->code = "failed";
        err->message = "record.start: no encoder or the output cannot be opened (see log)";
        return false;
    }
    (void)result;
    return true;
}

static bool cmd_record_stop(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)params;
    (void)err;
    (void)ud;
    devapi_add_number(result, "frames", (double)nt_recorder_stop());
    return true;
}

static const nt_devapi_command_desc k_record_cmds[] = {
    {
        .method = "record.start",
        .group = "record",
        .summary = "start recording an MP4 of the presented frames on game time",
        .params_shape = "{name:string, fps:int}",
        .result_shape = "{}",
        .frame_behavior = "any",
        .side_effects = "starts nt_recorder; web dispatches 'nt-recorder' after stop",
    },
    {
        .method = "record.stop",
        .group = "record",
        .summary = "stop the recording; frames is 0 when none was active (or it ended itself)",
        .params_shape = "{}",
        .result_shape = "{frames:int}",
        .frame_behavior = "any",
        .side_effects = "finishes the file",
    },
};

static const nt_devapi_handler_fn k_record_handlers[] = {cmd_record_start, cmd_record_stop};
_Static_assert(sizeof(k_record_cmds) / sizeof(k_record_cmds[0]) == sizeof(k_record_handlers) / sizeof(k_record_handlers[0]), "record: descriptor/handler arrays must have equal length");

void nt_devapi_register_recorder(void) {
    int n = (int)(sizeof(k_record_cmds) / sizeof(k_record_cmds[0]));
    for (int i = 0; i < n; i++) {
        nt_result_t rr = nt_devapi_register(&k_record_cmds[i], k_record_handlers[i], NULL);
        NT_ASSERT(rr == NT_OK);
        (void)rr;
    }
}

#endif /* NT_DEVAPI_GROUP_RECORDER */
