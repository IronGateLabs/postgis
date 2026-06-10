## Purpose

Defines the spatial relationship predicate functions that test topological relationships between two geometries using the DE-9IM (Dimensionally Extended 9-Intersection Model). Covers ST_Intersects, ST_Contains, ST_Within, ST_Covers, ST_CoveredBy, ST_Crosses, ST_Overlaps, ST_Touches, ST_Disjoint, ST_Equals, ST_ContainsProperly, ST_OrderingEquals, ST_Relate, and ST_RelateMatch. These predicates are the primary spatial query filters and are accelerated by GiST index bounding-box pre-filtering and prepared geometry caching. See the `geometry-types` spec for the LWGEOM type system and the `spatial-indexing` spec for GiST operator class details.

## ADDED Requirements

### Requirement: DE-9IM matrix computation via ST_Relate
ST_Relate(geom1, geom2) SHALL compute the full 9-character DE-9IM intersection matrix string describing the topological relationship between two geometries. The matrix encodes the dimension of intersection between the Interior, Boundary, and Exterior of each geometry. An optional third integer argument SHALL control the Boundary Node Rule (default OGC). ST_Relate(geom1, geom2, pattern) SHALL return a boolean indicating whether the computed matrix matches the given pattern. Both inputs SHALL have matching SRIDs or an error SHALL be raised.

#### Scenario: Full DE-9IM matrix for overlapping polygons
- **GIVEN** two overlapping polygons `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Relate(geom1, geom2)` is called
- **THEN** the result SHALL be `'212101212'`
- Validated by: regress/core/relate.sql

#### Scenario: Pattern match with ST_Relate three-argument form
- **GIVEN** a polygon `POLYGON((0 0,2 0,2 2,0 2,0 0))` and a point `POINT(1 1)` inside it
- **WHEN** `ST_Relate(poly, point, 'T*F**FFF*')` is called
- **THEN** the result SHALL be TRUE (the pattern matches "contains")
- Validated by: regress/core/relate.sql

#### Scenario: SRID mismatch error
- **GIVEN** `ST_SetSRID('POINT(0 0)'::geometry, 4326)` and `ST_SetSRID('POINT(1 1)'::geometry, 3857)`
- **WHEN** `ST_Relate(geom1, geom2)` is called
- **THEN** the system SHALL raise an error containing "Operation on mixed SRID geometries"
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Boundary node rule parameter
- **GIVEN** two geometries and boundary node rule value 2 (Endpoint)
- **WHEN** `ST_Relate(geom1, geom2, 2)` is called
- **THEN** the result SHALL be a 9-character DE-9IM string computed using the Endpoint boundary node rule
- Validated by: regress/core/relate_bnr.sql

### Requirement: ST_RelateMatch pattern matching
ST_RelateMatch(matrix, pattern) SHALL return TRUE if a 9-character DE-9IM matrix string matches the given pattern. The pattern uses characters T (any non-F dimension), F (no intersection), 0 (dimension 0), 1 (dimension 1), 2 (dimension 2), and * (wildcard). Pattern matching is case-insensitive.

#### Scenario: Matrix matches contains pattern
- **WHEN** `ST_RelateMatch('T*F**FFF*', 'T*F**FFF*')` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/relatematch.sql

#### Scenario: Matrix does not match pattern
- **WHEN** `ST_RelateMatch('FF*FF****', 'T*F**FFF*')` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/relatematch.sql

#### Scenario: Case-insensitive pattern matching
- **WHEN** `ST_RelateMatch('212101212', 't*t***t**')` is called
- **THEN** the result SHALL be TRUE (lowercase 't' matches any non-F dimension)
- Validated by: regress/core/relatematch.sql

### Requirement: ST_Intersects predicate
ST_Intersects(geom1, geom2) SHALL return TRUE if the two geometries share any portion of space (are not disjoint). It SHALL return FALSE if either input is empty. It SHALL error on SRID mismatch. The function SHALL use bounding box pre-filtering, point-in-polygon interval tree optimization for point/polygon pairs, and prepared geometry caching for repeated calls.

#### Scenario: Intersecting polygons
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Intersects(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Non-intersecting polygons
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((5 5,6 5,6 6,5 6,5 5))`
- **WHEN** `ST_Intersects(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POINT EMPTY` and `POINT(0 0)`
- **WHEN** `ST_Intersects(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Point-in-polygon fast path
- **GIVEN** `POINT(1 1)` and `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- **WHEN** `ST_Intersects(point, polygon)` is called
- **THEN** the result SHALL be TRUE, computed via the interval tree point-in-polygon path without invoking GEOS
- Validated by: regress/core/regress_ogc_prep.sql

### Requirement: ST_Contains predicate
ST_Contains(A, B) SHALL return TRUE if geometry B is completely inside geometry A (no point of B is outside A, and at least one point of B interior is inside A interior). It SHALL return FALSE if either input is empty. It SHALL use bounding box containment pre-filtering and point-in-polygon interval tree optimization.

#### Scenario: Polygon contains interior point
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(5 5)`
- **WHEN** `ST_Contains(polygon, point)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Polygon does not contain exterior point
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(15 15)`
- **WHEN** `ST_Contains(polygon, point)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Polygon does not contain boundary point for Contains (but does for Covers)
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(0 0)` (on the boundary)
- **WHEN** `ST_Contains(polygon, point)` is called
- **THEN** the result SHALL be FALSE (Contains requires interior intersection)
- Validated by: regress/core/regress_ogc_cover.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT EMPTY`
- **WHEN** `ST_Contains(polygon, empty)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_Within predicate
ST_Within(A, B) SHALL return TRUE if geometry A is completely inside geometry B. Semantically, `ST_Within(A, B)` SHALL be equivalent to `ST_Contains(B, A)`. It SHALL return FALSE if either input is empty. The SQL-level `_ST_Within` is implemented as `_ST_Contains($2,$1)`.

#### Scenario: Point within polygon
- **GIVEN** `POINT(5 5)` and `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- **WHEN** `ST_Within(point, polygon)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Point outside polygon
- **GIVEN** `POINT(15 15)` and `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- **WHEN** `ST_Within(point, polygon)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Symmetry with ST_Contains
- **GIVEN** geometries A and B
- **WHEN** `ST_Within(A, B)` and `ST_Contains(B, A)` are both called
- **THEN** both SHALL return the same boolean value
- Validated by: regress/core/regress_ogc.sql

### Requirement: ST_Covers and ST_CoveredBy predicates
ST_Covers(A, B) SHALL return TRUE if no point of B is outside A. Unlike ST_Contains, ST_Covers does not require interior intersection, so a point on the boundary of A is covered by A. ST_CoveredBy(A, B) SHALL return TRUE if no point of A is outside B. Both SHALL return FALSE if either input is empty. ST_Covers uses the DE-9IM pattern `******FF*` and ST_CoveredBy uses GEOSCoveredBy. Both SHALL use point-in-polygon interval tree optimization for point/polygon pairs.

#### Scenario: Covers includes boundary point
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(0 0)` (vertex on boundary)
- **WHEN** `ST_Covers(polygon, point)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc_cover.sql

#### Scenario: CoveredBy is inverse of Covers
- **GIVEN** `POINT(5 5)` and `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- **WHEN** `ST_CoveredBy(point, polygon)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc_cover.sql

#### Scenario: Covers with empty geometry returns FALSE
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT EMPTY`
- **WHEN** `ST_Covers(polygon, empty)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_Touches predicate
ST_Touches(A, B) SHALL return TRUE if geometries A and B have at least one point in common but their interiors do not intersect. It SHALL return FALSE if either input is empty.

#### Scenario: Adjacent polygons sharing an edge
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((1 0,2 0,2 1,1 1,1 0))`
- **WHEN** `ST_Touches(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Overlapping polygons do not touch
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Touches(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POINT EMPTY` and `POLYGON((0 0,1 0,1 1,0 1,0 0))`
- **WHEN** `ST_Touches(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_Disjoint predicate
ST_Disjoint(A, B) SHALL return TRUE if the two geometries have no point in common. It SHALL return TRUE if either input is empty (empty geometries are disjoint from everything). It is the logical negation of ST_Intersects.

#### Scenario: Separated polygons are disjoint
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((5 5,6 5,6 6,5 6,5 5))`
- **WHEN** `ST_Disjoint(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Overlapping polygons are not disjoint
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Disjoint(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Empty geometry is disjoint from everything
- **GIVEN** `POINT EMPTY` and `POINT(0 0)`
- **WHEN** `ST_Disjoint(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Status: untested -- inferred from source code (returns true for empty)

### Requirement: ST_Crosses predicate
ST_Crosses(A, B) SHALL return TRUE if the geometries have some but not all interior points in common, and the dimension of the intersection is less than the maximum dimension of the two inputs. Typically used for line/line or line/polygon crossings. It SHALL return FALSE if either input is empty.

#### Scenario: Line crossing through a polygon
- **GIVEN** `LINESTRING(0 0, 10 10)` and `POLYGON((2 0, 8 0, 8 6, 2 6, 2 0))`
- **WHEN** `ST_Crosses(line, polygon)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Line fully inside polygon does not cross
- **GIVEN** `LINESTRING(3 3, 5 5)` and `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- **WHEN** `ST_Crosses(line, polygon)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `LINESTRING EMPTY` and `POLYGON((0 0,1 0,1 1,0 1,0 0))`
- **WHEN** `ST_Crosses(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_Overlaps predicate
ST_Overlaps(A, B) SHALL return TRUE if the geometries share space, are of the same dimension, but are not completely contained by each other. It SHALL return FALSE if either input is empty.

#### Scenario: Partially overlapping polygons
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Overlaps(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Contained polygon does not overlap
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POLYGON((2 2,4 2,4 4,2 4,2 2))`
- **WHEN** `ST_Overlaps(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POLYGON EMPTY` and `POLYGON((0 0,1 0,1 1,0 1,0 0))`
- **WHEN** `ST_Overlaps(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_Equals spatial equality
ST_Equals(A, B) SHALL return TRUE if geometries A and B are spatially equal (they cover the same point set). Two empty geometries SHALL be considered equal. The function uses bounding box equality as a short-circuit and binary equality as a fast path before falling back to GEOSEquals. ST_OrderingEquals tests strict structural equality (same vertices in same order).

#### Scenario: Identical polygons are equal
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((0 0,1 0,1 1,0 1,0 0))`
- **WHEN** `ST_Equals(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Same polygon with different vertex order
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((1 1,0 1,0 0,1 0,1 1))`
- **WHEN** `ST_Equals(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE (spatial equality ignores vertex order)
- **AND** `ST_OrderingEquals(geom1, geom2)` SHALL return FALSE (structural inequality)
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Two empty geometries are equal
- **GIVEN** `POINT EMPTY` and `GEOMETRYCOLLECTION EMPTY`
- **WHEN** `ST_Equals(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc.sql

### Requirement: ST_ContainsProperly predicate
ST_ContainsProperly(A, B) SHALL return TRUE if B intersects the interior of A but not the boundary or exterior of A. This is stricter than ST_Contains. When no prepared geometry cache is available, it falls back to the DE-9IM pattern `T**FF*FF*`. It SHALL return FALSE if either input is empty.

#### Scenario: Interior point is properly contained
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(5 5)`
- **WHEN** `ST_ContainsProperly(polygon, point)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/regress_ogc_prep.sql

#### Scenario: Boundary point is not properly contained
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT(0 0)`
- **WHEN** `ST_ContainsProperly(polygon, point)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/regress_ogc_prep.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and `POINT EMPTY`
- **WHEN** `ST_ContainsProperly(polygon, empty)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: Spatial predicate NULL propagation
All spatial predicate functions (ST_Intersects, ST_Contains, ST_Within, ST_Covers, ST_CoveredBy, ST_Crosses, ST_Overlaps, ST_Touches, ST_Disjoint, ST_Equals, ST_ContainsProperly) SHALL follow standard SQL NULL propagation: if either geometry argument is NULL, the function SHALL return NULL. This is handled by PostgreSQL's strict function declaration.

#### Scenario: NULL first argument
- **WHEN** `ST_Intersects(NULL::geometry, 'POINT(0 0)'::geometry)` is called
- **THEN** the result SHALL be NULL
- Validated by: regress/core/regress_ogc.sql

#### Scenario: NULL second argument
- **WHEN** `ST_Contains('POLYGON((0 0,1 0,1 1,0 1,0 0))'::geometry, NULL::geometry)` is called
- **THEN** the result SHALL be NULL
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Both arguments NULL
- **WHEN** `ST_Intersects(NULL::geometry, NULL::geometry)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- standard PostgreSQL strict function behavior

### Requirement: SRID mismatch error for all predicates
All spatial predicate functions SHALL call `gserialized_error_if_srid_mismatch` before performing any computation. If the two input geometries have different non-zero SRIDs, the function SHALL raise an error with a message containing "Operation on mixed SRID geometries".

#### Scenario: Different SRIDs cause error
- **GIVEN** `ST_SetSRID('POINT(0 0)'::geometry, 4326)` and `ST_SetSRID('POINT(1 1)'::geometry, 3857)`
- **WHEN** any spatial predicate is called with these arguments
- **THEN** the system SHALL raise an error containing "Operation on mixed SRID geometries"
- Validated by: regress/core/regress_ogc.sql

#### Scenario: SRID 0 does not trigger mismatch with non-zero SRID
- **GIVEN** a geometry with SRID 0 and a geometry with SRID 4326
- **WHEN** any spatial predicate is called
- **THEN** the system SHALL raise the SRID mismatch error (SRID 0 is still a distinct SRID)
- Status: untested -- behavior depends on gserialized_error_if_srid_mismatch implementation

#### Scenario: Both SRID 0 does not error
- **GIVEN** two geometries both with SRID 0
- **WHEN** any spatial predicate is called
- **THEN** no SRID error SHALL be raised
- Validated by: regress/core/regress_ogc.sql

### Requirement: Prepared geometry caching optimization
When a spatial predicate function is called repeatedly with the same geometry as one argument (e.g., in a join or subquery), the system SHALL cache a GEOS PreparedGeometry for the repeated argument. Subsequent calls SHALL use the prepared geometry for faster evaluation. This optimization is transparent and SHALL not change the predicate result. The functions ST_Intersects, ST_Contains, ST_Within, ST_Covers, ST_CoveredBy, ST_Touches, ST_Crosses, ST_Overlaps, ST_Disjoint, and ST_ContainsProperly all support prepared geometry caching. ST_Relate with a pattern argument (three-argument form) supports prepared geometry caching on GEOS 3.13+ (`POSTGIS_GEOS_VERSION >= 31300`).

#### Scenario: Prepared geometry gives same result as non-prepared
- **GIVEN** a polygon `POLYGON((0 0,10 0,10 10,0 10,0 0))` and multiple test points
- **WHEN** `ST_Contains(polygon, point)` is called for each test point
- **THEN** the results SHALL be identical whether the prepared geometry cache is used or not
- Validated by: regress/core/regress_ogc_prep.sql

#### Scenario: Prepared relate pattern on GEOS 3.13+
- **GIVEN** GEOS version >= 3.13 and repeated `ST_Relate(geom1, geom2, pattern)` calls
- **WHEN** the same first geometry appears in repeated calls
- **THEN** the system SHALL use `GEOSPreparedRelatePattern` with DE-9IM matrix inversion when the prepared argument is not argument 1
- Requires `POSTGIS_GEOS_VERSION >= 31300`
- Status: untested -- optimization is transparent, no behavioral test exists

#### Scenario: Prepared geometry with ST_DWithin
- **GIVEN** repeated `ST_DWithin(polygon, point, distance)` calls with the same polygon
- **WHEN** the polygon is large enough (> 1024 bytes serialized)
- **THEN** the system SHALL use `GEOSPreparedDistanceWithin` for acceleration
- Validated by: regress/core/regress_ogc_prep.sql

---

## Coverage Summary

**Functions covered:** ST_Intersects, ST_Contains, ST_Within, ST_Covers, ST_CoveredBy, ST_Crosses, ST_Overlaps, ST_Touches, ST_Disjoint, ST_Equals, ST_OrderingEquals, ST_ContainsProperly, ST_Relate (2-arg and 3-arg), ST_RelateMatch, ST_DWithin (predicate aspects)

**Functions deferred:** ST_3DIntersects (covered in `measurement-functions` spec as it is implemented via 3D distance). Geography-type predicates (ST_Intersects, ST_Covers for geography) are deferred to the `geography-type` spec.

**Source files analyzed:** `postgis/lwgeom_geos_predicates.c`, `postgis/lwgeom_geos_relatematch.c`, `postgis/lwgeom_itree.c`, `postgis/postgis.sql.in`

**Test coverage:** 14 requirements, 46 scenarios total. 30 scenarios validated by existing regression tests, 16 scenarios untested (inferred from source code analysis).
