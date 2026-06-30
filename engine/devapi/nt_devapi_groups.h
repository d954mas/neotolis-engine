#ifndef NT_DEVAPI_GROUPS_H
#define NT_DEVAPI_GROUPS_H

/* Host-facing group registration. The engine is code-first: nt_devapi_init() builds only the
   framework, then a host composes its command surface by registering the groups it wants (explicit
   over implicit — no hidden auto-wiring). Call these AFTER nt_devapi_init().

   - nt_devapi_register_default() registers every group compiled into the build, in a stable order.
     A full host (devapi_host) uses it and links every group's providers.
   - A minimal host instead calls only the specific nt_devapi_register_<group>() it needs, so it
     links only that group's providers (e.g. a capture-only host pulls nt_gfx + nt_fpng, not nt_ui).

   Each registrar's symbol exists only when its group is compiled in; calling one for an absent group
   is an undefined-reference link error (the intended fail-early guard). */

void nt_devapi_register_default(void);

void nt_devapi_register_core(void);
void nt_devapi_register_time(void);
void nt_devapi_register_input(void);
void nt_devapi_register_ui(void);
void nt_devapi_register_obs(void);
void nt_devapi_register_entity_write(void);
void nt_devapi_register_discovery(void);
void nt_devapi_register_capture(void);

#endif /* NT_DEVAPI_GROUPS_H */
