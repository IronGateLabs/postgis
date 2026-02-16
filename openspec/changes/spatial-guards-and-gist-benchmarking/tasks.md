# Tasks: Spatial Guards and GiST Benchmarking

## 1. Geocentric Guards — Reject

- [x] 1.1 Add guard to `LWGEOM_simplify2d` (ST_Simplify) in `postgis/lwgeom_functions_analytic.c`
- [x] 1.2 Add guard to `topologypreservesimplify` (ST_SimplifyPreserveTopology) in `postgis/lwgeom_geos.c`
- [x] 1.3 Add guard to `ST_SimplifyPolygonHull` in `postgis/lwgeom_geos.c`
- [x] 1.4 Add guard to `ST_CoverageSimplify` in `postgis/lwgeom_window.c`
- [x] 1.5 Add guard to `convexhull` (ST_ConvexHull) in `postgis/lwgeom_geos.c`
- [x] 1.6 Add guard to `ST_DelaunayTriangles` in `postgis/lwgeom_geos.c`
- [x] 1.7 Add guard to `ST_Voronoi` in `postgis/lwgeom_geos.c`
- [x] 1.8 Add guard to `LWGEOM_line_interpolate_point` (ST_LineInterpolatePoint 2D) in `postgis/lwgeom_functions_analytic.c`

## 2. Geocentric Guards — Adaptive 3D Dispatch

- [x] 2.1 Add adaptive 3D dispatch to `LWGEOM_closestpoint` (ST_ClosestPoint 2D) — route to `lwgeom_closestpoint3d` when input SRID is geocentric
- [x] 2.2 Add adaptive 3D dispatch to `LWGEOM_shortestline2d` (ST_ShortestLine 2D) — route to `lwgeom_shortestline3d` when input SRID is geocentric

## 3. Regression Tests for Guards

- [x] 3.1 Add guard error tests to `regress/core/ecef_eci.sql` — verify ERROR raised for each guarded function with ECEF (SRID 4978) input
- [x] 3.2 Add guard error tests for ECI (SRID 900001) input on a subset of guarded functions
- [x] 3.3 Add adaptive dispatch tests — verify ST_ClosestPoint and ST_ShortestLine return correct 3D results for ECEF input
- [x] 3.4 Update expected output files for the new tests

## 4. GiST 3D Benchmark Expansion

- [ ] 4.1 Expand `regress/core/ecef_gist_benchmark.sql` dataset generation to support 100K and 500K scales with LEO/MEO/GEO distribution
- [ ] 4.2 Add proximity search template (`ST_3DDWithin` with index) with timing
- [ ] 4.3 Add k-NN search template (`ORDER BY <#> LIMIT k`) with timing
- [ ] 4.4 Add range scan template (`&&&` 3D bbox operator) with timing
- [ ] 4.5 Add multi-run timing collection (5 runs per template, report median/p95/stddev)
