#include "version/nt_version.h"

#include "core/nt_core.h"
#include "nt_version_commit.h"

const char *nt_version_engine(void) { return NT_VERSION_SEMVER "+" NT_VERSION_COMMIT; }
