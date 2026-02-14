## MODIFIED Requirements

### Requirement: Epoch-parameterized ECI-to-ECEF transformation
The system SHALL support transformation between ECI and ECEF frames with an epoch parameter that accounts for Earth's rotation. The epoch MAY be provided as the M coordinate of `POINT4D` geometries or as an explicit parameter to `ST_Transform`.

#### Scenario: ECI to ECEF with M-coordinate epoch
- **WHEN** `ST_Transform(eci_geom, 4978)` is called on an ECI geometry where each point's M coordinate contains the epoch (Julian date or Unix timestamp)
- **THEN** the transformation SHALL apply Earth rotation correction for each point's epoch, producing correct ECEF coordinates

**Gap:** The C functions `lwgeom_transform_eci_to_ecef()` and `lwgeom_transform_ecef_to_eci()` exist on develop and accept a single epoch parameter. The `feature/ecef-eci-implementation` branch exposes these as `ST_ECEF_To_ECI`/`ST_ECI_To_ECEF` SQL functions with TIMESTAMPTZ epoch. However, no branch implements per-point M-coordinate epoch extraction in `ST_Transform`. This requires modifying the `ST_Transform` code path to detect `LW_CRS_INERTIAL` source/target CRS family and iterate points, extracting M as epoch.

#### Scenario: ECI to ECEF with explicit epoch parameter
- **WHEN** `ST_Transform(eci_geom, 4978, epoch => '2025-01-01T00:00:00Z')` is called with a single epoch for the entire geometry
- **THEN** the transformation SHALL apply Earth rotation correction at the specified epoch for all points

**Gap:** No `ST_Transform` overload accepting a TIMESTAMPTZ epoch parameter exists on any branch. A new SQL function signature `ST_Transform(geometry, integer, timestamptz)` must be added, with a C implementation that converts TIMESTAMPTZ to decimal-year and delegates to the existing `lwgeom_transform_eci_to_ecef()` or `lwgeom_transform_ecef_to_eci()`.

#### Scenario: Missing epoch raises error
- **WHEN** an ECI-to-ECEF transformation is attempted without epoch information (no M coordinates and no explicit epoch parameter)
- **THEN** the system SHALL raise an error indicating that epoch is required for inertial-to-fixed frame transformations

**Gap:** The C layer already raises `lwerror("epoch is required for ECI-to-ECEF conversion")`. The SQL path through `ST_Transform(geometry, integer)` must detect ECI CRS family and check for M coordinates before calling the C function, raising a user-friendly SQL error if epoch is missing.

#### Scenario: Dedicated SQL wrapper functions
- **WHEN** `ST_ECEF_To_ECI(geom, epoch, frame)` or `ST_ECI_To_ECEF(geom, epoch)` is called
- **THEN** the function SHALL perform the conversion using the C transform functions with the TIMESTAMPTZ epoch converted to decimal-year

**Status:** Implemented on `feature/ecef-eci-implementation` branch in `postgis/ecef_eci.sql.in` and `postgis/lwgeom_ecef_eci.c`. Needs merge to develop.

### Requirement: ECI geometry storage
The system SHALL store ECI geometries using the same `GEOMETRY` type with 3D or 4D coordinates (X/Y/Z in meters from Earth center, M as epoch when present). ECI geometries SHALL be distinguished from ECEF by their SRID.

#### Scenario: Store ECI point with epoch
- **WHEN** a user inserts `ST_SetSRID(ST_MakePointM(x, y, z, epoch), eci_srid)`
- **THEN** the geometry SHALL be stored with the ECI SRID, X/Y/Z as inertial-frame Cartesian coordinates in meters, and M as the epoch value

**Status:** Storage works inherently through PostGIS geometry type. No code changes needed.

#### Scenario: ECI coordinate retrieval
- **WHEN** a user retrieves an ECI geometry with `ST_X`, `ST_Y`, `ST_Z`, `ST_M`
- **THEN** the values SHALL match the stored inertial-frame coordinates and epoch exactly

**Status:** Works inherently. Verified in regression tests on the feature branch.

#### Scenario: ECI SRIDs registered in spatial_ref_sys
- **WHEN** a user queries `spatial_ref_sys` for SRID 900001, 900002, or 900003
- **THEN** the system SHALL return entries for ICRF, J2000, and TEME frames with appropriate auth_name, auth_srid, srtext, and proj4text values

**Gap:** SRID registration exists only on `feature/ecef-eci-implementation` branch in `postgis/ecef_eci.sql.in`. Needs merge to develop. The INSERT statements use auth_name='POSTGIS' since these are not EPSG-registered CRS.

### Requirement: PROJ version gating for ECI features
ECI transformation features that require PROJ 9.x dynamic datum support SHALL be compile-time gated and produce clear error messages when the installed PROJ version is insufficient.

#### Scenario: ECI transform on PROJ 6.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 6.x
- **THEN** the transformation SHALL proceed using the pure C ERA computation, since ECI transforms do not depend on PROJ

**Refinement:** The original spec required an error on PROJ < 9.x. Since the ECI transform implementation uses pure C Earth Rotation Angle math (`lweci_earth_rotation_angle()` in `lwgeom_eci.c`), PROJ is not invoked for ECI/ECEF conversions. The PROJ version gate applies only to non-ECI transforms that use PROJ's time-dependent pipeline (e.g., dynamic datum shifts). ECI transforms work correctly regardless of PROJ version.

#### Scenario: ECI transform on PROJ 9.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 9.x+
- **THEN** the transformation SHALL proceed using the pure C ERA computation (same as PROJ 6.x)

**Refinement:** PROJ 9.x is not required for ECI transforms. The original scenario assumed ECI transforms would use PROJ's time-dependent capabilities, but the implementation uses standalone C math. PROJ 9.x may be relevant for future precision enhancements (e.g., EOP-enhanced nutation models), but the base implementation has no PROJ dependency.

#### Scenario: Compile-time gating for non-ECI time-dependent transforms
- **WHEN** PostGIS is compiled against PROJ < 9.x
- **THEN** non-ECI time-dependent transforms (dynamic datums, plate motion) SHALL be gated with `#if POSTGIS_PROJ_VERSION >= 90000` macros, while ECI transforms remain available

**Status:** Compile-time macros exist in `liblwgeom.h.in`. The distinction between ECI (no PROJ dependency) and non-ECI time-dependent transforms (PROJ dependency) is the key refinement.
