/* Each mode runs in a separate process so ASan cannot hide the UBSan witness. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }
    if (strcmp(argv[1], "asan") == 0) {
        volatile char *buf = (volatile char *)malloc(10);
        if (!buf) {
            return 1;
        }
        buf[10] = 'x'; /* One byte past end. */
        free((void *)buf);
    } else if (strcmp(argv[1], "ubsan") == 0) {
        volatile int x = INT_MAX;
        x += 1; /* Signed overflow. */
        (void)x;
    } else {
        return 1;
    }
    /* A missed diagnostic must fail the CTest output oracle. */
    return 0;
}
