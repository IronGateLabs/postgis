## MODIFIED Requirements

### Requirement: Audit of hard-coded CRS assumptions
The analysis SHALL produce a documented inventory of every location in the PostGIS C codebase where coordinate system family is assumed rather than checked. This inventory SHALL cover:

1. The `source_is_latlong` boolean in `LWPROJ` (`liblwgeom/liblwgeom.h.in`) — now supplemented by `source_crs_family` / `target_crs_family` fields
2. The `LWFLAG_GEODETIC` flag usage in `liblwgeom/lwgeodetic.c` and `liblwgeom/gbox.c`
3. Unit-sphere conversions (`geog2cart`, `cart2geog`, `ll2cart` in `liblwgeom/lwgeodetic.c`) that assume radius=1.0
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

#### Scenario: Audit report identifies refactored vs remaining code paths
- **WHEN** the audit examines `source_is_latlong` alongside `source_crs_family`
- **THEN** the report SHALL distinguish between code paths already refactored to use CRS family dispatch and those still relying on the boolean flag, with a priority classification for each remaining path

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

#### Scenario: Matrix populated with tested evidence
- **WHEN** the gap analysis is produced
- **THEN** each cell classification SHALL be backed by a specific test or code reference demonstrating the behavior, not assumptions
