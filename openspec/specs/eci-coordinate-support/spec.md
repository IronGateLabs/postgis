## Purpose

Core PostGIS support for ECI (Earth-Centered Inertial) coordinate frames, including frame identification, epoch-parameterized transformations, geometry storage with temporal coordinates, and PROJ version gating.

## Requirements

### Requirement: ECI frame identification
The system SHALL identify Earth-Centered Inertial (ECI) coordinate systems (ICRF, J2000, TEME) as a distinct CRS family (`LW_CRS_INERTIAL`) separate from ECEF/geocentric, based on PROJ CRS metadata or a curated SRID registry.

#### Scenario: ICRF frame recognized
- **WHEN** a geometry is assigned an SRID corresponding to an ICRF/J2000 inertial frame
- **THEN** the internal CRS family classification SHALL be `LW_CRS_INERTIAL`

#### Scenario: ECI vs ECEF distinction
- **WHEN** two geometries exist, one with an ECI SRID and one with ECEF SRID 4978
- **THEN** the system SHALL classify them under different CRS families and prevent direct spatial operations between them without explicit transformation

#### Scenario: ECI SRIDs blocked by spatial function guards
- **WHEN** an error-class spatial function (e.g., `ST_Area`, `ST_Buffer`, `ST_Perimeter`) is called on a geometry with an ECI SRID (900001-900003)
- **THEN** the `srid_check_crs_family_not_geocentric()` guard SHALL block the call with an error, same as for ECEF/geocentric SRIDs
- **AND** the error message SHALL indicate that the operation is not supported for this CRS family

> **Note (contract alignment v0.2.0):** The guard function `srid_check_crs_family_not_geocentric()` scope was broadened to block both `LW_CRS_GEOCENTRIC` and `LW_CRS_INERTIAL` CRS families. See the `ecef-coordinate-support` spec for the full list of error-class functions.

### Requirement: Epoch-parameterized ECI-to-ECEF transformation (IAU 2006/2000A)
The system SHALL support transformation between ECI and ECEF frames with an epoch parameter, implementing the **IAU 2006 precession** combined with the **IAU 2000A nutation** model as the celestial-to-terrestrial rotation. Distinct handling is provided for the three supported frames (ICRF, J2000, TEME); they SHALL NOT collapse to the same rotation. Transforms are built via the vendored ERFA subset (`liblwgeom/erfa/`) and the per-geometry rotation matrix is amortized across all points.

Precision contract: ~1 micrometer at Earth radius when EOP data is available (ST_ECEF_To_ECI_EOP with `postgis_eop` populated from IERS Bulletin A), and ~6 centimeters at Earth radius when EOP data is unavailable (zero-correction IAU 2006/2000A via ST_ECEF_To_ECI).

The epoch MAY be provided as the M coordinate of `POINT4D` geometries (uses the simplified Z-rotation per-point path for speed), or as an explicit parameter to `ST_Transform`, `ST_ECEF_To_ECI`, or `ST_ECI_To_ECEF` (uses the full IAU 2006/2000A matrix path).

#### Scenario: ICRF, J2000, and TEME produce distinct results
- **WHEN** `ST_ECEF_To_ECI(geom, epoch, frame)` is called with the same geometry and epoch for each of the three frames `ICRF`, `J2000`, and `TEME`
- **THEN** the three results SHALL differ in a way consistent with their physical definitions
- **AND** the ICRF and J2000 results SHALL differ by the IAU 2000 frame bias (sub-meter at Earth radius)
- **AND** the TEME result SHALL differ from ICRF and J2000 by tens of kilometers because TEME uses Greenwich Mean Sidereal Time instead of Earth Rotation Angle

#### Scenario: ECI to ECEF with M-coordinate epoch
- **WHEN** `ST_Transform(eci_geom, 4978)` is called on an ECI geometry where each point's M coordinate contains the epoch (decimal year)
- **THEN** the transformation SHALL apply the per-point Z-axis rotation (simplified ERA-only model, SIMD-dispatched) for each point's epoch
- **AND** the per-point path does NOT use the full IAU 2006/2000A matrix because per-point matrix rebuild would be prohibitive; callers needing precision SHALL use `ST_ECEF_To_ECI(geom, epoch, frame)` with a fixed epoch instead

#### Scenario: ECI to ECEF with explicit epoch parameter
- **WHEN** `ST_Transform(eci_geom, 4978, epoch => '2025-01-01T00:00:00Z')` is called with a single epoch for the entire geometry
- **THEN** the transformation SHALL apply Earth rotation correction at the specified epoch for all points

#### Scenario: Missing epoch raises error
- **WHEN** an ECI-to-ECEF transformation is attempted without epoch information (no M coordinates and no explicit epoch parameter)
- **THEN** the system SHALL raise an error indicating that epoch is required for inertial-to-fixed frame transformations

#### Scenario: Dedicated SQL wrapper functions
- **WHEN** `ST_ECEF_To_ECI(geom, epoch, frame)` or `ST_ECI_To_ECEF(geom, epoch)` is called
- **THEN** the function SHALL perform the conversion using the C transform functions with the TIMESTAMPTZ epoch converted to decimal-year

### Requirement: ECI geometry storage
The system SHALL store ECI geometries using the same `GEOMETRY` type with 3D or 4D coordinates (X/Y/Z in meters from Earth center, M as epoch when present). ECI geometries SHALL be distinguished from ECEF by their SRID.

#### Scenario: Store ECI point with epoch
- **WHEN** a user inserts `ST_SetSRID(ST_MakePointM(x, y, z, epoch), eci_srid)`
- **THEN** the geometry SHALL be stored with the ECI SRID, X/Y/Z as inertial-frame Cartesian coordinates in meters, and M as the epoch value

#### Scenario: ECI coordinate retrieval
- **WHEN** a user retrieves an ECI geometry with `ST_X`, `ST_Y`, `ST_Z`, `ST_M`
- **THEN** the values SHALL match the stored inertial-frame coordinates and epoch exactly

#### Scenario: ECI SRIDs registered in spatial_ref_sys
- **WHEN** a user queries `spatial_ref_sys` for SRID 900001, 900002, or 900003
- **THEN** the system SHALL return entries for ICRF, J2000, and TEME frames with appropriate auth_name, auth_srid, srtext, and proj4text values

### Requirement: ECI bounding box computation
The system SHALL compute bounding boxes for ECI geometries using 3D Cartesian metric ranges, similar to ECEF. Temporal extent (epoch range) SHALL be tracked in the M bounds of the GBOX when M coordinates are present.

#### Scenario: GBOX for ECI trajectory
- **WHEN** a bounding box is computed for an ECI linestring representing a satellite trajectory with varying epochs
- **THEN** the GBOX SHALL contain correct X/Y/Z metric bounds AND mmin/mmax reflecting the earliest and latest epochs

### Requirement: PROJ version gating for ECI features
ECI transformation features SHALL NOT be gated by PROJ version. The ECI transform implementation uses pure C Earth Rotation Angle math (IERS 2003) with no PROJ dependency. PROJ version gating applies only to non-ECI time-dependent transforms (dynamic datums, plate motion).

#### Scenario: ECI transform on PROJ 6.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 6.x
- **THEN** the transformation SHALL proceed using the pure C ERA computation, since ECI transforms do not depend on PROJ

#### Scenario: ECI transform on PROJ 9.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 9.x+
- **THEN** the transformation SHALL proceed using the pure C ERA computation (same as PROJ 6.x)

#### Scenario: Compile-time gating for non-ECI time-dependent transforms
- **WHEN** PostGIS is compiled against PROJ < 9.x
- **THEN** non-ECI time-dependent transforms (dynamic datums, plate motion) SHALL be gated with `#if POSTGIS_PROJ_VERSION >= 90000` macros, while ECI transforms remain available
