## Context

PostGIS has a two-layer ECI implementation split across branches:

**On develop (merged):**
- `liblwgeom/liblwgeom.h.in`: `LW_CRS_INERTIAL` enum value, SRID macros (`SRID_ECI_ICRF=900001`, `SRID_ECI_J2000=900002`, `SRID_ECI_TEME=900003`), `SRID_IS_ECI()` macro, `lwcrs_family_requires_epoch()` macro
- `liblwgeom/lwgeom_eci.c`: `lweci_earth_rotation_angle()` (IERS 2003 simplified ERA), `lwgeom_transform_eci_to_ecef()`, `lwgeom_transform_ecef_to_eci()`, `lwgeom_eci_compute_gbox()`
- `liblwgeom/lwgeom_transform.c`: `lwsrid_get_crs_family()` returns `LW_CRS_INERTIAL` for SRIDs 900001-900099
- `liblwgeom/cunit/cu_eci.c`: ~850 lines of unit tests covering ERA, round-trip transforms, bounding boxes, edge cases
- `liblwgeom/cunit/cu_crs_family.c`: CRS family classification tests including ECI SRID checks

**On `feature/ecef-eci-implementation` (not merged):**
- `postgis/ecef_eci.sql.in`: SQL function declarations for `ST_ECEF_To_ECI`, `ST_ECI_To_ECEF`, ECEF accessors, ECI SRID INSERT statements, EOP table schema
- `postgis/lwgeom_ecef_eci.c`: C implementations of SQL wrapper functions
- `extensions/postgis_ecef_eci/`: Extension packaging (Makefile.in, control file)
- `doc/ecef_eci.xml`: DocBook documentation (711 lines)
- `regress/core/ecef_eci.sql` + `ecef_eci_expected`: Regression tests (320 lines)

The remaining gap is `ST_Transform` integration: no branch implements the spec's requirement for `ST_Transform(eci_geom, 4978)` to automatically detect ECI CRS family and apply epoch-based rotation.

## Goals / Non-Goals

**Goals:**
- Merge the feature branch SQL interface, SRID registration, extension packaging, docs, and tests to develop
- Add an `ST_Transform` overload that accepts a TIMESTAMPTZ epoch parameter for ECI/ECEF conversions
- Implement M-coordinate epoch extraction in `ST_Transform` when source or target is `LW_CRS_INERTIAL`
- Clarify PROJ version gating: ECI transforms work without PROJ
- Ensure all five spec requirements are fully satisfied

**Non-Goals:**
- Modifying the C-level ERA computation or transform algorithms
- Implementing full IAU 2006/2000A precession-nutation (future EOP enhancement)
- Changing the `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` functions from the feature branch
- Adding ECI support to `ST_Distance`, `ST_Area`, or other spatial analysis functions
- Implementing inter-ECI-frame transforms (ICRF-to-TEME) -- these require precession-nutation models

## Decisions

### Decision 1: ST_Transform epoch integration strategy

**Choice:** Add a new SQL overload `ST_Transform(geometry, integer, timestamptz)` that accepts an explicit epoch. Additionally, modify the existing `ST_Transform(geometry, integer)` code path to detect `LW_CRS_INERTIAL` CRS family and extract per-point M coordinates as epochs.

**Rationale:** The spec explicitly requires both paths: M-coordinate epoch and explicit epoch parameter. The dedicated `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` functions from the feature branch serve users who want explicit frame control, while `ST_Transform` integration provides a familiar interface for users who think in terms of SRID-to-SRID transformation.

**Alternatives considered:**
- (A) Only use dedicated functions, skip `ST_Transform` integration -- rejected because the spec explicitly requires `ST_Transform` support, and users expect SRID-based transform workflows to work uniformly
- (B) Use a TEXT parameter `ST_Transform(geom, srid, epoch => '...')` -- rejected because TIMESTAMPTZ is the natural PostgreSQL type for timestamps; TEXT would require parsing

**Implementation sketch:** In `postgis/lwgeom_transform.c` (the `transform()` PG function), after determining `source_crs_family` and `target_crs_family`, check if either is `LW_CRS_INERTIAL`. If so, bypass the PROJ pipeline and dispatch to `lwgeom_transform_ecef_to_eci()` or `lwgeom_transform_eci_to_ecef()` with the epoch. For the M-coordinate path, iterate points and call the C transform per-point. For the explicit epoch overload, convert TIMESTAMPTZ to decimal-year and pass uniformly.

### Decision 2: Merge strategy for feature branch

**Choice:** Cherry-pick the SQL interface commits from `feature/ecef-eci-implementation` into develop, rather than merging the entire branch.

**Rationale:** The feature branch contains 4 commits beyond develop (SQL interface, build fix, docs, EOP bug fix). Cherry-picking allows selecting only the relevant changes and resolving any conflicts with develop's current state. A full merge could pull in intermediate states.

**Alternatives considered:**
- (A) Full merge of `feature/ecef-eci-implementation` into develop -- viable but risks merge conflicts with spec/design work already on develop
- (B) Rebase feature branch onto develop, then fast-forward merge -- cleaner history but rewrites commits

### Decision 3: PROJ version gating for ECI

**Choice:** ECI transforms SHALL NOT be gated by PROJ version. The PROJ version check applies only to non-ECI time-dependent transforms (dynamic datums, plate motion models).

**Rationale:** The ECI transform implementation uses pure C math: `lweci_earth_rotation_angle()` computes Earth Rotation Angle from the IERS 2003 formula using only standard C `fmod()`. No PROJ functions are called during ECI/ECEF conversion. Gating ECI transforms on PROJ version would artificially restrict functionality that has no PROJ dependency.

**Implementation:** Remove or update any compile-time guards (`#if POSTGIS_PROJ_VERSION >= ...`) that gate ECI-specific functions. Keep guards on code paths that actually call PROJ for time-dependent operations. Add a brief comment in `lwgeom_eci.c` noting the PROJ-independence.

**Alternatives considered:**
- (A) Gate ECI on PROJ 9.x anyway for future-proofing -- rejected because it prevents users with PROJ 6.x-8.x from using ECI transforms that work correctly
- (B) Remove all PROJ version guards -- rejected because non-ECI time-dependent transforms genuinely need PROJ 9.x features

### Decision 4: Epoch parameter type and conversion

**Choice:** The SQL `ST_Transform` epoch overload SHALL accept `TIMESTAMPTZ`. The C boundary converts to decimal-year using PostgreSQL's `DatumGetTimestampTz()` and arithmetic equivalent to the feature branch's existing conversion in `lwgeom_ecef_eci.c`.

**Rationale:** Consistent with Decision 1 in the `ecef-eci-sql-interface` archived design (TIMESTAMPTZ at SQL level, decimal-year internally). Users work with PostgreSQL timestamp types; the C layer expects numeric epochs.

## Risks / Trade-offs

- **[Risk] ST_Transform overload ambiguity** -- Adding `ST_Transform(geometry, integer, timestamptz)` could create overload resolution issues with existing `ST_Transform(geometry, integer)` when a third argument is provided. PostgreSQL's function resolution rules handle this via exact type matching, but it should be verified with test cases. Mitigation: regression tests with explicit epoch and without.

- **[Risk] M-coordinate epoch interpretation** -- The spec allows M to be "Julian date or Unix timestamp" but does not specify which. The C layer uses decimal-year internally. This ambiguity could produce incorrect transforms if M contains an unexpected epoch format. Mitigation: document the expected M format (decimal-year matching the C layer) and add validation that raises an error for clearly out-of-range values.

- **[Risk] Cherry-pick conflicts** -- The feature branch commits may conflict with develop's current state (spec/design files, Makefile changes). Mitigation: review diff before cherry-picking; resolve conflicts favoring develop's current state for non-code files.

- **[Trade-off] Two transform interfaces** -- Users can transform ECI/ECEF via `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` or via `ST_Transform`. Having two paths adds API surface but serves different user mental models. The dedicated functions offer explicit frame control and TIMESTAMPTZ epoch; `ST_Transform` offers SRID-based uniformity and M-coordinate epochs.

- **[Trade-off] Simplified ERA vs full precession-nutation** -- The current implementation uses IERS 2003 simplified ERA, which provides sub-arcsecond accuracy for current epochs but degrades for historical dates. Full IAU 2006/2000A precession-nutation is deferred to the EOP enhancement. Users needing arcsecond-level precision for historical epochs should be warned.
