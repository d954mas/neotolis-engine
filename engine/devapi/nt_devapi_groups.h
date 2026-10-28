#ifndef NT_DEVAPI_GROUPS_H
#define NT_DEVAPI_GROUPS_H

#include <stdbool.h>

/* Host-facing group registration. The engine is code-first: nt_devapi_init() builds only the
   framework, then a host composes its command surface by registering the groups it wants (explicit
   over implicit — no hidden auto-wiring). Call these AFTER nt_devapi_init(). A host that registers
   capture inits nt_gfx and calls nt_devapi_frame_rendered() before each swap.

   - nt_devapi_register_default() registers every group compiled into the build, in a stable order.
     A full host (devapi_host) uses it and links every group's providers.
   - A minimal host instead calls only the specific nt_devapi_register_<group>() it needs, so it
     links only that group's providers (e.g. a capture-only host pulls nt_gfx + nt_fpng, not nt_ui).

   Each registrar's symbol exists only when its group is compiled in; calling one for an absent group
   is an undefined-reference link error (the intended fail-early guard). The discovery commands
   (endpoints / command.describe) need no call: nt_devapi_init registers them. */

void nt_devapi_register_default(void);

void nt_devapi_register_core(void);
void nt_devapi_register_time(void);
void nt_devapi_register_input(void);
void nt_devapi_register_ui(void);
void nt_devapi_register_obs(void);
void nt_devapi_register_entity_write(void);
void nt_devapi_register_capture(void);
void nt_devapi_register_recorder(void);
void nt_devapi_register_gfx(void);

#ifdef NT_DEVAPI_GROUP_GFX
/* Game switch of the gfx group's recording mode, the same as gfx.record: on records every frame from the
   next one that records; off keeps the ring and stores the frame in flight; off then on clears the ring.
   Valid anywhere in a frame once gfx is initialized with a nonzero capture_capacity. */
void nt_devapi_gfx_record(bool enabled);
#endif

#endif /* NT_DEVAPI_GROUPS_H */
