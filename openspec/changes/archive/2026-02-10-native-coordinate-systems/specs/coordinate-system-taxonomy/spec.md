## ADDED Requirements

### Requirement: CRS family classification enum
The system SHALL define a CRS family classification with at minimum the following categories:
- `LW_CRS_GEOGRAPHIC` -- Latitude/longitude on an ellipsoid (e.g., EPSG:4326)
- `LW_CRS_PROJECTED` -- Planar Cartesian derived from a map projection (e.g., EPSG:32632 UTM)
- `LW_CRS_GEOCENTRIC` -- Earth-Centered Earth-Fixed Cartesian (e.g., EPSG:4978)
- `LW_CRS_INERTIAL` -- Earth-Centered Inertial frames (ICRF, J2000, TEME)
- `LW_CRS_TOPOCENTRIC` -- Local tangent plane (East-North-Up or North-East-Down)
- `LW_CRS_ENGINEERING` -- Local/engineering coordinate systems with no geodetic datum
- `LW_CRS_UNKNOWN` -- Unclassified or unresolvable CRS

#### Scenario: Enum values defined in liblwgeom header
- **WHEN** the CRS family enum is compiled
- **THEN** it SHALL be available as a C enum in `liblwgeom.h` with stable integer values suitable for caching and comparison

#### Scenario: Enum covers all PJ_TYPE geocentric types
- **WHEN** the enum is mapped from PROJ `PJ_TYPE` values
- **THEN** `PJ_TYPE_GEOCENTRIC_CRS` SHALL map to `LW_CRS_GEOCENTRIC`, `PJ_TYPE_GEOGRAPHIC_2D_CRS` and `PJ_TYPE_GEOGRAPHIC_3D_CRS` SHALL map to `LW_CRS_GEOGRAPHIC`, and all `PJ_TYPE_PROJECTED_CRS` SHALL map to `LW_CRS_PROJECTED`

### Requirement: Audit of hard-coded CRS assumptions
The analysis SHALL produce a documented inventory of every location in the PostGIS C codebase where coordinate system family is assumed rather than checked. This inventory SHALL cover:

1. The `source_is_latlong` boolean in `LWPROJ` (`liblwgeom/liblwgeom.h.in`)
2. The `LWFLAG_GEODETIC` flag usage in `liblwgeom/lwgeodetic.c` and `liblwgeom/gbox.c`
3. Unit-sphere conversions (`geog2cart`, `cart2geog`, `ll2cart`) that assume radius=1.0
4. GBOX range assumptions ([-1,1] for geodetic, unbounded for Cartesian)
5. Coordinate wrapping/normalization code assuming longitude in [-180,180] and latitude in [-90,90]
6. The `geography_distance_*` family of functions that assume spheroidal input
7. Serialization/deserialization in `gserialized*.c` that encodes geodetic flag

#### Scenario: Audit report produced for source_is_latlong
- **WHEN** the audit examines `source_is_latlong` usage
- **THEN** the report SHALL list every file and function where this flag controls branching logic, with a classification of whether the branch would produce incorrect results for geocentric input

#### Scenario: Audit report produced for unit-sphere functions
- **WHEN** the audit examines geodetic conversion functions
- **THEN** the report SHALL identify which functions would need ellipsoid-aware variants for true ECEF support vs. which can remain unit-sphere-only

### Requirement: CRS family derivation from SRID
The system SHALL provide a function that, given an SRID, returns the CRS family classification by querying PROJ for the CRS type. This function SHALL be cached for performance.

#### Scenario: Derive family for EPSG 4326
- **WHEN** `lwsrid_get_crs_family(4326)` is called
- **THEN** the result SHALL be `LW_CRS_GEOGRAPHIC`

#### Scenario: Derive family for EPSG 32632
- **WHEN** `lwsrid_get_crs_family(32632)` is called
- **THEN** the result SHALL be `LW_CRS_PROJECTED`

#### Scenario: Derive family for EPSG 4978
- **WHEN** `lwsrid_get_crs_family(4978)` is called
- **THEN** the result SHALL be `LW_CRS_GEOCENTRIC`

#### Scenario: Derive family for unknown SRID
- **WHEN** `lwsrid_get_crs_family(999999)` is called with an SRID not in `spatial_ref_sys`
- **THEN** the result SHALL be `LW_CRS_UNKNOWN`

### Requirement: Gap analysis matrix
The analysis SHALL produce a matrix of coordinate system families vs. PostGIS capabilities, identifying support level for each combination:

| Capability | Geographic | Projected | Geocentric | Inertial | Topocentric | Engineering |
|---|---|---|---|---|---|---|
| Storage | ? | ? | ? | ? | ? | ? |
| ST_Transform | ? | ? | ? | ? | ? | ? |
| ST_Distance | ? | ? | ? | ? | ? | ? |
| ST_Area | ? | ? | ? | ? | ? | ? |
| GIST indexing | ? | ? | ? | ? | ? | ? |
| ST_AsText/GeoJSON | ? | ? | ? | ? | ? | ? |

Each cell SHALL be classified as: FULL (works correctly), PARTIAL (works with caveats), PROXY (works via transform to supported type), NONE (not supported), or ERROR (silently incorrect).

#### Scenario: Matrix identifies geographic as fully supported
- **WHEN** the gap analysis evaluates geographic CRS (EPSG:4326)
- **THEN** all core capabilities SHALL be classified as FULL

#### Scenario: Matrix identifies geocentric gaps
- **WHEN** the gap analysis evaluates geocentric CRS (EPSG:4978)
- **THEN** the matrix SHALL identify specific capabilities as NONE or PARTIAL with explanations of what fails and why

#### Scenario: Matrix identifies inertial as unsupported
- **WHEN** the gap analysis evaluates inertial CRS
- **THEN** all capabilities SHALL be classified as NONE with notes on what would be required
