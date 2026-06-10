## Why

PostGIS has substantial C-level ECI infrastructure on the develop branch -- `LW_CRS_INERTIAL` enum, SRID macros (900001-900099), `SRID_IS_ECI()`, `lwgeom_transform_ecef_to_eci()`/`lwgeom_transform_eci_to_ecef()` with simplified ERA computation, `lwgeom_eci_compute_gbox()`, and comprehensive unit tests in `cu_eci.c`. The `feature/ecef-eci-implementation` branch extends this with SQL wrappers (`ST_ECEF_To_ECI`, `ST_ECI_To_ECEF`), ECI SRID registration in `spatial_ref_sys`, a `postgis_ecef_eci` extension package, DocBook documentation, and regression tests -- but this work has not been merged to develop.

Three gaps remain to fulfill the `eci-coordinate-support` spec:

1. **ST_Transform epoch integration** -- the spec requires `ST_Transform(eci_geom, 4978, epoch => ...)` to transparently handle ECI/ECEF conversion. No branch implements this; the feature branch uses separate `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` functions instead.
2. **ECI SRID registration** -- the `spatial_ref_sys` entries for ICRF (900001), J2000 (900002), and TEME (900003) exist only on the feature branch.
3. **PROJ version gating refinement** -- compile-time macros exist, but the spec requires a runtime error for old PROJ versions. Since ECI transforms use pure C ERA math (not PROJ), the gating strategy needs clarification.

## What Changes

- Merge the `feature/ecef-eci-implementation` branch work (SQL wrappers, SRID registration, extension packaging, docs, regression tests) into develop
- Add a new `ST_Transform` overload accepting an epoch parameter for ECI/ECEF conversions, dispatching to the existing C transform functions when source or target CRS family is `LW_CRS_INERTIAL`
- Implement M-coordinate epoch extraction in the `ST_Transform` code path so `ST_Transform(eci_geom, 4978)` uses per-point M values as epochs
- Refine PROJ version gating: since ECI transforms use pure C ERA math, PROJ is not required for ECI/ECEF conversion. Gate only the PROJ-dependent path (non-ECI transforms); ECI transforms work regardless of PROJ version.
- Add error handling for missing epoch (no M coordinate and no explicit epoch parameter)

## Capabilities

### New Capabilities

(none -- this change closes gaps in an existing spec)

### Modified Capabilities

- `eci-coordinate-support` requirement 2 (Epoch-parameterized ECI-to-ECEF transformation): Add `ST_Transform` epoch parameter overload and M-coordinate epoch path, complementing the dedicated `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` functions from the feature branch
- `eci-coordinate-support` requirement 3 (ECI geometry storage): Merge ECI SRID registration from feature branch to develop
- `eci-coordinate-support` requirement 5 (PROJ version gating): Refine gating strategy -- ECI transforms use pure C math and do not require PROJ; remove misleading PROJ version requirement for ECI-specific paths

## Impact

- **C layer (`liblwgeom/`)**: No changes to existing `lwgeom_eci.c` or `liblwgeom.h.in`. The C infrastructure is complete.
- **SQL layer (`postgis/`)**: New `ST_Transform(geometry, integer, timestamptz)` overload in `postgis.sql.in`. Modifications to `lwgeom_transform.c` (or the `postgis_ecef_eci` C wrapper) to detect ECI CRS family and dispatch to `lwgeom_transform_ecef_to_eci()`/`lwgeom_transform_eci_to_ecef()`.
- **`spatial_ref_sys`**: Three INSERT statements for ECI SRIDs 900001-900003 (from feature branch).
- **Extension**: `postgis_ecef_eci` extension packaging (from feature branch) provides `ST_ECEF_To_ECI`, `ST_ECI_To_ECEF`, EOP infrastructure, SRID registration.
- **Regression tests**: Merge existing tests from feature branch; add new tests for `ST_Transform` epoch overload and M-coordinate epoch extraction.
- **Dependencies**: No new external dependencies. PROJ is not required for ECI transforms (pure C ERA math).
- **Risk**: Medium -- `ST_Transform` is a heavily-used function; adding an overload requires careful signature design to avoid ambiguity with existing overloads.
