#include "log/nt_log.h"

int main(void) {
    nt_log_write(NT_LOG_LEVEL_ERROR, "consumer", "missing implementation");
    return 0;
}
