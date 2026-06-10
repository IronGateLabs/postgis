## Why

The fork's current ECEF↔ECI transforms use a simplified IERS 2003 Earth Rotation Angle (ERA) model — just a Z-axis rotation by ERA(UT1), optionally augmented with polar motion (Rx/Ry on xp/yp) when EOP data is available. This works for demonstration purposes and yields accuracy on the order of meters to tens of meters at Earth's surface, but it **does not implement the actual IAU 2006/2000A precession-nutation model** that is the current international standard for celestial-to-terrestrial coordinate transformations.

The gap matters for several reasons:

1. **We are publishing functions named `ST_ECEF_To_ECI` and `ST_ECI_To_ECEF` with a `frame` argument that accepts `ICRF`, `J2000`, and `TEME`.** A user would reasonably expect these to be *correct* transforms to the named frame — not a rough approximation that collapses all three to the same Z-rotation. The current implementation silently ignores the frame distinction because it doesn't compute the bias-precession-nutation matrix that makes ICRF vs J2000 vs TEME actually different.

2. **The `dx`/`dy` columns in `postgis_eop` exist but are ignored by the transform code.** These are the IAU 2006/2000A Celestial Intermediate Pole (CIP) correction offsets published by IERS in finals2000A. They are meaningless as standalone rotations — they are small residual corrections to the full precession-nutation matrix. Applying them requires having the full matrix first.

3. **No ground-truth validation exists.** Round-trip tests pass trivially (R then R⁻¹ cancels regardless of what R is). Without comparison against an independent reference (ERFA, SPICE, astropy), we cannot claim any specific accuracy contract — we can only claim "this compiles and runs".

4. **Future GPU/SIMD work depends on knowing what's actually being computed.** The `multi-vendor-gpu-rollout` roadmap assumes the ECEF/ECI dispatch will eventually route through backend-specific kernels. Porting a simplified Z-rotation to CUDA and ROCm is ~50 LOC; porting a full IAU 2006/2000A reduction is a serious undertaking. We need to know which one we are actually committing to before we scale it across backends.

5. **"80% done" was a trap.** An earlier pass at this proposal scoped just "apply dx/dy as extra rotations" as a 30-LOC patch. That was wrong: it would have overclaimed precision we weren't delivering and baked in a shortcut that would be harder to replace later than to do right the first time.

This change commits to the full, correct IAU 2006/2000A model using ERFA as the reference implementation. ERFA (Essential Routines for Fundamental Astronomy) is the astropy project's BSD-licensed fork of IAU SOFA — GPL-compatible, widely validated, actively maintained, and used by the entire astronomy research community. Porting its routines (or vendoring a subset of them) gives us a real precision contract backed by the same math used by JPL, ESA, and every satellite tracker on Earth.

## What Changes

This change replaces the existing ERA-only transforms with a full IAU 2006/2000A implementation. It is a **significant new subsystem** — not a tweak — and will be delivered over multiple focused PRs as phases.

### Phase 1: Vendor ERFA subset and build integration

- Add `liblwgeom/erfa/` directory containing the subset of ERFA source files needed for precession-nutation reduction:
  - `t2c.c` (terrestrial-to-celestial 3x3 matrix for specified date)
  - `c2t.c` (celestial-to-terrestrial, the inverse)
  - `bp06.c` / `bpn2xy.c` / `s06.c` (bias-precession-nutation matrix from IAU 2006)
  - `pnm06a.c` (precession-nutation matrix, IAU 2006)
  - `era00.c` (Earth rotation angle, IAU 2000 — replaces our `lweci_earth_rotation_angle`)
  - `gmst06.c` / `gst06a.c` (Greenwich mean/apparent sidereal time for J2000/TEME conversions)
  - Supporting files: `rxr.c` (matrix multiply), `trxp.c` (matrix-vector), `ir.c`/`zp.c` (matrix init)
  - Nutation/precession coefficient tables: `nut00a.c` (IAU 2000A nutation), `xy06.c` (IAU 2006 X, Y series)
- Add ERFA copyright header and BSD license attribution to each vendored file.
- Update `liblwgeom/Makefile.in` to compile the `erfa/` subdirectory into `liblwgeom.a`.
- Add `liblwgeom/ERFA-VERSION.txt` recording the vendored ERFA release version and source URL.
- The vendored subset should be ~30 files, ~15k LOC, almost entirely coefficient tables.

### Phase 2: Replace `lweci_earth_rotation_angle` with ERFA-backed computation

- Rewrite `lweci_earth_rotation_angle()` in `liblwgeom/lwgeom_eci.c` to call `eraEra00()` (preserving the existing function signature so callers don't change).
- Rewrite `lweci_epoch_to_jd()` to produce the two-part (jd1, jd2) Julian Date form ERFA expects for high precision.
- Update `cu_eci.c` unit tests to verify `lweci_earth_rotation_angle()` matches `eraEra00()` reference values to machine precision (they should be bit-identical since we're now wrapping the same code).
- At this phase, the existing `ST_ECEF_To_ECI` function still uses Z-only rotation; only the internal ERA computation has been upgraded. No user-visible behavior change yet.

### Phase 3: Full bias-precession-nutation transforms

- Add new liblwgeom functions:
  - `lweci_bpn_matrix_iau2006(double jd1, double jd2, double matrix[3][3])` — build the 3x3 celestial-to-intermediate rotation matrix using `eraPnm06a()`.
  - `lweci_cip_correction(double dx, double dy, double matrix[3][3])` — apply the CIP offset corrections to the matrix via `eraXy2mx` (or equivalent helper).
  - `lweci_polar_motion_matrix(double xp, double yp, double sp, double matrix[3][3])` — build the polar motion matrix using `eraPom00()`.
- Rewrite `lwgeom_transform_ecef_to_eci_eop()` and `lwgeom_transform_eci_to_ecef_eop()` to:
  1. Build the full BPN matrix via `lweci_bpn_matrix_iau2006()`
  2. Apply CIP corrections via `lweci_cip_correction()`
  3. Build polar motion matrix via `lweci_polar_motion_matrix()`
  4. Multiply BPN × PM to get the full celestial-to-terrestrial (or inverse) matrix
  5. Apply to geometry points
- Update `ST_ECEF_To_ECI()` / `ST_ECI_To_ECEF()` (the non-EOP variants) to still work — they should build the BPN matrix with zero CIP corrections and zero polar motion, which is the correct IAU 2006/2000A result when EOP data is unavailable.
- Frame-specific handling: ICRF is the ERFA default; J2000 requires applying only the frame bias matrix (not precession-nutation); TEME requires Greenwich apparent sidereal time instead of ERA. Implement each frame correctly rather than collapsing them.

### Phase 4: Ground-truth regression tests

- Create `regress/core/ecef_eci_iau2006.sql` with a table of known test cases:
  - For each test case: epoch, ECEF coordinates, expected ICRF coordinates, expected J2000 coordinates, expected TEME coordinates
  - Values generated offline via ERFA directly (a small Python script using `erfa` package) and committed as inline arrays
  - Assertion tolerance: 1 micron (1e-6 meters) at Earth radius scale, which is well within what IAU 2006/2000A can deliver
- Create `regress/core/ecef_eci_iau2006_expected` with expected output
- Add test cases exercising:
  - Epoch near J2000.0 (2000-01-01 12:00 TT) — simplest case, minimal precession
  - Epoch at 2025 — exercises accumulated precession
  - Epoch with and without EOP data available — verifies fallback to zero-correction still gives IAU 2006/2000A result
  - Round-trip ECEF → ICRF → ECEF — should return identity within 1 micron
  - Cross-frame: ECEF → J2000 and ECEF → ICRF at same epoch — should differ by the known frame bias
- Add CUnit tests in `liblwgeom/cunit/cu_eci_iau2006.c` for each new lweci function against ERFA reference values.

### Phase 5: Deprecate the ERA-only shortcut and document the precision contract

- Mark the old `lweci_earth_rotation_angle` + manual `Rz` code path as a fallback-only routine with a clear comment stating it is NOT the IAU 2006/2000A transform and should only be used when ERFA linkage is unavailable.
- Add documentation in `doc/reference_accessor.xml` (or a new `doc/reference_ecef_eci.xml`) describing:
  - Which IAU model is used (2006/2000A)
  - What accuracy the contract guarantees (sub-millimeter at Earth radius for epochs within finals2000A range)
  - How EOP data affects the result (UT1, polar motion, CIP offsets all applied when available)
  - How the three frames (ICRF, J2000, TEME) are handled differently
- Update the fork's `openspec/specs/eci-coordinate-support` spec to reference the IAU 2006/2000A model.

### Explicitly out of scope

- **HTTP fetching in `postgis_refresh_eop()`** — remains a placeholder; external cron is the right pattern.
- **Full IAU 2000A nutation series with all 1,365 terms** if ERFA provides a simplified version that hits the tolerance. We will use whatever ERFA provides out of the box (`eraPnm06a` is the full model, so we will get the full series).
- **Upstream contribution** — this stays fork-internal until the upstream roadmap's Phase 3/4 opens it up.
- **GPU backend adaptation** — Phase 3 of this change delivers the scalar CPU path. GPU porting is a separate future change under `multi-vendor-gpu-rollout`.
- **Vendoring all of ERFA** — only the subset needed for our specific transforms. Other ERFA routines (ecliptic conversions, heliocentric, etc.) are not needed and are not vendored.

## Capabilities

### Modified Capabilities

- `earth-orientation-parameters` — Add requirement that EOP-enhanced transforms SHALL apply UT1, polar motion, AND CIP offsets via the IAU 2006/2000A bias-precession-nutation matrix, not a simplified ERA rotation.
- `eci-coordinate-support` — Add requirement that ECI frame conversions SHALL use the IAU 2006/2000A model, and that ICRF, J2000, and TEME SHALL produce distinct results reflecting their respective frame definitions.

### New Capabilities

- `iau2006-precession-nutation` — New capability documenting the vendored ERFA subset, the precision contract (1 micron at Earth radius when EOP data is available), the frame handling model (ICRF / J2000 / TEME), and the fallback behavior when EOP data is not available.

## Impact

- **Code**:
  - Phase 1: ~15,000 LOC of vendored ERFA source (almost entirely coefficient tables, not logic)
  - Phase 2: ~50 LOC rewrite of `lweci_earth_rotation_angle` and `lweci_epoch_to_jd`
  - Phase 3: ~300 LOC of new transform code in `liblwgeom/lwgeom_eci.c`
  - Phase 4: ~200 LOC regression test + ~150 LOC CUnit tests + small Python helper script for reference-value generation (not shipped)
  - Phase 5: ~100 LOC documentation
  - **Total**: ~15,800 LOC, of which ~15,000 is vendored third-party (BSD-licensed) and ~800 is new fork code
- **Precision contract**: sub-millimeter (target: 1 micron) ECEF↔ECI round-trip when EOP data is available for the epoch, monotonic accuracy loss as epoch moves outside the finals2000A range
- **Licensing**: ERFA is BSD-3-Clause, GPL-compatible, attribution preserved in each vendored file and in `ERFA-VERSION.txt`
- **Performance**: per-point transform cost increases from ~3 rotations (ERA-only) to ~2 matrix multiplies (3x3 × 3x1 = 9 FLOPs × 2 = 18 FLOPs per point). Negligible in absolute terms; possibly slower than current code by a small constant factor. Not the bottleneck for any realistic workload.
- **Build**: new `liblwgeom/erfa/` subdirectory; no new external dependencies
- **Test time**: regression test adds ~1 second to `make check` runtime
- **Binary size**: liblwgeom grows by ~1 MB due to precession-nutation coefficient tables (almost all static const data)
- **Upgrade path**: existing `postgis_eop` table and `postgis_eop_load()` are unchanged. The `_postgis_*_eop` C functions change internal implementation but keep the same SQL signature — no extension version bump required, no stubs needed.
- **Upstream contribution implication**: None. Still `FORK_SPECIFIC` because the ERFA vendoring is a fork-specific design choice — upstream PostGIS would likely prefer linking to a system ERFA package if they adopted this.

## Open Questions

1. **Vendor ERFA source files directly into the tree, or add ERFA as a git submodule?** — Recommendation: vendor directly. PostGIS has no submodules today; adding one is extra complexity. ERFA is stable (coefficient tables don't change; new releases fix typos, not logic). We commit to a specific ERFA release and re-vendor when we need updates.

2. **Link against system ERFA if available?** — Recommendation: no, for two reasons. (1) ERFA is not widely packaged outside astronomy distributions; most Linux systems don't have it. (2) Version skew between vendored and system would be a nightmare to test. Always use the vendored copy.

3. **How much of ERFA do we actually need?** — Initial estimate: ~30 files for precession-nutation + ERA + polar motion + matrix utilities. Subset will be finalized during Phase 1 implementation by tracing function dependencies starting from `eraPnm06a` and `eraEra00`.

4. **Should we support IAU 1976 as a legacy option?** — No. The ERA-only code path remains as a fallback for environments where the vendored ERFA doesn't build (which should be nobody), but we do not expose a user-facing "model" argument. The fork always computes IAU 2006/2000A.

5. **Do we need to run the regression test against a specific operating system's ERFA to detect platform-specific drift?** — No. ERFA is pure C math with no platform-specific behavior. If the tests pass on the CI reference platform, they pass everywhere.

6. **How do we keep the vendored ERFA in sync with upstream releases?** — We don't, unless there's a concrete reason. The IAU 2006/2000A model is standardized. New ERFA releases are typo fixes and additions of other (unrelated) routines. We re-vendor only if a bug is found in the specific files we use.

## Phased delivery

Each phase ships as a focused PR against fork develop:

- **PR A** — Phase 1: vendor ERFA subset, compile clean, no functional change yet. (Largest diff by far, but purely mechanical.)
- **PR B** — Phase 2: swap ERA computation to ERFA-backed, unit tests prove bit-identical results. (Small.)
- **PR C** — Phase 3: implement full BPN transforms with CIP corrections and polar motion matrix. (Medium.)
- **PR D** — Phase 4: ground-truth regression tests. (Medium.)
- **PR E** — Phase 5: docs and fallback marking. (Small.)

Each phase is independently reviewable and mergeable. If Phase 1 turns out to have unexpected build problems, we halt before touching any live code.
