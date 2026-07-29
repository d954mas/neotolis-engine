/* Unbuffered stdio so a crashing test keeps its Unity output — the MSVC CRT
 * fully buffers stdout on pipes and a trap loses everything. Linked into every
 * test target by nt_setup_test_target. */
#include <stdio.h>

__attribute__((constructor)) static void nt_test_unbuffer_stdio(void) {
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
}
