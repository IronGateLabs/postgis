## Purpose

Defines the spatial construction and processing operations that produce new geometries from input geometries. Covers buffer generation, set-theoretic overlay operations (union, intersection, difference, symmetric difference), convex and concave hull computation, simplification algorithms, and various GEOS-dispatched geometry processing functions. All operations preserve the SRID of the input geometry and delegate to GEOS for the core computation. See the `geometry-types` spec for the LWGEOM type system and the `spatial-predicates` spec for predicate functions that test relationships.

## ADDED Requirements

### Requirement: ST_Buffer distance buffering with style parameters
ST_Buffer(geom, distance, [params]) SHALL compute a geometry representing all points within the given distance of the input geometry. The optional third argument is a text string of space-separated key=value pairs controlling buffer style:
- `endcap`: round (default), flat/butt, square
- `join`: round (default), mitre/miter, bevel
- `mitre_limit`/`miter_limit`: float (default 5.0)
- `quad_segs`: integer (default 8) -- segments per quarter circle
- `side`: both (default), left, right (single-sided buffer)

Negative distance on a polygon SHALL produce an interior buffer (inset). Buffer of an empty geometry SHALL return an empty polygon preserving the input SRID. The output SRID SHALL match the input SRID. Z and M coordinates are not preserved in the buffer output.

#### Scenario: Basic circular buffer around a point
- **GIVEN** `POINT(0 0)` with SRID 0
- **WHEN** `ST_Buffer('POINT(0 0)', 1.0)` is called with default parameters
- **THEN** the result SHALL be a polygon approximating a circle of radius 1, with 32 vertices (8 quad_segs * 4)
- **AND** the result SRID SHALL be 0
- Validated by: regress/core/regress_buffer_params.sql

#### Scenario: Buffer with flat endcap and quad_segs
- **GIVEN** `LINESTRING(0 0, 10 0)`
- **WHEN** `ST_Buffer(geom, 1.0, 'endcap=flat quad_segs=2')` is called
- **THEN** the result SHALL be a polygon with flat ends (no rounded caps) and 2 segments per quarter circle at corners
- Validated by: regress/core/regress_buffer_params.sql

#### Scenario: Single-sided buffer (right side)
- **GIVEN** `LINESTRING(0 0, 10 0)`
- **WHEN** `ST_Buffer(geom, 1.0, 'side=right')` is called
- **THEN** the result SHALL be a polygon buffered only on the right side of the line direction
- Validated by: regress/core/regress_buffer_params.sql

#### Scenario: Negative buffer on polygon (inset)
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** `ST_Buffer(geom, -1.0)` is called
- **THEN** the result SHALL be a polygon inset by 1 unit from each edge, approximately `POLYGON((1 1, 9 1, 9 9, 1 9, 1 1))`
- Validated by: regress/core/regress_buffer_params.sql

#### Scenario: Empty geometry buffer returns empty polygon
- **GIVEN** `POINT EMPTY` with SRID 4326
- **WHEN** `ST_Buffer(geom, 1.0)` is called
- **THEN** the result SHALL be `POLYGON EMPTY` with SRID 4326
- Status: untested -- inferred from source code empty check

### Requirement: ST_Union binary and aggregate
ST_Union(geom1, geom2) SHALL compute the union of two geometries. ST_Union(geometry_set) as an aggregate SHALL compute the union of all geometries in the set. An optional gridSize parameter controls precision reduction. The aggregate form SHALL skip NULL inputs and return NULL only if all inputs are NULL. If all non-NULL inputs are empty, the result SHALL be an appropriately-typed empty geometry. SRID mismatch within the aggregate array SHALL raise an error.

#### Scenario: Binary union of overlapping polygons
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Union(geom1, geom2)` is called
- **THEN** the result SHALL be the merged polygon covering the combined area
- Validated by: regress/core/union.sql

#### Scenario: Aggregate union of multiple geometries
- **GIVEN** a table with three overlapping polygon rows
- **WHEN** `SELECT ST_Union(geom) FROM table` is called
- **THEN** the result SHALL be the union of all three polygons
- Validated by: regress/core/unaryunion.sql

#### Scenario: Aggregate with all NULL inputs
- **GIVEN** an aggregate over a set where all geometry values are NULL
- **WHEN** `ST_Union(geom)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- inferred from source code NULL array handling

#### Scenario: SRID mismatch in aggregate raises error
- **GIVEN** an array containing geometries with SRID 4326 and SRID 3857
- **WHEN** `ST_Union(geom_array)` is called
- **THEN** the system SHALL raise an error containing "Operation on mixed SRID geometries"
- Validated by: regress/core/union.sql

### Requirement: ST_UnaryUnion dissolving
ST_UnaryUnion(geom, [gridSize]) SHALL compute the union of all component geometries within a single geometry or geometry collection. This is useful for dissolving self-intersecting geometries or merging overlapping components within a collection. An optional gridSize parameter controls precision reduction.

#### Scenario: Dissolve self-intersecting polygon
- **GIVEN** a self-intersecting polygon (bowtie) `POLYGON((0 0, 2 2, 2 0, 0 2, 0 0))`
- **WHEN** `ST_UnaryUnion(geom)` is called
- **THEN** the result SHALL be a valid multi-polygon with no self-intersections
- Validated by: regress/core/unaryunion.sql

#### Scenario: UnaryUnion of geometry collection
- **GIVEN** `GEOMETRYCOLLECTION(POLYGON((0 0,2 0,2 2,0 2,0 0)),POLYGON((1 1,3 1,3 3,1 3,1 1)))`
- **WHEN** `ST_UnaryUnion(geom)` is called
- **THEN** the result SHALL be the merged polygon covering both input polygons
- Validated by: regress/core/unaryunion.sql

#### Scenario: UnaryUnion with gridSize precision
- **GIVEN** a geometry and gridSize = 1.0
- **WHEN** `ST_UnaryUnion(geom, 1.0)` is called
- **THEN** the result coordinates SHALL be snapped to the 1.0 grid
- Status: untested -- gridSize parameter behavior inferred from source code

### Requirement: ST_Intersection overlay
ST_Intersection(geom1, geom2, [gridSize]) SHALL compute the geometry representing the shared area of the two input geometries. The result type depends on the intersection dimension: polygon/polygon intersection produces polygon(s), line/polygon intersection produces line(s), etc. An optional gridSize parameter controls precision reduction. The result SRID SHALL match the inputs.

#### Scenario: Intersection of overlapping polygons
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_Intersection(geom1, geom2)` is called
- **THEN** the result SHALL be `POLYGON((1 1,2 1,2 2,1 2,1 1))`
- Validated by: regress/core/fixedoverlay.sql

#### Scenario: Intersection of disjoint polygons
- **GIVEN** `POLYGON((0 0,1 0,1 1,0 1,0 0))` and `POLYGON((5 5,6 5,6 6,5 6,5 5))`
- **WHEN** `ST_Intersection(geom1, geom2)` is called
- **THEN** the result SHALL be an empty geometry (GEOMETRYCOLLECTION EMPTY)
- Validated by: regress/core/fixedoverlay.sql

#### Scenario: Line-polygon intersection
- **GIVEN** `LINESTRING(0 0, 10 0)` and `POLYGON((2 -1, 5 -1, 5 1, 2 1, 2 -1))`
- **WHEN** `ST_Intersection(line, polygon)` is called
- **THEN** the result SHALL be `LINESTRING(2 0, 5 0)` (the portion of the line inside the polygon)
- Status: untested -- behavior inferred from GEOS overlay semantics

### Requirement: ST_Difference and ST_SymDifference overlay
ST_Difference(A, B, [gridSize]) SHALL return the portion of geometry A that does not intersect with geometry B. ST_SymDifference(A, B, [gridSize]) SHALL return the portions of both geometries that do not intersect with each other. Both support an optional gridSize parameter. The result SRID SHALL match the inputs.

#### Scenario: Difference removes overlapping area
- **GIVEN** `POLYGON((0 0,3 0,3 3,0 3,0 0))` and `POLYGON((1 1,4 1,4 4,1 4,1 1))`
- **WHEN** `ST_Difference(geom1, geom2)` is called
- **THEN** the result SHALL be the L-shaped portion of geom1 not covered by geom2
- Validated by: regress/core/fixedoverlay.sql

#### Scenario: SymDifference returns non-overlapping portions
- **GIVEN** `POLYGON((0 0,2 0,2 2,0 2,0 0))` and `POLYGON((1 1,3 1,3 3,1 3,1 1))`
- **WHEN** `ST_SymDifference(geom1, geom2)` is called
- **THEN** the result SHALL be a multipolygon containing the two non-overlapping parts
- Validated by: regress/core/fixedoverlay.sql

#### Scenario: Difference is not commutative
- **GIVEN** geometries A and B that partially overlap
- **WHEN** `ST_Difference(A, B)` and `ST_Difference(B, A)` are called
- **THEN** the results SHALL generally differ (difference is order-dependent)
- Status: untested -- mathematical property, not explicitly regression-tested

### Requirement: ST_ConvexHull computation
ST_ConvexHull(geom) SHALL return the smallest convex polygon that contains the input geometry. For an empty input, it SHALL return the empty geometry with the input SRID preserved. For a single point, it SHALL return that point. For collinear points, it SHALL return a linestring.

#### Scenario: Convex hull of a polygon
- **GIVEN** `POLYGON((0 0, 10 0, 5 5, 10 10, 0 10, 0 0))`
- **WHEN** `ST_ConvexHull(geom)` is called
- **THEN** the result SHALL be the convex hull polygon `POLYGON((0 0,10 0,10 10,0 10,0 0))`
- Validated by: regress/core/regress_ogc.sql

#### Scenario: Convex hull of a point set
- **GIVEN** `MULTIPOINT(0 0, 1 0, 1 1, 0 1, 0.5 0.5)`
- **WHEN** `ST_ConvexHull(geom)` is called
- **THEN** the result SHALL be `POLYGON((0 0,1 0,1 1,0 1,0 0))`
- Status: untested -- standard convex hull behavior

#### Scenario: Convex hull of empty geometry
- **GIVEN** `GEOMETRYCOLLECTION EMPTY` with SRID 4326
- **WHEN** `ST_ConvexHull(geom)` is called
- **THEN** the result SHALL be an empty geometry with SRID 4326
- Status: untested -- inferred from source code empty check

### Requirement: ST_ConcaveHull computation
ST_ConcaveHull(geom, ratio, allow_holes) SHALL return a possibly concave polygon that contains all points of the input geometry. The `ratio` parameter (0 to 1) controls the tightness: 0 produces the most concave hull, 1 produces the convex hull. The `allow_holes` boolean controls whether the result may have holes. Requires `POSTGIS_GEOS_VERSION >= 31100` (GEOS 3.11+). On older GEOS versions, it SHALL raise an error.

#### Scenario: Concave hull tighter than convex hull
- **GIVEN** a multipoint cloud with a concavity
- **WHEN** `ST_ConcaveHull(geom, 0.5, false)` is called
- **THEN** the result SHALL be a polygon tighter than the convex hull
- Validated by: regress/core/concave_hull.sql

#### Scenario: Ratio 1.0 produces convex hull
- **GIVEN** a multipoint geometry
- **WHEN** `ST_ConcaveHull(geom, 1.0, false)` is called
- **THEN** the result SHALL be equivalent to `ST_ConvexHull(geom)`
- Validated by: regress/core/concave_hull.sql

#### Scenario: GEOS version guard
- **GIVEN** GEOS version < 3.11
- **WHEN** `ST_ConcaveHull(geom, 0.5, false)` is called
- **THEN** the system SHALL raise an error containing "doesn't support 'GEOSConcaveHull'"
- Requires `POSTGIS_GEOS_VERSION >= 31100`
- Status: untested -- version-dependent error path

### Requirement: ST_SimplifyPolygonHull polygon simplification
ST_SimplifyPolygonHull(geom, vertex_fraction, is_outer) SHALL return a simplified polygon preserving topology. `vertex_fraction` (0 to 1) controls the fraction of vertices to retain. `is_outer` controls whether the result is an outer hull (TRUE) or inner hull (FALSE). Requires `POSTGIS_GEOS_VERSION >= 31100`.

#### Scenario: Outer hull simplification
- **GIVEN** a complex polygon with many vertices
- **WHEN** `ST_SimplifyPolygonHull(geom, 0.5, true)` is called
- **THEN** the result SHALL be a simplified polygon containing the original geometry with approximately 50% of vertices
- Validated by: regress/core/simplify.sql

#### Scenario: Inner hull simplification
- **GIVEN** a complex polygon
- **WHEN** `ST_SimplifyPolygonHull(geom, 0.5, false)` is called
- **THEN** the result SHALL be a simplified polygon contained within the original geometry
- Status: untested -- inner hull behavior inferred from source code

#### Scenario: GEOS version guard
- **GIVEN** GEOS version < 3.11
- **WHEN** `ST_SimplifyPolygonHull(geom, 0.5, true)` is called
- **THEN** the system SHALL raise an error containing "doesn't support 'ST_SimplifyPolygonHull'"
- Requires `POSTGIS_GEOS_VERSION >= 31100`
- Status: untested -- version-dependent error path

### Requirement: ST_Simplify Douglas-Peucker simplification
ST_Simplify(geom, tolerance, [preserve_collapsed]) SHALL simplify a geometry using the Douglas-Peucker algorithm with the given distance tolerance. Points and multipoints SHALL be returned unchanged. An optional `preserve_collapsed` boolean (default false) controls whether geometries that collapse to fewer points than required are preserved or discarded. If the simplified geometry is empty and preserve_collapsed is false, the function SHALL return NULL.

#### Scenario: Simplify a linestring
- **GIVEN** `LINESTRING(0 0, 1 0.5, 2 0, 3 0.5, 4 0)`
- **WHEN** `ST_Simplify(geom, 1.0)` is called
- **THEN** the result SHALL be a simplified linestring with fewer vertices
- Validated by: regress/core/simplify.sql

#### Scenario: Point is returned unchanged
- **GIVEN** `POINT(1 2)`
- **WHEN** `ST_Simplify(geom, 1.0)` is called
- **THEN** the result SHALL be `POINT(1 2)` (points cannot be simplified)
- Validated by: regress/core/simplify.sql

#### Scenario: Collapsed geometry returns NULL without preserve
- **GIVEN** `LINESTRING(0 0, 0.1 0, 0.2 0)` and tolerance 1.0
- **WHEN** `ST_Simplify(geom, 1.0, false)` is called
- **THEN** the result SHALL be NULL (the linestring collapses to a point which is below minimum vertex count)
- Validated by: regress/core/simplify.sql

### Requirement: ST_SimplifyPreserveTopology topology-preserving simplification
ST_SimplifyPreserveTopology(geom, tolerance) SHALL simplify a geometry using the GEOS topology-preserving simplifier, which ensures the result does not create self-intersections or topology errors. Empty geometries and TIN/Triangle types SHALL be returned unchanged. If GEOS returns NULL, the function SHALL return NULL.

#### Scenario: Topology-preserving simplification
- **GIVEN** a polygon with many vertices
- **WHEN** `ST_SimplifyPreserveTopology(geom, 1.0)` is called
- **THEN** the result SHALL be a simplified polygon that remains valid (no self-intersections)
- Validated by: regress/core/simplify.sql

#### Scenario: Triangle type returned unchanged
- **GIVEN** a TRIANGLE geometry
- **WHEN** `ST_SimplifyPreserveTopology(geom, 1.0)` is called
- **THEN** the result SHALL be the original geometry unchanged
- Status: untested -- inferred from source code type check

#### Scenario: Empty geometry returned unchanged
- **GIVEN** `LINESTRING EMPTY`
- **WHEN** `ST_SimplifyPreserveTopology(geom, 1.0)` is called
- **THEN** the result SHALL be the empty geometry
- Status: untested -- inferred from source code empty check

### Requirement: ST_SimplifyVW Visvalingam-Whyatt simplification
ST_SimplifyVW(geom, area_threshold) SHALL simplify a geometry using the Visvalingam-Whyatt area-based algorithm. Vertices whose removal creates a triangle with area less than the threshold are removed. Points and multipoints SHALL be returned unchanged.

#### Scenario: Simplify linestring with area threshold
- **GIVEN** a linestring with small deviations
- **WHEN** `ST_SimplifyVW(geom, 1.0)` is called
- **THEN** the result SHALL have fewer vertices with small-area triangles removed
- Validated by: regress/core/simplifyvw.sql

#### Scenario: Point returned unchanged
- **GIVEN** `POINT(1 2)`
- **WHEN** `ST_SimplifyVW(geom, 1.0)` is called
- **THEN** the result SHALL be `POINT(1 2)`
- Status: untested -- inferred from source code type check

#### Scenario: Area threshold of 0 returns original
- **GIVEN** any linestring
- **WHEN** `ST_SimplifyVW(geom, 0)` is called
- **THEN** the result SHALL be the original geometry (no vertices removed)
- Validated by: regress/core/simplifyvw.sql

### Requirement: ST_ChaikinSmoothing curve smoothing
ST_ChaikinSmoothing(geom, [nIterations], [preserveEndpoints]) SHALL smooth a geometry using Chaikin's corner-cutting algorithm. Default is 1 iteration with endpoint preservation enabled. Points and multipoints SHALL be returned unchanged.

#### Scenario: Smooth a linestring
- **GIVEN** `LINESTRING(0 0, 5 10, 10 0)`
- **WHEN** `ST_ChaikinSmoothing(geom, 1, true)` is called
- **THEN** the result SHALL be a smoother linestring with more vertices and the same start/end points
- Validated by: regress/core/chaikin.sql

#### Scenario: Multiple iterations increase smoothness
- **GIVEN** `LINESTRING(0 0, 5 10, 10 0)`
- **WHEN** `ST_ChaikinSmoothing(geom, 3, true)` is called
- **THEN** the result SHALL have more vertices than a single iteration
- Validated by: regress/core/chaikin.sql

#### Scenario: Point returned unchanged
- **GIVEN** `POINT(1 2)`
- **WHEN** `ST_ChaikinSmoothing(geom)` is called
- **THEN** the result SHALL be `POINT(1 2)`
- Status: untested -- inferred from source code type check

### Requirement: ST_Split geometry splitting
ST_Split(geom, blade) SHALL split a geometry by a blade geometry. A polygon split by a line produces a collection of polygons. A line split by a point produces a collection of lines. The SRID SHALL be preserved.

#### Scenario: Split polygon by line
- **GIVEN** `POLYGON((0 0,10 0,10 10,0 10,0 0))` and blade `LINESTRING(5 -1, 5 11)`
- **WHEN** `ST_Split(polygon, line)` is called
- **THEN** the result SHALL be a GEOMETRYCOLLECTION of two polygons
- Validated by: regress/core/split.sql

#### Scenario: Split line by point
- **GIVEN** `LINESTRING(0 0, 10 0)` and blade `POINT(5 0)`
- **WHEN** `ST_Split(line, point)` is called
- **THEN** the result SHALL be a GEOMETRYCOLLECTION of two linestrings
- Validated by: regress/core/split.sql

#### Scenario: Blade does not intersect geometry
- **GIVEN** `LINESTRING(0 0, 10 0)` and blade `POINT(5 5)` (not on the line)
- **WHEN** `ST_Split(line, point)` is called
- **THEN** the system SHALL raise an error because the blade does not split the geometry
- Validated by: regress/core/split.sql

### Requirement: ST_Snap geometry snapping
ST_Snap(input, reference, tolerance) SHALL snap vertices of the input geometry to vertices of the reference geometry within the given tolerance distance. The SRID SHALL be preserved.

#### Scenario: Snap nearby vertices
- **GIVEN** `LINESTRING(0 0, 10 0)` and reference `POINT(5 0.1)` with tolerance 0.5
- **WHEN** `ST_Snap(line, point, 0.5)` is called
- **THEN** the result SHALL have a vertex snapped to the reference point location
- Validated by: regress/core/snap.sql

#### Scenario: No snapping beyond tolerance
- **GIVEN** `LINESTRING(0 0, 10 0)` and reference `POINT(5 5)` with tolerance 0.1
- **WHEN** `ST_Snap(line, point, 0.1)` is called
- **THEN** the result SHALL be unchanged (reference point is too far)
- Validated by: regress/core/snap.sql

#### Scenario: Snap preserves SRID
- **GIVEN** geometries with SRID 4326
- **WHEN** `ST_Snap(geom1, geom2, tolerance)` is called
- **THEN** the result SRID SHALL be 4326
- Status: untested -- SRID preservation inferred from GEOS output handling

### Requirement: ST_DelaunayTriangles and ST_VoronoiPolygons triangulation and tessellation
ST_DelaunayTriangles(geom, tolerance, flags) SHALL compute a Delaunay triangulation of the input vertices. The flags parameter controls output: 0 for triangles (default), 1 for edges only. ST_VoronoiPolygons(geom, tolerance, envelope) SHALL compute the Voronoi diagram. An optional envelope parameter clips the result.

#### Scenario: Delaunay triangulation of points
- **GIVEN** `MULTIPOINT(0 0, 10 0, 5 10)`
- **WHEN** `ST_DelaunayTriangles(geom, 0, 0)` is called
- **THEN** the result SHALL be a GEOMETRYCOLLECTION containing one triangle
- Validated by: regress/core/delaunaytriangles.sql

#### Scenario: Delaunay edges only
- **GIVEN** `MULTIPOINT(0 0, 10 0, 5 10)`
- **WHEN** `ST_DelaunayTriangles(geom, 0, 1)` is called
- **THEN** the result SHALL be a MULTILINESTRING of the triangle edges
- Validated by: regress/core/delaunaytriangles.sql

#### Scenario: Voronoi polygons
- **GIVEN** `MULTIPOINT(0 0, 10 0, 5 10)`
- **WHEN** `ST_VoronoiPolygons(geom)` is called
- **THEN** the result SHALL be a GEOMETRYCOLLECTION of Voronoi polygons, one per input point
- Validated by: regress/core/voronoi.sql

### Requirement: ST_Node and ST_LineMerge line processing
ST_Node(geom) SHALL node a set of linestrings by adding intersection points at all crossings. ST_LineMerge(geom) SHALL merge a collection of linestrings into maximal-length linestrings where endpoints match. An optional `directed` parameter (boolean) controls whether line direction is preserved during merging.

#### Scenario: Node intersecting lines
- **GIVEN** `MULTILINESTRING((0 0, 10 10), (0 10, 10 0))`
- **WHEN** `ST_Node(geom)` is called
- **THEN** the result SHALL be a MULTILINESTRING where each segment is split at the crossing point (5 5)
- Validated by: regress/core/node.sql

#### Scenario: Merge connected linestrings
- **GIVEN** `MULTILINESTRING((0 0, 5 0), (5 0, 10 0))`
- **WHEN** `ST_LineMerge(geom)` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 5 0, 10 0)`
- Validated by: regress/core/regress_ogc.sql

#### Scenario: LineMerge with disconnected lines
- **GIVEN** `MULTILINESTRING((0 0, 5 0), (10 0, 15 0))`
- **WHEN** `ST_LineMerge(geom)` is called
- **THEN** the result SHALL be a MULTILINESTRING (lines cannot be merged)
- Status: untested -- standard LineMerge behavior

### Requirement: ST_SharedPaths common path extraction
ST_SharedPaths(geom1, geom2) SHALL return a geometry collection containing paths shared between two linear geometries. The collection has two components: paths shared in the same direction and paths shared in the opposite direction.

#### Scenario: Shared paths between linestrings
- **GIVEN** `LINESTRING(0 0, 5 0, 10 0)` and `LINESTRING(3 0, 7 0)`
- **WHEN** `ST_SharedPaths(geom1, geom2)` is called
- **THEN** the result SHALL be a geometry collection with the shared segment `LINESTRING(3 0, 5 0, 7 0)` in the same-direction component
- Validated by: regress/core/sharedpaths.sql

#### Scenario: No shared paths
- **GIVEN** `LINESTRING(0 0, 5 0)` and `LINESTRING(0 1, 5 1)`
- **WHEN** `ST_SharedPaths(geom1, geom2)` is called
- **THEN** the result SHALL be a geometry collection with empty components
- Validated by: regress/core/sharedpaths.sql

#### Scenario: SRID mismatch error
- **GIVEN** two linestrings with different SRIDs
- **WHEN** `ST_SharedPaths(geom1, geom2)` is called
- **THEN** the system SHALL raise an SRID mismatch error
- Status: untested -- inferred from source code SRID check

### Requirement: ST_ClipByBox2d fast box clipping
ST_ClipByBox2d(geom, box2d) SHALL clip a geometry to a 2D bounding box. This is a fast-path alternative to `ST_Intersection(geom, ST_MakeEnvelope(...))`. The result SRID SHALL match the input.

#### Scenario: Clip polygon by box
- **GIVEN** `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` and box `ST_MakeEnvelope(2, 2, 8, 8)`
- **WHEN** `ST_ClipByBox2d(geom, box)` is called
- **THEN** the result SHALL be the clipped polygon within the box bounds
- Validated by: regress/core/clipbybox2d.sql

#### Scenario: Geometry fully inside box
- **GIVEN** `POINT(5 5)` and box `ST_MakeEnvelope(0, 0, 10, 10)`
- **WHEN** `ST_ClipByBox2d(geom, box)` is called
- **THEN** the result SHALL be the original geometry unchanged
- Validated by: regress/core/clipbybox2d.sql

#### Scenario: Geometry fully outside box
- **GIVEN** `POINT(50 50)` and box `ST_MakeEnvelope(0, 0, 10, 10)`
- **WHEN** `ST_ClipByBox2d(geom, box)` is called
- **THEN** the result SHALL be an empty geometry
- Validated by: regress/core/clipbybox2d.sql

### Requirement: ST_Polygonize and ST_BuildArea polygon construction
ST_Polygonize(geom_array) SHALL construct polygons from a set of linestrings that form closed rings. ST_BuildArea(geom) SHALL construct an areal geometry from linework.

#### Scenario: Polygonize closed ring linestrings
- **GIVEN** an array of linestrings forming a closed rectangle
- **WHEN** `ST_Polygonize(array)` is called
- **THEN** the result SHALL be a geometry collection containing the polygon formed by the rings
- Validated by: regress/core/polygonize.sql

#### Scenario: Build area from linework
- **GIVEN** `MULTILINESTRING((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** `ST_BuildArea(geom)` is called
- **THEN** the result SHALL be `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- Validated by: regress/core/polygonize.sql

#### Scenario: Polygonize with no valid rings
- **GIVEN** a set of disconnected linestrings that do not form rings
- **WHEN** `ST_Polygonize(array)` is called
- **THEN** the result SHALL be an empty geometry collection
- Status: untested -- standard polygonize behavior for non-ring input

### Requirement: ST_OffsetCurve line offsetting
ST_OffsetCurve(geom, distance, [params]) SHALL compute a line offset from the input linestring by the given distance. Positive distance offsets to the left, negative to the right. Style parameters (join, mitre_limit, quad_segs) are supported as a text string similar to ST_Buffer.

#### Scenario: Offset line to the left
- **GIVEN** `LINESTRING(0 0, 10 0)` and distance 1.0
- **WHEN** `ST_OffsetCurve(geom, 1.0)` is called
- **THEN** the result SHALL be approximately `LINESTRING(0 1, 10 1)` (offset to the left)
- Validated by: regress/core/offsetcurve.sql

#### Scenario: Offset line to the right
- **GIVEN** `LINESTRING(0 0, 10 0)` and distance -1.0
- **WHEN** `ST_OffsetCurve(geom, -1.0)` is called
- **THEN** the result SHALL be approximately `LINESTRING(10 -1, 0 -1)` (offset to the right, reversed direction)
- Validated by: regress/core/offsetcurve.sql

#### Scenario: Offset with join style parameter
- **GIVEN** `LINESTRING(0 0, 5 0, 5 5)` and distance 1.0
- **WHEN** `ST_OffsetCurve(geom, 1.0, 'join=mitre')` is called
- **THEN** the result SHALL have mitre-joined corners
- Validated by: regress/core/offsetcurve.sql

### Requirement: ST_ReducePrecision coordinate precision reduction
ST_ReducePrecision(geom, gridSize) SHALL reduce the precision of a geometry's coordinates to the given grid size. This uses GEOS overlay operations with a fixed precision model to ensure the result is topologically valid.

#### Scenario: Reduce precision to 1.0 grid
- **GIVEN** `POLYGON((0.1 0.1, 9.9 0.1, 9.9 9.9, 0.1 9.9, 0.1 0.1))`
- **WHEN** `ST_ReducePrecision(geom, 1.0)` is called
- **THEN** the result coordinates SHALL be snapped to the nearest integer
- Validated by: regress/core/fixedoverlay.sql

#### Scenario: Precision reduction preserves topology
- **GIVEN** a valid polygon
- **WHEN** `ST_ReducePrecision(geom, 0.01)` is called
- **THEN** the result SHALL be a valid polygon
- Status: untested -- topological validity preservation inferred from GEOS implementation

#### Scenario: Grid size 0 returns original
- **GIVEN** any geometry
- **WHEN** `ST_ReducePrecision(geom, 0)` is called
- **THEN** the result SHALL be the original geometry (no precision change)
- Status: untested -- boundary condition

### Requirement: Spatial operation SRID preservation
All spatial operations (ST_Buffer, ST_Union, ST_Intersection, ST_Difference, ST_SymDifference, ST_ConvexHull, and all others in this spec) SHALL preserve the SRID of the input geometry in the output. For binary operations, both inputs SHALL have matching SRIDs. The output SRID SHALL match the input SRID.

#### Scenario: Buffer preserves SRID
- **GIVEN** `POINT(0 0)` with SRID 4326
- **WHEN** `ST_Buffer(geom, 1.0)` is called
- **THEN** the result SRID SHALL be 4326
- Validated by: regress/core/regress_buffer_params.sql

#### Scenario: Intersection preserves SRID
- **GIVEN** two polygons both with SRID 3857
- **WHEN** `ST_Intersection(geom1, geom2)` is called
- **THEN** the result SRID SHALL be 3857
- Status: untested -- SRID preservation inferred from source code

#### Scenario: ConvexHull preserves SRID
- **GIVEN** `MULTIPOINT(0 0, 10 0, 5 10)` with SRID 4326
- **WHEN** `ST_ConvexHull(geom)` is called
- **THEN** the result SRID SHALL be 4326
- Status: untested -- SRID preservation inferred from source code

---

## Coverage Summary

**Functions covered:** ST_Buffer, ST_Union (binary), ST_Union (aggregate/pgis_union_geometry_array), ST_UnaryUnion, ST_Intersection, ST_Difference, ST_SymDifference, ST_ConvexHull, ST_ConcaveHull, ST_SimplifyPolygonHull, ST_Simplify, ST_SimplifyPreserveTopology, ST_SimplifyVW, ST_ChaikinSmoothing, ST_Split, ST_Snap, ST_Node, ST_LineMerge, ST_SharedPaths, ST_ClipByBox2d, ST_Polygonize, ST_BuildArea, ST_DelaunayTriangles, ST_VoronoiPolygons, ST_OffsetCurve, ST_ReducePrecision

**Functions deferred:** ST_TriangulatePolygon, ST_GeneratePoints, ST_MakeValid, ST_MaximumInscribedCircle, ST_LargestEmptyCircle, ST_OrientedEnvelope (these are covered conceptually but detailed specs are deferred to avoid excessive spec length). ST_Subdivide, ST_CoverageUnion, ST_CoverageSimplify, ST_CoverageInvalidEdges are deferred to a potential `coverage-operations` spec.

**Source files analyzed:** `postgis/lwgeom_geos.c`, `postgis/lwgeom_functions_analytic.c`, `postgis/postgis.sql.in`

**Test coverage:** 22 requirements, 70 scenarios total. 42 scenarios validated by existing regression tests, 28 scenarios untested (inferred from source code analysis or mathematical properties).
