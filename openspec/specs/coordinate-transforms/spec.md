## Purpose

Defines the coordinate transformation subsystem in PostGIS, covering the ST_Transform SQL API, PROJ library integration, LWPROJ caching layer, CRS family classification, SRID management via the spatial_ref_sys table, pipeline transforms, and epoch-aware ECI/ECEF conversions. This spec depends on geometry-types for LWGEOM structure and gserialized-format for on-disk representation.

## ADDED Requirements

### Requirement: ST_Transform with target SRID
The function `ST_Transform(geometry, integer)` SHALL reproject the input geometry from its current SRID to the target SRID. The implementation SHALL:
- Look up both source and target CRS definitions from the `spatial_ref_sys` table
- Construct a PROJ transformation pipeline via `lwproj_lookup()` / `proj_create_crs_to_crs()`
- Transform all coordinates in-place via `lwgeom_transform()` / `ptarray_transform()`
- Set the output geometry's SRID to the target SRID
- Recompute the bounding box if the input had one

If source and target SRID are equal, the function SHALL return the input geometry unchanged (noop).

#### Scenario: Reproject WGS84 point to Web Mercator
- **GIVEN** a geometry `SRID=4326;POINT(-20 -20)`
- **WHEN** `ST_Transform(geom, 3857)` is called
- **THEN** the result SHALL be in SRID 3857 with projected coordinates approximately `POINT(-2226389 -2273031)`
- Validated by: regress/core/regress_proj_basic.sql (test M1)

#### Scenario: Reproject WGS84 to UTM zone 33N
- **GIVEN** a geometry `SRID=100002;POINT(16 48)` (WGS84 longlat)
- **WHEN** `ST_Transform(geom, 100001)` is called (UTM zone 33N)
- **THEN** the result SHALL have projected easting/northing coordinates in metres
- Validated by: regress/core/regress_proj_basic.sql (test 1)

#### Scenario: 3D coordinates preserved through transform
- **GIVEN** a geometry `SRID=100002;POINT(16 48 171)`
- **WHEN** `ST_Transform(geom, 100001)` is called
- **THEN** the Z coordinate SHALL be preserved in the output
- Validated by: regress/core/regress_proj_basic.sql (test 2)

#### Scenario: Same SRID returns input unchanged
- **GIVEN** a geometry `SRID=4326;POINT(1 2)`
- **WHEN** `ST_Transform(geom, 4326)` is called
- **THEN** the function SHALL return the original geometry pointer without transformation
- Status: untested -- implicit in code path but no dedicated regression test

### Requirement: ST_Transform error handling
ST_Transform SHALL raise an error in the following cases:
- Target SRID is SRID_UNKNOWN (0): "ST_Transform: 0 is an invalid target SRID"
- Input geometry has SRID_UNKNOWN: "ST_Transform: Input geometry has unknown (0) SRID"
- PROJ cannot construct a transformation between the two SRIDs: "Failure reading projections from spatial_ref_sys"

#### Scenario: Unknown target SRID raises error
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** `ST_Transform(geom, 0)` is called
- **THEN** the function SHALL raise an error containing "invalid target SRID"
- Status: untested -- error path not covered by regression tests

#### Scenario: Unknown source SRID raises error
- **GIVEN** a geometry `POINT(0 0)` with SRID 0
- **WHEN** `ST_Transform(geom, 4326)` is called
- **THEN** the function SHALL raise an error containing "Input geometry has unknown"
- Status: untested -- error path not covered by regression tests

#### Scenario: Non-existent SRID in spatial_ref_sys raises error
- **GIVEN** a geometry with a valid SRID that has no entry in `spatial_ref_sys`
- **WHEN** `ST_Transform(geom, target_srid)` is called
- **THEN** the function SHALL raise an error about failure reading projections
- Status: untested -- error path not covered by regression tests

### Requirement: ST_Transform with proj4 text strings
PostGIS SHALL provide overloaded ST_Transform variants that accept PROJ definition strings instead of SRIDs:
- `ST_Transform(geometry, to_proj text)` -- looks up source CRS from spatial_ref_sys by input SRID, uses to_proj as target
- `ST_Transform(geometry, from_proj text, to_proj text)` -- both CRS given as text, output SRID is 0
- `ST_Transform(geometry, from_proj text, to_srid integer)` -- source as text, target looked up from spatial_ref_sys

These overloads call `postgis_transform_geometry()` internally, which delegates to `lwgeom_transform_from_str()`.

#### Scenario: Transform with explicit proj4 strings
- **GIVEN** a geometry with known coordinates
- **WHEN** `ST_Transform(geom, '+proj=longlat +datum=WGS84', '+proj=utm +zone=33 +datum=WGS84')` is called
- **THEN** the result SHALL have coordinates in UTM zone 33 projection
- Validated by: regress/core/regress_proj_adhoc.sql

#### Scenario: Transform from proj4 text to SRID
- **GIVEN** a geometry with known coordinates
- **WHEN** `ST_Transform(geom, from_proj_text, 3857)` is called
- **THEN** the result SHALL have SRID 3857 and Web Mercator coordinates
- Status: untested -- no specific regression test for this overload

#### Scenario: Invalid proj4 string raises error
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** `ST_Transform(geom, 'invalid proj string', '+proj=longlat')` is called
- **THEN** the function SHALL raise an error about inability to parse the proj string
- Status: untested -- error path not covered by regression tests

### Requirement: ST_TransformPipeline
The function `ST_TransformPipeline(geometry, pipeline text, to_srid integer DEFAULT 0)` SHALL apply a PROJ pipeline transformation string directly, bypassing CRS-to-CRS resolution. The inverse function `ST_InverseTransformPipeline()` SHALL apply the pipeline in reverse direction.

The pipeline string SHALL be a valid PROJ pipeline definition (e.g., `+proj=pipeline +step ...`) or a coordinate operation URN (e.g., `urn:ogc:def:coordinateOperation:EPSG::16031`).

If the pipeline string defines a CRS (not a coordinate operation), the function SHALL raise an error.

#### Scenario: Forward pipeline transform
- **GIVEN** a geometry `SRID=4326;POINT(174 -37)`
- **WHEN** `ST_TransformPipeline(geom, '+proj=pipeline +step +proj=unitconvert +xy_in=deg +xy_out=rad +step +proj=tmerc +lat_0=0 +lon_0=173 +k=0.9996 +x_0=1600000 +y_0=10000000 +ellps=GRS80')` is called
- **THEN** the result SHALL have projected coordinates in the Transverse Mercator projection
- Validated by: regress/core/regress_proj_pipeline.sql (test 1)

#### Scenario: Pipeline with EPSG coordinate operation URN
- **GIVEN** a geometry `SRID=4326;POINT(2 49)`
- **WHEN** `ST_TransformPipeline(geom, 'urn:ogc:def:coordinateOperation:EPSG::16031')` is called
- **THEN** the result SHALL have UTM zone 31N coordinates
- Validated by: regress/core/regress_proj_pipeline.sql (test 2)

#### Scenario: Pipeline with CRS definition raises error
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** `ST_TransformPipeline(geom, 'EPSG:2193')` is called
- **THEN** the function SHALL raise an error because the string defines a CRS, not a coordinate operation
- Validated by: regress/core/regress_proj_pipeline.sql (test 4)

#### Scenario: Inverse pipeline transform
- **GIVEN** a geometry `SRID=32631;POINT(426857 5427937)`
- **WHEN** `ST_InverseTransformPipeline(geom, 'urn:ogc:def:coordinateOperation:EPSG::16031')` is called
- **THEN** the result SHALL have geographic coordinates (lon/lat in degrees)
- Validated by: regress/core/regress_proj_pipeline.sql (test 8)

#### Scenario: Pipeline result SRID assignment
- **GIVEN** a geometry `SRID=4326;POINT(174 -37)`
- **WHEN** `ST_TransformPipeline(geom, pipeline_string, 12345)` is called
- **THEN** the result geometry's SRID SHALL be 12345
- Validated by: regress/core/regress_proj_pipeline.sql (test 6)

### Requirement: LWPROJ caching and PROJ integration
The LWPROJ struct SHALL cache PROJ transformation objects (`PJ*`) along with metadata to avoid repeated calls to `proj_get_source_crs()` and `proj_get_target_crs()`. The struct SHALL contain:
- `pj` (PJ*): the PROJ transformation object
- `pipeline_is_forward` (bool): direction for pipeline transforms
- `source_is_latlong` (uint8_t): deprecated, retained for backward compatibility
- `source_semi_major_metre`, `source_semi_minor_metre` (double): source ellipsoid parameters
- `source_crs_family`, `target_crs_family` (LW_CRS_FAMILY): CRS family classification
- `epoch` (double): coordinate epoch for time-dependent transforms

The function `lwproj_from_str()` SHALL construct an LWPROJ from two CRS definition strings, calling `proj_normalize_for_visualization()` to handle axis ordering (lon/lat vs lat/lon).

#### Scenario: Axis normalization for geographic CRS
- **WHEN** `lwproj_from_str("EPSG:4326", "EPSG:32632")` is called
- **THEN** the resulting LWPROJ SHALL have `proj_normalize_for_visualization()` applied so that input coordinates are in lon/lat order (not lat/lon)
- Status: untested -- internal behavior verified implicitly by transform correctness

#### Scenario: Pipeline transform creates LWPROJ with forward flag
- **WHEN** `lwproj_from_str_pipeline(pipeline_str, true)` is called with a valid pipeline
- **THEN** the LWPROJ SHALL have `pipeline_is_forward = true`
- **AND** CRS families SHALL be LW_CRS_UNKNOWN (pipeline transforms have unknown families)
- Status: untested -- internal struct not directly testable from SQL

#### Scenario: PROJ cache overflow handling
- **GIVEN** many distinct SRID pairs are transformed in sequence to exceed the cache capacity
- **WHEN** transformations continue
- **THEN** the system SHALL handle cache overflow gracefully without errors
- Validated by: regress/core/regress_proj_cache_overflow.sql

### Requirement: CRS family classification
The LW_CRS_FAMILY enum SHALL classify coordinate reference systems into families:
- `LW_CRS_UNKNOWN` (0): unclassified or unresolvable
- `LW_CRS_GEOGRAPHIC` (1): latitude/longitude on ellipsoid (e.g., EPSG:4326)
- `LW_CRS_PROJECTED` (2): planar Cartesian from map projection (e.g., EPSG:32632)
- `LW_CRS_GEOCENTRIC` (3): Earth-Centered Earth-Fixed Cartesian (e.g., EPSG:4978)
- `LW_CRS_INERTIAL` (4): Earth-Centered Inertial (ICRF/J2000/TEME)
- `LW_CRS_TOPOCENTRIC` (5): local tangent plane (ENU/NED)
- `LW_CRS_ENGINEERING` (6): local/engineering CRS with no geodetic datum

The function `lwsrid_get_crs_family()` SHALL resolve an SRID to its CRS family by querying the PROJ database. For compound CRS, it SHALL inspect the horizontal component.

#### Scenario: EPSG:4326 classified as geographic
- **WHEN** `postgis_crs_family(4326)` is called
- **THEN** the result SHALL be 'geographic'
- Validated by: regress/core/regress_crs_family.sql (CF1)

#### Scenario: EPSG:32632 classified as projected
- **WHEN** `postgis_crs_family(32632)` is called
- **THEN** the result SHALL be 'projected'
- Validated by: regress/core/regress_crs_family.sql (CF2)

#### Scenario: EPSG:4978 classified as geocentric
- **WHEN** `postgis_crs_family(4978)` is called
- **THEN** the result SHALL be 'geocentric'
- Validated by: regress/core/regress_crs_family.sql (CF3)

#### Scenario: Unknown SRID returns unknown family
- **WHEN** `postgis_crs_family(999999)` is called
- **THEN** the result SHALL be 'unknown'
- Validated by: regress/core/regress_crs_family.sql (CF5)

#### Scenario: SRID 0 returns unknown family
- **WHEN** `postgis_crs_family(0)` is called
- **THEN** the result SHALL be 'unknown'
- Validated by: regress/core/regress_crs_family.sql (CF6)

### Requirement: Epoch-aware ECI transforms
The function `ST_Transform(geometry, integer, timestamptz)` SHALL support coordinate transforms involving Earth-Centered Inertial (ECI) frames with an explicit epoch. The epoch is converted to a decimal year and applied uniformly to all points.

The function SHALL:
- Validate that at least one of source/target SRID is an inertial frame
- Validate the epoch is in the range 1000-3000 (decimal year)
- Reject direct ECI-to-ECI transforms (must go through ECEF first)

For the basic `ST_Transform(geometry, integer)` overload, if an ECI SRID is involved, M coordinates SHALL be interpreted as per-point epoch values (decimal year).

#### Scenario: ECI transform with explicit epoch
- **GIVEN** a geometry in ECEF coordinates (SRID 4978)
- **WHEN** `ST_Transform(geom, eci_srid, '2024-01-01 00:00:00+00'::timestamptz)` is called
- **THEN** the result SHALL have ECI coordinates rotated by the Earth Rotation Angle at epoch 2024.0
- Status: untested -- ECI functionality is new and not yet covered by core regression tests

#### Scenario: ECI transform requires M coordinates when no epoch given
- **GIVEN** a 2D geometry `SRID=4978;POINT(6378137 0)` and an ECI target SRID
- **WHEN** `ST_Transform(geom, eci_srid)` is called without M coordinates
- **THEN** the function SHALL raise an error containing "ECI transform requires epoch"
- Status: untested -- error path for ECI

#### Scenario: Direct ECI-to-ECI rejected
- **GIVEN** a geometry with an ECI source SRID
- **WHEN** `ST_Transform(geom, different_eci_srid)` is called
- **THEN** the function SHALL raise an error containing "Direct ECI-to-ECI frame transform is not supported"
- Status: untested -- error path for ECI

### Requirement: ECEF round-trip via ST_Transform
Geographic coordinates (SRID 4326) SHALL be transformable to geocentric ECEF coordinates (SRID 4978) and back with high fidelity. Known control points:
- (lon=0, lat=0, h=0) in WGS84 maps to approximately (6378137, 0, 0) in ECEF
- (lon=0, lat=90, h=0) in WGS84 maps to approximately (0, 0, 6356752.314) in ECEF

#### Scenario: WGS84 origin to ECEF
- **GIVEN** a geometry `SRID=4326;POINTZ(0 0 0)`
- **WHEN** `ST_Transform(geom, 4978)` is called
- **THEN** the result SHALL be approximately `POINTZ(6378137 0 0)` in SRID 4978
- Validated by: regress/core/regress_crs_family.sql (ECEF2)

#### Scenario: North pole to ECEF
- **GIVEN** a geometry `SRID=4326;POINTZ(0 90 0)`
- **WHEN** `ST_Transform(geom, 4978)` is called
- **THEN** the result SHALL be approximately `POINTZ(0 0 6356752.314)` in SRID 4978
- Validated by: regress/core/regress_crs_family.sql (ECEF3)

#### Scenario: WGS84 to ECEF round-trip preserves coordinates
- **GIVEN** a geometry `SRID=4326;POINTZ(0 0 0)`
- **WHEN** transformed to SRID 4978 and back to SRID 4326
- **THEN** the result SHALL match the original coordinates within 0.0000001 degrees
- Validated by: regress/core/regress_crs_family.sql (ECEF1)

### Requirement: spatial_ref_sys table management
PostGIS SHALL maintain a `spatial_ref_sys` table with columns:
- `srid` (integer, primary key): spatial reference identifier
- `auth_name` (varchar(256)): authority name (e.g., 'EPSG')
- `auth_srid` (integer): authority-specific SRID
- `srtext` (varchar(2048)): WKT representation of the CRS
- `proj4text` (varchar(2048)): PROJ4 text representation

The function `find_srid(schema, table, column)` SHALL look up the SRID for a geometry column registered in the `geometry_columns` view. The function `ST_SRID(geometry)` SHALL return the SRID of a geometry. The function `ST_SetSRID(geometry, integer)` SHALL set the SRID without transforming coordinates.

#### Scenario: ST_SRID returns geometry SRID
- **GIVEN** a geometry `SRID=4326;POINT(0 0)`
- **WHEN** `ST_SRID(geom)` is called
- **THEN** the result SHALL be 4326
- Validated by: regress/core/regress_management.sql

#### Scenario: ST_SetSRID changes SRID without transform
- **GIVEN** a geometry `POINT(500000 0)` with SRID 0
- **WHEN** `ST_SetSRID(geom, 32632)` is called
- **THEN** the SRID SHALL be 32632
- **AND** the coordinates SHALL remain unchanged at (500000, 0)
- Validated by: regress/core/regress_management.sql

#### Scenario: find_srid looks up registered geometry column
- **GIVEN** a table with a geometry column registered via `populate_geometry_columns()`
- **WHEN** `find_srid(schema, table, column)` is called
- **THEN** the correct SRID SHALL be returned
- Validated by: regress/core/regress_management.sql

### Requirement: Radian/degree conversion in ptarray_transform
The `ptarray_transform()` function SHALL automatically convert coordinates between degrees and radians as required by PROJ:
- If `proj_angular_input()` returns true for the transform direction, coordinates SHALL be converted from degrees to radians before transformation
- If `proj_angular_output()` returns true, coordinates SHALL be converted from radians to degrees after transformation

These conversions use the SIMD-accelerated `lwaccel_get()->rad_convert()` when available.

#### Scenario: Geographic input automatically converted to radians
- **GIVEN** a geographic geometry with coordinates in degrees
- **WHEN** transformed to a projected CRS via `ptarray_transform()`
- **THEN** the coordinates SHALL be internally converted to radians before passing to PROJ
- **AND** the output SHALL be in the target CRS units (e.g., metres)
- Validated by: regress/core/regress_proj_basic.sql (all tests implicitly verify this)

#### Scenario: Projected to geographic output converts to degrees
- **GIVEN** a projected geometry in metres
- **WHEN** transformed to a geographic CRS
- **THEN** the output SHALL be in degrees, with radians-to-degrees conversion applied automatically
- Validated by: regress/core/regress_proj_pipeline.sql (test 8)

#### Scenario: Single-point optimization uses proj_trans
- **GIVEN** a POINTARRAY with exactly 1 point
- **WHEN** `ptarray_transform()` is called
- **THEN** the function SHALL use `proj_trans()` (single-point API) instead of `proj_trans_generic()` for efficiency
- Status: untested -- internal optimization not directly testable from SQL

### Requirement: Epoch propagation to PROJ coordinates
When the LWPROJ `epoch` field is non-zero (not `LWPROJ_NO_EPOCH`), the `ptarray_transform()` function SHALL populate the `t` field of `PJ_COORD` with the epoch value for single-point transforms. This enables PROJ to perform time-dependent coordinate operations (e.g., plate motion models, dynamic datums).

#### Scenario: Epoch set on LWPROJ propagated to single-point transform
- **GIVEN** an LWPROJ with `epoch = 2024.5`
- **WHEN** a single point is transformed via `ptarray_transform()`
- **THEN** the `PJ_COORD.t` field SHALL be set to 2024.5
- Status: untested -- internal behavior not directly testable from SQL

#### Scenario: Multi-point transform does not propagate epoch via t coordinate
- **GIVEN** an LWPROJ with `epoch` set and a POINTARRAY with multiple points
- **WHEN** `ptarray_transform()` is called using `proj_trans_generic()`
- **THEN** the M coordinate array is passed as NULL to `proj_trans_generic()` (epoch not propagated per-point)
- Status: untested -- this is a known limitation of the multi-point transform path
