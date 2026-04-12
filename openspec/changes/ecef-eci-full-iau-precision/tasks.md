## Phase tracking checklist

This change ships as **5 focused PRs** against fork develop. Each phase is independently reviewable and can serve as a natural stopping point if ROI doesn't pan out.

## Phase 1: Vendor ERFA subset (PR A)

**Goal**: get ERFA compiled into `liblwgeom.a` with zero functional changes to the fork's behavior. If this phase has unexpected build problems, we halt here with zero impact to user-facing code.

### 1a. Identify exact ERFA subset

- [ ] 1a.1 Download the latest stable ERFA release tarball (target: v2.0.1 or newer) from https://github.com/liberfa/erfa/releases
- [ ] 1a.2 Extract the ERFA source tree to a scratch directory (outside the fork repo)
- [ ] 1a.3 Starting from the entry points (`eraC2t06a`, `eraEra00`, `eraPom00`, `eraPnm06a`), manually trace transitive dependencies by grepping each ERFA source file for `era*(` calls on non-local functions. Build a dependency list.
- [ ] 1a.4 Verify the dependency list against design.md Decision 2 (initial estimate: 28 .c files + 3 .h files). Document any discrepancies — if the real count is significantly different, re-check the entry points.
- [ ] 1a.5 Record the final file list in `liblwgeom/erfa/ERFA-VERSION.txt` (will be created in step 1b.1)

### 1b. Create liblwgeom/erfa/ directory

- [ ] 1b.1 Create `liblwgeom/erfa/` directory
- [ ] 1b.2 Create `liblwgeom/erfa/ERFA-VERSION.txt` recording: ERFA release version, source URL (`https://github.com/liberfa/erfa`), SHA of the vendored release tag, BSD-3-Clause license summary, vendored file list with each file's purpose
- [ ] 1b.3 Copy the vendored `.c` files from the scratch directory to `liblwgeom/erfa/` preserving ERFA's internal file structure (flat, no subdirectories)
- [ ] 1b.4 Copy `erfa.h`, `erfam.h`, and `erfaextra.h` to `liblwgeom/erfa/`
- [ ] 1b.5 Verify every vendored file still has its original BSD-3-Clause license header unchanged
- [ ] 1b.6 Stage all vendored files in a separate commit with message `liblwgeom/erfa: vendor ERFA <version> subset for IAU 2006/2000A`

### 1c. Build integration

- [ ] 1c.1 Add `liblwgeom/erfa/Makefile.in` (or equivalent snippet) that compiles each `.c` file to a `.o` object. Use `-Wno-unused-variable -Wno-unused-parameter -Wno-unused-but-set-variable` locally to suppress warnings in vendored code while preserving `-Wall -Werror` globally.
- [ ] 1c.2 Update `liblwgeom/Makefile.in` to descend into `erfa/` before archiving, and to include `erfa/*.o` in the `liblwgeom.a` archive rule
- [ ] 1c.3 Enforce standard IEEE-754 FP math for the `erfa/` subdirectory (no `-ffast-math`). Add a comment in the Makefile snippet explaining why.
- [ ] 1c.4 Run `./autogen.sh && ./configure --with-raster --with-topology && make -C liblwgeom clean && make -C liblwgeom` and verify clean build with zero warnings
- [ ] 1c.5 Run `nm liblwgeom.a | grep -c "^.*T era"` and confirm the symbol count matches the expected number of exported ERFA routines (expected: ~28)
- [ ] 1c.6 Run a smoke test C program outside the test suite that links against `liblwgeom.a` and calls `eraEra00(2451545.0, 0.0)` — verify it returns the expected ERA value for J2000.0 (approximately 4.8949612 radians)

### 1d. SonarCloud exclusion

- [ ] 1d.1 Add `liblwgeom/erfa/**,\` to `sonar-project.properties` `sonar.exclusions`
- [ ] 1d.2 Push a branch with the exclusion change and verify on the next SonarCloud Scan that no new issues are flagged under `liblwgeom/erfa/`

### 1e. Ship PR A

- [ ] 1e.1 Open focused PR titled `liblwgeom/erfa: vendor ERFA subset for IAU 2006/2000A precession-nutation`
- [ ] 1e.2 PR description MUST include: (1) rationale referencing this change's proposal.md, (2) the exact file list, (3) the ERFA version and source URL, (4) the BSD-3-Clause license statement, (5) a note that this PR is compile-clean and NOT user-visible — no behavior change yet
- [ ] 1e.3 Run full CI matrix and confirm clean build across all platforms (pg14-18, asan, usan, mingw, latest)
- [ ] 1e.4 Merge PR A after green CI

## Phase 2: Replace `lweci_earth_rotation_angle` with ERFA-backed (PR B)

**Goal**: swap the internal ERA computation to use `eraEra00` without changing any external behavior. Unit tests prove bit-identical results.

### 2a. Rewrite `lweci_earth_rotation_angle`

- [ ] 2a.1 Open a new branch `feature/eci-era-erfa-swap` off develop (post-PR-A merge)
- [ ] 2a.2 Include `erfa.h` in `liblwgeom/lwgeom_eci.c`
- [ ] 2a.3 Rewrite `lweci_earth_rotation_angle(double jd)` to internally call `eraEra00(jd, 0.0)` (using the two-part Julian date form where the second component is zero since callers currently pass a single-part JD)
- [ ] 2a.4 Verify the function signature and return type are unchanged so all existing callers continue to work

### 2b. Two-part Julian date support

- [ ] 2b.1 Add a new function `lweci_epoch_to_jd_two_part(double epoch, double *jd1, double *jd2)` that returns the high-precision two-part form where `jd1 = 2400000.5` (MJD epoch constant) and `jd2 = mjd + fraction`. Keep the existing single-part `lweci_epoch_to_jd()` for backwards compatibility with callers that don't need maximum precision.
- [ ] 2b.2 Add documentation comment explaining when to use each variant

### 2c. Unit tests

- [ ] 2c.1 Update `liblwgeom/cunit/cu_eci.c` to add a new test `test_era_matches_erfa` that calls both `lweci_earth_rotation_angle()` and `eraEra00()` directly for a range of test epochs and asserts bit-identical results (equality, not tolerance)
- [ ] 2c.2 Verify all existing `cu_eci.c` tests still pass (the rewrite should not change any observable output)
- [ ] 2c.3 Run `make check` locally and confirm green

### 2d. Ship PR B

- [ ] 2d.1 Open focused PR titled `liblwgeom: use ERFA eraEra00 for ECI Earth Rotation Angle computation`
- [ ] 2d.2 PR description MUST include: (1) that this is a drop-in internal replacement, (2) cu_eci tests prove bit-identical results, (3) zero behavior change
- [ ] 2d.3 Run full CI matrix and confirm clean
- [ ] 2d.4 Merge PR B after green CI

## Phase 3: Full bias-precession-nutation transforms (PR C)

**Goal**: the real work. Replace the simplified Z-rotation in `lwgeom_transform_ecef_to_eci*` with the full IAU 2006/2000A matrix chain.

### 3a. Design the new lweci API

- [ ] 3a.1 Draft and add to `liblwgeom/lwgeom_eci.h`:
  - `int lweci_bpn_matrix_iau2006(double jd1_tt, double jd2_tt, double matrix[3][3])` — build the celestial-to-terrestrial intermediate matrix (no EOP, no ERA, no polar motion)
  - `int lweci_cip_correction_apply(double dx_arcsec, double dy_arcsec, double matrix[3][3])` — apply CIP offset corrections by adjusting the X,Y components and rebuilding `rc2i`
  - `int lweci_polar_motion_matrix(double xp_arcsec, double yp_arcsec, double sp_rad, double matrix[3][3])` — build the polar motion matrix
  - `int lweci_icrf_to_j2000_bias(double matrix[3][3])` — apply the frame bias that takes ICRF to J2000
  - `int lweci_icrf_to_teme(double jd1_ut1, double jd2_ut1, double matrix[3][3])` — replace the ERA column of an ICRF matrix with GMST to get TEME
- [ ] 3a.2 Document each function's inputs, outputs, and unit conventions (arcsec vs radians) in the header

### 3b. Implement the lweci matrix builders

- [ ] 3b.1 Implement `lweci_bpn_matrix_iau2006` by calling `eraPnm06a(jd1_tt, jd2_tt, rnpb)`
- [ ] 3b.2 Implement `lweci_polar_motion_matrix` by calling `eraSp00(jd1_tt, jd2_tt, &sp)` and `eraPom00(xp, yp, sp, rpom)`
- [ ] 3b.3 Implement `lweci_cip_correction_apply` by extracting X,Y from the current matrix (or recomputing via `eraBpn2xy`), adding `dx`, `dy` converted from arcseconds to radians, then rebuilding via `eraC2ixys(x + dx, y + dy, eraS06(...))`
- [ ] 3b.4 Implement `lweci_icrf_to_j2000_bias` by calling `eraBi00` to get the frame bias angles, then `eraIr + eraRx + eraRy + eraRz` to build the bias matrix, then `eraRxr` to compose
- [ ] 3b.5 Implement `lweci_icrf_to_teme` by calling `eraGmst06(jd1_ut1, jd2_ut1, jd1_tt, jd2_tt)` and replacing the ERA column of the celestial-to-intermediate portion with GMST

### 3c. Rewrite the transform functions

- [ ] 3c.1 Rewrite `lwgeom_transform_ecef_to_eci_eop()` to:
  1. Split the epoch into `(jd1_tt, jd2_tt)` and `(jd1_ut1, jd2_ut1)` — UT1 = UTC + dut1
  2. Call `lweci_bpn_matrix_iau2006(jd1_tt, jd2_tt, &rc2t)` to get the base matrix
  3. Call `lweci_cip_correction_apply(dx, dy, rc2t)` if dx/dy are non-zero
  4. Build the polar motion matrix via `lweci_polar_motion_matrix(xp, yp, sp, rpom)`
  5. Build the ERA rotation via `eraEra00(jd1_ut1, jd2_ut1)` and `eraRz`
  6. Compose: `rc2t = rpom × rera × rbpn` using `eraRxr`
  7. Branch on frame: ICRF uses rc2t directly; J2000 right-multiplies by the inverse frame bias; TEME replaces the ERA portion with GMST before the compose
  8. Walk the geometry points and apply `rc2t × point` to each using `eraRxp`
- [ ] 3c.2 Rewrite `lwgeom_transform_eci_to_ecef_eop()` symmetrically — same matrix construction, then apply the transpose via `eraTr(rc2t, rt2c)` before walking points
- [ ] 3c.3 Rewrite `lwgeom_transform_ecef_to_eci()` (non-EOP variant) to call `lwgeom_transform_ecef_to_eci_eop()` with all five corrections set to 0 — this is the correct IAU 2006/2000A result when EOP is unavailable
- [ ] 3c.4 Rewrite `lwgeom_transform_eci_to_ecef()` similarly

### 3d. Thread new parameters through PG wrappers

- [ ] 3d.1 The internal C functions `postgis_ecef_to_eci_eop` and `postgis_eci_to_ecef_eop` in `postgis/lwgeom_ecef_eci.c` currently take 6 args (geom, epoch, frame, dut1, xp, yp). Update them to take 8 args (add dx, dy). Update the `PG_GETARG_*` calls.
- [ ] 3d.2 Update the SQL declarations in `postgis/ecef_eci.sql.in` for `_postgis_ecef_to_eci_eop` and `_postgis_eci_to_ecef_eop` to match the new 8-arg signature.
- [ ] 3d.3 Update the PL/pgSQL wrappers `ST_ECEF_To_ECI_EOP` and `ST_ECI_To_ECEF_EOP` in `postgis/ecef_eci.sql.in` to pass `eop.dx` and `eop.dy` from the interpolation result.
- [ ] 3d.4 Handle the upgrade path in `postgis/ecef_eci_upgrade.sql.in`: use `DROP FUNCTION IF EXISTS ... (old signature)` followed by `CREATE OR REPLACE FUNCTION ... (new signature)`. Document the version bump rationale.
- [ ] 3d.5 Determine if this requires an extension minor version bump; check `extensions/postgis_ecef_eci/` control file and bump if needed.

### 3e. Remove the ERA-only shortcut code path

- [ ] 3e.1 Delete the old Z-rotation-only implementation in `lwgeom_transform_ecef_to_eci` (it is now replaced by the call to `lwgeom_transform_ecef_to_eci_eop` with zero corrections)
- [ ] 3e.2 Verify that no other code path in the fork still invokes a simplified Z-rotation for ECI transforms
- [ ] 3e.3 Leave `lweci_earth_rotation_angle()` in place (it is still useful internally and is now just a wrapper around `eraEra00`)

### 3f. Manual smoke test

- [ ] 3f.1 Build, install, start psql, and run: `SELECT ST_AsText(ST_ECEF_To_ECI(ST_MakePoint(6378137, 0, 0), '2025-01-01'::timestamptz, 'ICRF'));`
- [ ] 3f.2 Verify the output is a 3D point with non-zero X,Y,Z that is NOT simply the input rotated by ERA
- [ ] 3f.3 Repeat with frame `'J2000'` — verify the result differs from ICRF by a small amount (frame bias)
- [ ] 3f.4 Repeat with frame `'TEME'` — verify the result differs from both ICRF and J2000
- [ ] 3f.5 If any frame produces identical output, the dispatch is broken — stop and debug before proceeding

### 3g. Ship PR C

- [ ] 3g.1 Open focused PR titled `liblwgeom: implement full IAU 2006/2000A precession-nutation for ECEF/ECI`
- [ ] 3g.2 PR description MUST include: (1) that this replaces the simplified Z-rotation with the full BPN matrix, (2) that CIP offsets dx/dy are now applied, (3) that ICRF/J2000/TEME now produce distinct results, (4) manual smoke test output, (5) note that comprehensive regression tests follow in PR D
- [ ] 3g.3 Run full CI matrix and confirm clean
- [ ] 3g.4 Merge PR C after green CI

## Phase 4: Ground-truth regression tests (PR D)

**Goal**: prove the precision contract against ERFA reference values.

### 4a. Reference value generation script

- [ ] 4a.1 Create `ci/generate_erfa_reference.py` (not shipped in install; used at development time only). The script uses the Python `erfa` package to generate reference values for a fixed set of test cases and prints them as SQL INSERT statements.
- [ ] 4a.2 Test cases to generate:
  - Epoch at J2000.0 exactly (2000-01-01 12:00:00 TT) with ECEF point (6378137, 0, 0)
  - Epoch at J2000.0 with ECEF point on a non-equator location (45N, 90E projected to ECEF)
  - Epoch at 2025-01-01 with a satellite-like coordinate (GEO orbit, ~42164 km radius)
  - Epoch at 2025-06-15 (different nutation phase)
  - Epoch at 2030-12-31 (outside typical EOP coverage to test fallback)
  - For each epoch and point, generate expected output for all three frames (ICRF, J2000, TEME)
- [ ] 4a.3 Run the script and commit the generated values as inline literals in the regression test SQL (NOT as a data file — inline for maximum reviewability)

### 4b. Regression test file

- [ ] 4b.1 Create `regress/core/ecef_eci_iau2006.sql` with:
  - Header comment explaining the precision contract (1 micron with EOP, ~6 cm without)
  - Small embedded IERS Bulletin A snippet (~10 days) inserted into `postgis_eop` for the EOP test cases
  - Test cases from 4a.2, each asserting ST_X/Y/Z match expected values within 1e-6 absolute tolerance
  - Round-trip tests: `ST_ECI_To_ECEF(ST_ECEF_To_ECI(p, epoch, frame), epoch, frame) ≈ p` for all three frames, same 1e-6 tolerance
  - Cross-frame differential tests: assert that ICRF and J2000 results differ by the known frame bias magnitude
- [ ] 4b.2 Create `regress/core/ecef_eci_iau2006_expected` with the expected output (tolerance-based assertions will output "t" for each row if pass)
- [ ] 4b.3 Add `ecef_eci_iau2006` to `regress/core/tests.mk.in`

### 4c. CUnit tests for new lweci API

- [ ] 4c.1 Create `liblwgeom/cunit/cu_eci_iau2006.c` with tests for each new function from Phase 3a:
  - `test_bpn_matrix_matches_erfa` — calls `lweci_bpn_matrix_iau2006` and compares to direct `eraPnm06a` reference
  - `test_polar_motion_matrix_matches_erfa` — same for `lweci_polar_motion_matrix`
  - `test_cip_correction_small_magnitude` — verifies CIP correction application is linearly scaled by dx/dy magnitude
  - `test_icrf_to_j2000_bias_magnitude` — verifies the bias is approximately 17 milliarcseconds
  - `test_icrf_to_teme_differs_from_icrf` — verifies TEME and ICRF are not equal
- [ ] 4c.2 Register the new suite in `liblwgeom/cunit/cu_tester.c`
- [ ] 4c.3 Run `make check` locally and confirm green

### 4d. Ship PR D

- [ ] 4d.1 Open focused PR titled `liblwgeom/regress: ground-truth regression tests for IAU 2006/2000A ECEF/ECI transforms`
- [ ] 4d.2 PR description MUST include: (1) precision contract statement, (2) reference value source (ERFA Python bindings), (3) test case coverage table
- [ ] 4d.3 Run full CI matrix and confirm clean
- [ ] 4d.4 Merge PR D after green CI

## Phase 5: Documentation and spec updates (PR E)

**Goal**: document what the fork now provides.

- [ ] 5.1 Create `doc/reference_ecef_eci.xml` (or add to an existing reference doc file) documenting:
  - IAU 2006/2000A model citation
  - Precision contract (1 micron with EOP, ~6 cm without)
  - Frame handling model (ICRF as reference, J2000 adds bias, TEME uses GMST)
  - EOP data flow (load via `postgis_eop_load`, automatic lookup by `*_EOP` functions, fallback when missing)
  - How to obtain and load IERS Bulletin A data
- [ ] 5.2 Update `openspec/specs/eci-coordinate-support/spec.md` to reference IAU 2006/2000A and drop the "simplified ERA" language
- [ ] 5.3 Update `openspec/specs/earth-orientation-parameters/spec.md` to describe the new full-BPN application of dut1/xp/yp/dx/dy
- [ ] 5.4 Create `openspec/specs/iau2006-precession-nutation/spec.md` with the final capability definition
- [ ] 5.5 Archive this openspec change via `openspec archive ecef-eci-full-iau-precision` after PR E merges
- [ ] 5.6 Open focused PR titled `docs: IAU 2006/2000A precision contract and ECEF/ECI precision documentation`
- [ ] 5.7 Merge PR E after green CI

## Cross-references

- **Depends on**: `earth-orientation-parameters` capability (postgis_eop table, loader, interpolation — already exists)
- **Modifies**: `eci-coordinate-support`, `earth-orientation-parameters`
- **Creates**: `iau2006-precession-nutation`
- **Does not block**: `multi-vendor-gpu-rollout` — GPU backends stay on simplified path for now; a future change decides whether to port BPN computation to GPU or precompute on CPU and pass matrix as constant buffer
- **Does not block**: `upstream-postgis-contribution-roadmap` — ECEF/ECI remains `FORK_SPECIFIC` because the ERFA vendoring is a fork-internal design choice

## Rollback criteria

If any phase fails unexpectedly, we roll back and stop:

- **Phase 1 rollback**: revert the vendoring commit. No user-visible impact.
- **Phase 2 rollback**: revert the ERA-swap commit. ERA computation goes back to the internal formula.
- **Phase 3 rollback**: revert the transform rewrite. Transforms revert to Z-rotation-only.
- **Phase 4/5 rollback**: docs and tests only; revert the individual commits without touching code.

Each phase is a natural exit point. We do NOT commit to completing all five phases in a single session — they are explicitly designed to be independently shippable over multiple days or weeks.
