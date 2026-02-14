## 1. source_is_latlong Refactoring

- [x] 1.1 Reimplement `lwproj_is_latlong()` in `libpgcommon/lwgeom_transform.c` to return `pj->source_crs_family == LW_CRS_GEOGRAPHIC` instead of `pj->source_is_latlong`
- [x] 1.2 Update `srid_axis_precision()` in `libpgcommon/lwgeom_transform.c` to branch on `srid_get_crs_family(srid) == LW_CRS_GEOGRAPHIC` instead of `srid_is_latlong(srid)`
- [x] 1.3 Add `/* DEPRECATED: use source_crs_family instead */` annotation to the `source_is_latlong` field in `liblwgeom/liblwgeom.h.in:78`
- [x] 1.4 Verify `srid_check_latlong()` behavior is unchanged after the `lwproj_is_latlong` migration (it calls `srid_is_latlong` which calls `lwproj_is_latlong`)
- [x] 1.5 Update unit tests in `cu_crs_family.c` to verify `lwproj_is_latlong()` returns correct results based on CRS family for geographic, projected, geocentric, and inertial SRIDs
- [x] 1.6 Grep for any remaining direct reads of `->source_is_latlong` outside of the LWPROJ population code and migrate them

## 2. Spatial Function Error Guards

- [x] 2.1 Add `gserialized_check_crs_family_not_geocentric(g, "ST_Buffer")` call at the top of `ST_Buffer` implementation in `postgis/lwgeom_geos.c`
- [x] 2.2 Add `gserialized_check_crs_family_not_geocentric(g, "ST_Area")` call at the top of `ST_Area` implementation
- [x] 2.3 Add `gserialized_check_crs_family_not_geocentric(g, "ST_Centroid")` call at the top of `ST_Centroid` implementation
- [x] 2.4 Add `gserialized_check_crs_family_not_geocentric(g, "ST_OffsetCurve")` call at the top of `ST_OffsetCurve` implementation
- [x] 2.5 Add `gserialized_check_crs_family_not_geocentric(g, "ST_BuildArea")` call at the top of `ST_BuildArea` implementation
- [x] 2.6 Add regression tests that verify each guarded function raises the expected error when given a geometry with SRID 4978

## 3. Spatial Function CRS Dispatch

- [x] 3.1 Add CRS family check to `ST_Distance`: when both inputs have `LW_CRS_GEOCENTRIC` family, compute 3D Euclidean distance instead of the default 2D distance
- [x] 3.2 Add CRS family check to `ST_Length`: when input has `LW_CRS_GEOCENTRIC` family, compute 3D Euclidean length
- [x] 3.3 Add CRS family mismatch detection to binary spatial functions (`ST_Intersects`, `ST_Contains`, `ST_Within`, `ST_Distance`): when SRIDs differ and CRS families also differ, raise an error
- [x] 3.4 Add regression tests for 3D Euclidean distance correctness with known ECEF coordinate pairs
- [x] 3.5 Add regression tests for CRS family mismatch errors

## 4. Verification of Implemented Requirements

- [x] 4.1 Verify requirement 1 (CRS family metadata in LWPROJ): confirm `source_crs_family` and `target_crs_family` are correctly populated for geographic, projected, geocentric, and pipeline transforms by reviewing existing `cu_crs_family.c` tests
- [x] 4.2 Verify requirement 3 (PROJ cache includes CRS family): confirm `srid_get_crs_family()` returns correct family after cache hit by adding a test that calls it twice for the same SRID
- [x] 4.3 Verify requirement 4 (GSERIALIZED compatibility): confirm no on-disk format changes by running `pg_dump`/`pg_restore` round-trip test with mixed-CRS geometries
- [x] 4.4 Verify requirement 6 (documentation/metadata): confirm `postgis_crs_family(4978)` returns 'geocentric' and `ST_Summary` includes CRS family for geocentric geometries
