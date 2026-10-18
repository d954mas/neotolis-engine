#include "core/nt_core.h"
#include "unity.h"
#include "version/nt_version.h"

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

void test_engine_version_is_semver_plus_commit(void) {
    const char *ver = nt_version_engine();
    const char *prefix = NT_VERSION_SEMVER "+";
    TEST_ASSERT_EQUAL_INT(0, strncmp(ver, prefix, strlen(prefix)));
    const char *commit = ver + strlen(prefix);
    TEST_ASSERT_TRUE_MESSAGE(strcmp(commit, "unknown") == 0 || is_commit(commit), ver);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_engine_version_is_semver_plus_commit);
    return UNITY_END();
}
