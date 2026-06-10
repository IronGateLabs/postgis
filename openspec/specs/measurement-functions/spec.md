## Purpose

Defines the measurement functions that compute distances, lengths, areas, and perimeters for geometry types. Covers minimum and maximum distance computations (2D and 3D), proximity testing (DWithin, DFullyWithin), closest/shortest/longest point and line extraction, length and perimeter for linestrings and polygons, area computation, and shape similarity measures (Hausdorff distance, Frechet distance, minimum clearance). All distance and length functions operate in the units of the geometry's SRID coordinate system. For geography-type geodesic measurements, see the `geography-type` spec. See the `geometry-types` spec for the LWGEOM type system.

## ADDED Requirements

### Requirement: ST_Distance minimum 2D distance
ST_Distance(geom1, geom2) SHALL return the minimum 2D Cartesian distance between two geometries. If either geometry is empty, the function SHALL return NULL (the internal FLT_MAX sentinel is not returned to the caller). Both inputs SHALL have matching SRIDs or an error SHALL be raised. For geometries with geocentric (ECEF) CRS family, the function SHALL use 3D Euclidean distance instead of 2D.

#### Scenario: Distance between two points
- **GIVEN** `POINT(0 0)` and `POINT(3 4)`
- **WHEN** `ST_Distance(geom1, geom2)` is called
- **THEN** the result SHALL be 5.0
- Validated by: regress/core/measures.sql

#### Scenario: Distance between identical points is zero
- **GIVEN** `POINT(1 2)` and `POINT(1 2)`
- **WHEN** `ST_Distance(geom1, geom2)` is called
- **THEN** the result SHALL be 0
- Validated by: regress/core/measures.sql

#### Scenario: Distance between point and polygon
- **GIVEN** `POINT(15 15)` and `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** `ST_Distance(point, polygon)` is called
- **THEN** the result SHALL be the distance from the point to the nearest edge of the polygon
- Validated by: regress/core/measures.sql

#### Scenario: Distance with empty geometry returns NULL
- **GIVEN** `POINT(0 0)` and `POINT EMPTY`
- **WHEN** `ST_Distance(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code (FLT_MAX check returns NULL)

#### Scenario: SRID mismatch error
- **GIVEN** `ST_SetSRID('POINT(0 0)', 4326)` and `ST_SetSRID('POINT(1 1)', 3857)`
- **WHEN** `ST_Distance(geom1, geom2)` is called
- **THEN** the system SHALL raise an error containing "Operation on mixed SRID geometries"
- Validated by: regress/core/measures.sql

### Requirement: ST_3DDistance minimum 3D distance
ST_3DDistance(geom1, geom2) SHALL return the minimum 3D Euclidean distance between two geometries, using Z coordinates when available. If either geometry is empty, the function SHALL return NULL. Both inputs SHALL have matching SRIDs.

#### Scenario: 3D distance between points with Z
- **GIVEN** `POINT Z (0 0 0)` and `POINT Z (1 1 1)`
- **WHEN** `ST_3DDistance(geom1, geom2)` is called
- **THEN** the result SHALL be approximately 1.732 (sqrt(3))
- Validated by: regress/core/measures.sql

#### Scenario: 3D distance collapses to 2D when no Z
- **GIVEN** `POINT(0 0)` and `POINT(3 4)` (no Z coordinates)
- **WHEN** `ST_3DDistance(geom1, geom2)` is called
- **THEN** the result SHALL be 5.0 (Z treated as 0)
- Validated by: regress/core/measures.sql

#### Scenario: Empty geometry returns NULL
- **GIVEN** `POINT Z (0 0 0)` and `POINT EMPTY`
- **WHEN** `ST_3DDistance(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code (FLT_MAX check)

### Requirement: ST_MaxDistance maximum 2D distance
ST_MaxDistance(geom1, geom2) SHALL return the maximum 2D distance between any two points of the input geometries. If either geometry is empty, the function SHALL return NULL (internal sentinel -1 triggers NULL return).

#### Scenario: Max distance between two points
- **GIVEN** `POINT(0 0)` and `POINT(3 4)`
- **WHEN** `ST_MaxDistance(geom1, geom2)` is called
- **THEN** the result SHALL be 5.0 (same as min distance for points)
- Validated by: regress/core/measures.sql

#### Scenario: Max distance of identical points is zero
- **GIVEN** `POINT(1 2)` and `POINT(1 2)`
- **WHEN** `ST_MaxDistance(geom1, geom2)` is called
- **THEN** the result SHALL be 0
- Validated by: regress/core/measures.sql

#### Scenario: Max distance between polygon corners
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` and `POLYGON((20 20, 30 20, 30 30, 20 30, 20 20))`
- **WHEN** `ST_MaxDistance(geom1, geom2)` is called
- **THEN** the result SHALL be the distance between the farthest corners
- Validated by: regress/core/measures.sql

### Requirement: ST_DWithin proximity testing
ST_DWithin(geom1, geom2, distance) SHALL return TRUE if the minimum 2D distance between the geometries is less than or equal to the given distance. The distance parameter SHALL NOT be negative (error raised). Empty geometries SHALL return FALSE. Both inputs SHALL have matching SRIDs. The function supports prepared geometry caching for repeated calls with large geometries (> 1024 bytes). See the `spatial-predicates` spec for additional predicate semantics.

#### Scenario: Points within distance
- **GIVEN** `POINT(0 0)` and `POINT(1 0)` with distance tolerance 2.0
- **WHEN** `ST_DWithin(geom1, geom2, 2.0)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/measures.sql

#### Scenario: Points not within distance
- **GIVEN** `POINT(0 0)` and `POINT(10 0)` with distance tolerance 2.0
- **WHEN** `ST_DWithin(geom1, geom2, 2.0)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/measures.sql

#### Scenario: Negative distance raises error
- **GIVEN** two geometries and distance -1.0
- **WHEN** `ST_DWithin(geom1, geom2, -1.0)` is called
- **THEN** the system SHALL raise an error "Tolerance cannot be less than zero"
- Validated by: regress/core/measures.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POINT(0 0)` and `POINT EMPTY` with distance 100.0
- **WHEN** `ST_DWithin(geom1, geom2, 100.0)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code empty check

### Requirement: ST_3DDWithin 3D proximity testing
ST_3DDWithin(geom1, geom2, distance) SHALL return TRUE if the minimum 3D distance between the geometries is less than or equal to the given distance. Negative distance SHALL raise an error. Both inputs SHALL have matching SRIDs. Empty geometries SHALL return FALSE (FLT_MAX internal distance exceeds any tolerance).

#### Scenario: 3D points within distance
- **GIVEN** `POINT Z (0 0 0)` and `POINT Z (1 1 1)` with distance 2.0
- **WHEN** `ST_3DDWithin(geom1, geom2, 2.0)` is called
- **THEN** the result SHALL be TRUE (3D distance ~1.73 < 2.0)
- Validated by: regress/core/measures.sql

#### Scenario: 3D points not within distance
- **GIVEN** `POINT Z (0 0 0)` and `POINT Z (10 10 10)` with distance 2.0
- **WHEN** `ST_3DDWithin(geom1, geom2, 2.0)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/measures.sql

#### Scenario: Negative distance raises error
- **GIVEN** two 3D geometries and distance -1.0
- **WHEN** `ST_3DDWithin(geom1, geom2, -1.0)` is called
- **THEN** the system SHALL raise an error "Tolerance cannot be less than zero"
- Status: untested -- inferred from source code tolerance check

### Requirement: ST_DFullyWithin and ST_3DDFullyWithin containment distance
ST_DFullyWithin(geom1, geom2, distance) SHALL return TRUE if the maximum 2D distance between any two points of the geometries is less than or equal to the given distance (i.e., geom2 is fully within a buffer of geom1). ST_3DDFullyWithin uses 3D distance. Negative distance SHALL raise an error. Empty 3D geometries SHALL return FALSE.

#### Scenario: Point fully within distance of polygon
- **GIVEN** `POINT(5 5)` and `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` with distance 20.0
- **WHEN** `ST_DFullyWithin(point, polygon, 20.0)` is called
- **THEN** the result SHALL be TRUE (maximum distance between any points is within 20)
- Validated by: regress/core/measures.sql

#### Scenario: Far apart geometries not fully within
- **GIVEN** `POINT(0 0)` and `POINT(100 100)` with distance 10.0
- **WHEN** `ST_DFullyWithin(geom1, geom2, 10.0)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- basic fully-within negative case

#### Scenario: 3D fully within distance
- **GIVEN** `POINT Z (0 0 0)` and `POINT Z (1 1 1)` with distance 5.0
- **WHEN** `ST_3DDFullyWithin(geom1, geom2, 5.0)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/measures.sql

### Requirement: ST_ClosestPoint and ST_ShortestLine nearest geometry features
ST_ClosestPoint(geom1, geom2) SHALL return the point on geom1 that is closest to geom2. ST_ShortestLine(geom1, geom2) SHALL return the 2-point linestring connecting the closest points on geom1 and geom2. Both functions SHALL return NULL if either input is empty. Both have 3D variants (ST_3DClosestPoint, ST_3DShortestLine). For geometries with geocentric (ECEF) CRS family, the 2D functions SHALL use 3D distance algorithms. Both inputs SHALL have matching SRIDs.

#### Scenario: Closest point on polygon to external point
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` and `POINT(15 5)`
- **WHEN** `ST_ClosestPoint(polygon, point)` is called
- **THEN** the result SHALL be `POINT(10 5)` (the nearest point on the polygon boundary)
- Validated by: regress/core/measures.sql

#### Scenario: Shortest line between disjoint polygons
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` and `POLYGON((11 0, 20 0, 20 10, 11 10, 11 0))`
- **WHEN** `ST_ShortestLine(geom1, geom2)` is called
- **THEN** the result SHALL be a LINESTRING connecting the nearest points on each polygon
- Validated by: regress/core/measures.sql

#### Scenario: Closest point with empty geometry returns NULL
- **GIVEN** `POINT(0 0)` and `POINT EMPTY`
- **WHEN** `ST_ClosestPoint(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code empty check returning NULL

### Requirement: ST_LongestLine and ST_3DLongestLine farthest geometry features
ST_LongestLine(geom1, geom2) SHALL return the 2-point linestring connecting the farthest points between the two geometries. ST_3DLongestLine uses 3D distance. Both return NULL if either input is empty.

#### Scenario: Longest line between two polygons
- **GIVEN** `POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))` and `POLYGON((10 10, 11 10, 11 11, 10 11, 10 10))`
- **WHEN** `ST_LongestLine(geom1, geom2)` is called
- **THEN** the result SHALL be a LINESTRING connecting the two farthest corners
- Validated by: regress/core/measures.sql

#### Scenario: Longest line for identical points
- **GIVEN** `POINT(1 2)` and `POINT(1 2)`
- **WHEN** `ST_LongestLine(geom1, geom2)` is called
- **THEN** the result SHALL be `LINESTRING(1 2, 1 2)` (degenerate line of length 0)
- Validated by: regress/core/measures.sql

#### Scenario: Empty geometry returns NULL
- **GIVEN** `POINT(0 0)` and `LINESTRING EMPTY`
- **WHEN** `ST_LongestLine(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code empty check

### Requirement: ST_Length and ST_3DLength line length computation
ST_Length(geom) SHALL return the 2D length of a linestring or multilinestring. For points and polygons, it SHALL return 0. ST_3DLength(geom) SHALL compute length using 3D coordinates (including Z). For geometries with geocentric (ECEF) CRS family, ST_Length2D SHALL use 3D Euclidean length.

#### Scenario: Length of a simple linestring
- **GIVEN** `LINESTRING(0 0, 10 0)`
- **WHEN** `ST_Length(geom)` is called
- **THEN** the result SHALL be 10.0
- Validated by: regress/core/measures.sql

#### Scenario: Length of multilinestring
- **GIVEN** `MULTILINESTRING((0 0, 1 1),(0 0, 1 1, 2 2))`
- **WHEN** `ST_Length2D(geom)` is called
- **THEN** the result SHALL be the sum of all linestring lengths
- Validated by: regress/core/measures.sql

#### Scenario: 3D length with Z coordinates
- **GIVEN** `MULTILINESTRING((0 0 0, 1 1 1),(0 0 0, 1 1 1, 2 2 2))`
- **WHEN** `ST_3DLength(geom)` is called
- **THEN** the result SHALL include Z distance in the computation (greater than the 2D length)
- Validated by: regress/core/measures.sql

#### Scenario: Length of a point is zero
- **GIVEN** `POINT(1 2)`
- **WHEN** `ST_Length(geom)` is called
- **THEN** the result SHALL be 0
- Status: untested -- inferred from source code documentation

### Requirement: ST_Area area computation
ST_Area(geom) SHALL return the area of a polygon or multipolygon geometry. For points and linestrings, it SHALL return 0. The area is computed in the planar units of the geometry's SRID.

#### Scenario: Area of a unit square
- **GIVEN** `POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))`
- **WHEN** `ST_Area(geom)` is called
- **THEN** the result SHALL be 1.0
- Validated by: regress/core/measures.sql

#### Scenario: Area of multipolygon with holes
- **GIVEN** `MULTIPOLYGON(((0 0, 10 0, 10 10, 0 10, 0 0)), ((0 0, 10 0, 10 10, 0 10, 0 0),(5 5, 7 5, 7 7, 5 7, 5 5)))`
- **WHEN** `ST_Area(geom)` is called
- **THEN** the result SHALL be the sum of polygon areas minus hole areas
- Validated by: regress/core/measures.sql

#### Scenario: Area of linestring is zero
- **GIVEN** `LINESTRING(0 0, 10 0)`
- **WHEN** `ST_Area(geom)` is called
- **THEN** the result SHALL be 0
- Status: untested -- inferred from source code documentation

### Requirement: ST_Perimeter and ST_3DPerimeter perimeter computation
ST_Perimeter(geom) SHALL return the perimeter of a polygon or multipolygon using 3D/2D coordinates based on dimensionality. ST_Perimeter2D always uses 2D. ST_3DPerimeter always uses 3D. For non-polygonal geometries, the perimeter SHALL be 0.

#### Scenario: Perimeter of a unit square
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** `ST_Perimeter2D(geom)` is called
- **THEN** the result SHALL be 40.0
- Validated by: regress/core/measures.sql

#### Scenario: 3D perimeter with Z coordinates
- **GIVEN** `POLYGON((0 0 1, 10 0 1, 10 10 1, 0 10 1, 0 0 1))`
- **WHEN** `ST_3DPerimeter(geom)` is called
- **THEN** the result SHALL be 40.0 (flat polygon in Z=1 plane has same 3D perimeter as 2D)
- Validated by: regress/core/measures.sql

#### Scenario: Perimeter of multipolygon with holes
- **GIVEN** a multipolygon with exterior rings and interior rings (holes)
- **WHEN** `ST_Perimeter2D(geom)` is called
- **THEN** the result SHALL be the sum of all ring perimeters (exterior and interior)
- Validated by: regress/core/measures.sql

### Requirement: ST_HausdorffDistance shape similarity
ST_HausdorffDistance(geom1, geom2, [densifyFrac]) SHALL return the Hausdorff distance between two geometries, which measures the maximum distance from any point on one geometry to the nearest point on the other. An optional `densifyFrac` parameter densifies geometries before computing the distance. If either geometry is empty, the function SHALL return NULL.

#### Scenario: Hausdorff distance between polygons
- **GIVEN** `POLYGON((0 0, 0 2, 1 2, 2 2, 2 0, 0 0))` and `POLYGON((0.5 0.5, 0.5 2.5, 1.5 2.5, 2.5 2.5, 2.5 0.5, 0.5 0.5))`
- **WHEN** `ST_HausdorffDistance(geom1, geom2)` is called
- **THEN** the result SHALL be approximately 0.70711 (sqrt(2)/2)
- Validated by: regress/core/hausdorff.sql

#### Scenario: Hausdorff distance between linestrings
- **GIVEN** `LINESTRING(0 0, 2 1)` and `LINESTRING(0 0, 2 0)`
- **WHEN** `ST_HausdorffDistance(geom1, geom2)` is called
- **THEN** the result SHALL be 1.0
- Validated by: regress/core/hausdorff.sql

#### Scenario: Hausdorff distance with densification
- **GIVEN** two linestrings and densifyFrac = 0.5
- **WHEN** `ST_HausdorffDistance(geom1, geom2, 0.5)` is called
- **THEN** the result SHALL be a more accurate Hausdorff distance (densification may produce a larger result)
- Validated by: regress/core/hausdorff.sql

#### Scenario: Empty geometry returns NULL
- **GIVEN** `POINT(0 0)` and `POINT EMPTY`
- **WHEN** `ST_HausdorffDistance(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code empty check

### Requirement: ST_FrechetDistance curve similarity
ST_FrechetDistance(geom1, geom2, [densifyFrac]) SHALL return the Frechet distance between two linestrings, which measures similarity while considering the order of points along the curves. An optional `densifyFrac` parameter densifies the geometries. If either geometry is empty, the function SHALL return NULL.

#### Scenario: Frechet distance between linestrings
- **GIVEN** `LINESTRING(0 0, 100 0)` and `LINESTRING(0 0, 50 50, 100 0)`
- **WHEN** `ST_FrechetDistance(geom1, geom2)` is called
- **THEN** the result SHALL be approximately 70.71 (the maximum deviation)
- Validated by: regress/core/frechet.sql

#### Scenario: Frechet distance with densification
- **GIVEN** two linestrings and densifyFrac = 0.5
- **WHEN** `ST_FrechetDistance(geom1, geom2, 0.5)` is called
- **THEN** the result SHALL be a refined Frechet distance
- Validated by: regress/core/frechet.sql

#### Scenario: Empty geometry returns NULL
- **GIVEN** `LINESTRING(0 0, 1 1)` and `LINESTRING EMPTY`
- **WHEN** `ST_FrechetDistance(geom1, geom2)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code empty check

### Requirement: ST_MinimumClearance and ST_MinimumClearanceLine robustness measure
ST_MinimumClearance(geom) SHALL return the minimum distance between any two vertices or vertex-edge pairs in the geometry, which indicates the geometry's robustness to coordinate perturbation. ST_MinimumClearanceLine(geom) SHALL return the 2-point linestring representing the minimum clearance pair. The result SRID SHALL match the input.

#### Scenario: Minimum clearance of a near-degenerate polygon
- **GIVEN** a polygon with nearly-coincident vertices
- **WHEN** `ST_MinimumClearance(geom)` is called
- **THEN** the result SHALL be the minimum distance between the closest vertex pair
- Validated by: regress/core/minimum_clearance.sql

#### Scenario: Minimum clearance line
- **GIVEN** a polygon
- **WHEN** `ST_MinimumClearanceLine(geom)` is called
- **THEN** the result SHALL be a LINESTRING connecting the two closest features
- **AND** `ST_Length(ST_MinimumClearanceLine(geom))` SHALL equal `ST_MinimumClearance(geom)`
- Validated by: regress/core/minimum_clearance.sql

#### Scenario: MinimumClearanceLine preserves SRID
- **GIVEN** a geometry with SRID 4326
- **WHEN** `ST_MinimumClearanceLine(geom)` is called
- **THEN** the result SRID SHALL be 4326
- Status: untested -- inferred from source code SRID handling

### Requirement: Measurement function NULL propagation
All measurement functions SHALL follow standard SQL NULL propagation: if any geometry argument is NULL, the function SHALL return NULL. This applies to ST_Distance, ST_3DDistance, ST_MaxDistance, ST_DWithin, ST_3DDWithin, ST_DFullyWithin, ST_3DDFullyWithin, ST_Length, ST_Area, ST_Perimeter, ST_HausdorffDistance, ST_FrechetDistance, ST_MinimumClearance, ST_ClosestPoint, ST_ShortestLine, ST_LongestLine, and their 3D variants. This is enforced by PostgreSQL's strict function declaration.

#### Scenario: NULL geometry in ST_Distance
- **WHEN** `ST_Distance(NULL::geometry, 'POINT(0 0)'::geometry)` is called
- **THEN** the result SHALL be NULL
- Validated by: regress/core/measures.sql

#### Scenario: NULL in ST_Area
- **WHEN** `ST_Area(NULL::geometry)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- standard PostgreSQL strict function behavior

#### Scenario: NULL distance in ST_DWithin
- **WHEN** `ST_DWithin('POINT(0 0)', 'POINT(1 1)', NULL::float8)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- standard PostgreSQL strict function behavior

### Requirement: ST_3DIntersects 3D intersection test
ST_3DIntersects(geom1, geom2) SHALL return TRUE if the minimum 3D distance between the geometries is exactly 0. It is implemented via `lwgeom_mindistance3d_tolerance(geom1, geom2, 0.0)`. Both inputs SHALL have matching SRIDs. Empty geometries SHALL return FALSE (internal FLT_MAX distance makes 0.0 == FLT_MAX false).

#### Scenario: 3D intersecting geometries
- **GIVEN** `POINT Z (0 0 0)` and `LINESTRING Z (0 0 0, 1 1 1)`
- **WHEN** `ST_3DIntersects(geom1, geom2)` is called
- **THEN** the result SHALL be TRUE
- Validated by: regress/core/measures.sql

#### Scenario: 3D non-intersecting geometries
- **GIVEN** `POINT Z (0 0 5)` and `LINESTRING Z (0 0 0, 1 0 0)`
- **WHEN** `ST_3DIntersects(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Validated by: regress/core/measures.sql

#### Scenario: Empty geometry returns FALSE
- **GIVEN** `POINT Z (0 0 0)` and `POINT EMPTY`
- **WHEN** `ST_3DIntersects(geom1, geom2)` is called
- **THEN** the result SHALL be FALSE
- Status: untested -- inferred from source code (FLT_MAX != 0.0)

---

## Coverage Summary

**Functions covered:** ST_Distance, ST_3DDistance, ST_MaxDistance, ST_DWithin (uncached and cached), ST_3DDWithin, ST_DFullyWithin, ST_3DDFullyWithin, ST_ClosestPoint, ST_3DClosestPoint, ST_ShortestLine, ST_3DShortestLine, ST_LongestLine, ST_3DLongestLine, ST_Length, ST_Length2D, ST_3DLength, ST_Area, ST_Perimeter, ST_Perimeter2D, ST_3DPerimeter, ST_HausdorffDistance, ST_FrechetDistance, ST_MinimumClearance, ST_MinimumClearanceLine, ST_3DIntersects

**Functions deferred:** Geography measurement functions (ST_Distance for geography, ST_Length for geography, ST_Area for geography) are covered by the `geography-type` spec. ST_Azimuth, ST_Angle, ST_Project are deferred to a future `directional-functions` spec.

**Source files analyzed:** `postgis/lwgeom_functions_basic.c`, `postgis/lwgeom_geos.c`, `liblwgeom/measures.c`, `liblwgeom/measures3d.c`, `postgis/postgis.sql.in`

**Test coverage:** 16 requirements, 53 scenarios total. 33 scenarios validated by existing regression tests, 20 scenarios untested (inferred from source code analysis).
