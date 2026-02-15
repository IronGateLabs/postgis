## ADDED Requirements

### Requirement: GiST 3D index build performance at scale
The system SHALL support building GiST 3D indexes on ECEF (SRID 4978) and ECI (SRID 900001) geometry columns containing 10K, 50K, and 100K points with measurable build times.

#### Scenario: Index creation on 100K ECEF points
- **WHEN** a GiST ND index is created on a table with 100,000 ECEF PointZ geometries (SRID 4978)
- **THEN** the index SHALL be created successfully without error
- **AND** the build time SHALL be recorded for comparison against geographic baseline

#### Scenario: Index creation on 100K ECI points
- **WHEN** a GiST ND index is created on a table with 100,000 ECI PointZ geometries (SRID 900001)
- **THEN** the index SHALL be created successfully without error
- **AND** the build time SHALL be recorded for comparison against ECEF and geographic baselines

### Requirement: GiST 3D query throughput measurement
The system SHALL measure query throughput for `ST_3DDWithin` range queries and bounding box overlap (`&&&`) on indexed ECEF/ECI tables at 10K–100K scale, with comparison against an equivalent geographic (SRID 4326, geography type) baseline.

#### Scenario: ST_3DDWithin range query uses index scan on 100K ECEF points
- **WHEN** `ST_3DDWithin(geom, probe, 500000)` is executed on a 100K-row ECEF table with a GiST ND index
- **THEN** the query planner SHALL choose an index scan (not sequential scan)
- **AND** the query execution time SHALL be recorded

#### Scenario: Bounding box overlap query uses index scan on 100K ECEF points
- **WHEN** a `&&& box3d` query is executed on a 100K-row ECEF table with a GiST ND index
- **THEN** the query planner SHALL choose an index scan
- **AND** the query execution time SHALL be recorded

#### Scenario: Geographic baseline comparison
- **WHEN** equivalent range queries are executed on a geography table with 100K points and a GiST index
- **THEN** the execution times SHALL be recorded alongside ECEF/ECI results for comparison

### Requirement: ST_3DDistance accuracy with indexed ECEF data
The system SHALL return identical `ST_3DDistance` results whether the query uses an index scan or a sequential scan on ECEF data.

#### Scenario: Index scan matches sequential scan for ST_3DDistance
- **WHEN** `ST_3DDistance` is computed between an ECEF probe point and all rows in a 10K+ ECEF table
- **THEN** the results with the GiST ND index enabled SHALL exactly match results from a sequential scan (with `SET enable_indexscan = off`)

### Requirement: Mixed-SRID safety for ECEF/ECI indexed queries
The system SHALL raise an error when spatial operators are applied across mismatched SRIDs in ECEF/ECI indexed queries, rather than returning silently incorrect results.

#### Scenario: ECEF vs geographic SRID mismatch raises error
- **WHEN** `ST_3DDWithin` is called with one geometry at SRID 4978 and another at SRID 4326
- **THEN** the system SHALL raise an SRID mismatch error

#### Scenario: ECEF vs ECI SRID mismatch raises error
- **WHEN** `ST_3DDWithin` is called with one geometry at SRID 4978 and another at SRID 900001
- **THEN** the system SHALL raise an SRID mismatch error

#### Scenario: Same-SRID ECI query succeeds
- **WHEN** `ST_3DDWithin` is called with both geometries at SRID 900001
- **THEN** the query SHALL execute normally without error
