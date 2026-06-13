#ifndef NT_DEVAPI_INTERNAL_H
#define NT_DEVAPI_INTERNAL_H

/* Internal registry layout, shared by the registry store and Plans 02/03
   dispatch/discovery. Not part of the public API. */

#include "devapi/nt_devapi.h"

/* One registered command. The 7 descriptor strings are strdup-owned copies
   (D-03) freed at shutdown; handler + user_data are stored verbatim. */
typedef struct nt_devapi_slot {
    char *method;
    char *layer;
    char *summary;
    char *params_shape;
    char *result_shape;
    char *frame_behavior;
    char *side_effects;
    nt_devapi_handler_fn handler;
    void *user_data;
} nt_devapi_slot;

/* Registry-table accessors (used by Plan 02 dispatch + Plan 03 discovery). */
int nt_devapi_registry_count(void);
const nt_devapi_slot *nt_devapi_registry_slot(int index);
const nt_devapi_slot *nt_devapi_registry_find(const char *method);

/* Group-name list accessors (used by Plan 03 `features`). */
int nt_devapi_group_count(void);
const char *nt_devapi_group_name(int index);

#endif /* NT_DEVAPI_INTERNAL_H */
