## ADDED Requirements

### Requirement: ST_ECEF_X coordinate accessor
The system SHALL provide a SQL function `ST_ECEF_X(geometry) RETURNS FLOAT8` that extracts the X coordinate (meters, prime meridian direction) from an ECEF geometry. The function SHALL be declared IMMUTABLE and PARALLEL SAFE.

#### Scenario: Extract X from ECEF point
- **WHEN** `ST_ECEF_X(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978))` is called
- **THEN** the result SHALL be 6378137.0

#### Scenario: IMMUTABLE declaration enables index expressions
- **WHEN** `CREATE INDEX idx ON t (ST_ECEF_X(pos))` is executed on a table with an ECEF geometry column
- **THEN** the index SHALL be created successfully (PostgreSQL requires IMMUTABLE functions for index expressions)

### Requirement: ST_ECEF_Y coordinate accessor
The system SHALL provide a SQL function `ST_ECEF_Y(geometry) RETURNS FLOAT8` that extracts the Y coordinate (meters, 90 degrees east direction) from an ECEF geometry. The function SHALL be declared IMMUTABLE and PARALLEL SAFE.

#### Scenario: Extract Y from ECEF point
- **WHEN** `ST_ECEF_Y(ST_SetSRID(ST_MakePoint(0, 6378137, 0), 4978))` is called
- **THEN** the result SHALL be 6378137.0

### Requirement: ST_ECEF_Z coordinate accessor
The system SHALL provide a SQL function `ST_ECEF_Z(geometry) RETURNS FLOAT8` that extracts the Z coordinate (meters, north pole direction) from an ECEF geometry. The function SHALL be declared IMMUTABLE and PARALLEL SAFE.

#### Scenario: Extract Z from ECEF point
- **WHEN** `ST_ECEF_Z(ST_SetSRID(ST_MakePoint(0, 0, 6356752), 4978))` is called
- **THEN** the result SHALL be 6356752.0

### Requirement: SRID validation
The ECEF accessor functions SHALL validate that the input geometry has SRID 4978 (ECEF/WGS84 geocentric) and raise an error for other SRIDs.

#### Scenario: Geographic SRID rejected
- **WHEN** `ST_ECEF_X(ST_SetSRID(ST_MakePoint(-122.4, 37.8, 0), 4326))` is called
- **THEN** the system SHALL raise an error indicating that SRID 4978 (ECEF) is required, not 4326

#### Scenario: Projected SRID rejected
- **WHEN** `ST_ECEF_X(ST_SetSRID(ST_MakePoint(500000, 4649776, 0), 32610))` is called
- **THEN** the system SHALL raise an error indicating that SRID 4978 (ECEF) is required

#### Scenario: SRID 0 rejected
- **WHEN** `ST_ECEF_X(ST_MakePoint(1, 2, 3))` is called (default SRID 0)
- **THEN** the system SHALL raise an error indicating that SRID 4978 (ECEF) is required

### Requirement: Empty and non-point geometry handling
The ECEF accessor functions SHALL return NULL for empty geometries and non-point geometry types, consistent with `ST_X` behavior.

#### Scenario: Empty geometry returns NULL
- **WHEN** `ST_ECEF_X('POINT EMPTY'::geometry)` is called (assuming SRID is set to 4978)
- **THEN** the result SHALL be NULL

#### Scenario: Non-point geometry returns NULL
- **WHEN** `ST_ECEF_X(ST_SetSRID(ST_MakeLine(ST_MakePoint(0,0,0), ST_MakePoint(1,1,1)), 4978))` is called
- **THEN** the result SHALL be NULL

### Requirement: Implementation uses fast-path coordinate extraction
The ECEF accessor functions SHALL use `gserialized_peek_first_point` (or equivalent) for coordinate extraction, avoiding full geometry deserialization for optimal performance.

#### Scenario: Performance on large table
- **WHEN** `SELECT ST_ECEF_X(pos) FROM large_ecef_table` is executed on a table with 1M+ rows
- **THEN** the function SHALL perform comparably to `ST_X` on the same data (within 2x overhead for SRID validation)
