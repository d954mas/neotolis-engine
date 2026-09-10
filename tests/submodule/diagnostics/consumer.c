#include "expected.h"

#if defined(NT426_CHECK_LOG)
#include "log/nt_log.h"
_Static_assert(NT_LOG_MIN_LEVEL == NT426_EXPECT_LOG, "log interface floor");
#elif defined(NT426_CHECK_CORE) || defined(NT426_CHECK_COMPONENT)
#include "introspect/nt_introspect.h"
#if defined(NT426_CHECK_COMPONENT)
#include "transform_comp/nt_transform_comp.h"
#endif
_Static_assert(NT_INTROSPECT_ENABLED == NT426_EXPECT_INTROSPECT, "core introspection configuration");
_Static_assert(NT_INTROSPECT_WRITE_ENABLED == NT426_EXPECT_INTROSPECT_WRITE, "core introspection write configuration");
_Static_assert(NT_LOG_MIN_LEVEL == NT426_EXPECT_LOG, "core log floor");
#elif defined(NT426_CHECK_UI) || defined(NT426_CHECK_UI_STUB)
#include "ui/nt_ui.h"
_Static_assert(NT_RESOURCE_TIMING_ENABLED == NT426_EXPECT_RESOURCE_TIMING, "UI resource timing configuration");
_Static_assert(NT_UI_TIMING_ENABLED == NT426_EXPECT_UI_TIMING, "UI timing configuration");
#if defined(NT426_CHECK_UI_STUB)
_Static_assert(NT_UI_DEBUG_TOOLS == 1, "UI stub keeps its existing probe contract");
#endif
#elif defined(NT426_CHECK_RESOURCE)
#include "resource/nt_resource.h"
_Static_assert(NT_RESOURCE_TIMING_ENABLED == NT426_EXPECT_RESOURCE_TIMING, "resource timing interface configuration");
#elif defined(NT426_CHECK_GFX)
#include "graphics/nt_gfx.h"
_Static_assert(NT_GFX_GPU_TIMING_ENABLED == NT426_EXPECT_GFX_GPU_TIMING, "GPU timing interface configuration");
#elif defined(NT426_CHECK_METRICS)
#include "metrics/nt_metrics.h"
_Static_assert(NT_METRICS_ENABLED == NT426_EXPECT_METRICS, "metrics target configuration");
#elif defined(NT426_CHECK_RING)
#include "log/nt_log_ring.h"
_Static_assert(NT_LOG_RING_ENABLED == NT426_EXPECT_LOG_RING, "ring target configuration");
#else
#error "Select a diagnostics consumer"
#endif
