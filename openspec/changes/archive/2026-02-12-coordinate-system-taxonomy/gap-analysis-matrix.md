# Gap Analysis Matrix: CRS Family vs PostGIS Capabilities

**Test environment:** PostGIS 3.7.0dev, PROJ 9.2.1, PostgreSQL 16, GEOS 3.12.0
**Date:** 2026-02-12
**Method:** Direct SQL testing in Docker (pg16-geos312-gdal37-proj921 image)

## Summary Matrix

| Capability | Geographic (4326) | Projected (32632) | Geocentric (4978) | Inertial (900001+) | Topocentric | Engineering (SRID 0) |
|---|---|---|---|---|---|---|
| Storage | FULL | FULL | FULL | NONE | NONE | FULL |
| ST_Transform | FULL | FULL | FULL | NONE | NONE | NONE |
| ST_Distance (geometry) | PARTIAL | FULL | PARTIAL | NONE | NONE | FULL |
| ST_Distance (geography) | FULL | N/A | ERROR | NONE | NONE | N/A |
| ST_Area (geometry) | PARTIAL | FULL | ERROR | NONE | NONE | FULL |
| ST_Area (geography) | FULL | N/A | ERROR | NONE | NONE | N/A |
| GiST Index (2D) | FULL | FULL | PARTIAL | NONE | NONE | FULL |
| GiST Index (3D/ND) | N/A | N/A | PARTIAL | NONE | NONE | N/A |
| ST_AsText | FULL | FULL | FULL | NONE | NONE | FULL |
| ST_AsGeoJSON | FULL | FULL | FULL | NONE | NONE | FULL |
| ST_AsBinary | FULL | FULL | FULL | NONE | NONE | FULL |
| postgis_crs_family() | FULL* | FULL | FULL | NONE | NONE | FULL |

\* Bug found and fixed: SRID 4326 was returning "unknown" due to `SRID_DEFAULT` check in `postgis_crs_family()`.

## Classification Key

- **FULL**: Works correctly with semantically appropriate results
- **PARTIAL**: Returns a result, but semantics may be inappropriate for the CRS type
- **PROXY**: Works via automatic transform to a supported type
- **NONE**: Not supported (SRID not registered in spatial_ref_sys, or no implementation)
- **ERROR**: Produces an error or silently incorrect results

## Detailed Evidence

### 1. Storage

All CRS families that have registered SRIDs can store and retrieve geometry values without error.

| Test | Query | Result | Classification |
|---|---|---|---|
| Geographic | `ST_AsText(ST_SetSRID(ST_MakePoint(-73.9857, 40.7484), 4326))` | `POINT(-73.9857 40.7484)` | FULL |
| Projected | `ST_AsText(ST_SetSRID(ST_MakePoint(500000, 4649776), 32632))` | `POINT(500000 4649776)` | FULL |
| Geocentric (4978) | `ST_AsText(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978))` | `POINT Z (6378137 0 0)` | FULL |
| Geocentric (4936) | `ST_AsText(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4936))` | `POINT Z (6378137 0 0)` | FULL |
| Engineering (SRID 0) | `ST_AsText(ST_MakePoint(100, 200, 300))` | `POINT Z (100 200 300)` | FULL |
| Inertial | Not tested - SRIDs 900001-900099 not in spatial_ref_sys on develop branch | N/A | NONE |

**Notes:** Storage is purely geometric; PostGIS stores coordinates without interpreting CRS. All types work.

### 2. ST_Transform

| Test | Query | Result | Classification |
|---|---|---|---|
| Geographic -> Projected | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(9, 42), 4326), 32632))` | 500000 | FULL |
| Projected -> Geographic | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(500000, 4649776), 32632), 4326))` | 9.0000 | FULL |
| Geographic -> Geocentric | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(0, 0, 0), 4326), 4978))` | 6378137 | FULL |
| Geocentric -> Geographic | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978), 4326))` | 0.0000 | FULL |
| Projected -> Geocentric | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(500000, 0), 32632), 4978))` | 6299612 | FULL |
| Geocentric -> Projected | `ST_X(ST_Transform(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978), 32632))` | -505647 | FULL |
| Engineering (SRID 0) | Cannot transform - no CRS definition | N/A | NONE |

**Notes:** All transforms between geographic, projected, and geocentric CRS families succeed via PROJ. The geocentric->projected result (-505647) is geometrically correct for an equator point at 0 degrees longitude projected to UTM zone 32N (centered at 9E).

### 3. ST_Distance

| Test | Query | Result | Classification | Notes |
|---|---|---|---|---|
| Geographic (geometry) | `ST_Distance(geom_4326_a, geom_4326_b)` | 1.000000 | PARTIAL | Returns planar distance in degrees, not geodesic meters |
| Geographic (geography) | `ST_Distance(geog_a, geog_b)` | 111319 m | FULL | Correct geodesic distance (~111 km per degree at equator) |
| Projected | `ST_Distance(geom_32632_a, geom_32632_b)` | 1000 m | FULL | Correct Cartesian distance in projection units (meters) |
| Geocentric (2D geometry) | `ST_Distance(ecef_a, ecef_b)` | 9020048 | PARTIAL | Returns Euclidean distance in ECEF meters; not geodesic |
| Geocentric (3D geometry) | `ST_3DDistance(ecef_a, ecef_b)` | 9020048 | PARTIAL | Same as 2D for Z=0 case; returns chord distance, not arc |
| Geocentric (geography) | `ST_Distance(ecef::geography, ...)` | ERROR | ERROR | "Only lon/lat coordinate systems are supported in geography" |
| Engineering | `ST_Distance(MakePoint(0,0), MakePoint(3,4))` | 5 | FULL | Correct Euclidean distance |

**Notes:**
- Geographic geometry ST_Distance returns degrees (planar) - semantically misleading but expected behavior
- Geocentric ST_Distance returns Euclidean chord distance, not great-circle/geodesic distance. For points on Earth's surface separated by 90 degrees, chord = R*sqrt(2) = 6378137*1.414 = ~9,020,048 m. This is geometrically correct as a Euclidean metric but not the geodesic distance users would typically want.
- Geocentric geography cast is properly rejected with an error

### 4. ST_Area

| Test | Query | Result | Classification | Notes |
|---|---|---|---|---|
| Geographic (geometry) | `ST_Area(envelope_4326)` | 1.000000 | PARTIAL | Returns area in degrees^2 |
| Geographic (geography) | `ST_Area(envelope_4326::geography)` | 12308778361 m^2 | FULL | Correct geodesic area |
| Projected | `ST_Area(envelope_32632)` | 1000000 m^2 | FULL | Correct (1000m x 1000m) |
| Geocentric | `ST_Area(polygon_4978)` | 2.03e13 | ERROR | Treats ECEF XYZ as planar 2D coordinates; nonsensical result |
| Engineering | `ST_Area(envelope_srid0)` | 100 | FULL | Correct (10 x 10) |

**Notes:**
- Geocentric ST_Area computes a 2D planar area using XY coordinates that are actually ECEF X/Y in meters. The result (20.3 trillion) is meaningless. PostGIS does not detect the geocentric SRID and warn/error.

### 5. GiST Indexing

| Test | Query | Result | Classification | Notes |
|---|---|---|---|---|
| Geographic (2D) | `geom && envelope_4326` | Works (index created, used) | FULL | Standard 2D bbox semantics |
| Projected (2D) | `geom && envelope_32632` | true | FULL | Standard 2D bbox semantics |
| Geocentric (2D) | `geom && envelope_4978` | 9 rows | PARTIAL | 2D bbox on ECEF XY; Z dimension ignored; semantics questionable |
| Geocentric (3D ND) | `geom &&& line_4978` | 9 rows | PARTIAL | 3D ND-tree works but box semantics not ideal for spherical data |
| Engineering (2D) | `geom && envelope_srid0` | true | FULL | Standard 2D bbox semantics |

**Notes:**
- GiST indexes can be created for all CRS families. The index stores bounding boxes.
- For geocentric ECEF data, the 2D bounding box uses XY coordinates (ignoring Z), which is geometrically incorrect for spatial queries on Earth's surface. The 3D ND index includes all axes but the axis-aligned bounding box may include large volumes of empty space for points distributed on a sphere.

### 6. Serialization

All serialization functions work correctly for all testable CRS families:

| Format | Geographic | Projected | Geocentric |
|---|---|---|---|
| ST_AsText | `POINT(1.5 2.5)` | `POINT(500000 4649776)` | `POINT Z (6378137 0 0)` |
| ST_AsGeoJSON | `{"type":"Point","coordinates":[1.5,2.5]}` | Includes CRS name | Includes CRS name |
| ST_AsBinary | 21 bytes | 21 bytes | 29 bytes (3D) |

**Notes:** Serialization is format-agnostic; it serializes coordinates without CRS interpretation. ST_AsGeoJSON includes CRS name for non-4326 SRIDs.

### 7. postgis_crs_family()

| SRID | Expected | Actual (before fix) | Actual (after fix) |
|---|---|---|---|
| 4326 | geographic | **unknown** (BUG) | geographic |
| 32632 | projected | projected | projected |
| 4978 | geocentric | geocentric | geocentric |
| 0 | unknown | unknown | unknown |
| 999999 | error | ERROR: Invalid reserved SRID | (same) |

**Bug found:** `postgis/lwgeom_transform.c:788` had `if (srid == SRID_DEFAULT || srid == SRID_UNKNOWN)` which short-circuited SRID 4326 (= `SRID_DEFAULT`) to return "unknown". Fix: removed `SRID_DEFAULT` from the condition.

## Priority Findings for Future CRS-Aware Refactoring

1. **CRITICAL: ST_Area on geocentric data returns nonsensical values silently** - Should either error or transform to geographic first
2. **CRITICAL: `postgis_crs_family(4326)` returned "unknown"** - Fixed in this change
3. **HIGH: ST_Distance on geocentric returns Euclidean chord, not geodesic** - Users expect geodesic; should warn or auto-transform
4. **MEDIUM: GiST 2D index on ECEF data uses XY bounding box** - Spatially incorrect; may produce false negatives for nearby points on opposite sides of a sphere
5. **LOW: Geography cast rejects geocentric** - Correct behavior, well-reported error message
