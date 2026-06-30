#ifndef NT_DEVAPI_TIME_INTERNAL_H
#define NT_DEVAPI_TIME_INTERNAL_H

/* Time-group command caps (frame.wait / time.step / time.wait). Kept out of the core internal header so
   the core never knows the group's limits; the time group + its unit test derive their bounds here. */

/* Upper bound for frame.wait{frames}, sized to the default 5s client read-timeout: at 60fps a
   256-frame RUN wait is ~4.3s, under the timeout (a bigger ceiling rejects nothing the client
   could wait for anyway). Not the slot-count cap. Override per build with -D. */
#ifndef NT_DEVAPI_FRAME_WAIT_MAX
#define NT_DEVAPI_FRAME_WAIT_MAX 256
#endif

/* Upper bound for time.step{count}: a fail-fast ceiling + sane batch size, NOT a UB/overflow guard
   (cJSON clamps to INT_MAX, nt_app_step saturates). Crunch runs uncapped, so 2^20 is ~a second of
   fast frames; heavier per-frame work wants smaller batches or a raised client timeout. -D to override. */
#ifndef NT_DEVAPI_STEP_MAX
#define NT_DEVAPI_STEP_MAX 1048576
#endif

/* Upper bound for time.wait{seconds} (game-time deadline), sized to the default 5s read-timeout:
   at scale 1 a wait runs at wall rate, so >5s never resolves before the client gives up. 4s leaves
   margin; longer waits need scale>1 or a raised client timeout. Override per build with -D. */
#ifndef NT_DEVAPI_TIME_WAIT_MAX_SECONDS
#define NT_DEVAPI_TIME_WAIT_MAX_SECONDS 4.0
#endif

#endif /* NT_DEVAPI_TIME_INTERNAL_H */
