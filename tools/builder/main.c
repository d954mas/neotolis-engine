#include "core/nt_core.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("Neotolis Builder v%s\n", nt_engine_version_string());
    printf("Native build OK\n");

    return 0;
}
