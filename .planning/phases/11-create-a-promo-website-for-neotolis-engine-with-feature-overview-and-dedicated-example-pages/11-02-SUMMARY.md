---
phase: 11-create-a-promo-website-for-neotolis-engine-with-feature-overview-and-dedicated-example-pages
plan: 02
subsystem: ui
tags: [astro, content-collections, wasm-demo, example-showcase, iframe]

requires:
  - phase: 11-01
    provides: Astro project scaffold with Tailwind dark theme, BaseLayout, home page with Hero/Overview/Features
provides:
  - Content collection with glob loader reading example promo data from examples/*/site_promo/
  - ExampleCard component with cover/placeholder, title, gzip size, collapsible breakdown
  - ExamplesGrid component fetching size data at build time from GitHub Pages JSON
  - ExampleLayout with back button and full-viewport content area
  - Dynamic [slug].astro route generating per-example WASM demo pages with iframe
affects: [11-03]

tech-stack:
  added: []
  patterns: [astro-content-collections-glob, native-details-summary-collapsible, build-time-fetch-with-fallback]

key-files:
  created:
    - examples/hello/site_promo/index.md
    - neotolis-engine-site/src/content.config.ts
    - neotolis-engine-site/src/components/PlaceholderCover.astro
    - neotolis-engine-site/src/components/ExampleCard.astro
    - neotolis-engine-site/src/components/ExamplesGrid.astro
    - neotolis-engine-site/src/layouts/ExampleLayout.astro
    - neotolis-engine-site/src/pages/examples/[slug].astro
  modified:
    - neotolis-engine-site/src/pages/index.astro

key-decisions:
  - "Glob loader with base: '../examples' successfully resolves example promo data from outside Astro project root"
  - "Size data fetched at build time with local file fallback (CI) then GitHub Pages URL fallback (local dev)"
  - "Native HTML details/summary for collapsible size breakdown -- zero JS, accessible by default"
  - "ExampleLayout is standalone HTML document (not BaseLayout) for full-viewport WASM demo experience"

patterns-established:
  - "Example promo data format: examples/<name>/site_promo/index.md with title/description frontmatter"
  - "Content collection ID derived from directory name via generateId split"
  - "Size data dual-source: local gh-pages-data/ in CI, fetch from GitHub Pages URL locally"
  - "Example pages use separate ExampleLayout (not BaseLayout) for minimal chrome"

requirements-completed: [SITE-05, SITE-06, SITE-07, SITE-08]

duration: 9min
completed: 2026-03-10
---

# Phase 11 Plan 02: Example Showcase System Summary

**Content collection-driven example cards on home page with cover/size/breakdown, plus per-example WASM demo pages with full-viewport iframe**

## Performance

- **Duration:** 9 min
- **Started:** 2026-03-10T15:41:41Z
- **Completed:** 2026-03-10T15:51:00Z
- **Tasks:** 2
- **Files modified:** 8

## Accomplishments
- Example promo data format established with markdown frontmatter in examples/hello/site_promo/
- Content collection with glob loader reads promo data from outside Astro project root
- Home page Examples section with card showing placeholder cover, title, gzip size from CI data, and collapsible per-file size breakdown
- Dynamic example pages with back button, title bar, and full-viewport iframe for WASM demos

## Task Commits

Each task was committed atomically:

1. **Task 1: Create example promo data, content collection, and example cards for home page** - `f8a1401` (feat)
2. **Task 2: Create example layout and dynamic example pages with WASM iframe** - `97beed8` (feat)

## Files Created/Modified
- `examples/hello/site_promo/index.md` - Example promo metadata (title, description)
- `neotolis-engine-site/src/content.config.ts` - Content collection with glob loader for example promo data
- `neotolis-engine-site/src/components/PlaceholderCover.astro` - CSS-only placeholder for missing cover images
- `neotolis-engine-site/src/components/ExampleCard.astro` - Card with cover/placeholder, title, gzip size, collapsible breakdown
- `neotolis-engine-site/src/components/ExamplesGrid.astro` - Grid of ExampleCards with build-time size data fetching
- `neotolis-engine-site/src/layouts/ExampleLayout.astro` - Minimal layout with back button and full-viewport slot
- `neotolis-engine-site/src/pages/examples/[slug].astro` - Dynamic route for per-example WASM demo pages
- `neotolis-engine-site/src/pages/index.astro` - Updated to import and render ExamplesGrid

## Decisions Made
- Glob loader with `base: '../examples'` successfully resolves example promo data from outside the Astro project root -- no workaround or prebuild copy step needed.
- Size data fetched with dual-source strategy: first tries local `../gh-pages-data/sizes/{slug}.json` (available in CI after gh-pages checkout), falls back to fetch from `https://d954mas.github.io/neotolis-engine/sizes/{slug}.json` (works locally and during development).
- Used native HTML `<details>/<summary>` for collapsible size breakdown -- zero JavaScript, accessible, styled with Tailwind.
- ExampleLayout is a standalone HTML document (not extending BaseLayout) because example pages need full-viewport experience with minimal chrome (just a thin top bar with back button and title).

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Example showcase system complete, ready for Plan 03 (CI deployment to GitHub Pages)
- WASM demo iframe will show 404 locally since WASM files are only deployed in CI -- this is expected
- Size data displays when fetched from GitHub Pages URL; in CI will use local gh-pages-data/ checkout

## Self-Check: PASSED

All 8 files verified present on disk. Both task commits (f8a1401, 97beed8) verified in git log.

---
*Phase: 11-create-a-promo-website-for-neotolis-engine-with-feature-overview-and-dedicated-example-pages*
*Completed: 2026-03-10*
