#include "nt_build_assert.h"

/* Hookable so tests can catch a builder assert with setjmp/longjmp. */
nt_build_assert_handler_t nt_build_assert_handler = NULL;
