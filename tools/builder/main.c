#include "core/nt_core.h"
#include "log/nt_log.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    NT_LOG_INFO("Neotolis Builder v%s", nt_engine_version_string());
    NT_LOG_INFO("Native build OK");

    return 0;
}
