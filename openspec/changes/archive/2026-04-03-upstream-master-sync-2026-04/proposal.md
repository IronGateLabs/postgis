## Why

Our fork's `master` branch is 94 commits behind `upstream/master` (postgis/postgis). Upstream has landed significant new features (ST_CoverageEdges, ST_MinimumSpanningTree), PG19 compatibility fixes, security hardening, and build system updates. Our `develop` branch — which carries all our ECEF/ECI, GPU acceleration, and geocentric guard work — needs to rebase onto the latest upstream to avoid growing merge conflicts and to pick up build/CI fixes that affect our own CI.

Additionally, 3 merged feature branches (`feature/ecef-eci-extension-test`, `feature/eop-enhanced-transforms`, `feature/eop-tests-and-docs`) need cleanup from the remote.

## What Changes

- Sync `origin/master` with `upstream/master` (fast-forward merge of 94 upstream commits)
- Rebase `develop` onto the updated `master`, resolving any conflicts introduced by upstream changes
- Delete 3 stale feature branches that are already merged into `develop`
- Verify build and regression tests pass after rebase

Key upstream changes being incorporated:
- **New spatial functions**: `ST_CoverageEdges` (GEOS 3.15), `ST_MinimumSpanningTree` (window function)
- **PG19 compatibility**: build fixes, `composite_to_json()` usage, SPI read-only execution
- **Security hardening**: `AddToSearchPath` removal, function qualification against name squatting, `quote_identifier` usage
- **Build system**: K&R style removal (`func()` → `func(void)`), fallthrough warnings, compiler updates
- **CI improvements**: fuzzer centralization, codespell integration, topology test stabilization
- **Translations**: Japanese Weblate updates
- **Bug fixes**: 32-bit topology crashes, ST_Clip sparse band crash, address_standardizer hardening

## Capabilities

### New Capabilities
_None — this is a sync operation, not introducing new capabilities._

### Modified Capabilities
_No spec-level requirement changes. Upstream changes are additive features and fixes that don't conflict with our ECEF/ECI/GPU specs._

## Impact

- **Code**: Potential merge conflicts in build system files, CI configs, and any C files where both upstream and our fork made changes (e.g., `lwgeom_functions_basic.c`, topology, extension packaging)
- **Build**: K&R removal and PG19 changes may require our ECEF/ECI C code to follow `func(void)` convention
- **CI**: Upstream CI changes may interact with our custom CI steps
- **Dependencies**: GEOS 3.15 now expected for full feature coverage (ST_CoverageEdges)
- **Risk**: Low-medium. Our ECEF/ECI work is largely additive (new files/functions), so conflicts should be limited to shared infrastructure files
