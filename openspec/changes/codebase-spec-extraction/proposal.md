## Why

PostGIS has over 249K lines of C and SQL implementing a rich spatial database extension -- geometry types, hundreds of spatial functions, multiple index strategies, coordinate reference system management, raster support, and ISO topology. Yet none of this existing functionality is captured in machine-readable or structured specifications.

The OpenSpec workflow is already in use for this fork's new capabilities (ECEF/ECI coordinate support, multi-CRS type system, etc.), but the 11 specs in `openspec/specs/` only cover the fork's additions. Every new OpenSpec change that extends or modifies core PostGIS behavior has to describe that behavior from scratch in its delta specs, with no baseline to diff against. This makes delta specs verbose, harder to review, and impossible to validate for completeness.

Without baseline specs, there is no way to answer: "What existing behavior does this change affect?" or "Does this new spec conflict with an existing capability?"

## What Changes

This is a **documentation and analysis task only** -- no C code, SQL code, or build system modifications. The deliverable is a set of OpenSpec spec files (`spec.md`) covering core PostGIS capabilities, written into `openspec/specs/`.

### Phase 1: Core geometry types and serialization
Analyze and specify the LWGEOM type system (`liblwgeom/liblwgeom.h.in`, `liblwgeom/lwgeom.c`), GSERIALIZED on-disk format (`liblwgeom/gserialized*.c`), and serialization codecs (WKB, WKT, EWKB, EWKT, GeoJSON, KML, GML, SVG). Produce specs:
- `geometry-types` -- type hierarchy, flags (Z/M/SRID/geodetic), memory layout, and all serialization codecs (WKB/WKT/EWKB/EWKT/GeoJSON/KML/GML/SVG/TWKB round-trip behavior)
- `gserialized-format` -- on-disk wire format, version 1 vs 2, bounding box encoding

### Phase 2: Spatial operations
Analyze the most-used spatial functions and their dispatch logic. Source files: `postgis/lwgeom_functions_basic.c`, `postgis/lwgeom_geos.c`, `postgis/lwgeom_functions_analytic.c`, `postgis/geography_measurement.c`. Produce specs:
- `spatial-predicates` -- ST_Intersects, ST_Contains, ST_Within, ST_Crosses, ST_Touches, ST_Overlaps, ST_Disjoint, ST_Equals, DE-9IM
- `spatial-measurement` -- ST_Distance, ST_Length, ST_Area, ST_Perimeter (geometry and geography variants)
- `spatial-construction` -- ST_Buffer, ST_Union, ST_Intersection, ST_Difference, ST_SymDifference, ST_ConvexHull, ST_ConcaveHull
- `spatial-accessors` -- ST_X/Y/Z/M, ST_NPoints, ST_GeometryType, ST_SRID, ST_Envelope, ST_BoundingBox

### Phase 3: Spatial indexing
Analyze GiST, SP-GiST, and BRIN operator classes plus selectivity estimation. Source files: `postgis/gserialized_gist_*.c`, `postgis/gserialized_spgist_*.c`, `postgis/brin_*.c`, `postgis/gserialized_estimate.c`. Produce specs:
- `gist-spatial-index` -- GiST R-tree strategy, consistent/union/picksplit/penalty/same methods, 2D vs nD operators
- `spgist-spatial-index` -- SP-GiST quad-tree decomposition, operators
- `brin-spatial-index` -- BRIN summarization for spatial columns
- `selectivity-estimation` -- histogram-based selectivity for spatial operators, analyze functions

### Phase 4: Coordinate reference systems
Analyze SRID management and coordinate transformation. Source files: `postgis/lwgeom_transform.c`, `liblwgeom/lwgeom_transform.c`, `liblwgeom/liblwgeom.h.in` (LWPROJ). Produce specs:
- `srid-management` -- spatial_ref_sys table, SRID lookup, SRID 0 vs 4326 semantics, SRID range constraints
- `coordinate-transformation` -- ST_Transform dispatch, PROJ pipeline construction, axis ordering, grid shifts

### Phase 5: Geography type
Analyze geodesic operations on the geography type. Source files: `postgis/geography_*.c`, `liblwgeom/lwgeodetic.c`, `liblwgeom/lwspheroid.c`. Produce specs:
- `geography-type` -- geography vs geometry distinction, implicit SRID 4326, casting behavior
- `geodesic-operations` -- sphere vs spheroid dispatch, geodesic distance, geodesic area, great circle interpolation, datum handling

### Phase 6: Raster support
Analyze the raster data model and operations. Source files: `raster/rt_core/`, `raster/rt_pg/`. Produce specs:
- `raster-data-model` -- rt_raster structure, bands, pixel types, nodata, spatial reference
- `raster-operations` -- ST_Clip, ST_Union, ST_MapAlgebra, ST_Resample, raster/vector interaction
- `raster-gdal-integration` -- GDAL driver dispatch, format I/O, out-db rasters

### Phase 7: Topology
Analyze the ISO SQL/MM topology model. Source files: `topology/sql/*.sql.in`, `liblwgeom/topo/`. Produce specs:
- `topology-model` -- nodes, edges, faces, relations, TopoGeometry types, topology schema layout
- `topology-editing` -- ST_AddEdgeNewFaces, ST_RemEdgeModFace, ST_ModEdgeSplit, snap tolerance
- `topology-validation` -- ValidateTopology, topology constraints, face ring computation

### Phase 8: Extension lifecycle
Analyze CREATE EXTENSION flow, upgrade scripts, and pg_upgrade support. Source files: `extensions/`, `postgis/postgis_legacy.c`, `postgis/postgis_module.c`. Produce specs:
- `extension-lifecycle` -- CREATE EXTENSION, ALTER EXTENSION UPDATE, version discovery, dependency chain
- `upgrade-path` -- upgrade SQL generation, function stubbing, drop-before/add-after pattern, pg_upgrade compatibility
- `legacy-function-stubs` -- stub mechanism for removed functions, error messaging

## Capabilities

### New Capabilities
- Machine-readable baseline specs for core PostGIS functionality in `openspec/specs/`
- Future OpenSpec changes can write delta specs that reference existing specs by name
- Spec coverage map showing which SQL API functions are specified vs unspecified

### Modified Capabilities
_None -- this change does not modify any code or existing specs._

## Impact

- **Code**: No code changes. Zero risk of regressions.
- **Specs**: Adds approximately 20-25 new spec files under `openspec/specs/`. These are purely additive and do not modify any existing spec.
- **Build**: No build impact.
- **CI**: No CI impact.
- **Dependencies**: No dependency changes.

## Method

For each phase:

1. **Read source**: Identify the relevant C files, SQL template files, and header files. Extract function signatures, data structures, enums, and macros.
2. **Extract behavior**: Trace code paths for key operations. Identify preconditions, postconditions, error cases, and boundary behavior. Cross-reference with existing regression tests in `regress/` for concrete input/output examples.
3. **Write spec**: Produce a `spec.md` following the established pattern in `openspec/specs/` -- Purpose section, Requirements with SHALL statements, Scenarios with GIVEN/WHEN/THEN structure. Mark all requirements as ADDED since these are new specs for existing behavior.
4. **Validate against tests**: Verify that each scenario in the spec has a corresponding regression test or CUnit test that exercises the described behavior. Flag any spec claims not covered by tests.

## Success Criteria

- Specs cover 80%+ of the SQL API surface area (functions exposed in `postgis_sql.in`, `geography_sql.in`, `rtpostgis_sql.in`, `topology.sql.in`)
- Every spec requirement has at least one scenario with GIVEN/WHEN/THEN
- New OpenSpec changes can reference existing specs in their delta specs using spec names (e.g., `Extends: spatial-predicates`)
- No C or SQL code is created or modified as part of this change
