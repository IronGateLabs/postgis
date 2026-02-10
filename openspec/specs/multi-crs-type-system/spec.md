## ADDED Requirements

### Requirement: CRS family metadata in LWPROJ
The `LWPROJ` struct SHALL be extended to include a `crs_family` field (using the CRS family enum) that is populated when a transformation is created via `lwproj_from_PJ` or `lwproj_from_str_pipeline`.

#### Scenario: LWPROJ populated for geographic source
- **WHEN** a transformation is created from SRID 4326 to SRID 32632
- **THEN** the LWPROJ source CRS family SHALL be `LW_CRS_GEOGRAPHIC` and the target SHALL be `LW_CRS_PROJECTED`

#### Scenario: LWPROJ populated for geocentric target
- **WHEN** a transformation is created from SRID 4326 to SRID 4978
- **THEN** the LWPROJ source CRS family SHALL be `LW_CRS_GEOGRAPHIC` and the target SHALL be `LW_CRS_GEOCENTRIC`

#### Scenario: Pipeline transform family defaults
- **WHEN** a pipeline transform is created via `lwproj_from_str_pipeline`
- **THEN** source and target CRS families SHALL default to `LW_CRS_UNKNOWN` unless derivable from the pipeline definition

### Requirement: Replace source_is_latlong with CRS family check
All code paths that branch on `LWPROJ.source_is_latlong` SHALL be refactored to branch on `LWPROJ.source_crs_family` (and where needed, `target_crs_family`) to correctly handle geocentric and inertial CRS types.

#### Scenario: Geocentric source not treated as latlong
- **WHEN** a transformation with ECEF source (SRID 4978) is processed
- **THEN** the coordinate preparation code SHALL NOT apply radian conversion (ECEF coordinates are already in meters, not angular)

#### Scenario: Geographic source still gets radian conversion
- **WHEN** a transformation with geographic source (SRID 4326) is processed
- **THEN** the coordinate preparation code SHALL apply degree-to-radian conversion as it does today

#### Scenario: Projected source bypasses angular handling
- **WHEN** a transformation with projected source (SRID 32632) is processed
- **THEN** no angular conversion SHALL be applied (same as current behavior)

### Requirement: PROJ cache key includes CRS family
The `PROJSRSCacheItem` struct SHALL cache the source and target CRS families alongside the SRID pair, so that CRS family lookups do not require repeated PROJ queries.

#### Scenario: Cache hit returns CRS family
- **WHEN** a transformation for SRID pair (4326, 4978) is requested and exists in cache
- **THEN** the cached LWPROJ SHALL include source_crs_family=`LW_CRS_GEOGRAPHIC` and target_crs_family=`LW_CRS_GEOCENTRIC` without a new PROJ query

#### Scenario: Cache eviction preserves correctness
- **WHEN** the PROJ cache is full (128 items) and a new entry evicts an old one
- **THEN** the next request for the evicted SRID pair SHALL re-derive the CRS family from PROJ correctly

### Requirement: GSERIALIZED compatibility
Changes to CRS family handling SHALL NOT modify the `GSERIALIZED` on-disk format in this phase. CRS family SHALL be derived at runtime from the SRID stored in the existing 3-byte SRID field.

#### Scenario: Existing data remains readable
- **WHEN** a PostGIS database with existing geometries is upgraded to a version with CRS family support
- **THEN** all existing geometries SHALL be readable without migration, with CRS family derived from their SRID at query time

#### Scenario: Dump and restore compatibility
- **WHEN** a database is dumped with `pg_dump` and restored on a version with CRS family support
- **THEN** all geometries SHALL function correctly with CRS family derived from SRID

### Requirement: Spatial function dispatch by CRS family
Spatial functions SHALL inspect the CRS family of their input geometries and dispatch to appropriate implementations or raise errors for unsupported families.

#### Scenario: ST_Distance dispatches to 3D Euclidean for geocentric
- **WHEN** `ST_Distance` is called on two geocentric geometries
- **THEN** the function SHALL compute 3D Euclidean distance in meters

#### Scenario: ST_Buffer raises error for geocentric
- **WHEN** `ST_Buffer` is called on a geocentric geometry
- **THEN** the function SHALL raise an error indicating that buffering is not supported in geocentric coordinates and suggesting transformation to a projected CRS first

#### Scenario: Mixed CRS families raise error
- **WHEN** a binary spatial function (e.g., `ST_Intersects`) is called with one geographic and one geocentric geometry
- **THEN** the function SHALL raise an error indicating CRS family mismatch, rather than producing incorrect results

### Requirement: Documentation of CRS family in metadata functions
The `ST_SRID`, `PostGIS_Full_Version`, and related metadata functions SHALL expose CRS family information.

#### Scenario: Find_SRID returns family
- **WHEN** `SELECT postgis_crs_family(4978)` is called
- **THEN** the function SHALL return 'geocentric'

#### Scenario: Geometry metadata includes CRS family
- **WHEN** `SELECT ST_Summary(geom)` is called on a geometry with SRID 4978
- **THEN** the summary SHALL include the CRS family classification (e.g., "Geocentric" or "ECEF")
