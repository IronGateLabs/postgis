# Design: Full IAU 2006/2000A precession-nutation for ECEF/ECI

## Context

The fork currently computes ECEF↔ECI transforms using `era = 2π × (0.7790572732640 + 1.00273781191135448 × Du)` — the IERS 2003 Earth Rotation Angle formula — and then applies a single Z-axis rotation. This is a demonstration-grade implementation. The real international standard for celestial-to-terrestrial transformations is the IAU 2006/2000A resolution, which requires a bias-precession-nutation matrix built from truncated precession-nutation series with ~1,365 nutation terms and ~77 luni-solar bias terms. Implementing that from scratch is a serious undertaking that has already been done extremely well by the IAU SOFA C library; the astropy project forks SOFA as ERFA under BSD-3-Clause to provide GPL-compatible redistribution.

This design settles the specific technical choices that the proposal left open, particularly: which ERFA files to vendor, how the build integrates with the existing `liblwgeom/Makefile.in`, how the new code co-exists with the existing ERA-only path, and how we validate the result against a reference.

## Decision 1: Vendor ERFA rather than depend on a system package

**Decision**: Vendor a subset of ERFA source files directly into `liblwgeom/erfa/`. Do not link against a system ERFA package even if one is available.

**Why**:
- ERFA is not a mainstream Linux package. Debian/Ubuntu does not ship it in main; only `python3-erfa` exists. Red Hat does not package it at all. Homebrew has `erfa` but it is rarely installed outside astronomy environments.
- Supporting both vendored and system-linked builds multiplies test matrix and introduces version-skew risk. If a distro ships ERFA 2.0.0 and we vendored 2.0.1, we have two code paths to test.
- ERFA is stable. The precession-nutation coefficient tables are standardized by the IAU. New ERFA releases are typo fixes and additions of unrelated routines. Re-vendoring is rare.
- Vendoring gives us a single frozen reference that every build and test uses. Debugging is deterministic.

**Alternatives considered**:
- *git submodule*: PostGIS has no submodules today. Adding one means updating every build script, CI matrix, and contributor workflow. The cost is high and the benefit (keeping in sync with upstream) is low because we deliberately don't want to track upstream continuously.
- *Link against system ERFA when available, fall back to vendored*: adds autoconf complexity, doubles our test surface, and the user gains nothing.

## Decision 2: Which ERFA files to vendor

**Decision**: Vendor the minimum subset needed to compute `eraPnm06a`, `eraEra00`, `eraPom00`, `eraC2t06a`, and their transitive dependencies. Initial estimate: 28 source files and 3 header files.

**How we determined the list**: traced function dependencies from the entry points using ERFA's own source tree. The entry points are:

- `eraC2t06a(tta, ttb, uta, utb, xp, yp, rc2t)` — computes the full celestial-to-terrestrial matrix for IAU 2006, given TT and UT1 Julian dates and polar motion xp/yp. This is the function we actually want to call.
- `eraPnm06a(tta, ttb, rnpb)` — precession-nutation matrix (subset of C2T, useful for ICRF → J2000 bias analysis).
- `eraEra00(uta, utb)` — Earth Rotation Angle (we replace our `lweci_earth_rotation_angle` with this).
- `eraPom00(xp, yp, sp, rpom)` — polar motion matrix.

Transitive dependencies (from `eraC2t06a` downward):

| File | Purpose |
|---|---|
| `c2t06a.c` | Entry point: celestial-to-terrestrial matrix |
| `c2tcio.c` | Celestial-to-terrestrial from CIO-based components |
| `pnm06a.c` | Precession-nutation matrix (IAU 2006) |
| `pn06a.c` | Precession-nutation components |
| `nut06a.c` | Nutation components (IAU 2006) |
| `nut00a.c` | IAU 2000A nutation (the actual 1,365-term series) |
| `pfw06.c` | Precession Fukushima-Williams angles |
| `fw2m.c` | Fukushima-Williams angles to rotation matrix |
| `bi00.c` | Frame bias (IAU 2000) |
| `pr00.c` | IAU 2000 precession-rate adjustments |
| `obl06.c` | Mean obliquity (IAU 2006) |
| `s06.c` | CIO locator s for X,Y (IAU 2006) |
| `xy06.c` | X,Y coordinates of CIP (IAU 2006) |
| `era00.c` | Earth Rotation Angle (IAU 2000) |
| `pom00.c` | Polar motion matrix |
| `c2ixys.c` | Celestial-to-intermediate matrix from X,Y,s |
| `sp00.c` | Terrestrial Intermediate Origin locator s' |
| `rxr.c` | 3x3 matrix multiply |
| `rz.c` | Rotate by angle about Z |
| `rx.c` | Rotate by angle about X |
| `ry.c` | Rotate by angle about Y |
| `ir.c` | Initialize to identity matrix |
| `zp.c` | Zero a 3-vector (used by matrix utilities) |
| `cp.c` | Copy 3-vector |
| `cr.c` | Copy 3x3 matrix |
| `rxp.c` | Matrix times vector |
| `anp.c` | Normalize angle to [0, 2π) |
| `anpm.c` | Normalize angle to [-π, π) |

Headers:
- `erfa.h` — public API declarations
- `erfam.h` — internal macros and constants
- `erfaextra.h` — optional extras (vendor empty stub if not needed)

Total: 28 .c files + 3 .h files. Actual file sizes are small except for the nutation/precession coefficient tables — `nut00a.c` alone is ~3,000 lines of static const tables, which is normal for this kind of code and is the main reason the vendored subset totals ~15k LOC.

**Verification**: Phase 1 of the implementation will run `nm liblwgeom.a | grep era` to confirm every `era*` symbol referenced by our transform code is defined in the vendored subset. If an unresolved symbol shows up, we trace it back, vendor the missing file, and update this list.

## Decision 3: Build integration

**Decision**: Add `liblwgeom/erfa/` as a subdirectory with its own `Makefile.in` snippet, included from the main `liblwgeom/Makefile.in`. Compile ERFA sources into the same `liblwgeom.a` static archive, not a separate library.

**Why**:
- Keeps the install surface unchanged. No new `.so` / `.a` to track, no new include path for downstream consumers.
- Matches how PostGIS already handles small vendored helpers (e.g., `lookup3.c`, the flex-generated parsers).
- Simplifies the build: `make -C liblwgeom` still produces exactly one archive.

**Compiler flags**: ERFA sources need `-Wno-unused-variable` and `-Wno-unused-parameter` because several ERFA routines have signatures dictated by the IAU standard that include parameters the routine doesn't use. We apply these only to the `erfa/` subdirectory, not globally. The project's `-Wall -Werror` remains enabled for all fork code.

**Exclusions**: Add `liblwgeom/erfa/**` to `sonar-project.properties` `sonar.exclusions` so SonarCloud doesn't flag vendored code. The rationale matches the existing `lookup3.c` exclusion: vendored code is the upstream vendor's responsibility, not ours.

## Decision 4: Matrix composition model

**Decision**: The new `lwgeom_transform_ecef_to_eci_eop()` and `lwgeom_transform_eci_to_ecef_eop()` will compute a single 3x3 matrix `rc2t` (celestial-to-terrestrial) or `rt2c` (its transpose) per-call and apply it to every point in the geometry. Do not rebuild the matrix per-point.

**Why**: building the full IAU 2006/2000A BPN matrix is ~25,000 FLOPs because of the nutation series evaluation. A typical geometry has thousands of points, all at the same epoch. Per-geometry amortization is critical.

**Matrix composition** (for ECEF → ICRF, the forward direction):

```
rc2t = R_PM(xp, yp, sp) × R_ERA(era) × R_BPN(jd_tt)
```

where:
- `R_BPN` is the bias-precession-nutation matrix from `eraPnm06a(jd_tt_part1, jd_tt_part2)`
- `R_ERA` is the Earth rotation angle matrix from `eraEra00(jd_ut1_part1, jd_ut1_part2)`, applied as a Z-rotation
- `R_PM` is the polar motion matrix from `eraPom00(xp, yp, sp)` — `sp` is `eraSp00(jd_tt_part1, jd_tt_part2)`
- CIP corrections `dx`, `dy` are injected by applying them to the X,Y components before computing the celestial-to-intermediate matrix: `x += dx; y += dy;` then rebuild `rc2i` via `eraC2ixys(x, y, s)`.

For the inverse direction (ECI → ECEF), we use `eraTr(rc2t, rt2c)` to transpose and apply `rt2c` to the input points.

**Dates**: ERFA uses two-part Julian dates (jd1, jd2) to preserve precision over long time spans. The convention is `jd1 = 2400000.5` (MJD epoch) and `jd2 = mjd + fraction_of_day`. Our `lweci_epoch_to_jd()` will be rewritten to return two parts.

## Decision 5: Frame handling — ICRF, J2000, TEME

**Decision**: The three supported frames are handled distinctly:

- **ICRF** (SRID 900001): the reference celestial frame. This is the ERFA default — `rc2t` produced by `eraC2t06a` goes directly from ECEF to ICRF.
- **J2000** (SRID 900002): mean equator and equinox at J2000.0. Requires applying only the frame bias matrix `rb` (from `eraBi00`) to the ICRF result. We expose a small wrapper `lweci_icrf_to_j2000(matrix)` that right-multiplies by the inverse of `rb`.
- **TEME** (SRID 900003): True Equator, Mean Equinox — the frame used by SGP4/SDP4 satellite propagators. TEME uses Greenwich *mean* sidereal time (GMST) from `eraGmst06` instead of ERA, and uses the true-of-date equator/equinox. We expose `lweci_icrf_to_teme(matrix, jd_ut1_part1, jd_ut1_part2)` that computes the TEME rotation and applies it.

Frame-specific dispatch happens in `parse_eci_frame()` (already exists) by setting a `target_frame` enum, and `lwgeom_transform_ecef_to_eci_eop()` branches on it after building the base ICRF matrix.

**Why this matters**: the current implementation does `ST_ECEF_To_ECI(geom, epoch, 'J2000')` and `ST_ECEF_To_ECI(geom, epoch, 'TEME')` and produces the same output as `'ICRF'` — all three collapse to the Z-rotation. That is silently wrong. The new implementation produces distinct, correct results for each frame, and the regression tests will exercise all three.

## Decision 6: EOP data flow

**Decision**: The SQL wrapper `ST_ECEF_To_ECI_EOP()` remains PL/pgSQL and looks up EOP via `postgis_eop_interpolate()`. It passes all five EOP values (`dut1`, `xp`, `yp`, `dx`, `dy`) to the C function `_postgis_ecef_to_eci_eop()`. When EOP data is unavailable (interpolation returns NULL), the wrapper falls back to `ST_ECEF_To_ECI()` which uses zero corrections but still applies the full BPN matrix.

**Why not move EOP lookup to C**: benchmark first if ever needed. The lookup is a single indexed query returning 5 doubles; SPI call overhead is ~1 microsecond. For any geometry with >10 points, the transform cost dominates.

**Why zero-correction still gives IAU 2006/2000A results**: the BPN matrix is built from celestial mechanics that don't depend on EOP. EOP corrections are small residuals on top of that. A user with no EOP data still gets the correct precession-nutation (accurate to ~2 milliarcseconds, ~6 cm at Earth radius), just without the ~1 microarcsecond refinement EOP provides.

## Decision 7: Precision contract and what the tests prove

**Decision**: The precision contract is **1 micron (1e-6 m) at Earth radius for ECEF↔ECI round-trips when EOP data is available**, falling to **~6 cm when EOP data is unavailable** (pure IAU 2006/2000A without corrections).

**How we validate**:
1. Generate reference values offline using a Python script that imports `erfa` (the Python bindings to the same ERFA library we vendor). For each test epoch and test coordinate, compute the expected output to double precision.
2. Commit those reference values as inline `EXECUTE` statements in the regression test SQL.
3. The test `SELECT ST_X(transformed), ST_Y(transformed), ST_Z(transformed)` and compares to expected with `ABS(actual - expected) < 1e-6`.

**What the round-trip test adds**: round-trip is NOT a correctness proof (R then R⁻¹ cancels regardless). But it IS a useful regression test because:
- It catches matrix composition bugs (e.g., wrong transpose direction)
- It catches numerical precision loss in the matrix multiply chain
- It is fast to run and easy to cover many epochs

Both kinds of test are included. The ground-truth test proves "this is IAU 2006/2000A correct"; the round-trip test proves "this is self-consistent".

## Decision 8: What to do with the current ERA-only code

**Decision**: The current `lweci_earth_rotation_angle()` function is rewritten to call `eraEra00()` internally. Its signature and semantics stay the same. Any existing callers continue to work without changes. The "simplified Z-rotation only" code path in `lwgeom_transform_ecef_to_eci_eop` is deleted — the new path is strictly better.

**Why not keep a flag to select the model**: no user wants the wrong answer. Keeping the shortcut as an option means we have to test both paths forever. The old path was a placeholder; once the correct path works, the placeholder goes away.

## Decision 9: GPU backend implications

**Decision**: GPU backends stay on the current simplified path for now. The `lwgeom_gpu_rotate_z_m_epoch` kernel continues to do Z-rotation only, and the GPU dispatch table continues to route calls there. Per-call BPN matrix computation is not GPU-friendly and is not the common case.

**Future**: a later change under `multi-vendor-gpu-rollout` will decide whether to port BPN computation to GPU (unlikely — the matrix is per-epoch, not per-point) or precompute the matrix on CPU and pass it to GPU as a constant buffer (much more likely, since the GPU just needs to multiply 3x3 × 3x1 for each point).

This change does not block or unblock the GPU work.

## Risks and mitigations

**Risk 1: ERFA subset identification is wrong.** We trace dependencies manually; if we miss a transitive dependency, the build will fail with unresolved symbols. Mitigation: Phase 1 is explicitly "vendor and compile clean", before any functional changes land. If we miss files, we catch it immediately and iterate.

**Risk 2: ERFA license compliance.** ERFA is BSD-3-Clause which requires preserving the copyright notice and disclaimer in any redistribution. Mitigation: add the license text to every vendored file (it is already there in the upstream files), add `ERFA-VERSION.txt` with the upstream source URL and license summary, add a note to `LICENSE.txt` or equivalent in the fork.

**Risk 3: Vendored code grows over time as we find more missing functions.** Mitigation: we re-audit during Phase 3 (when we actually exercise the full transform path). If the subset grows beyond ~50 files, we reconsider whether to vendor the entire ERFA library.

**Risk 4: Ground-truth test values drift when we re-vendor ERFA.** Mitigation: the Python script that generates reference values is committed (but not shipped). When re-vendoring, we re-run the script; if values drift, we update the test. If drift is unexpected, we investigate.

**Risk 5: Numerical precision on platforms with `-ffast-math`.** The fork does not enable `-ffast-math` globally, and ERFA relies on IEEE-754 semantics. Mitigation: document this in `ERFA-VERSION.txt` and add a comment in the Makefile snippet explicitly enforcing standard FP math for the `erfa/` subdirectory.

**Risk 6: SonarCloud flags vendored code.** Mitigation: add `liblwgeom/erfa/**` to `sonar.exclusions` in Phase 1.

**Risk 7: This is a big commitment with no user currently demanding it.** Mitigation: phased delivery. If after Phase 1 (vendoring) we decide the ROI isn't there, we can stop with zero impact to user-facing code. If after Phase 2 (ERA replacement) we decide the same, we still have working code. Each phase is a natural exit point.

## Out of scope (reaffirming proposal)

- HTTP fetching for EOP data
- Upstream contribution (stays fork-specific)
- GPU backend port of the new transform
- IAU 1976 legacy option
- Linking to system ERFA

## Success criteria

- Phase 1: `make -C liblwgeom` builds cleanly with the vendored ERFA subset; `nm liblwgeom.a | grep -c "^.*T era"` returns the expected number of ERFA symbols.
- Phase 2: `cu_eci` unit tests pass; `lweci_earth_rotation_angle` returns values bit-identical to direct `eraEra00` calls for a range of test epochs.
- Phase 3: compile cleanly; manual smoke test showing that `ST_ECEF_To_ECI(point, epoch, 'J2000')` and `ST_ECEF_To_ECI(point, epoch, 'ICRF')` produce *different* results.
- Phase 4: regression test `ecef_eci_iau2006` passes with 1 micron tolerance against reference values; round-trip test passes with 1 micron tolerance.
- Phase 5: documentation builds cleanly; capability spec updated.
