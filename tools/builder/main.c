#include "log/nt_log.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    NT_LOG_INFO("Neotolis Builder");
    NT_LOG_INFO("Native build OK");

    return 0;
}
