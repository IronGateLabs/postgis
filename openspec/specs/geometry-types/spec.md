## Purpose

Defines the LWGEOM type hierarchy used internally by PostGIS, the POINTARRAY coordinate storage model, the flags byte encoding dimensionality and metadata, and all serialization codecs that convert between in-memory LWGEOM representations and external formats (WKB, EWKB, WKT, EWKT, GeoJSON, GML, KML, SVG, TWKB, Encoded Polyline, X3D, FlatGeobuf). This spec is the foundation referenced by all other PostGIS specs.

## ADDED Requirements

### Requirement: Geometry type enumeration
The system SHALL define 15 geometry types identified by integer constants, used throughout the codebase for type dispatch:

| Constant | Value | Description |
|---|---|---|
| POINTTYPE | 1 | Single point |
| LINETYPE | 2 | LineString |
| POLYGONTYPE | 3 | Polygon with exterior and optional interior rings |
| MULTIPOINTTYPE | 4 | Collection of points |
| MULTILINETYPE | 5 | Collection of linestrings |
| MULTIPOLYGONTYPE | 6 | Collection of polygons |
| COLLECTIONTYPE | 7 | Heterogeneous geometry collection |
| CIRCSTRINGTYPE | 8 | Circular arc string (SQL/MM) |
| COMPOUNDTYPE | 9 | Compound curve (SQL/MM) |
| CURVEPOLYTYPE | 10 | Curve polygon (SQL/MM) |
| MULTICURVETYPE | 11 | Multi curve (SQL/MM) |
| MULTISURFACETYPE | 12 | Multi surface (SQL/MM) |
| POLYHEDRALSURFACETYPE | 13 | Polyhedral surface |
| TRIANGLETYPE | 14 | Triangle |
| TINTYPE | 15 | Triangulated irregular network |

The constant `NUMTYPES` (16) SHALL be defined as one more than the highest type number, for use in array sizing.

#### Scenario: All 15 types have distinct integer values
- **WHEN** the type constants are compiled from `liblwgeom.h`
- **THEN** each constant SHALL have a unique integer value from 1 to 15
- **AND** NUMTYPES SHALL equal 16
- Validated by: liblwgeom/cunit/cu_gserialized1.c (type and flag constant tests)

#### Scenario: Type constant used for dispatch in WKB output
- **WHEN** a geometry is serialized to WKB via `lwgeom_to_wkb_buf()`
- **THEN** the type integer written to the byte stream SHALL match the WKB type constant corresponding to the LWGEOM's `type` field (e.g., POINTTYPE 1 maps to WKB_POINT_TYPE 1)
- Validated by: regress/core/wkb.sql

#### Scenario: Unknown type triggers error
- **WHEN** `lwgeom_to_wkb_buf()` encounters a geometry with a type value not in the 1-15 range
- **THEN** it SHALL call `lwerror()` with "Unsupported geometry type"
- Status: untested -- no existing regression test covers this case

### Requirement: LWGEOM base structure
Every geometry type SHALL share a common header layout (the LWGEOM struct) containing:
- `bbox` (GBOX pointer): optional bounding box, NULL when not computed
- `srid` (int32_t): spatial reference identifier, SRID_UNKNOWN (0) when unset
- `flags` (lwflags_t / uint16_t): dimension and metadata flags
- `type` (uint8_t): one of the LWTYPE constants

Concrete types (LWPOINT, LWLINE, LWPOLY, LWCOLLECTION, etc.) SHALL embed this header at identical offsets so that casting to LWGEOM is safe for reading common fields.

#### Scenario: LWPOINT structure layout
- **GIVEN** an LWPOINT created with `lwpoint_make2d(4326, 1.0, 2.0)`
- **WHEN** the LWPOINT is cast to LWGEOM via `lwpoint_as_lwgeom()`
- **THEN** `lwgeom->type` SHALL equal POINTTYPE (1)
- **AND** `lwgeom->srid` SHALL equal 4326
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: LWCOLLECTION structure layout
- **GIVEN** an LWCOLLECTION with `type` = COLLECTIONTYPE (7), containing 3 sub-geometries
- **WHEN** cast to LWGEOM
- **THEN** `lwgeom->type` SHALL equal 7
- **AND** the `ngeoms` field (at the collection-specific offset) SHALL equal 3
- Validated by: regress/core/binary.sql

#### Scenario: Common fields at identical offsets across types
- **GIVEN** any concrete geometry type (LWPOINT, LWLINE, LWPOLY, LWMPOINT, LWMLINE, LWMPOLY, LWCOLLECTION, LWTRIANGLE, LWCIRCSTRING, LWCOMPOUND, LWCURVEPOLY, LWMCURVE, LWMSURFACE, LWPSURFACE, LWTIN)
- **WHEN** cast to LWGEOM pointer
- **THEN** the `bbox`, `srid`, `flags`, and `type` fields SHALL be readable at the same memory offsets as defined in the LWGEOM struct
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: Dimensionality flags
The flags byte SHALL encode dimensionality using the following bit positions:
- Bit 0 (`LWFLAG_Z`, 0x01): Z ordinate present
- Bit 1 (`LWFLAG_M`, 0x02): M ordinate present

The `FLAGS_NDIMS()` macro SHALL compute the number of dimensions as `2 + hasZ + hasM`, yielding 2 (XY), 3 (XYZ or XYM), or 4 (XYZM).

All coordinates within a single geometry and its POINTARRAY SHALL have the same dimensionality. Mixing 2D and 3D points within a single POINTARRAY is not permitted.

#### Scenario: 2D geometry has no Z or M flags
- **GIVEN** a geometry created from WKT `POINT(1 2)`
- **WHEN** its flags are examined
- **THEN** `FLAGS_GET_Z(flags)` SHALL return 0
- **AND** `FLAGS_GET_M(flags)` SHALL return 0
- **AND** `FLAGS_NDIMS(flags)` SHALL return 2
- Validated by: regress/core/wkt.sql

#### Scenario: 3DZ geometry has Z flag set
- **GIVEN** a geometry created from WKT `POINT Z (1 2 3)`
- **WHEN** its flags are examined
- **THEN** `FLAGS_GET_Z(flags)` SHALL return 1
- **AND** `FLAGS_GET_M(flags)` SHALL return 0
- **AND** `FLAGS_NDIMS(flags)` SHALL return 3
- Validated by: regress/core/wkt.sql

#### Scenario: 3DM geometry has M flag set
- **GIVEN** a geometry created from WKT `POINT M (1 2 3)`
- **WHEN** its flags are examined
- **THEN** `FLAGS_GET_Z(flags)` SHALL return 0
- **AND** `FLAGS_GET_M(flags)` SHALL return 1
- **AND** `FLAGS_NDIMS(flags)` SHALL return 3
- Validated by: regress/core/wkt.sql

#### Scenario: 4D geometry has both Z and M flags
- **GIVEN** a geometry created from WKT `POINT ZM (1 2 3 4)`
- **WHEN** its flags are examined
- **THEN** `FLAGS_GET_Z(flags)` SHALL return 1
- **AND** `FLAGS_GET_M(flags)` SHALL return 1
- **AND** `FLAGS_NDIMS(flags)` SHALL return 4
- Validated by: regress/core/wkt.sql

### Requirement: Additional metadata flags
The flags byte SHALL also encode:
- Bit 2 (`LWFLAG_BBOX`, 0x04): bounding box has been computed and attached
- Bit 3 (`LWFLAG_GEODETIC`, 0x08): geometry is geodetic (geography type)
- Bit 4 (`LWFLAG_READONLY`, 0x10): geometry data is read-only (shared memory)
- Bit 5 (`LWFLAG_SOLID`, 0x20): geometry represents a solid (closed polyhedral surface)

#### Scenario: Bounding box flag set after lwgeom_add_bbox
- **GIVEN** a geometry `LINESTRING(0 0, 1 1)` with no bounding box
- **WHEN** `lwgeom_add_bbox()` is called
- **THEN** `FLAGS_GET_BBOX(flags)` SHALL return 1
- **AND** `lwgeom->bbox` SHALL be non-NULL with xmin=0, xmax=1, ymin=0, ymax=1
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: Geodetic flag set for geography input
- **GIVEN** a geometry parsed as geography type
- **WHEN** the geodetic flag is examined
- **THEN** `FLAGS_GET_GEODETIC(flags)` SHALL return 1
- Validated by: regress/core/binary.sql (geography section)

#### Scenario: Bounding box removed after lwgeom_drop_bbox
- **GIVEN** a geometry with a computed bounding box
- **WHEN** `lwgeom_drop_bbox()` is called
- **THEN** `FLAGS_GET_BBOX(flags)` SHALL return 0
- **AND** `lwgeom->bbox` SHALL be NULL
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: POINTARRAY coordinate storage
Coordinates SHALL be stored in a POINTARRAY structure containing:
- `npoints` (uint32_t): number of points currently stored
- `maxpoints` (uint32_t): allocated capacity
- `flags` (lwflags_t): dimensionality flags (must match the parent geometry)
- `serialized_pointlist` (uint8_t*): raw byte array of coordinates, laid out as contiguous POINT2D, POINT3DZ, POINT3DM, or POINT4D values

Individual coordinate structs are:
- POINT2D: `{double x, double y}` (16 bytes)
- POINT3DZ / POINT3D: `{double x, double y, double z}` (24 bytes)
- POINT3DM: `{double x, double y, double m}` (24 bytes)
- POINT4D: `{double x, double y, double z, double m}` (32 bytes)

#### Scenario: 2D point array stores 16 bytes per point
- **GIVEN** a POINTARRAY with 2D flags and 3 points
- **WHEN** the serialized data is examined
- **THEN** the serialized_pointlist SHALL contain exactly 48 bytes (3 * 16)
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: 4D point array stores 32 bytes per point
- **GIVEN** a POINTARRAY with XYZM flags and 2 points
- **WHEN** the serialized data is examined
- **THEN** the serialized_pointlist SHALL contain exactly 64 bytes (2 * 32)
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: POINTARRAY flags match parent geometry
- **GIVEN** a 3DZ LWLINE with a POINTARRAY
- **WHEN** the POINTARRAY flags are examined
- **THEN** `FLAGS_GET_Z(pa->flags)` SHALL return 1 and `FLAGS_GET_M(pa->flags)` SHALL return 0, matching the parent LWLINE
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: SRID handling
Every LWGEOM SHALL carry an SRID in the `srid` field. The following SRID constants are defined:
- `SRID_UNKNOWN` (0): no SRID assigned; `SRID_IS_UNKNOWN(x)` returns true for any value <= 0
- `SRID_DEFAULT` (4326): WGS84 geographic
- `SRID_MAXIMUM`: maximum allowed SRID (21-bit, value 2097152 less a reserved range)
- `SRID_USER_MAXIMUM`: maximum user-assignable SRID (SRID_MAXIMUM minus 1000 reserved values)

The function `clamp_srid()` SHALL remap out-of-range SRID values and emit a notice (not an error):
- Negative SRIDs (other than SRID_UNKNOWN) are converted to SRID_UNKNOWN (0) with a notice: "SRID value %d converted to the officially unknown SRID value %d"
- SRIDs greater than SRID_MAXIMUM are remapped into the reserved range above SRID_USER_MAXIMUM using modular arithmetic, with a notice: "SRID value %d > SRID_MAXIMUM converted to %d"

#### Scenario: SRID preserved through WKT round-trip
- **GIVEN** a geometry `SRID=4326;POINT(1 2)`
- **WHEN** it is serialized to EWKT via `ST_AsEWKT()` and parsed back via `ST_GeomFromEWKT()`
- **THEN** the resulting geometry's SRID SHALL be 4326
- Validated by: regress/core/wkt.sql

#### Scenario: SRID 0 treated as unknown
- **GIVEN** a geometry `POINT(1 2)` with no SRID specified
- **WHEN** the SRID is read
- **THEN** the value SHALL be SRID_UNKNOWN (0)
- **AND** `SRID_IS_UNKNOWN(srid)` SHALL return true
- Validated by: regress/core/wkt.sql

#### Scenario: Out-of-range SRID remapped with notice
- **GIVEN** an attempt to set SRID to a value greater than SRID_MAXIMUM
- **WHEN** `clamp_srid()` is called
- **THEN** the SRID SHALL be remapped into the reserved range above SRID_USER_MAXIMUM
- **AND** a notice SHALL be emitted: "SRID value %d > SRID_MAXIMUM converted to %d"
- Status: untested -- no existing regression test covers clamp_srid() for out-of-range values

### Requirement: Empty geometry representation
Every geometry type SHALL support an empty state. For simple types (LWPOINT, LWLINE, LWTRIANGLE, LWCIRCSTRING), empty means the POINTARRAY has `npoints = 0`. For LWPOLY, empty means `nrings = 0`. For collection types, empty means `ngeoms = 0`.

The function `lwgeom_is_empty()` SHALL return LW_TRUE for any geometry in the empty state.

#### Scenario: Empty point
- **GIVEN** a geometry created from WKT `POINT EMPTY`
- **WHEN** `lwgeom_is_empty()` is called
- **THEN** it SHALL return LW_TRUE
- **AND** `ST_AsText()` SHALL return `POINT EMPTY`
- Validated by: regress/core/wkt.sql, regress/core/empty.sql

#### Scenario: Empty polygon
- **GIVEN** a geometry created from WKT `POLYGON EMPTY`
- **WHEN** `lwgeom_is_empty()` is called
- **THEN** it SHALL return LW_TRUE
- **AND** the LWPOLY's `nrings` field SHALL be 0
- Validated by: regress/core/wkt.sql, regress/core/empty.sql

#### Scenario: Empty geometry collection
- **GIVEN** a geometry created from WKT `GEOMETRYCOLLECTION EMPTY`
- **WHEN** `lwgeom_is_empty()` is called
- **THEN** it SHALL return LW_TRUE
- **AND** the LWCOLLECTION's `ngeoms` field SHALL be 0
- Validated by: regress/core/wkt.sql, regress/core/empty.sql

#### Scenario: All 15 types support empty state with all dimension variants
- **GIVEN** each of the 15 geometry types in 2D, 3DZ, 3DM, and 4D variants
- **WHEN** an empty instance is created and stored then retrieved
- **THEN** the type and dimensionality SHALL be preserved through PostgreSQL binary COPY round-trip
- Validated by: regress/core/binary.sql

### Requirement: Collection type hierarchy
Collection types (MULTIPOINTTYPE, MULTILINETYPE, MULTIPOLYGONTYPE, COLLECTIONTYPE, COMPOUNDTYPE, MULTICURVETYPE, MULTISURFACETYPE) SHALL store an array of sub-geometry pointers. Typed collections SHALL restrict sub-geometry types:
- LWMPOINT: sub-geometries must be LWPOINT
- LWMLINE: sub-geometries must be LWLINE
- LWMPOLY: sub-geometries must be LWPOLY
- LWPSURFACE: sub-geometries must be LWPOLY
- LWTIN: sub-geometries must be LWTRIANGLE
- LWCOLLECTION: sub-geometries can be any LWGEOM type (heterogeneous)

LWCOMPOUND sub-geometries must be LWLINE or LWCIRCSTRING. LWCURVEPOLY rings must be LWLINE, LWCIRCSTRING, or LWCOMPOUND.

#### Scenario: MultiPoint contains only points
- **GIVEN** a geometry `MULTIPOINT((0 0),(1 1),(2 2))`
- **WHEN** parsed and examined
- **THEN** the LWMPOINT's `geoms` array SHALL contain 3 LWPOINT pointers, each with `type == POINTTYPE`
- Validated by: regress/core/wkb.sql

#### Scenario: GeometryCollection allows mixed types
- **GIVEN** a geometry `GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(0 0, 1 1), POLYGON((0 0, 1 0, 1 1, 0 0)))`
- **WHEN** parsed and examined
- **THEN** the LWCOLLECTION SHALL contain 3 sub-geometries with types POINTTYPE, LINETYPE, and POLYGONTYPE respectively
- Validated by: regress/core/wkb.sql

#### Scenario: Nested collections
- **GIVEN** a geometry `GEOMETRYCOLLECTION(GEOMETRYCOLLECTION(POINT(0 0)))`
- **WHEN** parsed
- **THEN** the outer LWCOLLECTION SHALL have `ngeoms == 1` and its first sub-geometry SHALL be an LWCOLLECTION with `ngeoms == 1`
- Validated by: regress/core/wkb.sql

### Requirement: WKB and EWKB serialization round-trip
The WKB codec SHALL support three variants controlled by flags:
- `WKB_ISO` (0x01): ISO 13249 standard WKB. Dimensions encoded in type integer: Z adds 1000, M adds 2000 (e.g., Point Z = 1001, Point ZM = 3001). No SRID.
- `WKB_EXTENDED` (0x04): PostGIS EWKB. Dimensions encoded as high bits on type integer: `WKBZOFFSET` (0x80000000) for Z, `WKBMOFFSET` (0x40000000) for M, `WKBSRIDFLAG` (0x20000000) for SRID presence. SRID follows the type integer when present.
- Byte order: `WKB_NDR` (0x08) for little-endian, `WKB_XDR` (0x10) for big-endian. First byte of output is 0x01 (NDR) or 0x00 (XDR).
- `WKB_HEX` (0x20): output as hex-encoded ASCII string rather than raw bytes.

For every geometry type and dimensionality, serializing to WKB and parsing back SHALL produce a geometry that is `ST_OrderingEquals()` to the original.

#### Scenario: 2D point WKB round-trip (NDR)
- **GIVEN** a geometry `POINT(0 0)`
- **WHEN** serialized to WKB NDR and parsed back with `ST_GeomFromWKB()`
- **THEN** `ST_OrderingEquals()` between original and result SHALL return true
- **AND** the hex WKB SHALL be `0101000000000000000000000000000000000000000000`
- Validated by: regress/core/wkb.sql

#### Scenario: 3DZ point WKB round-trip
- **GIVEN** a geometry `POINT Z (1 2 3)`
- **WHEN** serialized to WKB NDR and parsed back
- **THEN** `ST_OrderingEquals()` SHALL return true
- Validated by: regress/core/wkb.sql

#### Scenario: EWKB preserves SRID
- **GIVEN** a geometry `SRID=4326;POINT(1 2)`
- **WHEN** serialized to EWKB (WKB_EXTENDED) and parsed back
- **THEN** the SRID SHALL be 4326 in the result
- **AND** the type integer in the byte stream SHALL have bit 0x20000000 set
- Validated by: regress/core/wkb.sql

#### Scenario: Empty geometry WKB round-trip for all types
- **GIVEN** each of the 15 geometry types in EMPTY state
- **WHEN** serialized to WKB and parsed back
- **THEN** `ST_OrderingEquals()` SHALL return true for each
- Validated by: regress/core/wkb.sql

#### Scenario: ISO WKB dimension encoding
- **GIVEN** a geometry `POINT ZM (1 2 3 4)`
- **WHEN** serialized to ISO WKB
- **THEN** the type integer SHALL be 3001 (1 + 1000 + 2000)
- Validated by: regress/core/wkb.sql

### Requirement: WKT and EWKT serialization round-trip
The WKT codec SHALL support:
- ISO WKT (`WKT_ISO`): Type name followed by dimension keyword (Z, M, ZM) when applicable. Coordinates in parentheses. Example: `POINT Z (1 2 3)`.
- EWKT: Prepends `SRID=<n>;` when an SRID is present. Example: `SRID=4326;POINT(1 2)`.

For every geometry type and dimensionality, serializing to WKT and parsing back SHALL produce a geometry that is `ST_OrderingEquals()` to the original.

#### Scenario: 2D point WKT round-trip
- **GIVEN** a geometry `POINT(0 0)`
- **WHEN** serialized to WKT via `ST_AsText()` and parsed back via `ST_GeomFromText()`
- **THEN** `ST_OrderingEquals()` SHALL return true
- **AND** the WKT output SHALL be `POINT(0 0)`
- Validated by: regress/core/wkt.sql

#### Scenario: 3DZ geometry WKT uses Z keyword
- **GIVEN** a geometry `POINT Z (0 0 0)`
- **WHEN** serialized to WKT
- **THEN** the output SHALL be `POINT Z (0 0 0)`
- Validated by: regress/core/wkt.sql

#### Scenario: EWKT includes SRID prefix
- **GIVEN** a geometry `SRID=4326;POLYGON EMPTY`
- **WHEN** serialized to EWKT via `ST_AsEWKT()`
- **THEN** the output SHALL begin with `SRID=4326;`
- Validated by: regress/core/empty.sql

#### Scenario: Malformed WKT raises parse error
- **GIVEN** the invalid WKT string `POINT ZM (0 0 0)` (missing fourth ordinate)
- **WHEN** parsed via geometry cast
- **THEN** a parse error SHALL be raised
- Validated by: regress/core/wkt.sql

### Requirement: GeoJSON serialization
The GeoJSON codec SHALL serialize geometries as JSON objects conforming to RFC 7946, with:
- `"type"`: the GeoJSON type name (Point, LineString, Polygon, MultiPoint, MultiLineString, MultiPolygon, GeometryCollection)
- `"coordinates"`: nested arrays of coordinate values
- SRID: optionally included as a `"crs"` property (non-standard extension) when present on the input geometry

`ST_GeomFromGeoJSON()` SHALL accept text, json, or jsonb input.

GeoJSON does not support CircularString, CompoundCurve, CurvePolygon, MultiCurve, MultiSurface, PolyhedralSurface, Triangle, or TIN types. These SHALL be converted to their linear equivalents before output (curve types are stroked).

#### Scenario: Point GeoJSON round-trip
- **GIVEN** a geometry `POINT(1 1)`
- **WHEN** serialized via `ST_AsGeoJSON()` and parsed back via `ST_GeomFromGeoJSON()`
- **THEN** `ST_AsText()` of the result SHALL be `POINT(1 1)`
- Validated by: regress/core/in_geojson.sql

#### Scenario: GeoJSON with SRID
- **GIVEN** a geometry `SRID=3005;MULTIPOINT(1 1, 1 1)`
- **WHEN** serialized to GeoJSON and parsed back
- **THEN** the SRID SHALL be preserved in the round-trip
- Validated by: regress/core/in_geojson.sql

#### Scenario: Invalid GeoJSON raises error
- **GIVEN** the invalid JSON `{"type": "Point", "crashme": [100.0, 0.0]}`
- **WHEN** parsed via `ST_GeomFromGeoJSON()`
- **THEN** an error SHALL be raised (missing "coordinates" key)
- Validated by: regress/core/in_geojson.sql

#### Scenario: Empty GeoJSON polygon
- **GIVEN** a GeoJSON object `{"type":"Polygon","coordinates":[]}`
- **WHEN** parsed via `ST_GeomFromGeoJSON()`
- **THEN** the result SHALL be an empty polygon
- Validated by: regress/core/in_geojson.sql

### Requirement: GML output
The GML codec SHALL support GML 2 and GML 3 output via `ST_AsGML()`. GML 3 uses `<gml:pos>` / `<gml:posList>` while GML 2 uses `<gml:coordinates>`.

Empty geometries SHALL produce valid GML output (empty coordinate elements).

#### Scenario: GML 2 point output
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** serialized via `ST_AsGML(2, geom)`
- **THEN** the output SHALL contain `<gml:Point>` and `<gml:coordinates>1,2</gml:coordinates>`
- Validated by: regress/core/out_gml.sql

#### Scenario: GML 3 point output
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** serialized via `ST_AsGML(3, geom)`
- **THEN** the output SHALL contain `<gml:Point>` and `<gml:pos>1 2</gml:pos>`
- Validated by: regress/core/out_gml.sql

#### Scenario: Empty geometry GML output
- **GIVEN** a geometry `POINT EMPTY`
- **WHEN** serialized via `ST_AsGML()`
- **THEN** valid GML SHALL be produced (not an error)
- Validated by: regress/core/empty.sql

### Requirement: KML output
The KML codec SHALL produce KML-conformant XML via `ST_AsKML()`. KML uses `<coordinates>lon,lat[,alt]</coordinates>` format. Since KML assumes WGS84, non-4326 geometries are typically transformed before output.

#### Scenario: KML point output
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** serialized via `ST_AsKML()`
- **THEN** the output SHALL contain `<Point><coordinates>1,2</coordinates></Point>`
- Validated by: regress/core/in_kml.sql (via round-trip tests)

#### Scenario: 3D KML output includes altitude
- **GIVEN** a geometry `POINT Z (1 2 3)`
- **WHEN** serialized via `ST_AsKML()`
- **THEN** the output SHALL contain coordinates `1,2,3`
- Status: untested -- no dedicated KML 3D output test

#### Scenario: Empty geometry KML output
- **GIVEN** a geometry `LINESTRING EMPTY`
- **WHEN** serialized via `ST_AsKML()`
- **THEN** valid KML SHALL be produced
- Status: untested -- no dedicated KML empty geometry test

### Requirement: SVG output
The SVG codec SHALL produce SVG path data via `ST_AsSVG()`. Points produce `cx="x" cy="y"` attributes. Lines and polygons produce `d="M x y L x y ..."` path data. The Y axis is inverted (negated) by default for SVG coordinate space.

#### Scenario: SVG point output
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** serialized via `ST_AsSVG()`
- **THEN** the output SHALL contain `cx="1" cy="-2"` (Y negated)
- Validated by: regress/core/out_geometry.sql

#### Scenario: SVG line output
- **GIVEN** a geometry `LINESTRING(0 0, 1 1, 2 0)`
- **WHEN** serialized via `ST_AsSVG()`
- **THEN** the output SHALL be an SVG path string starting with `M`
- Status: untested -- no dedicated SVG line test in core regression

#### Scenario: Empty geometry SVG output
- **GIVEN** a geometry `POINT EMPTY`
- **WHEN** serialized via `ST_AsSVG()`
- **THEN** an empty string or valid SVG placeholder SHALL be produced
- Status: untested -- no dedicated SVG empty geometry test

### Requirement: TWKB serialization
The TWKB (Tiny WKB) codec SHALL produce a compact binary format via `ST_AsTWKB()` that uses variable-length integer encoding and coordinate precision scaling. Key properties:
- Precision parameter controls decimal places preserved
- Coordinate values are delta-encoded relative to the previous point
- Type byte encodes geometry type in low nibble and precision in high nibble
- Optional SRID, bounding box, and size fields controlled by metadata flags

#### Scenario: TWKB point round-trip
- **GIVEN** a geometry `POINT(1 2)` with precision 0
- **WHEN** serialized via `ST_AsTWKB(geom, 0)` and parsed back via `ST_GeomFromTWKB()`
- **THEN** the result SHALL be `POINT(1 2)`
- Validated by: regress/core/twkb.sql

#### Scenario: TWKB with high precision
- **GIVEN** a geometry `POINT(1.23456 2.34567)` with precision 5
- **WHEN** serialized to TWKB and parsed back
- **THEN** the coordinates SHALL match to 5 decimal places
- Validated by: regress/core/twkb.sql

#### Scenario: TWKB with SRID and bbox
- **GIVEN** a geometry with SRID 4326 and optional bounding box
- **WHEN** serialized to TWKB with SRID and bbox included
- **THEN** the SRID and bounding box SHALL be recoverable from the TWKB
- Validated by: regress/core/twkb.sql

### Requirement: Encoded Polyline serialization
The Encoded Polyline codec SHALL serialize LINESTRING geometries to/from Google's Encoded Polyline Algorithm Format via `ST_AsEncodedPolyline()` and `ST_LineFromEncodedPolyline()`. The format uses variable-length ASCII encoding of coordinate deltas.

Only LINESTRING and MULTIPOINT types are supported. Other types SHALL raise an error.

#### Scenario: Encoded polyline round-trip
- **GIVEN** a LINESTRING geometry
- **WHEN** serialized via `ST_AsEncodedPolyline()` and parsed back via `ST_LineFromEncodedPolyline()`
- **THEN** the coordinate values SHALL match within the precision of the encoding
- Validated by: regress/core/in_encodedpolyline.sql

#### Scenario: Encoded polyline precision parameter
- **GIVEN** a LINESTRING geometry and precision 6
- **WHEN** serialized with precision 6
- **THEN** coordinates SHALL be preserved to 6 decimal places
- Validated by: regress/core/in_encodedpolyline.sql

#### Scenario: Non-linestring raises error
- **GIVEN** a POLYGON geometry
- **WHEN** `ST_AsEncodedPolyline()` is called
- **THEN** an error SHALL be raised
- Status: untested -- no existing regression test covers this case

### Requirement: Dimension coercion
The functions `lwgeom_force_2d()`, `lwgeom_force_3dz()`, `lwgeom_force_3dm()`, and `lwgeom_force_4d()` SHALL change the dimensionality of a geometry:
- `force_2d`: drops Z and M, resulting in FLAGS_NDIMS = 2
- `force_3dz`: adds Z if missing (default Z=0), drops M, resulting in FLAGS_NDIMS = 3 with Z
- `force_3dm`: adds M if missing (default M=0), drops Z, resulting in FLAGS_NDIMS = 3 with M
- `force_4d`: adds Z and M if missing (default 0), resulting in FLAGS_NDIMS = 4

#### Scenario: Force 2D drops Z
- **GIVEN** a geometry `POINT Z (1 2 3)`
- **WHEN** `ST_Force2D()` is applied
- **THEN** the result SHALL be `POINT(1 2)` with FLAGS_NDIMS = 2
- Validated by: regress/core/binary.sql (force3dz/force3dm/force4d tests)

#### Scenario: Force 3DZ adds default Z
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** `ST_Force3DZ()` is applied
- **THEN** the result SHALL be `POINT Z (1 2 0)` with Z flag set
- Validated by: regress/core/binary.sql

#### Scenario: Force 4D on 2D geometry
- **GIVEN** a geometry `POINT(1 2)`
- **WHEN** `ST_Force4D()` is applied
- **THEN** the result SHALL be `POINT ZM (1 2 0 0)`
- Validated by: regress/core/binary.sql

### Requirement: NULL propagation for serialization functions
All serialization SQL functions (`ST_AsText()`, `ST_AsBinary()`, `ST_AsEWKT()`, `ST_AsEWKB()`, `ST_AsGeoJSON()`, `ST_AsGML()`, `ST_AsKML()`, `ST_AsSVG()`, `ST_AsTWKB()`, `ST_AsEncodedPolyline()`) SHALL return NULL when the input geometry is NULL (standard SQL NULL propagation).

All deserialization SQL functions (`ST_GeomFromText()`, `ST_GeomFromWKB()`, `ST_GeomFromEWKT()`, `ST_GeomFromEWKB()`, `ST_GeomFromGeoJSON()`) SHALL return NULL when the input is NULL.

#### Scenario: ST_AsText with NULL input
- **WHEN** `ST_AsText(NULL::geometry)` is evaluated
- **THEN** the result SHALL be NULL
- Validated by: regress/core/wkt.sql (implicit from PostgreSQL strict function behavior)

#### Scenario: ST_GeomFromGeoJSON with NULL input
- **WHEN** `ST_GeomFromGeoJSON(NULL::text)` is evaluated
- **THEN** the result SHALL be NULL
- Status: untested -- no explicit NULL test for GeoJSON parsing

#### Scenario: ST_AsBinary with NULL input
- **WHEN** `ST_AsBinary(NULL::geometry)` is evaluated
- **THEN** the result SHALL be NULL
- Status: untested -- relies on PostgreSQL STRICT function marking

### Requirement: SQL/MM curve type serialization
Curve types (CircularString, CompoundCurve, CurvePolygon, MultiCurve, MultiSurface) SHALL be fully supported in WKB/EWKB and WKT/EWKT serialization with their native SQL/MM type codes.

For GeoJSON, GML 3, and other formats that do not natively support curves, curve geometries SHALL be stroked (converted to linear approximations) before output.

#### Scenario: CircularString WKB round-trip
- **GIVEN** a geometry `CIRCULARSTRING(0 0, 1 1, 2 0)`
- **WHEN** serialized to WKB and parsed back
- **THEN** `ST_OrderingEquals()` SHALL return true
- **AND** the WKB type integer SHALL be 8 (CIRCSTRINGTYPE)
- Validated by: regress/core/wkb.sql

#### Scenario: CompoundCurve WKB round-trip
- **GIVEN** a geometry `COMPOUNDCURVE(CIRCULARSTRING(0 0, 1 1, 2 0), (2 0, 3 0))`
- **WHEN** serialized to WKB and parsed back
- **THEN** `ST_OrderingEquals()` SHALL return true
- Validated by: regress/core/wkb.sql

#### Scenario: CurvePolygon WKT round-trip
- **GIVEN** a geometry `CURVEPOLYGON(CIRCULARSTRING(0 0, 1 1, 2 0, 1 -1, 0 0))`
- **WHEN** serialized to WKT and parsed back
- **THEN** `ST_OrderingEquals()` SHALL return true
- Validated by: regress/core/sql-mm-serialize.sql

### Requirement: PostgreSQL binary COPY round-trip
All 15 geometry types in all dimension variants (2D, 3DZ, 3DM, 4D), with and without SRID, SHALL survive a PostgreSQL binary COPY export/import cycle with exact fidelity. The same requirement applies to geography type. After COPY-out and COPY-in, `ST_OrderingEquals()` SHALL return true for every geometry.

#### Scenario: All geometry types survive binary COPY
- **GIVEN** a table containing all 15 geometry types in 2D, 3DZ, 3DM, and 4D variants, plus with SRID=4326
- **WHEN** the table is exported via `COPY ... WITH BINARY` and imported back
- **THEN** every row SHALL satisfy `ST_OrderingEquals(original, imported) = true`
- Validated by: regress/core/binary.sql

#### Scenario: Geography types survive binary COPY
- **GIVEN** the same geometries cast to geography
- **WHEN** exported and imported via binary COPY
- **THEN** every row SHALL match after round-trip
- Validated by: regress/core/binary.sql

#### Scenario: Binary COPY count matches
- **GIVEN** the full set of test geometries (15 types * 4 dimension variants * 2 SRID states = 120 rows)
- **WHEN** binary COPY round-trip is performed
- **THEN** the count of matching rows SHALL equal the total row count
- Validated by: regress/core/binary.sql

## Coverage Summary

**Functions covered:** POINTTYPE through TINTYPE constants, LWGEOM/LWPOINT/LWLINE/LWPOLY/LWTRIANGLE/LWCIRCSTRING/LWCOMPOUND/LWCURVEPOLY/LWMPOINT/LWMLINE/LWMPOLY/LWCOLLECTION/LWMCURVE/LWMSURFACE/LWPSURFACE/LWTIN structs, POINTARRAY, POINT2D/3DZ/3DM/4D, FLAGS_GET_Z/M/BBOX/GEODETIC/READONLY/SOLID, FLAGS_SET_Z/M/BBOX/GEODETIC/READONLY/SOLID, FLAGS_NDIMS, lwgeom_is_empty, lwgeom_add_bbox, lwgeom_drop_bbox, lwgeom_force_2d/3dz/3dm/4d, lwgeom_has_z, lwgeom_has_m, lwgeom_ndims, clamp_srid, ST_AsText, ST_AsEWKT, ST_AsBinary, ST_AsEWKB, ST_GeomFromText, ST_GeomFromEWKT, ST_GeomFromWKB, ST_GeomFromEWKB, ST_AsGeoJSON, ST_GeomFromGeoJSON, ST_AsGML, ST_AsKML, ST_AsSVG, ST_AsTWKB, ST_GeomFromTWKB, ST_AsEncodedPolyline, ST_LineFromEncodedPolyline.

**Deferred to other specs:**
- ST_Transform, spatial_ref_sys: see `coordinate-transforms` spec
- ST_Intersects, ST_Contains, etc.: see `spatial-predicates` spec
- ST_Distance, ST_Area, etc.: see `measurement-functions` spec
- GiST/SP-GiST/BRIN indexing: see `spatial-indexing` spec
- Geography type behavior: see `geography-type` spec
- ST_MakePoint, ST_Dump, etc.: see `constructors-editors` spec
- GSERIALIZED on-disk format: see `gserialized-format` spec
- X3D output (`ST_AsX3D`): low priority, deferred
- FlatGeobuf (`ST_AsFlatGeobuf`, `ST_FromFlatGeobuf`): table-level format, deferred

**Test coverage:** 66 scenarios total; 57 validated by existing regression tests, 9 flagged as untested.
