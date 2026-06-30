#include "devapi/nt_devapi_groups.h"

/* The "register everything compiled in" bundle. A full host calls this once after nt_devapi_init();
   the order is stable (core..capture) so the self-describing `features` surface lists groups
   predictably. A minimal host skips this and registers only the groups it needs (see
   nt_devapi_groups.h). Each call is gated by its group's compile flag, so a group-OFF build links
   no reference to that group's registrar (and pulls none of its providers). */
void nt_devapi_register_default(void) {
#ifdef NT_DEVAPI_GROUP_CORE
    nt_devapi_register_core();
#endif
#ifdef NT_DEVAPI_GROUP_TIME
    nt_devapi_register_time();
#endif
#ifdef NT_DEVAPI_GROUP_INPUT
    nt_devapi_register_input();
#endif
#ifdef NT_DEVAPI_GROUP_UI
    nt_devapi_register_ui();
#endif
#ifdef NT_DEVAPI_GROUP_OBS
    nt_devapi_register_obs();
#endif
#ifdef NT_DEVAPI_GROUP_ENTITY_WRITE
    nt_devapi_register_entity_write();
#endif
#ifdef NT_DEVAPI_GROUP_DISCOVERY
    nt_devapi_register_discovery();
#endif
#ifdef NT_DEVAPI_GROUP_CAPTURE
    nt_devapi_register_capture();
#endif
}
