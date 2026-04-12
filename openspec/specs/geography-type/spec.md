## Purpose

Defines the PostGIS geography type, which represents spatial data on the Earth's surface using geodesic (great circle) calculations rather than planar Cartesian math. Covers the geography type definition, supported geometry subtypes, I/O formats, casting between geometry and geography, geodesic measurement functions (distance, area, length, perimeter), predicates (covers, intersects, DWithin), and limitations. This spec depends on geometry-types for LWGEOM structure and gserialized-format for on-disk representation.

## ADDED Requirements

### Requirement: Geography type definition and valid subtypes
The geography type SHALL be a distinct PostgreSQL type that stores spatial data with geodetic semantics. The geodetic flag (`LWFLAG_GEODETIC`) SHALL be set on all geography values.

Geography SHALL support only the following geometry subtypes:
- POINT, LINESTRING, POLYGON
- MULTIPOINT, MULTILINESTRING, MULTIPOLYGON
- GEOMETRYCOLLECTION

Attempting to create a geography from any other subtype (e.g., CIRCULARSTRING, COMPOUNDCURVE, CURVEPOLYGON, TIN, TRIANGLE, POLYHEDRALSURFACE) SHALL raise an error: "Geography type does not support <type_name>".

#### Scenario: Valid geography from POINT
- **GIVEN** a WKT input `POINT(-73.9857 40.7484)`
- **WHEN** cast to geography
- **THEN** a valid geography value SHALL be created with the geodetic flag set
- Validated by: regress/core/geography.sql

#### Scenario: Valid geography from MULTIPOLYGON
- **GIVEN** a MULTIPOLYGON WKT
- **WHEN** cast to geography
- **THEN** a valid geography value SHALL be created
- Validated by: regress/core/geography.sql

#### Scenario: Invalid subtype CIRCULARSTRING rejected
- **GIVEN** a CIRCULARSTRING WKT
- **WHEN** cast to geography
- **THEN** an error SHALL be raised containing "Geography type does not support CircularString"
- Status: untested -- no regression test for this specific rejection

### Requirement: Geography default SRID
When a geography value is created with SRID <= 0, the system SHALL automatically assign SRID_DEFAULT (4326, WGS84). This ensures all geography values have a valid geodetic datum.

#### Scenario: No SRID defaults to 4326
- **GIVEN** a geometry `POINT(0 0)` with SRID 0
- **WHEN** cast to geography
- **THEN** the resulting geography SHALL have SRID 4326
- Validated by: regress/core/geography.sql

#### Scenario: Explicit SRID preserved
- **GIVEN** a geometry `SRID=4269;POINT(0 0)` (NAD83)
- **WHEN** cast to geography
- **THEN** the resulting geography SHALL have SRID 4269
- Status: untested -- no regression test for non-4326 geography SRID

#### Scenario: Negative SRID replaced with 4326
- **GIVEN** a geometry with SRID -1
- **WHEN** cast to geography
- **THEN** the resulting geography SHALL have SRID 4326
- Status: untested -- edge case for negative SRID

### Requirement: Geography coordinate clamping
When creating a geography value, the system SHALL force coordinates into the valid geodetic range [-180, 180] for longitude and [-90, 90] for latitude via `lwgeom_force_geodetic()`. If coordinates are nudged or clamped, a NOTICE SHALL be emitted: "Coordinate values were coerced into range [-180 -90, 180 90] for GEOGRAPHY".

#### Scenario: Out-of-range longitude clamped
- **GIVEN** a geometry `POINT(200 45)` cast to geography
- **WHEN** the geography value is created
- **THEN** the longitude SHALL be clamped to the range [-180, 180]
- **AND** a NOTICE SHALL be emitted about coordinate coercion
- Validated by: regress/core/geography.sql

#### Scenario: Valid coordinates not modified
- **GIVEN** a geometry `POINT(-73.9857 40.7484)` within valid range
- **WHEN** cast to geography
- **THEN** the coordinates SHALL remain unchanged and no NOTICE SHALL be emitted
- Validated by: regress/core/geography.sql

#### Scenario: Pole-adjacent latitude preserved
- **GIVEN** a geometry `POINT(0 89.9999)` near the north pole
- **WHEN** cast to geography
- **THEN** the latitude SHALL be preserved without clamping
- Status: untested -- edge case near poles

### Requirement: Geography I/O formats
Geography SHALL support the following I/O formats:
- **Input**: WKT/EWKT (via `geography_in`), WKB/EWKB (hex detection in input), geometry cast
- **Output**: WKT (`ST_AsText`), EWKT (`ST_AsEWKT`), WKB (`ST_AsBinary`), GeoJSON (`ST_AsGeoJson`), GML (`ST_AsGML`), KML (`ST_AsKML`), SVG (`ST_AsSVG`)

The binary send/recv protocol SHALL use EWKB format for PostgreSQL COPY BINARY operations.

#### Scenario: Geography WKT round-trip
- **GIVEN** a geography `POINT(-73.9857 40.7484)`
- **WHEN** `ST_AsText(geog)` is called
- **THEN** the result SHALL be `POINT(-73.9857 40.7484)`
- Validated by: regress/core/out_geography.sql

#### Scenario: Geography EWKT output includes SRID
- **GIVEN** a geography with SRID 4326
- **WHEN** `ST_AsEWKT(geog)` is called
- **THEN** the result SHALL include `SRID=4326;` prefix
- Validated by: regress/core/out_geography.sql

#### Scenario: Geography GeoJSON output
- **GIVEN** a geography `POINT(-73.9857 40.7484)`
- **WHEN** `ST_AsGeoJson(geog)` is called
- **THEN** the result SHALL be valid GeoJSON with coordinates in [longitude, latitude] order
- Validated by: regress/core/out_geography.sql

### Requirement: Geometry-geography casting
PostGIS SHALL provide bidirectional casts between geometry and geography:
- `geometry::geography` (IMPLICIT cast): sets geodetic flag, validates subtype, clamps coordinates, defaults SRID to 4326
- `geography::geometry` (explicit cast): clears geodetic flag, preserves SRID and coordinates

The geometry-to-geography cast is IMPLICIT, meaning it is applied automatically in expressions. The geography-to-geometry cast is explicit, requiring `::geometry` syntax.

#### Scenario: Implicit geometry to geography cast
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** used where geography is expected (implicit cast)
- **THEN** the cast SHALL succeed and produce a geography with geodetic flag set
- Validated by: regress/core/geography.sql

#### Scenario: Explicit geography to geometry cast
- **GIVEN** a geography `POINT(-73.9857 40.7484)` with SRID 4326
- **WHEN** `geog::geometry` is evaluated
- **THEN** the result SHALL be a geometry with SRID 4326, geodetic flag cleared, same coordinates
- Validated by: regress/core/geography.sql

#### Scenario: Cast preserves SRID through round-trip
- **GIVEN** a geometry `SRID=4326;LINESTRING(0 0, 1 1)`
- **WHEN** cast to geography and back to geometry
- **THEN** the SRID SHALL remain 4326 and coordinates SHALL be unchanged
- Validated by: regress/core/geography.sql

### Requirement: Geodesic distance measurement
`ST_Distance(geography, geography, use_spheroid boolean DEFAULT true)` SHALL return the geodesic distance in metres between two geography objects. The function SHALL:
- Use spheroid distance by default (`use_spheroid = true`), using the ellipsoid parameters from the geography's SRID
- Fall back to spherical distance when `use_spheroid = false` (sets semi-major = semi-minor = radius)
- Use tree-based distance calculation by default for performance
- Round results to 10nm precision (when PROJ_GEODESIC defined) or 100nm (otherwise)
- Return NULL for empty geometries

#### Scenario: Distance between two points on spheroid
- **GIVEN** two geography points in New York and London
- **WHEN** `ST_Distance(nyc, london)` is called with default spheroid
- **THEN** the result SHALL be the geodesic distance in metres (approximately 5570 km)
- Validated by: regress/core/geography.sql

#### Scenario: Distance on sphere vs spheroid
- **GIVEN** two geography points
- **WHEN** `ST_Distance(g1, g2, false)` is called (sphere mode)
- **THEN** the result SHALL differ slightly from the spheroid result (sphere approximation)
- Validated by: regress/core/geography.sql

#### Scenario: Distance with empty geography returns NULL
- **GIVEN** a geography point and an empty geography
- **WHEN** `ST_Distance(point, empty)` is called
- **THEN** the result SHALL be NULL
- Status: untested -- empty geography distance return value

### Requirement: Geography DWithin and Intersects predicates
`ST_DWithin(geography, geography, distance float8, use_spheroid boolean DEFAULT true)` SHALL return true if the two geographies are within the specified distance (metres). It uses cached geodesic computations when available.

`ST_Intersects(geography, geography)` SHALL be implemented as `ST_DWithin(g1, g2, 0.0)`, testing whether the geographies touch or overlap on the sphere/spheroid.

Both functions SHALL return false for empty input geometries (not NULL).

#### Scenario: DWithin returns true for nearby points
- **GIVEN** two geography points 100 metres apart
- **WHEN** `ST_DWithin(g1, g2, 200)` is called
- **THEN** the result SHALL be true
- Validated by: regress/core/geography.sql

#### Scenario: DWithin returns false for distant points
- **GIVEN** two geography points 10000 metres apart
- **WHEN** `ST_DWithin(g1, g2, 100)` is called
- **THEN** the result SHALL be false
- Validated by: regress/core/geography.sql

#### Scenario: Intersects delegates to DWithin with zero distance
- **GIVEN** two overlapping geography polygons
- **WHEN** `ST_Intersects(g1, g2)` is called
- **THEN** the result SHALL be true, computed via `geography_dwithin` with tolerance 0
- Validated by: regress/core/geography.sql

#### Scenario: Empty geography returns false for DWithin
- **GIVEN** a geography point and an empty geography
- **WHEN** `ST_DWithin(point, empty, 1000)` is called
- **THEN** the result SHALL be false
- Status: untested -- empty geography DWithin behavior

### Requirement: Geography covers/coveredby predicates
`ST_Covers(geography, geography)` SHALL return true if the first geography completely covers the second. `ST_CoveredBy(geography, geography)` is the inverse.

These predicates operate on the sphere using geodesic algorithms, differing from their geometry counterparts which use planar GEOS operations.

#### Scenario: Polygon covers contained point
- **GIVEN** a geography polygon and a geography point inside it
- **WHEN** `ST_Covers(polygon, point)` is called
- **THEN** the result SHALL be true
- Validated by: regress/core/geography_covers.sql

#### Scenario: Polygon does not cover external point
- **GIVEN** a geography polygon and a geography point outside it
- **WHEN** `ST_Covers(polygon, point)` is called
- **THEN** the result SHALL be false
- Validated by: regress/core/geography_covers.sql

#### Scenario: CoveredBy is inverse of Covers
- **GIVEN** a geography point inside a geography polygon
- **WHEN** `ST_CoveredBy(point, polygon)` is called
- **THEN** the result SHALL be true (equivalent to `ST_Covers(polygon, point)`)
- Validated by: regress/core/geography_covers.sql

### Requirement: Geography area and perimeter
`ST_Area(geography, use_spheroid boolean DEFAULT true)` SHALL return the area in square metres of polygonal geography objects. Empty geometries SHALL return 0.0.

`ST_Perimeter(geography, use_spheroid boolean DEFAULT true)` SHALL return the perimeter in metres for polygonal features. Non-polygonal types (POINT, LINESTRING) SHALL return 0.0.

Both functions use `lwgeom_area_spheroid()` and `lwgeom_length_spheroid()` respectively, with sphere fallback when `use_spheroid = false`.

#### Scenario: Area of a geography polygon
- **GIVEN** a geography polygon representing a known region
- **WHEN** `ST_Area(geog)` is called
- **THEN** the result SHALL be the geodesic area in square metres
- Validated by: regress/core/geography.sql

#### Scenario: Area on sphere vs spheroid
- **GIVEN** a geography polygon
- **WHEN** `ST_Area(geog, false)` is called
- **THEN** the result SHALL be the spherical approximation of area
- Validated by: regress/core/geography.sql

#### Scenario: Perimeter of non-polygonal type returns zero
- **GIVEN** a geography `LINESTRING(0 0, 1 1)`
- **WHEN** `ST_Perimeter(geog)` is called
- **THEN** the result SHALL be 0.0
- Status: untested -- perimeter of non-polygonal geography

### Requirement: Geography length
`ST_Length(geography, use_spheroid boolean DEFAULT true)` SHALL return the geodesic length in metres for linear geography features. Polygonal types (POLYGON, MULTIPOLYGON) SHALL return 0.0. Empty geometries SHALL return 0.0.

#### Scenario: Length of a geography linestring
- **GIVEN** a geography `LINESTRING(0 0, 1 0)` (approximately 111 km along equator)
- **WHEN** `ST_Length(geog)` is called
- **THEN** the result SHALL be approximately 111195 metres
- Validated by: regress/core/geography.sql

#### Scenario: Length of polygon returns zero
- **GIVEN** a geography polygon
- **WHEN** `ST_Length(geog)` is called
- **THEN** the result SHALL be 0.0 (use ST_Perimeter for polygon boundaries)
- Status: untested -- length of polygonal geography

#### Scenario: Length on sphere
- **GIVEN** a geography linestring
- **WHEN** `ST_Length(geog, false)` is called
- **THEN** the result SHALL use spherical distance calculation
- Validated by: regress/core/geography.sql

### Requirement: Geography project and azimuth
`ST_Project(geography, distance float8, azimuth float8)` SHALL return a point projected from the input point along a geodesic at the given azimuth (radians from north) and distance (metres).

`ST_Azimuth(geography, geography)` SHALL return the forward azimuth (radians from north) of the geodesic from the first point to the second.

`ST_Project(geography, geography, distance float8)` SHALL return a point along the geodesic from the first to the second point at the given distance.

#### Scenario: Project point north by 1000 metres
- **GIVEN** a geography `POINT(0 0)`
- **WHEN** `ST_Project(geog, 1000, 0)` is called (azimuth 0 = north)
- **THEN** the result SHALL be a point approximately 1000 metres north of the equator
- Validated by: regress/core/geography.sql

#### Scenario: Azimuth between two points
- **GIVEN** two geography points
- **WHEN** `ST_Azimuth(g1, g2)` is called
- **THEN** the result SHALL be the forward azimuth in radians from north
- Validated by: regress/core/geography.sql

#### Scenario: Project along geodesic between two points
- **GIVEN** two geography points and a distance
- **WHEN** `ST_Project(g1, g2, distance)` is called
- **THEN** the result SHALL lie on the geodesic between g1 and g2 at the specified distance from g1
- Status: untested -- three-argument ST_Project

### Requirement: Geography segmentize
`ST_Segmentize(geography, max_segment_length float8)` SHALL densify a geography by adding intermediate points along geodesics so that no segment exceeds the specified maximum length (metres). This ensures that straight-line rendering of geography segments closely approximates the geodesic curve.

#### Scenario: Segmentize long geodesic
- **GIVEN** a geography `LINESTRING(0 0, 180 0)` (half the equator)
- **WHEN** `ST_Segmentize(geog, 1000000)` is called (1000 km max segment)
- **THEN** the result SHALL contain additional points along the equator so that no segment exceeds 1000 km
- Validated by: regress/core/geography.sql

#### Scenario: Short segments unchanged
- **GIVEN** a geography linestring with all segments shorter than the threshold
- **WHEN** `ST_Segmentize(geog, large_distance)` is called
- **THEN** the geometry SHALL be returned unchanged
- Status: untested -- no regression test for no-op segmentize

#### Scenario: Segmentize polygon ring
- **GIVEN** a geography polygon with long edges
- **WHEN** `ST_Segmentize(geog, max_length)` is called
- **THEN** intermediate points SHALL be added along each ring edge
- Status: untested -- polygon segmentize

### Requirement: Best SRID selection
The internal function `_ST_BestSRID(geography)` SHALL select an appropriate projected SRID for a geography based on its spatial extent. This is used internally by functions that need to project geography to geometry for operations not natively supported on the sphere (e.g., ST_Buffer).

#### Scenario: Point near equator selects appropriate UTM zone
- **GIVEN** a geography point near the equator
- **WHEN** `_ST_BestSRID(geog)` is called
- **THEN** an appropriate UTM zone SRID SHALL be selected
- Validated by: regress/core/bestsrid.sql

#### Scenario: Polar region selects polar projection
- **GIVEN** a geography point near a pole
- **WHEN** `_ST_BestSRID(geog)` is called
- **THEN** a polar stereographic or similar high-latitude SRID SHALL be selected
- Validated by: regress/core/bestsrid.sql

#### Scenario: Global extent selects world projection
- **GIVEN** a geography spanning the entire globe
- **WHEN** `_ST_BestSRID(geog)` is called
- **THEN** a world-scale projection SHALL be selected
- Validated by: regress/core/bestsrid.sql

### Requirement: Geography centroid
`ST_Centroid(geography)` SHALL return the geographic centroid of a geography object, computed on the sphere. The centroid is calculated by averaging the 3D unit vectors of the points and projecting back to the sphere.

#### Scenario: Centroid of geography polygon
- **GIVEN** a geography polygon
- **WHEN** `ST_Centroid(geog)` is called
- **THEN** the result SHALL be a geography point representing the spherical centroid
- Validated by: regress/core/geography_centroid.sql

#### Scenario: Centroid of geography multipoint
- **GIVEN** a geography `MULTIPOINT((0 0),(0 90))`
- **WHEN** `ST_Centroid(geog)` is called
- **THEN** the result SHALL be a point at the spherical average of the input points
- Validated by: regress/core/geography_centroid.sql

#### Scenario: Centroid of empty geography
- **GIVEN** an empty geography
- **WHEN** `ST_Centroid(geog)` is called
- **THEN** the result SHALL be an empty geography point
- Status: untested -- empty geography centroid

### Requirement: Geography limitations
The following operations are NOT natively supported on geography and SHALL require casting to geometry:
- `ST_Buffer(geography)` -- must use `_ST_BestSRID` to project, buffer in projected space, and reproject
- `ST_Union(geography)` -- not available; cast to geometry first
- `ST_Intersection(geography)` -- not available natively
- `ST_Difference(geography)` -- not available natively
- Topological predicates like `ST_Contains`, `ST_Within`, `ST_Touches` -- not available for geography (use ST_Covers/ST_CoveredBy instead)

#### Scenario: ST_Covers available but ST_Contains not for geography
- **GIVEN** two geography objects
- **WHEN** `ST_Covers(g1, g2)` is called
- **THEN** the function SHALL succeed with geodesic computation
- **BUT** `ST_Contains(g1, g2)` with geography arguments SHALL not resolve (no such function)
- Validated by: regress/core/geography_covers.sql

#### Scenario: Buffer on geography works via projection
- **GIVEN** a geography point
- **WHEN** `ST_Buffer(geog::geometry, distance)` is attempted after casting
- **THEN** the buffer SHALL work in projected (planar) space
- Status: untested -- no dedicated regression test for geography buffer workflow

#### Scenario: Geography type metadata functions
- **GIVEN** a geography value
- **WHEN** `GeometryType(geog)` and `ST_Summary(geog)` are called
- **THEN** the type name and summary SHALL be returned correctly
- Validated by: regress/core/out_geography.sql
