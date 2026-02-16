## ADDED Requirements

### Requirement: ST_Simplify rejects geocentric input
ST_Simplify, ST_SimplifyPreserveTopology, ST_SimplifyPolygonHull, and
ST_CoverageSimplify SHALL raise ERROR when called with a geometry whose SRID
resolves to `LW_CRS_GEOCENTRIC` or `LW_CRS_INERTIAL`.

#### Scenario: ST_Simplify with ECEF geometry
- **WHEN** ST_Simplify is called with a geometry having SRID 4978
- **THEN** the function SHALL raise ERROR with message containing
  "Operation is not supported for geocentric (ECEF) coordinates"

#### Scenario: ST_SimplifyPreserveTopology with ECI geometry
- **WHEN** ST_SimplifyPreserveTopology is called with a geometry having
  SRID 900001 (ICRF)
- **THEN** the function SHALL raise ERROR with message containing
  "Operation is not supported for inertial (ECI) coordinates"

### Requirement: Topology functions reject geocentric input
ST_ConvexHull, ST_DelaunayTriangles, and ST_Voronoi SHALL raise ERROR when
called with a geometry whose SRID resolves to `LW_CRS_GEOCENTRIC` or
`LW_CRS_INERTIAL`.

#### Scenario: ST_ConvexHull with ECEF geometry
- **WHEN** ST_ConvexHull is called with a geometry having SRID 4978
- **THEN** the function SHALL raise ERROR with message containing
  "Operation is not supported for geocentric (ECEF) coordinates"

#### Scenario: ST_Voronoi with ECEF geometry
- **WHEN** ST_Voronoi is called with a geometry having SRID 4978
- **THEN** the function SHALL raise ERROR with message containing
  "Operation is not supported for geocentric (ECEF) coordinates"

### Requirement: 2D ClosestPoint/ShortestLine use 3D dispatch for geocentric
ST_ClosestPoint (2D variant) and ST_ShortestLine (2D variant) SHALL
automatically dispatch to their 3D counterparts when called with geometries
whose SRID resolves to `LW_CRS_GEOCENTRIC`, matching the ST_Distance pattern.

#### Scenario: ST_ClosestPoint with ECEF geometries dispatches to 3D
- **WHEN** ST_ClosestPoint is called with two geometries having SRID 4978
- **THEN** the function SHALL return the 3D closest point (equivalent to
  ST_3DClosestPoint), not the 2D projection

#### Scenario: ST_ShortestLine with ECEF geometries dispatches to 3D
- **WHEN** ST_ShortestLine is called with two geometries having SRID 4978
- **THEN** the function SHALL return the 3D shortest line (equivalent to
  ST_3DShortestLine), not the 2D projection

### Requirement: 2D LineInterpolatePoint rejects geocentric input
ST_LineInterpolatePoint (2D variant) SHALL raise ERROR when called with a
geometry whose SRID resolves to `LW_CRS_GEOCENTRIC` or `LW_CRS_INERTIAL`.
The 3D variant (ST_3DLineInterpolatePoint) SHALL continue to work normally
for geocentric input.

#### Scenario: ST_LineInterpolatePoint with ECEF linestring
- **WHEN** ST_LineInterpolatePoint is called with a linestring having SRID 4978
- **THEN** the function SHALL raise ERROR with message containing
  "Operation is not supported for geocentric (ECEF) coordinates"

#### Scenario: ST_3DLineInterpolatePoint with ECEF linestring
- **WHEN** ST_3DLineInterpolatePoint is called with a linestring having
  SRID 4978
- **THEN** the function SHALL return the interpolated 3D point without error

### Requirement: Regression tests for all new guards
Each new guard SHALL have a corresponding regression test that verifies the
ERROR is raised with the expected message text.

#### Scenario: Guard regression tests pass
- **WHEN** the regression test suite runs against a database with ECEF data
  (SRID 4978)
- **THEN** all guard tests SHALL confirm ERROR is raised for each guarded
  function and the error message matches the expected pattern
