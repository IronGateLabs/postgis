## 1. Authentication & Remote Access

- [x] 1.1 Resolve SSH/HTTPS authentication to `origin` (IronGateLabs/postgis)
- [x] 1.2 Verify `upstream` remote fetches correctly

## 2. Branch Cleanup

- [x] 2.1 Delete `feature/ecef-eci-extension-test` from origin
- [x] 2.2 Delete `feature/eop-enhanced-transforms` from origin
- [x] 2.3 Delete `feature/eop-tests-and-docs` from origin

## 3. Master Sync

- [x] 3.1 Checkout `master` and fast-forward merge `upstream/master`
- [x] 3.2 Push updated `master` to `origin`

## 4. Develop Rebase

- [x] 4.1 Checkout `develop` and rebase onto updated `master`
- [x] 4.2 Resolve any merge conflicts (preserve our ECEF/ECI additions, adopt upstream style changes)
- [x] 4.3 Verify rebase result: `git log master..develop` shows only fork-specific commits

## 5. Verification

- [x] 5.1 Verify compilation (`./configure && make`) completes without errors — fixed missing `lwgeom_log.h` include in `accel/rotate_z_neon.c`
- [x] 5.2 Run regression tests if CI is available — local PostgreSQL not running; deferred to CI pipeline

## 6. PR Submission

- [x] 6.1 Push rebased `develop` to origin
- [x] 6.2 Updated existing PR #2 with upstream sync comment
- [x] 6.3 Committed openspec change artifacts
