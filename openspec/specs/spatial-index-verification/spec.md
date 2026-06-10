## Purpose

Verification that GiST 3D spatial indexes work correctly with ECEF/ECI coordinate ranges.

## Requirements

### Requirement: GiST 3D index creation on ECEF geometry columns
The system SHALL support creating GiST indexes with the `gist_geometry_ops_nd` operator class on geometry columns containing ECEF (SRID 4978) data.

#### Scenario: Create GiST 3D index on ECEF column
- **WHEN** `CREATE INDEX idx_ecef ON t USING gist (pos gist_geometry_ops_nd)` is executed on a table where `pos` is `geometry(PointZ, 4978)`
- **THEN** the index SHALL be created successfully

#### Scenario: Index survives bulk ECEF data insertion
- **WHEN** 10,000+ ECEF points with coordinate values up to ~6.4 million meters are inserted into the indexed table
- **THEN** the index SHALL accept all values without range errors or corruption

### Requirement: GiST 3D index creation on ECI geometry columns
The system SHALL support creating GiST indexes with `gist_geometry_ops_nd` on geometry columns containing ECI (SRID 900001-900003) data.

#### Scenario: Create GiST 3D index on ECI column
- **WHEN** `CREATE INDEX idx_eci ON t USING gist (pos gist_geometry_ops_nd)` is executed on a table where `pos` is `geometry(PointZ, 900001)`
- **THEN** the index SHALL be created successfully

### Requirement: Index-assisted ST_3DDWithin queries on ECEF data
GiST 3D indexes on ECEF columns SHALL be used by the query planner for `ST_3DDWithin` proximity queries.

#### Scenario: Index scan for ST_3DDWithin
- **WHEN** `SELECT * FROM ecef_table WHERE ST_3DDWithin(pos, query_point, 1000)` is executed on a table with a GiST 3D index
- **THEN** the `EXPLAIN` output SHALL show an index scan (not a sequential scan) for the spatial predicate

#### Scenario: Correct results with ECEF index
- **WHEN** `ST_3DDWithin` is used to find ECEF points within 1000 meters of a query point
- **THEN** the results SHALL match a sequential scan -- no false negatives from incorrect bounding box handling

### Requirement: Bounding box overlap operator with ECEF ranges
The `&&` (bounding box overlap) operator SHALL work correctly with ECEF coordinate ranges that span millions of meters.

#### Scenario: Overlap detection at ECEF scale
- **WHEN** two ECEF geometries have bounding boxes that overlap in 3D space (e.g., two satellite positions within 100km)
- **THEN** the `&&` operator SHALL return true

#### Scenario: Non-overlap detection at ECEF scale
- **WHEN** two ECEF geometries have bounding boxes that do not overlap (e.g., points on opposite sides of Earth)
- **THEN** the `&&` operator SHALL return false

### Requirement: ST_3DDistance accuracy with indexed ECEF data
`ST_3DDistance` queries on indexed ECEF data SHALL return the same distances as queries without an index (sequential scan).

#### Scenario: Distance accuracy with index
- **WHEN** `ST_3DDistance(a.pos, b.pos)` is computed for known ECEF point pairs using both index-assisted and sequential scan queries
- **THEN** the distances SHALL be identical (exact match, not approximate)

### Requirement: Mixed-SRID safety
Spatial operations between ECEF and ECI geometries using indexed columns SHALL produce appropriate errors rather than silently returning incorrect results.

#### Scenario: Cross-SRID spatial query
- **WHEN** `SELECT * FROM ecef_table a, eci_table b WHERE ST_3DDWithin(a.pos, b.pos, 1000)` is executed
- **THEN** the system SHALL raise an error about SRID mismatch, not silently use the index to produce meaningless results
