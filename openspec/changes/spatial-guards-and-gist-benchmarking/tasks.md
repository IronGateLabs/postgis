## 1. Spatial Function Guards — C Implementation

- [x] 1.1 Add `gserialized_check_crs_family_not_geocentric(geom, "ST_Perimeter")` to `LWGEOM_perimeter_poly` in `postgis/lwgeom_functions_basic.c`, before `lwgeom_from_gserialized`
- [x] 1.2 Add `gserialized_check_crs_family_not_geocentric(geom, "ST_Perimeter")` to `LWGEOM_perimeter2d_poly` in `postgis/lwgeom_functions_basic.c`, before `lwgeom_from_gserialized`
- [x] 1.3 Add `srid_check_crs_family_not_geocentric(srid, "ST_Azimuth")` to `LWGEOM_azimuth` in `postgis/lwgeom_functions_basic.c`, after SRID extraction
- [x] 1.4 Add `gserialized_check_crs_family_not_geocentric(geom1, "ST_Project")` to `geometry_project_direction` in `postgis/lwgeom_functions_basic.c`, after parameter extraction
- [x] 1.5 Add `gserialized_check_crs_family_not_geocentric(geom1, "ST_Project")` to `geometry_project_geometry` in `postgis/lwgeom_functions_basic.c`, after parameter extraction
- [x] 1.6 Add `gserialized_check_crs_family_not_geocentric(ingeom, "ST_Segmentize")` to `LWGEOM_segmentize2d` in `postgis/lwgeom_functions_basic.c`, after parameter extraction

## 2. Geography Cast Guard

- [x] 2.1 Add `srid_check_crs_family_not_geocentric(lwgeom->srid, "geometry::geography")` to `geography_from_geometry` in `postgis/geography_inout.c`, before the existing `srid_check_latlong` call

## 3. Regression Tests — Guards

- [x] 3.1 Add regression tests to `regress/core/ecef_eci.sql` verifying ST_Perimeter on SRID 4978 polygon raises error containing "geocentric"
- [x] 3.2 Add regression test verifying ST_Azimuth on SRID 4978 points raises error containing "geocentric"
- [x] 3.3 Add regression test verifying ST_Project (direction variant) on SRID 4978 point raises error containing "geocentric"
- [x] 3.4 Add regression test verifying ST_Project (geometry variant) on SRID 4978 points raises error containing "geocentric"
- [x] 3.5 Add regression test verifying ST_Segmentize on SRID 4978 linestring raises error containing "geocentric"
- [x] 3.6 Add regression test verifying `geom::geography` cast on SRID 4978 geometry raises error containing "geocentric"
- [x] 3.7 Add negative tests verifying ST_Perimeter, ST_Azimuth, ST_Project, ST_Segmentize still work on SRID 4326 and SRID 32632 input
- [x] 3.8 Update expected output file `regress/core/ecef_eci_expected` with new test results

## 4. Docker Build Validation

- [x] 4.1 Build in Docker container (`postgis/postgis-build-env:pg16-geos312-gdal37-proj921`), run `make` and `make check`
- [x] 4.2 Run `make installcheck` and verify all regression tests pass including new guard tests

## 5. GiST Benchmark — SQL Script

- [x] 5.1 Create `regress/core/ecef_gist_benchmark.sql` with ECEF point generation at 10K, 50K, 100K scale using `generate_series` and `ST_Transform(ST_SetSRID(ST_MakePoint(lon, lat, 0), 4326), 4978)`
- [x] 5.2 Add GiST ND index creation with `clock_timestamp()` timing for ECEF tables at each scale
- [x] 5.3 Add ECI (SRID 900001) point generation and index creation at 100K scale with timing
- [x] 5.4 Add geographic baseline: geography table with 100K points and GiST index with timing
- [x] 5.5 Add `ST_3DDWithin` range query benchmarks with `EXPLAIN ANALYZE` on ECEF, ECI, and geographic tables
- [x] 5.6 Add `&&&` bounding box overlap query benchmarks with `EXPLAIN ANALYZE`

## 6. GiST Correctness — ST_3DDistance and Mixed-SRID Safety

- [x] 6.1 Add regression test comparing `ST_3DDistance` results with index scan vs sequential scan (`SET enable_indexscan = off`) on 10K ECEF points
- [x] 6.2 Add regression test verifying ECEF (4978) vs geographic (4326) SRID mismatch in `ST_3DDWithin` raises error
- [x] 6.3 Add regression test verifying ECEF (4978) vs ECI (900001) SRID mismatch in `ST_3DDWithin` raises error
- [x] 6.4 Add regression test verifying same-SRID ECI (900001) `ST_3DDWithin` query succeeds
- [x] 6.5 Update expected output files for new benchmark/correctness tests

## 7. Final Validation

- [x] 7.1 Full Docker build and `make installcheck` with all new tests passing
- [x] 7.2 Verify no regressions in existing ecef_eci test suite
