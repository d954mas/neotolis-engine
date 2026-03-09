#ifndef NT_TYPES_H
#define NT_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    NT_OK = 0,
    NT_ERR_INIT_FAILED,
    NT_ERR_INVALID_ARG,
} nt_result_t;

#endif // NT_TYPES_H
