## 1. Verify CRS Family Enum Completeness

- [x] 1.1 Compare `LW_CRS_FAMILY` enum values against the full `PJ_TYPE` enumeration in the linked PROJ version header, documenting any unmapped types
- [x] 1.2 Verify `lwcrs_family_from_pj_type()` handles all `PJ_TYPE` values present in the project's minimum supported PROJ version
- [x] 1.3 Add unit test asserting enum stability (integer values must not change across versions)

## 2. Audit source_is_latlong Usage

- [x] 2.1 Grep all references to `source_is_latlong` in `liblwgeom/` and `libpgcommon/` and document each call site with file, function, line, and whether the branch would produce incorrect results for geocentric input
- [x] 2.2 Grep all references to `lwproj_is_latlong`, `srid_is_latlong`, and `srid_check_latlong` and classify each as: already refactored, needs refactoring, or correctly geographic-only
- [x] 2.3 Document which call sites have been augmented with `source_crs_family` checks vs. which still rely solely on the boolean flag

## 3. Audit LWFLAG_GEODETIC and Unit-Sphere Assumptions

- [x] 3.1 Grep all references to `FLAGS_GET_GEODETIC` and `FLAGS_SET_GEODETIC` and document which functions assume geodetic flag implies geographic coordinates
- [x] 3.2 Audit `geog2cart`, `cart2geog`, `ll2cart` call sites and classify which assume unit-sphere vs. which could handle ellipsoidal ECEF
- [x] 3.3 Document coordinate wrapping/normalization code that assumes longitude [-180,180] and latitude [-90,90], listing file and function for each occurrence
- [x] 3.4 Audit `geography_distance_*` functions for spheroidal-only assumptions

## 4. Produce Gap Analysis Matrix

- [x] 4.1 Test Storage capability for each CRS family (Geographic, Projected, Geocentric, Inertial, Topocentric, Engineering) by inserting and retrieving geometries with representative SRIDs
- [x] 4.2 Test ST_Transform capability for each CRS family pair, documenting which transforms succeed, fail, or produce incorrect results
- [x] 4.3 Test ST_Distance on same-CRS pairs for each family, documenting whether 2D, 3D, or geodesic distance is returned
- [x] 4.4 Test ST_Area on polygons for each CRS family, documenting errors vs. results
- [x] 4.5 Test GiST indexing (2D and 3D) for each CRS family, documenting index creation and query plan usage
- [x] 4.6 Test ST_AsText, ST_AsGeoJSON, ST_AsBinary for each CRS family, documenting output correctness
- [x] 4.7 Compile results into the gap matrix with FULL/PARTIAL/PROXY/NONE/ERROR classifications and supporting evidence

## 5. Documentation

- [x] 5.1 Write audit report as structured markdown in the change directory, organized by code area
- [x] 5.2 Write gap analysis matrix document with per-cell evidence references
- [x] 5.3 Add any missing test scenarios to `cu_crs_family.c` discovered during the audit
