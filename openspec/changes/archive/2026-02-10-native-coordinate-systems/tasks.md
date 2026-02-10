## 1. Codebase Audit -- Hard-Coded CRS Assumptions

- [x] 1.1 Audit all uses of `source_is_latlong` in `liblwgeom/lwgeom_transform.c`, `libpgcommon/lwgeom_transform.c`, and `postgis/lwgeom_transform.c`; document each branch and its behavior for geocentric input
- [x] 1.2 Audit all uses of `LWFLAG_GEODETIC` flag across `liblwgeom/lwgeodetic.c`, `liblwgeom/gbox.c`, `liblwgeom/gserialized1.c`, `liblwgeom/gserialized2.c`; classify each as safe/unsafe for geocentric coordinates
- [x] 1.3 Audit unit-sphere conversion functions (`geog2cart`, `cart2geog`, `ll2cart` in `lwgeodetic.c`); document where ellipsoid-aware variants would be needed for true ECEF
- [x] 1.4 Audit GBOX range assumptions: find all coordinate range checks/clamps (e.g., lon in [-180,180], lat in [-90,90], unit-sphere [-1,1]) across `gbox.c`, `lwgeodetic.c`, and GIST operator code
- [x] 1.5 Audit `geography_distance_*`, `geography_area_*`, and `geography_length_*` functions in `postgis/geography_measurement*.c` for spheroid-only assumptions
- [x] 1.6 Audit serialization code in `gserialized1.c` and `gserialized2.c` for geodetic flag encoding; identify bits available for CRS family extension
- [x] 1.7 Compile the full audit into a markdown report at `openspec/changes/native-coordinate-systems/audit-report.md`

## 2. CRS Family Taxonomy and Gap Analysis

- [x] 2.1 Define the `LW_CRS_FAMILY` enum in a header file with values: `LW_CRS_GEOGRAPHIC`, `LW_CRS_PROJECTED`, `LW_CRS_GEOCENTRIC`, `LW_CRS_INERTIAL`, `LW_CRS_TOPOCENTRIC`, `LW_CRS_ENGINEERING`, `LW_CRS_UNKNOWN`
- [x] 2.2 Create a mapping function `lwcrs_family_from_pj_type(PJ_TYPE)` that maps PROJ type codes to the CRS family enum
- [x] 2.3 Implement `lwsrid_get_crs_family(int32_t srid)` that queries PROJ for CRS type and returns the family enum
- [x] 2.4 Build the gap analysis matrix: for each CRS family x PostGIS capability (Storage, ST_Transform, ST_Distance, ST_Area, ST_Buffer, GIST indexing, ST_AsText, ST_AsGeoJSON), classify support level as FULL/PARTIAL/PROXY/NONE/ERROR
- [x] 2.5 Document the gap analysis matrix in `openspec/changes/native-coordinate-systems/audit-report.md`

## 3. ECEF (Geocentric) First-Class Support

- [x] 3.1 Add `PJ_TYPE_GEOCENTRIC_CRS` detection to `lwproj_from_PJ` in `liblwgeom/lwgeom_transform.c` alongside existing `PJ_TYPE_GEOGRAPHIC_*` checks
- [x] 3.2 Extend `LWPROJ` struct to include `source_crs_family` and `target_crs_family` fields (using CRS family enum)
- [x] 3.3 Refactor code paths that branch on `source_is_latlong` to use `source_crs_family` comparison; verify geocentric does not trigger radian conversion
- [x] 3.4 Verify EPSG:4978 (WGS 84 geocentric) is present in `spatial_ref_sys.sql` with correct WKT and PROJ definitions
- [x] 3.5 Test `ST_Transform(geom, 4978)` round-trip: geographic (4326) -> ECEF (4978) -> geographic (4326) with sub-millimeter precision validation
- [x] 3.6 Implement ECEF-aware GBOX computation: when CRS family is geocentric, use metric Cartesian ranges instead of unit-sphere normalization
- [x] 3.7 Add ECEF-aware error handling to spatial functions: `ST_Area`, `ST_Buffer` raise errors; `ST_Distance` returns 3D Euclidean distance

## 4. Multi-CRS Type System Extension

- [x] 4.1 Add `source_crs_family` and `target_crs_family` fields to `LWPROJ` struct in `liblwgeom/liblwgeom.h.in`
- [x] 4.2 Populate CRS family fields in `lwproj_from_PJ` and `lwproj_from_str_pipeline`
- [x] 4.3 Extend `PROJSRSCacheItem` in `libpgcommon/lwgeom_transform.h` to cache CRS family alongside the SRID pair
- [x] 4.4 Verify `GSERIALIZED` on-disk format is NOT modified: CRS family is derived at runtime from SRID only
- [x] 4.5 Add `postgis_crs_family(integer)` SQL function that returns CRS family name for a given SRID
- [x] 4.6 Extend `ST_Summary` output to include CRS family when SRID is set
- [x] 4.7 Add CRS family mismatch detection to binary spatial functions (`ST_Intersects`, `ST_Contains`, etc.) with clear error messages

## 5. ECI (Inertial Frame) Foundation

- [x] 5.1 Research PROJ 9.x capabilities for dynamic datums and time-dependent transformations; document findings
- [x] 5.2 Define ECI SRID registration strategy: curated list of ICRF/J2000/TEME SRIDs or user-defined via `spatial_ref_sys`
- [x] 5.3 Implement epoch parameterization design: M-coordinate as epoch and explicit `ST_Transform` epoch parameter
- [x] 5.4 Add PROJ version compile-time gating: `#if PROJ_VERSION_MAJOR >= 9` guard for ECI features
- [x] 5.5 Implement ECI-to-ECEF transformation path with Earth rotation correction (delegating to PROJ when available)
- [x] 5.6 Add ECI bounding box computation with temporal (M) extent tracking

## 6. Testing and Validation

- [x] 6.1 Write regression tests for ECEF round-trip transformations (geographic <-> ECEF) with known control points
- [x] 6.2 Write regression tests for CRS family detection: verify EPSG:4326=geographic, EPSG:32632=projected, EPSG:4978=geocentric
- [x] 6.3 Write regression tests for spatial function error handling: verify `ST_Area(ecef_geom)` raises error, `ST_Distance(ecef1, ecef2)` returns Euclidean distance
- [x] 6.4 Write regression tests for CRS family mismatch detection in binary spatial functions
- [x] 6.5 Write regression tests for ECI epoch-parameterized transformations (if PROJ 9.x available)
- [ ] 6.6 Benchmark GIST index performance with ECEF bounding boxes on a dataset of 100K+ points; compare with geographic index performance

## 7. Documentation

- [x] 7.1 Document the CRS family taxonomy and supported coordinate systems in PostGIS user documentation
- [x] 7.2 Document ECEF usage patterns: storage, transformation, querying with examples
- [x] 7.3 Document ECI usage patterns with epoch parameterization examples
- [x] 7.4 Document migration guide: how existing users can start using ECEF/ECI coordinates
- [x] 7.5 Document the `postgis_crs_family()` SQL function and extended `ST_Summary` output
