## Design: Codebase Spec Extraction

### Overview

This design describes the methodology, organization, and phasing for reverse-engineering PostGIS's existing functionality into structured OpenSpec specifications. The output is a set of `spec.md` files under `openspec/specs/`, one per capability area, covering the core PostGIS SQL API surface, internal type system, serialization formats, spatial indexing, and extension lifecycle.

No code changes are produced. The deliverable is documentation only.

---

### Extraction Methodology

For each capability area, the extraction follows a four-step process:

**Step 1: Source Identification**
Read the relevant C source files, SQL template files (`*.sql.in`), and header files. Identify:
- Public SQL functions (via `CREATE OR REPLACE FUNCTION` in `*.sql.in` files)
- C entry points (via `PG_FUNCTION_INFO_V1` macros)
- Data structures (typedefs in `liblwgeom.h.in`)
- Enums and flag macros (e.g., `LWTYPE` constants, `LWFLAG_*` macros)
- Version guards (`#if POSTGIS_GEOS_VERSION >= ...`)

**Step 2: Behavior Extraction**
Trace code paths for each public function:
- Preconditions: NULL checks, SRID validation, type constraints
- Core logic: algorithm dispatch (GEOS, PROJ, liblwgeom internal), coordinate system handling
- Postconditions: return type, SRID propagation, dimension preservation
- Error cases: what inputs trigger errors, what error messages are produced
- Edge cases: empty geometry, collection types, mixed SRIDs, Z/M handling

**Step 3: Spec Authoring**
Write a `spec.md` following the established pattern (see `openspec/specs/ecef-coordinate-support/spec.md`):
- **Purpose** section: one-paragraph description of the capability
- **Requirements** with SHALL statements: each requirement covers one behavioral contract
- **Scenarios** with GIVEN/WHEN/THEN: concrete, testable examples that exercise the requirement
- All requirements marked as ADDED (these are new specs for existing behavior)

**Step 4: Test Cross-Reference**
For each scenario, identify the regression test or CUnit test that validates the described behavior:
- Regression tests: files in `regress/core/*.sql` with expected output in `*_expected`
- CUnit tests: files in `liblwgeom/cunit/cu_*.c`
- Flag any spec claims not covered by existing tests as "untested" for future gap-filling

---

### Spec Organization

Specs are organized by functional area, not by source file. Each capability gets one directory under `openspec/specs/` containing a `spec.md`.

Rationale: A single SQL function like `ST_Distance` spans multiple source files (`lwgeom_functions_basic.c` for geometry, `geography_measurement.c` for geography, `liblwgeom/measures.c` for the algorithm). Organizing by function area keeps related behaviors together and avoids duplication.

Cross-references between specs use the directory name (e.g., "See the `geography-type` spec for geodesic dispatch rules").

---

### Planned Spec Inventory

#### `geometry-types/` -- LWGEOM Type System and Serialization Codecs

**Scope:** The LWGEOM type hierarchy (15 geometry types, POINTTYPE=1 through TINTYPE=15), POINTARRAY storage, flags byte (Z/M/SRID/Geodetic/Solid), dimension handling, and all serialization round-trip codecs (WKB, WKT, EWKB, EWKT, GeoJSON, KML, GML, SVG, TWKB, Encoded Polyline).

**Source files to analyze:**
- `liblwgeom/liblwgeom.h.in` -- type definitions: LWGEOM, LWPOINT, LWLINE, LWPOLY, LWTRIANGLE, LWCIRCSTRING, LWMPOINT, LWMLINE, LWMPOLY, LWCOLLECTION, LWPSURFACE, LWTIN, POINTARRAY, GBOX, POINT2D/3DZ/3DM/4D. Type constants: POINTTYPE (1) through TINTYPE (15). Flags: LWFLAG_Z, LWFLAG_M, LWFLAG_BBOX, LWFLAG_GEODETIC, LWFLAG_READONLY, LWFLAG_SOLID.
- `liblwgeom/lwgeom.c` -- generic LWGEOM operations: `lwgeom_clone`, `lwgeom_free`, `lwgeom_add_bbox`, `lwgeom_drop_bbox`, `lwgeom_force_2d`, `lwgeom_force_3dz`, `lwgeom_has_z`, `lwgeom_has_m`, `lwgeom_ndims`, `lwgeom_is_empty`.
- `liblwgeom/lwin_wkb.c` -- WKB/EWKB parsing (handles ISO WKB type codes and PostGIS EWKB SRID/Z/M flag encoding)
- `liblwgeom/lwout_wkb.c` -- WKB/EWKB output
- `liblwgeom/lwin_wkt.c`, `lwin_wkt_parse.c`, `lwin_wkt_lex.c` -- WKT/EWKT parser (flex/bison)
- `liblwgeom/lwout_wkt.c` -- WKT/EWKT output
- `liblwgeom/lwin_geojson.c` -- GeoJSON input
- `liblwgeom/lwout_geojson.c` -- GeoJSON output
- `liblwgeom/lwout_gml.c` -- GML 2/3 output
- `liblwgeom/lwout_kml.c` -- KML output
- `liblwgeom/lwout_svg.c` -- SVG output
- `liblwgeom/lwin_twkb.c`, `lwout_twkb.c` -- Tiny WKB (compressed format)
- `liblwgeom/lwin_encoded_polyline.c`, `lwout_encoded_polyline.c` -- Google Encoded Polyline
- `liblwgeom/lwout_x3d.c` -- X3D output

**Key regression tests:** `regress/core/wkb.sql`, `regress/core/wkt.sql`, `regress/core/binary.sql`, `regress/core/empty.sql`, `regress/core/in_geojson.sql`, `regress/core/out_geojson.sql`, `regress/core/in_gml.sql`, `regress/core/out_gml.sql`, `regress/core/in_kml.sql`, `regress/core/twkb.sql`, `regress/core/in_encodedpolyline.sql`, `regress/core/in_flatgeobuf.sql`, `regress/core/out_flatgeobuf.sql`, `regress/core/sql-mm-serialize.sql`

**Estimated requirements:** 15-20 (type hierarchy, each serialization format round-trip, dimension preservation, SRID handling, empty geometry encoding, collection nesting)

---

#### `gserialized-format/` -- On-Disk Serialization Format

**Scope:** The GSERIALIZED on-disk format used by PostgreSQL for geometry/geography storage. Version 1 vs version 2 differences, bounding box encoding, SRID packing, gflags byte, alignment, and the `gserialized.txt` specification document.

**Source files to analyze:**
- `liblwgeom/gserialized.h` -- GSERIALIZED struct definition (size, srid[3], gflags, data[1])
- `liblwgeom/gserialized.c` -- version-dispatching layer: `gserialized_get_type`, `gserialized_get_srid`, `gserialized_set_srid`, `gserialized_has_bbox`, `gserialized_has_z`, `gserialized_has_m`, `gserialized_is_geodetic`, `gserialized_get_gbox_p`, `gserialized_from_lwgeom`, `lwgeom_from_gserialized`
- `liblwgeom/gserialized1.c` -- GSERIALIZED v1 implementation (pre-3.0 format)
- `liblwgeom/gserialized1.h` -- v1 specifics
- `liblwgeom/gserialized2.c` -- GSERIALIZED v2 implementation (3.0+ default, extended flags, optional bbox)
- `liblwgeom/gserialized2.h` -- v2 specifics
- `liblwgeom/gserialized.txt` -- prose specification of the wire format

**Key regression tests:** `regress/core/binary.sql`, `regress/core/typmod.sql`, `regress/core/size.sql`

**Estimated requirements:** 8-12 (struct layout, SRID 3-byte packing, gflags encoding, bbox storage, v1 vs v2 detection, round-trip fidelity, geodetic flag, alignment padding)

---

#### `spatial-predicates/` -- Spatial Relationship Predicates

**Scope:** The DE-9IM spatial relationship model and all predicate functions: ST_Intersects, ST_Contains, ST_Within, ST_Covers, ST_CoveredBy, ST_Crosses, ST_Touches, ST_Overlaps, ST_Disjoint, ST_Equals, ST_ContainsProperly, ST_Relate, ST_RelateMatch, ST_OrderingEquals. Includes prepared geometry caching and operator-based dispatch for index support.

**Source files to analyze:**
- `postgis/postgis.sql.in` lines ~4543-4831 -- SQL function definitions for all predicates
- `postgis/lwgeom_geos.c` -- C implementations (these functions call GEOS via `GEOSIntersects_r`, `GEOSContains_r`, etc.)
- `postgis/lwgeom_geos_relatematch.c` -- ST_RelateMatch (pattern matching against DE-9IM matrix)
- `liblwgeom/lwgeom_geos.c` -- liblwgeom-level GEOS wrappers
- `postgis/lwgeom_itree.c` -- ST_IntersectsIntervalTree (optimized line/polygon intersection via segment tree)

**Key regression tests:** `regress/core/regress_ogc.sql`, `regress/core/regress_ogc_prep.sql`, `regress/core/regress_ogc_cover.sql`, `regress/core/relate.sql`, `regress/core/relate_bnr.sql`, `regress/core/relatematch.sql`, `regress/core/tickets.sql`

**Estimated requirements:** 12-15 (one per predicate function, plus DE-9IM relate matrix, pattern matching, prepared geometry caching, NULL handling, empty geometry behavior, SRID mismatch error)

---

#### `spatial-operations/` -- Spatial Construction and Processing Operations

**Scope:** Functions that produce new geometries from input geometries: ST_Buffer (with style parameters), ST_Union (pair and aggregate), ST_Intersection, ST_Difference, ST_SymDifference, ST_ConvexHull, ST_ConcaveHull, ST_SimplifyPolygonHull, ST_UnaryUnion, ST_Split, ST_Snap, ST_Node, ST_SharedPaths, ST_ClipByBox2d, ST_BuildArea, ST_Polygonize, ST_LineMerge, ST_DelaunayTriangles, ST_TriangulatePolygon, ST_Voronoi, ST_ReducePrecision, ST_MakeValid, ST_MaximumInscribedCircle, ST_LargestEmptyCircle, ST_OrientedEnvelope, ST_GeneratePoints, ST_OffsetCurve.

**Source files to analyze:**
- `postgis/lwgeom_geos.c` -- all GEOS-dispatched operations (buffer, union, intersection, difference, convexhull, concavehull, simplifypolygonhull, voronoi, delaunay, split, snap, node, sharedpaths, clipbybox2d, buildarea, polygonize, linemerge, reduceprecision, makevalid, orientedenvelope, generatepoints, offsetcurve, maximuminscribedcircle, largestemptycircle)
- `postgis/lwgeom_functions_analytic.c` -- simplify (Douglas-Peucker, Visvalingam-Whyatt), chaikin, filterm

**Key regression tests:** `regress/core/regress_buffer_params.sql`, `regress/core/concave_hull.sql`, `regress/core/concave_hull_hard.sql`, `regress/core/fixedoverlay.sql`, `regress/core/split.sql`, `regress/core/snap.sql`, `regress/core/node.sql`, `regress/core/sharedpaths.sql`, `regress/core/clipbybox2d.sql`, `regress/core/polygonize.sql`, `regress/core/delaunaytriangles.sql`, `regress/core/voronoi.sql`, `regress/core/simplify.sql`, `regress/core/simplifyvw.sql`, `regress/core/chaikin.sql`, `regress/core/coverage.sql`, `regress/core/clean.sql`, `regress/core/subdivide.sql`, `regress/core/offsetcurve.sql`, `regress/core/oriented_envelope.sql`, `regress/core/unaryunion.sql`, `regress/core/union.sql`

**Estimated requirements:** 20-25 (buffer styles, union pair vs aggregate, overlay operations SRID propagation, collection handling, empty input, NULL propagation, GEOS version guards for newer functions)

---

#### `measurement-functions/` -- Distance, Length, Area, and Perimeter

**Scope:** Measurement functions for geometry type: ST_Distance, ST_3DDistance, ST_MaxDistance, ST_DWithin, ST_3DDWithin, ST_DFullyWithin, ST_3DDFullyWithin, ST_Length, ST_Length2D, ST_3DLength, ST_Perimeter, ST_Perimeter2D, ST_Area, ST_ClosestPoint, ST_ShortestLine, ST_LongestLine, ST_3DClosestPoint, ST_3DShortestLine, ST_3DLongestLine, ST_HausdorffDistance, ST_FrechetDistance, ST_MinimumClearance, ST_MinimumClearanceLine.

**Source files to analyze:**
- `postgis/lwgeom_functions_basic.c` -- ST_Area, ST_Length, ST_Perimeter, ST_Distance, ST_3DDistance, ST_DWithin, closest/shortest/longest point/line functions (2D and 3D variants)
- `postgis/lwgeom_geos.c` -- ST_HausdorffDistance, ST_FrechetDistance, ST_MinimumClearance, ST_MinimumClearanceLine, ST_MaximumInscribedCircle, ST_LargestEmptyCircle
- `liblwgeom/measures.c` -- core distance algorithms (2D point-to-segment, segment-to-segment)
- `liblwgeom/measures3d.c` -- 3D distance algorithms

**Key regression tests:** `regress/core/measures.sql`, `regress/core/hausdorff.sql`, `regress/core/frechet.sql`, `regress/core/minimum_clearance.sql`, `regress/core/minimum_bounding_circle.sql`, `regress/core/regress_lrs.sql`

**Estimated requirements:** 12-16 (distance 2D, distance 3D, DWithin, DFullyWithin, length/perimeter for line vs polygon, area, closest/shortest/longest lines, Hausdorff, Frechet, NULL/empty handling)

---

#### `coordinate-transforms/` -- SRID Management and Coordinate Transformation

**Scope:** The `spatial_ref_sys` table, SRID validation, ST_Transform (SRID-based and pipeline-based), PROJ integration, axis ordering, grid shifts, SRID range constraints, ST_SetSRID, ST_SRID, CRS family classification.

**Source files to analyze:**
- `postgis/lwgeom_transform.c` -- PG-level ST_Transform, pipeline transforms, PROJ context management, PJ caching
- `liblwgeom/lwgeom_transform.c` -- liblwgeom-level `lwgeom_transform_from_str`, `lwproj_from_PJ`, CRS family detection
- `liblwgeom/liblwgeom.h.in` -- LWPROJ struct, LW_CRS_FAMILY enum, SRID macros (SRID_MAXIMUM, SRID_USER_MAXIMUM, SRID_UNKNOWN, SRID_DEFAULT)
- `postgis/postgis.sql.in` -- ST_Transform SQL definitions, `spatial_ref_sys` table DDL
- `spatial_ref_sys.sql` -- the default EPSG entries

**Key regression tests:** `regress/core/regress_proj_basic.sql`, `regress/core/regress_proj_adhoc.sql`, `regress/core/regress_proj_pipeline.sql`, `regress/core/regress_proj_cache_overflow.sql`, `regress/core/regress_proj_4890.sql`, `regress/core/regress_crs_family.sql`, `regress/core/regress_management.sql`

**Estimated requirements:** 10-14 (SRID lookup, ST_Transform SRID-based, ST_Transform pipeline, axis ordering, SRID propagation, SRID mismatch error, SRID range validation, CRS family detection, PROJ error handling, coordinate epoch)

---

#### `spatial-indexing/` -- GiST, SP-GiST, and BRIN Operator Classes

**Scope:** The three spatial index strategies: GiST 2D R-tree and nD R-tree, SP-GiST 2D/3D/nD quad-tree, BRIN 2D/3D/nD summarization. Operator classes (`gist_geometry_ops_2d`, `gist_geometry_ops_nd`, `spgist_geometry_ops_2d`, `spgist_geometry_ops_3d`, `spgist_geometry_ops_nd`, `brin_geometry_inclusion_ops_2d`, etc.), operator definitions (&&, @, ~, <<, >>, etc.), and selectivity/join estimation.

**Source files to analyze:**
- `postgis/gserialized_gist_2d.c` -- GiST 2D: consistent, union, compress, decompress, penalty, picksplit, same, distance operators. BOX2DF internal type.
- `postgis/gserialized_gist_nd.c` -- GiST nD: GIDX internal type, N-dimensional bounding box operations.
- `postgis/gserialized_spgist_2d.c` -- SP-GiST 2D quad-tree decomposition
- `postgis/gserialized_spgist_3d.c` -- SP-GiST 3D
- `postgis/gserialized_spgist_nd.c` -- SP-GiST nD
- `postgis/brin_2d.c`, `brin_nd.c`, `brin_common.c` -- BRIN index support
- `postgis/gserialized_estimate.c` -- selectivity estimation using 2D histograms, join selectivity
- `postgis/postgis.sql.in` -- operator and operator class definitions
- `postgis/postgis_brin.sql.in` -- BRIN operator class SQL
- `postgis/postgis_spgist.sql.in` -- SP-GiST operator class SQL

**Key regression tests:** `regress/core/regress_index.sql`, `regress/core/regress_index_nulls.sql`, `regress/core/regress_gist_index_nd.sql`, `regress/core/regress_spgist_index_2d.sql`, `regress/core/regress_spgist_index_3d.sql`, `regress/core/regress_spgist_index_nd.sql`, `regress/core/regress_brin_index.sql`, `regress/core/regress_brin_index_3d.sql`, `regress/core/regress_brin_index_geography.sql`, `regress/core/regress_selectivity.sql`, `regress/core/estimatedextent.sql`, `regress/core/knn_recheck.sql`, `regress/core/temporal_knn.sql`

**Estimated requirements:** 15-18 (GiST 2D consistent, GiST nD consistent, SP-GiST 2D, SP-GiST 3D, BRIN 2D, BRIN nD, each operator semantics, selectivity estimation, distance ordering, KNN, index-only scan support)

---

#### `geography-type/` -- Geography Type and Geodesic Operations

**Scope:** The `geography` PostgreSQL type, its distinction from `geometry`, implicit SRID 4326, casting behavior (geometry to/from geography), geodesic distance/area/length on sphere and spheroid, ST_DWithin (geography), ST_Covers (geography), ST_Intersects (geography), geography GIST/BRIN indexing, and the `use_spheroid` parameter pattern.

**Source files to analyze:**
- `postgis/geography.sql.in` -- geography type definition, casts, operators
- `postgis/geography_inout.c` -- geography I/O functions
- `postgis/geography_measurement.c` -- geography_distance, geography_dwithin, geography_intersects, geography_covers, geography_length, geography_area, geography_perimeter, geography_azimuth, geography_project
- `postgis/geography_measurement_trees.c` -- tree-accelerated geography measurements
- `postgis/geography_centroid.c` -- geography centroid
- `postgis/geography_btree.c` -- geography B-tree support
- `postgis/geography_brin.sql.in` -- geography BRIN operator class
- `liblwgeom/lwgeodetic.c` -- geodesic algorithms: edge intersections, point-in-polygon on sphere, great circle interpolation
- `liblwgeom/lwspheroid.c` -- spheroid-based calculations using PROJ geodesic API

**Key regression tests:** `regress/core/out_geography.sql`, `regress/core/geography_centroid.sql`, `regress/core/geography_covers.sql`, `regress/core/geographic.sql` (if exists), `regress/core/bestsrid.sql`, `regress/core/regress_lots_of_geographies.sql`, `regress/core/regress_brin_index_geography.sql`

**Estimated requirements:** 12-15 (geography type definition, SRID enforcement, geometry<->geography cast, geodesic distance sphere, geodesic distance spheroid, geodesic area, geodesic length, ST_DWithin geography, ST_Covers geography, ST_Intersects geography, geography index support, NULL/empty handling)

---

#### `constructors-editors/` -- Geometry Constructors, Accessors, and Editors

**Scope:** Functions that create geometries from coordinates or modify existing geometries: ST_MakePoint, ST_MakeLine, ST_MakePolygon, ST_MakeEnvelope, ST_MakeBox2D, ST_Collect, ST_Multi, ST_Force2D/3D/3DZ/3DM/4D, ST_ForceCollection, ST_ForceCurve, ST_ForceSFS, ST_SetSRID, ST_Dump, ST_DumpPoints, ST_DumpSegments, ST_DumpRings, ST_Reverse, ST_FlipCoordinates, ST_SwapOrdinates, ST_Normalize, ST_Scroll, ST_SnapToGrid, ST_RemoveRepeatedPoints, ST_RemoveIrrelevantPointsForView, ST_RemoveSmallParts, ST_SetPoint, ST_RemovePoint, ST_AddPoint, ST_WrapX, ST_QuantizeCoordinates. Accessors: ST_X, ST_Y, ST_Z, ST_M, ST_NPoints, ST_NRings, ST_NumGeometries, ST_GeometryN, ST_ExteriorRing, ST_InteriorRingN, ST_PointN, ST_StartPoint, ST_EndPoint, ST_GeometryType, ST_SRID, ST_CoordDim, ST_Dimension, ST_Envelope, ST_BoundingDiagonal, ST_Summary, ST_IsEmpty, ST_IsClosed, ST_IsRing, ST_IsSimple, ST_IsValid, ST_IsValidReason, ST_IsValidDetail, ST_IsCollection.

**Source files to analyze:**
- `postgis/lwgeom_functions_basic.c` -- most constructors, accessors, and editors
- `postgis/lwgeom_dump.c` -- ST_Dump, ST_DumpPoints, ST_DumpSegments, ST_DumpRings
- `postgis/lwgeom_ogc.c` -- OGC-compliant accessors
- `postgis/lwgeom_geos.c` -- ST_IsValid, ST_IsSimple, ST_IsRing, ST_MakeValid
- `postgis/postgis.sql.in` -- SQL definitions for all constructors/accessors

**Key regression tests:** `regress/core/ctors.sql`, `regress/core/dump.sql`, `regress/core/dumppoints.sql`, `regress/core/dumpsegments.sql`, `regress/core/empty.sql`, `regress/core/setpoint.sql`, `regress/core/removepoint.sql`, `regress/core/snap.sql`, `regress/core/snaptogrid.sql`, `regress/core/remove_repeated_points.sql`, `regress/core/remove_irrelevant_points_for_view.sql`, `regress/core/remove_small_parts.sql`, `regress/core/reverse.sql`, `regress/core/scroll.sql`, `regress/core/swapordinates.sql`, `regress/core/normalize.sql`, `regress/core/quantize_coordinates.sql`, `regress/core/wrapx.sql`, `regress/core/point_coordinates.sql`, `regress/core/iscollection.sql`, `regress/core/isvaliddetail.sql`, `regress/core/orientation.sql`, `regress/core/filterm.sql`, `regress/core/forcecurve.sql`, `regress/core/summary.sql`, `regress/core/letters.sql`

**Estimated requirements:** 25-30 (grouped by constructor functions, accessor functions, dimension coercion, dump functions, point editing, coordinate manipulation, validation functions)

---

#### `topology-model/` -- ISO SQL/MM Topology

**Scope:** The topology schema model (nodes, edges, faces, relations), TopoGeometry type, topology editing functions (ST_AddEdgeNewFaces, ST_RemEdgeModFace, ST_ModEdgeSplit, ST_NewEdgesSplit, ST_AddIsoNode, ST_AddIsoEdge, ST_MoveIsoNode, ST_RemoveIsoNode, ST_RemoveIsoEdge), topology construction (CreateTopology, DropTopology, TopologySummary, ValidateTopology), TopoGeometry operations (toTopoGeom, clearTopoGeom), and face ring computation.

**Source files to analyze:**
- `topology/sql/sqlmm.sql.in` -- ISO SQL/MM topology functions
- `topology/sql/populate.sql.in` -- topology population utilities
- `topology/sql/predicates.sql.in` -- topology spatial predicates
- `topology/sql/polygonize.sql.in` -- face computation
- `liblwgeom/topo/` -- C-level topology algorithms (if present)

**Key regression tests:** `regress/topology/` directory (separate from core tests)

**Estimated requirements:** 15-20 (topology schema creation, node/edge/face primitives, TopoGeometry CRUD, validation, editing operations, snap tolerance)

---

#### `raster-core/` -- Raster Data Model and Operations

**Scope:** The `raster` PostgreSQL type, rt_raster internal structure, bands, pixel types (1BB through 64BF), nodata values, spatial reference alignment, raster I/O (WKB), raster/vector interaction (ST_Clip, ST_Intersection with raster), raster operations (ST_Union, ST_MapAlgebra, ST_Resample, ST_Tile), band operations (ST_BandPixelType, ST_BandNoDataValue, ST_SetBandNoDataValue, ST_Value, ST_SetValue), and GDAL integration.

**Source files to analyze:**
- `raster/rt_core/librtcore.h` -- rt_raster struct, rt_band, pixel type enum, rt_context
- `raster/rt_core/librtcore_internal.h` -- internal structures
- `raster/rt_core/rt_serialize.h` -- raster WKB serialization
- `raster/rt_pg/rtpg_inout.c` -- raster I/O
- `raster/rt_pg/rtpg_raster_properties.c` -- raster property accessors
- `raster/rt_pg/rtpg_band_properties.c` -- band property accessors
- `raster/rt_pg/rtpg_pixel.c` -- pixel value access
- `raster/rt_pg/rtpg_mapalgebra.c` -- map algebra operations
- `raster/rt_pg/rtpg_spatial_relationship.c` -- raster/vector spatial predicates
- `raster/rt_pg/rtpg_geometry.c` -- raster/geometry conversion
- `raster/rt_pg/rtpg_create.c` -- raster construction
- `raster/rt_pg/rtpg_gdal.c` -- GDAL driver dispatch
- `raster/rt_pg/rtpostgis.sql.in` -- SQL API definitions

**Key regression tests:** `regress/raster/` directory

**Estimated requirements:** 20-25 (raster struct, band model, pixel types, nodata, spatial reference, WKB round-trip, value access, map algebra, raster/vector clip, GDAL I/O, resampling)

---

#### `extension-lifecycle/` -- CREATE EXTENSION, Upgrades, and Legacy Stubs

**Scope:** The `postgis`, `postgis_raster`, `postgis_topology`, `postgis_sfcgal` extension control files, CREATE EXTENSION flow, ALTER EXTENSION UPDATE paths, version discovery functions (postgis_version, postgis_full_version, postgis_geos_version, postgis_proj_version), upgrade SQL generation (`utils/create_upgrade.pl`), drop-before/add-after pattern, legacy function stubs for pg_upgrade compatibility, and the extension dependency chain.

**Source files to analyze:**
- `extensions/postgis/` -- control file, Makefile
- `extensions/postgis_raster/`, `extensions/postgis_topology/`, `extensions/postgis_sfcgal/` -- additional extension dirs
- `extensions/postgis_extension_helper.sql.in` -- helper functions for extension management
- `extensions/upgrade-paths-rules.mk` -- upgrade path generation rules
- `extensions/upgradeable_versions.mk` -- list of supported upgrade-from versions
- `postgis/postgis_legacy.c` -- legacy function stubs (stubbed-out functions that raise deprecation errors)
- `sfcgal/postgis_sfcgal_legacy.c` -- SFCGAL legacy stubs
- `raster/rt_pg/rtpg_legacy.c` -- raster legacy stubs
- `postgis/common_before_upgrade.sql`, `postgis/common_after_upgrade.sql` -- pre/post upgrade hooks
- `postgis/lwgeom_functions_basic.c` -- version discovery functions (postgis_version, postgis_lib_version, etc.)
- `utils/create_upgrade.pl` -- Perl script that generates upgrade SQL from diff of versions

**Key regression tests:** upgrade regression tests (via `make check RUNTESTFLAGS="--upgrade"`)

**Estimated requirements:** 10-14 (CREATE EXTENSION flow, ALTER EXTENSION UPDATE, version discovery, upgrade SQL generation, legacy stub mechanism, drop-before pattern, dependency chain, control file format)

---

### Quality Criteria

Each extracted spec must meet the following quality bar:

1. **Minimum scenarios:** Every requirement SHALL have at least 3 scenarios covering: (a) normal/happy path, (b) edge case (NULL, empty geometry, SRID 0), and (c) error case or boundary condition.

2. **Concrete values:** Scenarios SHALL use concrete WKT/EWKT geometries and expected output values, not abstract descriptions. Example: "GIVEN a point `POINT(1 2)` with SRID 4326" rather than "GIVEN a point with some coordinates".

3. **Test cross-reference:** Each scenario SHALL note the regression test file that validates it, or be flagged as "untested" if no existing test covers the behavior.

4. **SRID and dimension coverage:** Every spec covering spatial functions SHALL include scenarios for: SRID 0 (unknown), SRID 4326, mixed SRIDs (error case), 2D geometry, 3DZ geometry, and 3DM geometry where applicable.

5. **NULL propagation:** Every spec covering SQL functions SHALL document NULL-in/NULL-out behavior.

6. **Empty geometry handling:** Every spec covering spatial functions SHALL document behavior when one or both inputs are empty geometries (e.g., `GEOMETRYCOLLECTION EMPTY`).

7. **Version guards:** Requirements that depend on specific GEOS, PROJ, or PostgreSQL versions SHALL note the version guard macro (e.g., "Requires POSTGIS_GEOS_VERSION >= 31300").

---

### Phasing

Extraction proceeds in phases, with each phase building on the specs from prior phases. Earlier phases cover foundational types and formats; later phases cover higher-level operations that reference the foundational specs.

| Phase | Specs | Rationale |
|-------|-------|-----------|
| 1 -- Foundation | `geometry-types`, `gserialized-format` | All other specs reference the type system and on-disk format |
| 2 -- Core Operations | `spatial-predicates`, `spatial-operations`, `measurement-functions` | The most-used SQL API functions, all GEOS-dispatched |
| 3 -- Infrastructure | `coordinate-transforms`, `spatial-indexing` | Cross-cutting concerns referenced by predicates and operations |
| 4 -- Advanced Types | `geography-type`, `constructors-editors` | Geography builds on geometry-types; constructors/editors complete the SQL API |
| 5 -- Extensions | `topology-model`, `raster-core` | Separate PostgreSQL extensions with their own type systems |
| 6 -- Lifecycle | `extension-lifecycle` | Extension packaging and upgrade mechanics |
| 7 -- Validation | (cross-reference pass) | Verify all specs against regression tests, identify and fill gaps |

Within each phase, specs can be extracted in parallel since they cover non-overlapping source files.

---

### Relationship to Existing Specs

The 11 existing specs in `openspec/specs/` cover fork-specific ECEF/ECI additions (e.g., `ecef-coordinate-support`, `eci-coordinate-support`, `multi-crs-type-system`). The new baseline specs describe the upstream PostGIS behavior that those fork specs extend.

Once baseline specs exist, fork-specific change specs can use `Extends: geometry-types` or `Modifies: coordinate-transforms` references to clearly delineate what existing behavior they alter.

No existing spec files are modified by this change.
