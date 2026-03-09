#include "log/nt_log.h"
#include <stdio.h>

void nt_log_init(void) { printf("[nt_log] initialized\n"); }

void nt_log_info(const char *msg) { printf("[INFO] %s\n", msg); }

void nt_log_error(const char *msg) { (void)fprintf(stderr, "[ERROR] %s\n", msg); }
