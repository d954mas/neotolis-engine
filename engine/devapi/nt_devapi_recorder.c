#include "core/nt_assert.h"
#include "devapi/nt_devapi_internal.h"
#include "recorder/nt_recorder.h"

/* record.* group: a bot starts and stops nt_recorder. The host calls nt_recorder_frame every frame
   with game time; the web output is the 'nt-recorder' event, the Windows output the file at name. */

#ifdef NT_DEVAPI_GROUP_RECORDER

static bool cmd_record_start(const cJSON *params, cJSON *result, nt_devapi_error *err, void *ud) {
    (void)ud;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
    if (!cJSON_IsString(name) || name->valuestring[0] == '\0') {
        return nt_devapi_fail(err, "record.start: name must be a non-empty string");
    }
    const char *bad_fps = "record.start: fps must be an integer 1..120";
    uint32_t fps = 0;
    if (!nt_devapi_parse_u32_param_exact(cJSON_GetObjectItemCaseSensitive(params, "fps"), 120, err, bad_fps, &fps)) {
        return false;
    }
    if (fps == 0) {
        return nt_devapi_fail(err, bad_fps);
    }
    if (nt_recorder_active()) {
        return nt_devapi_fail_code(err, "recording", "record.start: a recording is active");
    }
    if (!nt_recorder_start(name->valuestring, fps)) {
        return nt_devapi_fail_code(err, "failed", "record.start: the recorder refused to start (see log and console)");
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

static const nt_devapi_command k_record_cmds[] = {
    {
        .method = "record.start",
        .summary = "start recording an MP4 of the presented frames on game time; web dispatches 'nt-recorder' when the recording ends, Windows writes the file at name (a path)",
        .params_shape = "{name:string, fps:int}",
        .result_shape = "{}",
        .fn = cmd_record_start,
    },
    {
        .method = "record.stop",
        .summary = "stop the recording; frames is 0 when none was active (or it ended itself)",
        .params_shape = "{}",
        .result_shape = "{frames:int}",
        .fn = cmd_record_stop,
    },
};

void nt_devapi_register_recorder(void) { nt_devapi_register(k_record_cmds, sizeof(k_record_cmds) / sizeof(k_record_cmds[0]), NULL); }

#endif /* NT_DEVAPI_GROUP_RECORDER */
