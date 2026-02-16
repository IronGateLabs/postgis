## Why

The geocentric CRS guard system protects users from silently wrong results when
spatial functions that assume planar or geographic coordinates are called with
ECEF/ECI inputs.  Guards have been added to the most common functions
(ST_Area, ST_Buffer, ST_Distance, ST_Length, ST_Perimeter, ST_Azimuth,
ST_Project, ST_Segmentize, geometry::geography cast), but several 2D
topology/analysis functions still lack them — ST_Simplify, ST_ConvexHull,
ST_DelaunayTriangles, ST_Voronoi, ST_ClosestPoint (2D), ST_ShortestLine (2D),
and ST_LineInterpolatePoint (2D) all silently produce meaningless results on
geocentric coordinates.  Additionally, the GiST 3D index benchmark exists as a
standalone script but needs expansion to cover realistic query patterns and
dataset sizes for regression tracking.

## What Changes

- Add `gserialized_check_crs_family_not_geocentric` guards to remaining 2D
  spatial functions that produce wrong results on geocentric inputs:
  ST_Simplify, ST_SimplifyPreserveTopology, ST_SimplifyPolygonHull,
  ST_CoverageSimplify, ST_ConvexHull, ST_DelaunayTriangles, ST_Voronoi,
  ST_ClosestPoint (2D variant), ST_ShortestLine (2D variant),
  ST_LineInterpolatePoint (2D variant)
- Add adaptive 3D dispatch for 2D functions that have 3D counterparts:
  ST_ClosestPoint and ST_ShortestLine (route to 3D variant when input is
  geocentric, matching ST_Distance pattern)
- Expand GiST 3D index benchmark to 100K+ point datasets with standardised
  query templates (proximity search, range scan, k-NN) and timing collection
- Add regression test coverage for the new guards (verify ERROR on ECEF input)

## Capabilities

### New Capabilities
- `geocentric-guard-expansion`: Guard coverage for remaining 2D spatial
  functions plus adaptive 3D dispatch for ClosestPoint/ShortestLine
- `gist-3d-benchmark`: Expanded GiST 3D index benchmark with 100K+ datasets,
  standardised query templates, and timing baselines

### Modified Capabilities
_(none)_

## Impact

- **Code**: ~50 lines of guard calls in `lwgeom_functions_basic.c`,
  `lwgeom_functions_analytic.c`, `lwgeom_geos.c`, `lwgeom_window.c`;
  ~20 lines of adaptive dispatch in `lwgeom_functions_basic.c`
- **Tests**: New regression test section in `regress/core/ecef_eci.sql` for
  guard error messages; expanded `regress/core/ecef_gist_benchmark.sql`
- **Behaviour**: Functions that previously returned wrong results on ECEF input
  now raise ERROR — **this is a breaking change for any user who was silently
  getting wrong results**
- **Dependencies**: None — uses existing CRS family infrastructure
