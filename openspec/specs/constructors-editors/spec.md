## Purpose

Defines the PostGIS constructor functions (creating geometries from coordinates and components), editor functions (modifying existing geometries), decomposition functions (extracting components), and accessor functions (reading properties). This spec depends on geometry-types for LWGEOM structure, gserialized-format for on-disk representation, and coordinate-transforms for SRID handling.

## ADDED Requirements

### Requirement: Point constructors
PostGIS SHALL provide the following point constructor functions:
- `ST_MakePoint(x float8, y float8)` -- 2D point
- `ST_MakePoint(x float8, y float8, z float8)` -- 3DZ point
- `ST_MakePoint(x float8, y float8, z float8, m float8)` -- 4D point
- `ST_MakePointM(x float8, y float8, m float8)` -- 3DM point
- `ST_Point(x float8, y float8)` -- 2D point (OGC-style, equivalent to ST_MakePoint 2D)
- `ST_PointZ(x float8, y float8, z float8, srid integer DEFAULT 0)` -- 3DZ with optional SRID
- `ST_PointM(x float8, y float8, m float8, srid integer DEFAULT 0)` -- 3DM with optional SRID
- `ST_PointZM(x float8, y float8, z float8, m float8, srid integer DEFAULT 0)` -- 4D with optional SRID

All constructors SHALL produce valid LWPOINT geometries with the appropriate dimensionality flags set.

#### Scenario: ST_MakePoint 2D
- **GIVEN** coordinates x=1.0, y=2.0
- **WHEN** `ST_MakePoint(1.0, 2.0)` is called
- **THEN** the result SHALL be a 2D point `POINT(1 2)` with SRID 0
- Validated by: regress/core/ctors.sql

#### Scenario: ST_MakePoint 3DZ
- **GIVEN** coordinates x=1.0, y=2.0, z=3.0
- **WHEN** `ST_MakePoint(1.0, 2.0, 3.0)` is called
- **THEN** the result SHALL be a 3DZ point with `ST_Z()` returning 3.0
- Validated by: regress/core/ctors.sql

#### Scenario: ST_MakePoint 4D
- **GIVEN** coordinates x=1.0, y=2.0, z=3.0, m=4.0
- **WHEN** `ST_MakePoint(1.0, 2.0, 3.0, 4.0)` is called
- **THEN** the result SHALL be a 4D point with both Z and M values
- Validated by: regress/core/ctors.sql

#### Scenario: ST_PointZ with SRID
- **GIVEN** coordinates and SRID 4326
- **WHEN** `ST_PointZ(1.0, 2.0, 3.0, 4326)` is called
- **THEN** the result SHALL have SRID 4326 and Z=3.0
- Validated by: regress/core/ctors.sql

#### Scenario: ST_MakePointM creates 3DM point
- **GIVEN** coordinates x=1.0, y=2.0, m=3.0
- **WHEN** `ST_MakePointM(1.0, 2.0, 3.0)` is called
- **THEN** the result SHALL have M flag set and Z flag not set
- Validated by: regress/core/ctors.sql

### Requirement: Line constructors
PostGIS SHALL provide line constructor functions:
- `ST_MakeLine(geometry, geometry)` -- creates a line from two points/lines
- `ST_MakeLine(geometry[])` -- creates a line from an array of points/lines
- `ST_MakeLine(geometry)` -- aggregate function collecting points into a line
- `ST_LineFromMultiPoint(geometry)` -- creates a line from a multipoint

ST_MakeLine SHALL accept POINT and LINESTRING inputs. When given linestrings, it SHALL concatenate their point sequences. The result SHALL have at least 2 points.

#### Scenario: MakeLine from two points
- **GIVEN** two points `POINT(0 0)` and `POINT(1 1)`
- **WHEN** `ST_MakeLine(p1, p2)` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 1 1)`
- Validated by: regress/core/ctors.sql

#### Scenario: MakeLine from array of points
- **GIVEN** an array of 4 points
- **WHEN** `ST_MakeLine(ARRAY[p1, p2, p3, p4])` is called
- **THEN** the result SHALL be a linestring with 4 vertices
- Validated by: regress/core/ctors.sql

#### Scenario: MakeLine aggregate
- **GIVEN** a table with point geometries
- **WHEN** `ST_MakeLine(geom ORDER BY id)` is used as an aggregate
- **THEN** the result SHALL be a single linestring connecting all points in order
- Validated by: regress/core/ctors.sql

#### Scenario: LineFromMultiPoint
- **GIVEN** a multipoint `MULTIPOINT((0 0),(1 1),(2 2))`
- **WHEN** `ST_LineFromMultiPoint(mp)` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 1 1, 2 2)`
- Validated by: regress/core/ctors.sql

### Requirement: Polygon and envelope constructors
PostGIS SHALL provide:
- `ST_MakePolygon(geometry)` -- creates a polygon from a closed linestring (shell only)
- `ST_MakePolygon(geometry, geometry[])` -- creates a polygon with shell and hole rings
- `ST_MakeEnvelope(xmin float8, ymin float8, xmax float8, ymax float8, srid integer DEFAULT 0)` -- creates a rectangular polygon from bounds
- `ST_TileEnvelope(zoom integer, x integer, y integer, bounds geometry DEFAULT ...)` -- creates a web map tile envelope

ST_MakePolygon SHALL require that the input linestring is closed (first point equals last point).

#### Scenario: MakePolygon from closed linestring
- **GIVEN** a closed linestring `LINESTRING(0 0, 10 0, 10 10, 0 10, 0 0)`
- **WHEN** `ST_MakePolygon(line)` is called
- **THEN** the result SHALL be a polygon with one exterior ring and no holes
- Validated by: regress/core/ctors.sql

#### Scenario: MakePolygon with holes
- **GIVEN** a closed outer ring and a closed inner ring
- **WHEN** `ST_MakePolygon(outer_ring, ARRAY[inner_ring])` is called
- **THEN** the result SHALL be a polygon with one exterior ring and one interior ring
- Validated by: regress/core/ctors.sql

#### Scenario: MakeEnvelope creates rectangle
- **GIVEN** bounds xmin=0, ymin=0, xmax=1, ymax=1 with SRID 4326
- **WHEN** `ST_MakeEnvelope(0, 0, 1, 1, 4326)` is called
- **THEN** the result SHALL be a rectangular polygon with SRID 4326 and 5 vertices (closed ring)
- Validated by: regress/core/ctors.sql

#### Scenario: TileEnvelope for web map tile
- **GIVEN** zoom=1, x=0, y=0
- **WHEN** `ST_TileEnvelope(1, 0, 0)` is called
- **THEN** the result SHALL be a polygon in SRID 3857 representing the northwest tile quadrant
- Validated by: regress/core/ctors.sql

### Requirement: Collection constructors
PostGIS SHALL provide:
- `ST_Collect(geometry, geometry)` -- binary collect, creates a collection from two geometries
- `ST_Collect(geometry)` -- aggregate function, collects all geometries into a collection
- `ST_Collect(geometry[])` -- array variant
- `ST_Multi(geometry)` -- promotes a simple geometry to its multi-type equivalent

ST_Collect SHALL create typed collections when inputs are homogeneous (all points -> MULTIPOINT, all lines -> MULTILINESTRING, all polygons -> MULTIPOLYGON) and GEOMETRYCOLLECTION when inputs are heterogeneous.

ST_Multi SHALL promote POINT to MULTIPOINT, LINESTRING to MULTILINESTRING, POLYGON to MULTIPOLYGON. Already-multi types SHALL be returned unchanged.

#### Scenario: Collect two points creates multipoint
- **GIVEN** two points
- **WHEN** `ST_Collect(p1, p2)` is called
- **THEN** the result SHALL be a MULTIPOINT containing both points
- Validated by: regress/core/ctors.sql

#### Scenario: Collect mixed types creates geometrycollection
- **GIVEN** a point and a linestring
- **WHEN** `ST_Collect(point, line)` is called
- **THEN** the result SHALL be a GEOMETRYCOLLECTION
- Validated by: regress/core/ctors.sql

#### Scenario: ST_Multi promotes point to multipoint
- **GIVEN** a point `POINT(0 0)`
- **WHEN** `ST_Multi(point)` is called
- **THEN** the result SHALL be `MULTIPOINT((0 0))`
- Validated by: regress/core/ctors.sql

#### Scenario: Collect aggregate
- **GIVEN** a table with mixed geometry types
- **WHEN** `ST_Collect(geom)` is used as an aggregate
- **THEN** the result SHALL be a single collection containing all geometries
- Validated by: regress/core/ctors.sql

### Requirement: Geometry decomposition (ST_Dump family)
PostGIS SHALL provide set-returning decomposition functions:
- `ST_Dump(geometry)` -- returns a set of `geometry_dump` (path integer[], geom geometry) for each component of a collection
- `ST_DumpPoints(geometry)` -- returns all vertices as individual points with path arrays
- `ST_DumpSegments(geometry)` -- returns all segments as 2-point linestrings with path arrays
- `ST_DumpRings(geometry)` -- returns all rings of a polygon (exterior and interior) as linestrings

The `path` array SHALL encode the traversal path into nested collections (e.g., `{1}` for the first element, `{2,3}` for the third sub-geometry of the second element in a nested collection).

#### Scenario: Dump multipoint
- **GIVEN** a multipoint `MULTIPOINT((0 0),(1 1),(2 2))`
- **WHEN** `ST_Dump(mp)` is called
- **THEN** 3 rows SHALL be returned, each with path `{1}`, `{2}`, `{3}` and the respective point geometry
- Validated by: regress/core/dump.sql

#### Scenario: DumpPoints of linestring
- **GIVEN** a linestring `LINESTRING(0 0, 1 1, 2 2)`
- **WHEN** `ST_DumpPoints(line)` is called
- **THEN** 3 rows SHALL be returned, each containing a POINT and a path array
- Validated by: regress/core/dumppoints.sql

#### Scenario: DumpSegments of linestring
- **GIVEN** a linestring `LINESTRING(0 0, 1 1, 2 2)`
- **WHEN** `ST_DumpSegments(line)` is called
- **THEN** 2 rows SHALL be returned, each containing a 2-point LINESTRING segment
- Validated by: regress/core/dumpsegments.sql

#### Scenario: DumpRings of polygon with hole
- **GIVEN** a polygon with one exterior ring and one interior ring
- **WHEN** `ST_DumpRings(poly)` is called
- **THEN** 2 rows SHALL be returned: path `{0}` for exterior ring, `{1}` for interior ring
- Validated by: regress/core/dump.sql

### Requirement: Dimension forcing
PostGIS SHALL provide functions to force geometries to specific dimensionality:
- `ST_Force2D(geometry)` -- strips Z and M, produces XY
- `ST_Force3D(geometry)` / `ST_Force3DZ(geometry)` -- ensures Z dimension, adding Z=0 if missing
- `ST_Force3DM(geometry)` -- ensures M dimension, adding M=0 if missing
- `ST_Force4D(geometry)` -- ensures both Z and M, adding 0 for missing dimensions
- `ST_ForceCollection(geometry)` -- wraps any geometry in a GEOMETRYCOLLECTION
- `ST_ForceSFS(geometry)` / `ST_ForceSFS(geometry, version text)` -- forces to SFS 1.1 types (replaces curves with linear approximations)
- `ST_ForceCurve(geometry)` -- promotes linear types to curve equivalents where possible

#### Scenario: Force2D strips Z
- **GIVEN** a 3DZ geometry `POINT Z (1 2 3)`
- **WHEN** `ST_Force2D(geom)` is called
- **THEN** the result SHALL be `POINT(1 2)` with no Z flag
- Validated by: regress/core/ctors.sql

#### Scenario: Force3DZ adds Z=0
- **GIVEN** a 2D geometry `POINT(1 2)`
- **WHEN** `ST_Force3DZ(geom)` is called
- **THEN** the result SHALL be `POINT Z (1 2 0)` with Z flag set
- Validated by: regress/core/ctors.sql

#### Scenario: Force4D adds both Z and M
- **GIVEN** a 2D geometry `POINT(1 2)`
- **WHEN** `ST_Force4D(geom)` is called
- **THEN** the result SHALL be `POINT ZM (1 2 0 0)` with both Z and M flags set
- Validated by: regress/core/ctors.sql

#### Scenario: ForceCurve promotes linestring to compoundcurve
- **GIVEN** a linestring geometry
- **WHEN** `ST_ForceCurve(geom)` is called
- **THEN** the result SHALL be a COMPOUNDCURVE containing the linestring
- Validated by: regress/core/forcecurve.sql

### Requirement: SRID and coordinate editors
PostGIS SHALL provide:
- `ST_SetSRID(geometry, integer)` -- sets the SRID metadata without transforming coordinates
- `ST_FlipCoordinates(geometry)` -- swaps X and Y ordinates (useful for lat/lon vs lon/lat correction)
- `ST_SwapOrdinates(geometry, text)` -- swaps any two ordinate axes (e.g., 'xy', 'xz', 'xm', 'yz', 'ym', 'zm')
- `ST_SnapToGrid(geometry, size float8)` -- snaps coordinates to a regular grid
- `ST_SnapToGrid(geometry, origin_x, origin_y, size_x, size_y)` -- grid with custom origin and cell sizes
- `ST_RemoveRepeatedPoints(geometry, tolerance float8 DEFAULT 0)` -- removes consecutive duplicate points
- `ST_QuantizeCoordinates(geometry, prec_x integer, prec_y integer DEFAULT prec_x, ...)` -- reduces coordinate precision

#### Scenario: FlipCoordinates swaps X and Y
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** `ST_FlipCoordinates(geom)` is called
- **THEN** the result SHALL be `POINT(2 1)`
- Validated by: regress/core/swapordinates.sql

#### Scenario: SwapOrdinates xz on 3D point
- **GIVEN** a geometry `POINT Z (1 2 3)`
- **WHEN** `ST_SwapOrdinates(geom, 'xz')` is called
- **THEN** the result SHALL be `POINT Z (3 2 1)`
- Validated by: regress/core/swapordinates.sql

#### Scenario: SnapToGrid rounds coordinates
- **GIVEN** a geometry `POINT(1.123456 2.654321)`
- **WHEN** `ST_SnapToGrid(geom, 0.01)` is called
- **THEN** the result SHALL be `POINT(1.12 2.65)`
- Validated by: regress/core/snaptogrid.sql

#### Scenario: RemoveRepeatedPoints with tolerance
- **GIVEN** a linestring with consecutive points closer than 0.001
- **WHEN** `ST_RemoveRepeatedPoints(geom, 0.001)` is called
- **THEN** consecutive points within tolerance SHALL be collapsed to one
- Validated by: regress/core/remove_repeated_points.sql

#### Scenario: QuantizeCoordinates reduces precision
- **GIVEN** a geometry with high-precision coordinates
- **WHEN** `ST_QuantizeCoordinates(geom, 4)` is called
- **THEN** coordinates SHALL be quantized to 4 significant digits of floating-point precision
- Validated by: regress/core/quantize_coordinates.sql

### Requirement: Vertex editing
PostGIS SHALL provide vertex-level editing functions for linestrings:
- `ST_AddPoint(geometry, geometry, position integer DEFAULT -1)` -- inserts a point at a position (-1 = append)
- `ST_RemovePoint(geometry, offset integer)` -- removes the point at the given offset (0-based)
- `ST_SetPoint(geometry, position integer, point geometry)` -- replaces the point at the given position

These functions SHALL only work on LINESTRING geometries and raise an error for other types. Position values SHALL be 0-based.

#### Scenario: AddPoint appends to linestring
- **GIVEN** a linestring `LINESTRING(0 0, 1 1)`
- **WHEN** `ST_AddPoint(line, ST_MakePoint(2, 2))` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 1 1, 2 2)`
- Validated by: regress/core/ctors.sql

#### Scenario: RemovePoint removes vertex by offset
- **GIVEN** a linestring `LINESTRING(0 0, 1 1, 2 2)`
- **WHEN** `ST_RemovePoint(line, 1)` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 2 2)`
- Validated by: regress/core/removepoint.sql

#### Scenario: SetPoint replaces vertex
- **GIVEN** a linestring `LINESTRING(0 0, 1 1, 2 2)`
- **WHEN** `ST_SetPoint(line, 1, ST_MakePoint(5, 5))` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 5 5, 2 2)`
- Validated by: regress/core/setpoint.sql

#### Scenario: AddPoint at specific position
- **GIVEN** a linestring `LINESTRING(0 0, 2 2)`
- **WHEN** `ST_AddPoint(line, ST_MakePoint(1, 1), 1)` is called
- **THEN** the result SHALL be `LINESTRING(0 0, 1 1, 2 2)` (inserted at position 1)
- Validated by: regress/core/ctors.sql

### Requirement: Affine transformations
PostGIS SHALL provide affine transformation functions that operate in coordinate space (not CRS transforms):
- `ST_Affine(geometry, a,b,c,d,e,f,g,h,i,xoff,yoff,zoff)` -- full 3D affine transform matrix
- `ST_Affine(geometry, a,b,d,e,xoff,yoff)` -- 2D affine transform
- `ST_Translate(geometry, dx float8, dy float8, dz float8 DEFAULT 0)` -- translate by offsets
- `ST_Scale(geometry, xfactor float8, yfactor float8, zfactor float8 DEFAULT 0)` -- scale by factors
- `ST_Scale(geometry, factor geometry)` -- scale using a point as factor vector
- `ST_Rotate(geometry, angle float8)` -- rotate around origin by angle (radians)
- `ST_Rotate(geometry, angle float8, origin geometry)` -- rotate around a point
- `ST_Rotate(geometry, angle float8, x0 float8, y0 float8)` -- rotate around (x0,y0)
- `ST_TransScale(geometry, dx float8, dy float8, xfactor float8, yfactor float8)` -- combined translate+scale

These functions SHALL recompute bounding boxes if the input had one.

#### Scenario: Translate point
- **GIVEN** a geometry `POINT(0 0)`
- **WHEN** `ST_Translate(geom, 10, 20)` is called
- **THEN** the result SHALL be `POINT(10 20)`
- Validated by: regress/core/affine.sql

#### Scenario: Scale geometry
- **GIVEN** a geometry `LINESTRING(1 1, 2 2)`
- **WHEN** `ST_Scale(geom, 2, 3)` is called
- **THEN** the result SHALL be `LINESTRING(2 3, 4 6)`
- Validated by: regress/core/affine.sql

#### Scenario: Rotate 90 degrees around origin
- **GIVEN** a geometry `POINT(1 0)`
- **WHEN** `ST_Rotate(geom, pi()/2)` is called
- **THEN** the result SHALL be approximately `POINT(0 1)` (90-degree counterclockwise rotation)
- Validated by: regress/core/affine.sql

#### Scenario: Full 3D affine transform
- **GIVEN** a 3D geometry
- **WHEN** `ST_Affine(geom, a,b,c,d,e,f,g,h,i,xoff,yoff,zoff)` is called
- **THEN** every coordinate SHALL be transformed by the 3x3 matrix plus translation vector
- Validated by: regress/core/affine.sql

### Requirement: Geometry reversal and orientation
PostGIS SHALL provide:
- `ST_Reverse(geometry)` -- reverses the vertex order of all components
- `ST_ForcePolygonCW(geometry)` / `ST_ForceRHR(geometry)` -- forces clockwise exterior ring (right-hand rule)
- `ST_ForcePolygonCCW(geometry)` -- forces counterclockwise exterior ring
- `ST_Normalize(geometry)` -- normalizes geometry to a canonical form
- `ST_Scroll(geometry, point geometry)` -- rotates a closed linestring/polygon ring to start at the given point

#### Scenario: Reverse linestring vertex order
- **GIVEN** a linestring `LINESTRING(0 0, 1 1, 2 2)`
- **WHEN** `ST_Reverse(line)` is called
- **THEN** the result SHALL be `LINESTRING(2 2, 1 1, 0 0)`
- Validated by: regress/core/reverse.sql

#### Scenario: ForcePolygonCW ensures clockwise exterior
- **GIVEN** a polygon with counterclockwise exterior ring
- **WHEN** `ST_ForcePolygonCW(poly)` is called
- **THEN** the exterior ring SHALL be clockwise and interior rings counterclockwise
- Validated by: regress/core/orientation.sql

#### Scenario: Scroll rotates ring start point
- **GIVEN** a closed linestring `LINESTRING(0 0, 1 0, 1 1, 0 1, 0 0)`
- **WHEN** `ST_Scroll(line, ST_MakePoint(1, 0))` is called
- **THEN** the result SHALL start at `(1 0)`: `LINESTRING(1 0, 1 1, 0 1, 0 0, 1 0)`
- Validated by: regress/core/scroll.sql

#### Scenario: Normalize produces canonical form
- **GIVEN** a geometry
- **WHEN** `ST_Normalize(geom)` is called
- **THEN** the result SHALL be in a canonical form (consistent ordering of points, rings, and sub-geometries)
- Validated by: regress/core/normalize.sql

### Requirement: Coordinate accessors
PostGIS SHALL provide accessor functions to read geometry properties:
- `ST_X(geometry)`, `ST_Y(geometry)`, `ST_Z(geometry)`, `ST_M(geometry)` -- ordinate values (POINT only, error for other types)
- `ST_NPoints(geometry)` -- total number of vertices
- `ST_NRings(geometry)` -- total number of rings (exterior + interior)
- `ST_NumGeometries(geometry)` -- number of sub-geometries in a collection
- `ST_GeometryN(geometry, n integer)` -- nth sub-geometry (1-based)
- `ST_ExteriorRing(geometry)` -- exterior ring of a polygon
- `ST_InteriorRingN(geometry, n integer)` -- nth interior ring (1-based)
- `ST_NumInteriorRings(geometry)` / `ST_NumInteriorRing(geometry)` -- number of interior rings
- `ST_PointN(geometry, n integer)` -- nth point of a linestring (1-based)
- `ST_StartPoint(geometry)` -- first point of a linestring
- `ST_EndPoint(geometry)` -- last point of a linestring
- `ST_GeometryType(geometry)` -- OGC type name string (e.g., 'ST_Point')
- `ST_SRID(geometry)` -- SRID value
- `ST_CoordDim(geometry)` -- coordinate dimension (2, 3, or 4)
- `ST_Dimension(geometry)` -- topological dimension (0 for point, 1 for line, 2 for polygon)

#### Scenario: ST_X and ST_Y extract coordinates
- **GIVEN** a point `POINT(1.5 2.5)`
- **WHEN** `ST_X(point)` and `ST_Y(point)` are called
- **THEN** the results SHALL be 1.5 and 2.5 respectively
- Validated by: regress/core/point_coordinates.sql

#### Scenario: ST_Z returns Z ordinate
- **GIVEN** a 3DZ point `POINT Z (1 2 3)`
- **WHEN** `ST_Z(point)` is called
- **THEN** the result SHALL be 3.0
- Validated by: regress/core/point_coordinates.sql

#### Scenario: ST_X on non-point raises error
- **GIVEN** a linestring geometry
- **WHEN** `ST_X(linestring)` is called
- **THEN** an error SHALL be raised (accessor is POINT-only)
- Validated by: regress/core/point_coordinates.sql

#### Scenario: ST_NPoints counts all vertices
- **GIVEN** a polygon `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` (5 vertices including closing point)
- **WHEN** `ST_NPoints(poly)` is called
- **THEN** the result SHALL be 5
- Validated by: regress/core/ctors.sql

#### Scenario: ST_GeometryN extracts sub-geometry (1-based)
- **GIVEN** a multipoint `MULTIPOINT((0 0),(1 1),(2 2))`
- **WHEN** `ST_GeometryN(mp, 2)` is called
- **THEN** the result SHALL be `POINT(1 1)`
- Validated by: regress/core/ctors.sql

### Requirement: Geometry type and property introspection
PostGIS SHALL provide functions for type and property inspection:
- `ST_IsEmpty(geometry)` -- returns true if geometry is empty
- `ST_IsClosed(geometry)` -- returns true if linestring start equals end, or all polygon rings are closed
- `ST_IsCollection(geometry)` -- returns true if geometry is a collection type
- `ST_Envelope(geometry)` -- returns the bounding box as a polygon (or point for point input)
- `ST_Summary(geometry)` -- returns a text summary of the geometry (type, SRID, flags, vertex count)

#### Scenario: IsEmpty on empty geometry
- **GIVEN** a geometry `POINT EMPTY`
- **WHEN** `ST_IsEmpty(geom)` is called
- **THEN** the result SHALL be true
- Validated by: regress/core/empty.sql

#### Scenario: IsEmpty on non-empty geometry
- **GIVEN** a geometry `POINT(0 0)`
- **WHEN** `ST_IsEmpty(geom)` is called
- **THEN** the result SHALL be false
- Validated by: regress/core/empty.sql

#### Scenario: IsClosed on closed linestring
- **GIVEN** a linestring `LINESTRING(0 0, 1 0, 1 1, 0 0)`
- **WHEN** `ST_IsClosed(line)` is called
- **THEN** the result SHALL be true
- Validated by: regress/core/ctors.sql

#### Scenario: IsCollection on multipoint
- **GIVEN** a multipoint geometry
- **WHEN** `ST_IsCollection(mp)` is called
- **THEN** the result SHALL be true
- Validated by: regress/core/iscollection.sql

#### Scenario: Envelope of linestring
- **GIVEN** a linestring `LINESTRING(0 0, 2 3)`
- **WHEN** `ST_Envelope(line)` is called
- **THEN** the result SHALL be a polygon representing the bounding box `POLYGON((0 0, 0 3, 2 3, 2 0, 0 0))`
- Validated by: regress/core/ctors.sql

#### Scenario: Summary shows geometry details
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** `ST_Summary(geom)` is called
- **THEN** the result SHALL include the type, SRID, and relevant flags
- Validated by: regress/core/summary.sql

### Requirement: WrapX and ShiftLongitude
PostGIS SHALL provide:
- `ST_WrapX(geometry, wrap float8, move float8)` -- wraps coordinates around a given X value, moving geometries that cross the wrap line
- `ST_ShiftLongitude(geometry)` -- shifts coordinates between -180..180 and 0..360 longitude ranges

These functions are useful for handling the antimeridian (international date line).

#### Scenario: ShiftLongitude converts negative to positive
- **GIVEN** a geometry with coordinates in the -180..180 range
- **WHEN** `ST_ShiftLongitude(geom)` is called
- **THEN** negative longitudes SHALL be shifted to 180..360 range
- Validated by: regress/core/wrapx.sql

#### Scenario: WrapX wraps around custom value
- **GIVEN** a geometry and a wrap value
- **WHEN** `ST_WrapX(geom, wrap, move)` is called
- **THEN** coordinates SHALL be adjusted relative to the wrap point
- Validated by: regress/core/wrapx.sql

#### Scenario: WrapX handles polygon crossing wrap line
- **GIVEN** a polygon that crosses the wrap boundary
- **WHEN** `ST_WrapX(geom, wrap, move)` is called
- **THEN** the polygon SHALL be split and reassembled on the correct side of the wrap line
- Validated by: regress/core/wrapx.sql

### Requirement: Segmentize
`ST_Segmentize(geometry, max_segment_length float8)` SHALL densify a geometry by adding vertices so that no edge exceeds the specified maximum length. This operates in coordinate units (not geodesic; for geodesic densification use the geography overload).

#### Scenario: Segmentize long edge
- **GIVEN** a linestring `LINESTRING(0 0, 100 0)` with max_segment_length=25
- **WHEN** `ST_Segmentize(geom, 25)` is called
- **THEN** the result SHALL have vertices at approximately 0, 25, 50, 75, and 100 along the X axis
- Status: untested -- no specific regression test for segmentize values

#### Scenario: Segmentize preserves already-dense geometry
- **GIVEN** a linestring with all segments shorter than the threshold
- **WHEN** `ST_Segmentize(geom, large_value)` is called
- **THEN** the result SHALL be identical to the input
- Status: untested -- no-op segmentize case

#### Scenario: Segmentize polygon edges
- **GIVEN** a polygon with long edges
- **WHEN** `ST_Segmentize(geom, max_length)` is called
- **THEN** each edge of each ring SHALL be densified to meet the max segment length
- Status: untested -- polygon segmentize behavior
