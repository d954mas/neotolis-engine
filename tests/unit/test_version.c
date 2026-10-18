#include "unity.h"
#include "version/nt_version.h"

#include <stdbool.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

static bool is_commit(const char *s) {
    for (int i = 0; i < 8; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) {
            return false;
        }
    }
    return s[8] == '\0' || strcmp(s + 8, "-dirty") == 0;
}

void test_engine_version_is_the_commit(void) {
    const char *ver = nt_version_engine();
    TEST_ASSERT_TRUE_MESSAGE(strcmp(ver, "unknown") == 0 || is_commit(ver), ver);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_engine_version_is_the_commit);
    return UNITY_END();
}
