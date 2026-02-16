## Purpose

SQL-level functions for converting between ECEF and ECI coordinate frames, wrapping existing C transform infrastructure.

## Requirements

### Requirement: SQL-level ECEF-to-ECI frame conversion
The system SHALL provide a SQL function `ST_ECEF_To_ECI(geometry, timestamptz, text)` that converts an ECEF geometry (SRID 4978) to an ECI frame geometry by wrapping the existing `lwgeom_transform_ecef_to_eci()` C function. The function SHALL accept a TIMESTAMPTZ epoch and a TEXT frame parameter (default 'ICRF').

#### Scenario: Convert ECEF point to ECI at a known epoch
- **WHEN** `ST_ECEF_To_ECI(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978), '2025-01-01 00:00:00+00'::timestamptz)` is called
- **THEN** the result SHALL be a geometry with SRID 900001 (ICRF) whose X/Y/Z coordinates reflect the Earth Rotation Angle applied at the given epoch

#### Scenario: Convert ECEF to J2000 frame
- **WHEN** `ST_ECEF_To_ECI(ecef_geom, epoch, 'J2000')` is called
- **THEN** the result SHALL have SRID 900002 and coordinates rotated by the ERA for the J2000 frame

#### Scenario: Convert ECEF to TEME frame
- **WHEN** `ST_ECEF_To_ECI(ecef_geom, epoch, 'TEME')` is called
- **THEN** the result SHALL have SRID 900003

### Requirement: SQL-level ECI-to-ECEF frame conversion
The system SHALL provide a SQL function `ST_ECI_To_ECEF(geometry, timestamptz, text)` that converts an ECI geometry (SRID 900001-900003) to an ECEF geometry (SRID 4978) by wrapping the existing `lwgeom_transform_eci_to_ecef()` C function.

#### Scenario: Convert ECI point to ECEF
- **WHEN** `ST_ECI_To_ECEF(eci_geom, '2025-01-01 00:00:00+00'::timestamptz)` is called on a geometry with SRID 900001
- **THEN** the result SHALL be a geometry with SRID 4978 whose coordinates reflect the inverse ERA rotation

#### Scenario: Round-trip conversion preserves coordinates
- **WHEN** an ECEF point is converted to ECI and back via `ST_ECI_To_ECEF(ST_ECEF_To_ECI(ecef_geom, epoch), epoch)`
- **THEN** the resulting coordinates SHALL match the original ECEF coordinates within 1mm precision

### Requirement: TIMESTAMPTZ epoch parameter
The frame conversion functions SHALL accept TIMESTAMPTZ as the epoch type at the SQL level and convert it to the decimal-year format expected by the C layer internally.

#### Scenario: TIMESTAMPTZ to decimal-year conversion
- **WHEN** `ST_ECEF_To_ECI(geom, '2025-06-15 12:00:00+00'::timestamptz)` is called
- **THEN** the epoch SHALL be converted to approximately 2025.4534 (mid-year) before calling the C transform function

#### Scenario: Epoch with timezone offset
- **WHEN** `ST_ECEF_To_ECI(geom, '2025-01-01 05:00:00+05'::timestamptz)` is called
- **THEN** the epoch SHALL be equivalent to '2025-01-01 00:00:00+00' (UTC normalization occurs via PostgreSQL's TIMESTAMPTZ handling)

### Requirement: SRID validation on input geometry
The frame conversion functions SHALL validate the input geometry SRID and raise an error if it does not match the expected coordinate system.

#### Scenario: ECEF-to-ECI with wrong SRID
- **WHEN** `ST_ECEF_To_ECI(ST_SetSRID(ST_MakePoint(0,0,0), 4326), epoch)` is called with a geographic SRID
- **THEN** the system SHALL raise an error indicating that SRID 4978 (ECEF) is required

#### Scenario: ECI-to-ECEF with wrong SRID
- **WHEN** `ST_ECI_To_ECEF(ST_SetSRID(ST_MakePoint(0,0,0), 4978), epoch)` is called with an ECEF SRID
- **THEN** the system SHALL raise an error indicating that an ECI SRID (900001-900003) is required

### Requirement: Unknown frame name error
The frame conversion functions SHALL raise a clear error when an unrecognized frame name is provided.

#### Scenario: Invalid frame name
- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'INVALID')` is called
- **THEN** the system SHALL raise an error listing the valid frame names: 'ICRF', 'J2000', 'TEME'

### Requirement: Function volatility and parallel safety
`ST_ECEF_To_ECI` and `ST_ECI_To_ECEF` SHALL be declared STABLE (the result depends on EOP data when available) and PARALLEL SAFE. The epoch-parameterized `ST_Transform(geometry, integer, timestamptz)` overload SHALL also be declared STABLE (not IMMUTABLE) because its result depends on EOP table data at query time.

#### Scenario: Function used in parallel query
- **WHEN** `ST_ECEF_To_ECI` is used in a query with `parallel_setup_cost = 0` on a partitioned table
- **THEN** the query planner SHALL be able to use parallel workers to execute the function

#### Scenario: ST_Transform epoch overload is STABLE
- **WHEN** `SELECT provolatile FROM pg_proc WHERE proname = 'st_transform' AND pronargs = 3` is queried for the `(geometry, integer, timestamptz)` overload
- **THEN** provolatile SHALL be `'s'` (STABLE), not `'i'` (IMMUTABLE), because the result depends on EOP data loaded in `postgis_eop`

### Requirement: NULL input handling
Frame conversion functions SHALL return NULL when the input geometry is NULL, following PostgreSQL convention for STRICT functions.

#### Scenario: NULL geometry input
- **WHEN** `ST_ECEF_To_ECI(NULL, epoch)` is called
- **THEN** the result SHALL be NULL without raising an error

#### Scenario: NULL epoch input
- **WHEN** `ST_ECEF_To_ECI(geom, NULL)` is called
- **THEN** the result SHALL be NULL without raising an error
