## MODIFIED Requirements

### Requirement: Spatial functions error on unsupported ECEF operations
Spatial functions that assume geographic or projected input (e.g., `ST_Distance` with geographic distance semantics, `ST_Area` on a spheroid) SHALL raise a clear error when called on ECEF geometries rather than returning silently incorrect results.

Functions are classified into three behavior categories for geocentric input:

1. **Dispatch**: Functions where a correct geocentric result exists and the function SHALL automatically produce it (e.g., `ST_Distance` dispatching to 3D Euclidean distance).
2. **Error**: Functions where no meaningful geocentric result exists and the function SHALL raise an error with a message identifying the CRS family (e.g., `ST_Area`, `ST_Buffer`).
3. **Pass-through**: Functions that are coordinate-system-agnostic and require no special handling (e.g., `ST_AsText`, `ST_X`, `ST_Y`, `ST_Z`, `ST_NPoints`).

The CRS family SHALL be determined by calling `lwsrid_get_crs_family()` on the geometry's SRID. A return value of `LW_CRS_GEOCENTRIC` triggers the dispatch or error behavior.

#### Scenario: ST_Distance on ECEF geometries returns 3D Euclidean distance
- **GIVEN** two ECEF points with SRID 4978: A at (6378137, 0, 0) and B at (0, 6378137, 0)
- **WHEN** `ST_Distance(A, B)` is called
- **THEN** the system SHALL return the 3D Euclidean distance (`sqrt((x2-x1)^2 + (y2-y1)^2 + (z2-z1)^2)`), approximately 9020047.8 meters
- **AND** the result SHALL match `ST_3DDistance(A, B)` exactly

#### Scenario: ST_Distance on ECEF geometries does not ignore Z
- **GIVEN** two ECEF points with SRID 4978 that share the same X and Y but differ in Z
- **WHEN** `ST_Distance(A, B)` is called
- **THEN** the result SHALL be non-zero, equal to the absolute Z difference
- **NOTE** Current behavior returns 0.0 because `ST_Distance` uses `lwgeom_mindistance2d` which ignores Z

#### Scenario: ST_DWithin on ECEF uses 3D Euclidean distance
- **GIVEN** two ECEF points with SRID 4978 separated by 1000m in Z only
- **WHEN** `ST_DWithin(A, B, 500)` is called
- **THEN** the result SHALL be false (3D distance 1000 > tolerance 500)
- **AND WHEN** `ST_DWithin(A, B, 1500)` is called
- **THEN** the result SHALL be true

#### Scenario: ST_Area on ECEF polygon raises error
- **WHEN** `ST_Area` is called on a polygon with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and indicating that area computation is not supported for this CRS family
- **NOTE** Current behavior returns a 2D projected area treating X/Y as planar coordinates, ignoring that these are 3D Cartesian meters from Earth's center

#### Scenario: ST_Buffer on ECEF geometry raises error
- **WHEN** `ST_Buffer` is called on a geometry with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and indicating that buffering is not supported for this CRS family

#### Scenario: ST_Length on ECEF linestring returns 3D length
- **GIVEN** an ECEF linestring with SRID 4978
- **WHEN** `ST_Length` is called
- **THEN** the system SHALL return the 3D Euclidean path length (sum of 3D segment lengths)
- **AND** the result SHALL match `ST_3DLength` for the same geometry

#### Scenario: Coordinate accessor functions work on ECEF without error
- **GIVEN** an ECEF point with SRID 4978 at (6378137, 0, 0)
- **WHEN** `ST_X`, `ST_Y`, `ST_Z`, `ST_AsText`, `ST_AsEWKT`, `ST_NPoints` are called
- **THEN** each function SHALL return the correct result without error (pass-through behavior)
