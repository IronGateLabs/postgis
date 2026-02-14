## MODIFIED Requirements

### Requirement: Replace source_is_latlong with CRS family check
All code paths that branch on `LWPROJ.source_is_latlong` SHALL be refactored to branch on `LWPROJ.source_crs_family` (and where needed, `target_crs_family`) to correctly handle geocentric and inertial CRS types.

The `source_is_latlong` field SHALL be retained but marked deprecated for one release cycle. All internal consumers SHALL be migrated to use the CRS family enum.

#### Scenario: Geocentric source not treated as latlong
- **WHEN** a transformation with ECEF source (SRID 4978) is processed
- **THEN** the coordinate preparation code SHALL NOT apply radian conversion (ECEF coordinates are already in meters, not angular)

#### Scenario: Geographic source still gets radian conversion
- **WHEN** a transformation with geographic source (SRID 4326) is processed
- **THEN** the coordinate preparation code SHALL apply degree-to-radian conversion as it does today

#### Scenario: Projected source bypasses angular handling
- **WHEN** a transformation with projected source (SRID 32632) is processed
- **THEN** no angular conversion SHALL be applied (same as current behavior)

#### Scenario: lwproj_is_latlong uses CRS family
- **WHEN** `lwproj_is_latlong()` is called on an LWPROJ with `source_crs_family == LW_CRS_GEOGRAPHIC`
- **THEN** the function SHALL return true, deriving the answer from `source_crs_family` rather than the deprecated `source_is_latlong` boolean

#### Scenario: srid_check_latlong uses CRS family internally
- **WHEN** `srid_check_latlong()` validates a geography SRID
- **THEN** the function SHALL check `source_crs_family == LW_CRS_GEOGRAPHIC` internally, while preserving its existing error message contract

#### Scenario: srid_axis_precision uses CRS family
- **WHEN** `srid_axis_precision()` determines decimal precision for a geographic CRS
- **THEN** the function SHALL branch on `source_crs_family == LW_CRS_GEOGRAPHIC` rather than the `source_is_latlong` boolean

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

#### Scenario: ST_Area raises error for geocentric
- **WHEN** `ST_Area` is called on a geocentric polygon
- **THEN** the function SHALL raise an error indicating that area computation is not supported in geocentric coordinates

#### Scenario: ST_Centroid raises error for geocentric
- **WHEN** `ST_Centroid` is called on a geocentric geometry
- **THEN** the function SHALL raise an error indicating that centroid computation is not supported in geocentric coordinates

#### Scenario: ST_Length dispatches to 3D Euclidean for geocentric
- **WHEN** `ST_Length` is called on a geocentric linestring
- **THEN** the function SHALL compute 3D Euclidean length in meters

#### Scenario: Error message includes function name and SRID
- **WHEN** a spatial function raises a CRS family error
- **THEN** the error message SHALL include the function name and SRID to aid debugging
