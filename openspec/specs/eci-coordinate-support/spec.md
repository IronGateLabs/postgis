## ADDED Requirements

### Requirement: ECI frame identification
The system SHALL identify Earth-Centered Inertial (ECI) coordinate systems (ICRF, J2000, TEME) as a distinct CRS family (`LW_CRS_INERTIAL`) separate from ECEF/geocentric, based on PROJ CRS metadata or a curated SRID registry.

#### Scenario: ICRF frame recognized
- **WHEN** a geometry is assigned an SRID corresponding to an ICRF/J2000 inertial frame
- **THEN** the internal CRS family classification SHALL be `LW_CRS_INERTIAL`

#### Scenario: ECI vs ECEF distinction
- **WHEN** two geometries exist, one with an ECI SRID and one with ECEF SRID 4978
- **THEN** the system SHALL classify them under different CRS families and prevent direct spatial operations between them without explicit transformation

### Requirement: Epoch-parameterized ECI-to-ECEF transformation
The system SHALL support transformation between ECI and ECEF frames with an epoch parameter that accounts for Earth's rotation. The epoch MAY be provided as the M coordinate of `POINT4D` geometries or as an explicit parameter to `ST_Transform`.

#### Scenario: ECI to ECEF with M-coordinate epoch
- **WHEN** `ST_Transform(eci_geom, 4978)` is called on an ECI geometry where each point's M coordinate contains the epoch (Julian date or Unix timestamp)
- **THEN** the transformation SHALL apply Earth rotation correction for each point's epoch, producing correct ECEF coordinates

#### Scenario: ECI to ECEF with explicit epoch parameter
- **WHEN** `ST_Transform(eci_geom, 4978, epoch => '2025-01-01T00:00:00Z')` is called with a single epoch for the entire geometry
- **THEN** the transformation SHALL apply Earth rotation correction at the specified epoch for all points

#### Scenario: Missing epoch raises error
- **WHEN** an ECI-to-ECEF transformation is attempted without epoch information (no M coordinates and no explicit epoch parameter)
- **THEN** the system SHALL raise an error indicating that epoch is required for inertial-to-fixed frame transformations

### Requirement: ECI geometry storage
The system SHALL store ECI geometries using the same `GEOMETRY` type with 3D or 4D coordinates (X/Y/Z in meters from Earth center, M as epoch when present). ECI geometries SHALL be distinguished from ECEF by their SRID.

#### Scenario: Store ECI point with epoch
- **WHEN** a user inserts `ST_SetSRID(ST_MakePointM(x, y, z, epoch), eci_srid)`
- **THEN** the geometry SHALL be stored with the ECI SRID, X/Y/Z as inertial-frame Cartesian coordinates in meters, and M as the epoch value

#### Scenario: ECI coordinate retrieval
- **WHEN** a user retrieves an ECI geometry with `ST_X`, `ST_Y`, `ST_Z`, `ST_M`
- **THEN** the values SHALL match the stored inertial-frame coordinates and epoch exactly

### Requirement: ECI bounding box computation
The system SHALL compute bounding boxes for ECI geometries using 3D Cartesian metric ranges, similar to ECEF. Temporal extent (epoch range) SHALL be tracked in the M bounds of the GBOX when M coordinates are present.

#### Scenario: GBOX for ECI trajectory
- **WHEN** a bounding box is computed for an ECI linestring representing a satellite trajectory with varying epochs
- **THEN** the GBOX SHALL contain correct X/Y/Z metric bounds AND mmin/mmax reflecting the earliest and latest epochs

### Requirement: PROJ version gating for ECI features
ECI transformation features that require PROJ 9.x dynamic datum support SHALL be compile-time gated and produce clear error messages when the installed PROJ version is insufficient.

#### Scenario: ECI transform on PROJ 6.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 6.x
- **THEN** the system SHALL raise an error: "ECI transformations require PROJ version 9.0 or later"

#### Scenario: ECI transform on PROJ 9.x
- **WHEN** ECI-to-ECEF transformation is attempted on a PostGIS build linked against PROJ 9.x+
- **THEN** the transformation SHALL proceed using PROJ's time-dependent transformation capabilities
