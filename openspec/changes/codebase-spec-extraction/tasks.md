## Tasks: Codebase Spec Extraction

### Phase 1: Foundation

These specs are prerequisites for all subsequent phases. They define the type system and on-disk format that every other spec references.

- [x] **Extract `geometry-types` spec** (completed 2026-04-03)
  - Read `liblwgeom/liblwgeom.h.in` (type constants POINTTYPE through TINTYPE, LWGEOM/LWPOINT/LWLINE/LWPOLY/LWCOLLECTION structs, POINTARRAY, flags byte, POINT2D/3DZ/3DM/4D)
  - Read `liblwgeom/lwgeom.c` (generic operations: clone, free, add_bbox, drop_bbox, force_2d/3dz, has_z, has_m, ndims, is_empty)
  - Read all serialization codecs: `liblwgeom/lwin_wkb.c`, `lwout_wkb.c`, `lwin_wkt.c`, `lwout_wkt.c`, `lwin_geojson.c`, `lwout_geojson.c`, `lwout_gml.c`, `lwout_kml.c`, `lwout_svg.c`, `lwin_twkb.c`, `lwout_twkb.c`, `lwin_encoded_polyline.c`, `lwout_encoded_polyline.c`, `lwout_x3d.c`
  - Cross-reference with regression tests: `regress/core/wkb.sql`, `wkt.sql`, `binary.sql`, `empty.sql`, `in_geojson.sql`, `out_geojson.sql`, `in_gml.sql`, `out_gml.sql`, `in_kml.sql`, `twkb.sql`, `in_encodedpolyline.sql`, `sql-mm-serialize.sql`, `in_flatgeobuf.sql`, `out_flatgeobuf.sql`
  - Write `openspec/specs/geometry-types/spec.md` with 15-20 requirements covering type hierarchy, each codec round-trip, dimension preservation, SRID handling, empty geometry encoding, collection nesting
  - Validate every scenario has a test reference or is flagged untested
  - **Result:** 17 requirements, 39 scenarios (30 tested, 9 untested)

- [x] **Extract `gserialized-format` spec** (completed 2026-04-03)
  - Read `liblwgeom/gserialized.h`, `gserialized.c` (version dispatch layer), `gserialized1.c`/`gserialized1.h` (v1), `gserialized2.c`/`gserialized2.h` (v2)
  - Read `liblwgeom/gserialized.txt` (prose format specification)
  - Cross-reference with regression tests: `regress/core/binary.sql`, `typmod.sql`, `size.sql`
  - Write `openspec/specs/gserialized-format/spec.md` with 8-12 requirements covering struct layout, SRID 3-byte packing, gflags encoding, bbox storage, v1 vs v2 detection, round-trip fidelity, geodetic flag, alignment
  - Validate every scenario has a test reference or is flagged untested
  - **Result:** 10 requirements, 30 scenarios (22 tested, 8 untested)

### Phase 2: Core Operations

These specs cover the most-used SQL API functions. They depend on Phase 1 for type definitions.

- [ ] **Extract `spatial-predicates` spec**
  - Read `postgis/postgis.sql.in` lines 4543-4831 (ST_Relate, ST_Disjoint, ST_Touches, ST_Intersects, ST_Crosses, ST_Contains, ST_ContainsProperly, ST_Within, ST_Covers, ST_CoveredBy, ST_Overlaps, ST_Equals, ST_OrderingEquals)
  - Read `postgis/lwgeom_geos.c` (C implementations dispatching to GEOS: GEOSIntersects_r, GEOSContains_r, etc.)
  - Read `postgis/lwgeom_geos_relatematch.c` (ST_RelateMatch DE-9IM pattern matching)
  - Read `postgis/lwgeom_itree.c` (ST_IntersectsIntervalTree optimized path)
  - Cross-reference with regression tests: `regress/core/regress_ogc.sql`, `regress_ogc_prep.sql`, `regress_ogc_cover.sql`, `relate.sql`, `relate_bnr.sql`, `relatematch.sql`
  - Write `openspec/specs/spatial-predicates/spec.md` with 12-15 requirements
  - Validate every scenario has a test reference or is flagged untested

- [ ] **Extract `spatial-operations` spec**
  - Read `postgis/lwgeom_geos.c` (buffer, union, intersection, difference, symdifference, convexhull, concavehull, simplifypolygonhull, unaryunion, split, snap, node, sharedpaths, clipbybox2d, buildarea, polygonize, linemerge, delaunaytriangles, triangulatepoly, voronoi, reduceprecision, makevalid, orientedenvelope, generatepoints, offsetcurve, maximuminscribedcircle, largestemptycircle)
  - Read `postgis/lwgeom_functions_analytic.c` (simplify Douglas-Peucker, Visvalingam-Whyatt, Chaikin)
  - Cross-reference with regression tests: `regress/core/regress_buffer_params.sql`, `concave_hull.sql`, `concave_hull_hard.sql`, `fixedoverlay.sql`, `split.sql`, `snap.sql`, `node.sql`, `sharedpaths.sql`, `clipbybox2d.sql`, `polygonize.sql`, `delaunaytriangles.sql`, `voronoi.sql`, `simplify.sql`, `simplifyvw.sql`, `chaikin.sql`, `coverage.sql`, `clean.sql`, `subdivide.sql`, `offsetcurve.sql`, `oriented_envelope.sql`, `unaryunion.sql`, `union.sql`
  - Write `openspec/specs/spatial-operations/spec.md` with 20-25 requirements
  - Validate every scenario has a test reference or is flagged untested

- [ ] **Extract `measurement-functions` spec**
  - Read `postgis/lwgeom_functions_basic.c` (ST_Area, ST_Length, ST_Length2D, ST_3DLength, ST_Perimeter, ST_Perimeter2D, ST_Distance, ST_3DDistance, ST_DWithin, ST_3DDWithin, ST_DFullyWithin, ST_3DDFullyWithin, closest/shortest/longest point/line 2D and 3D)
  - Read `postgis/lwgeom_geos.c` (ST_HausdorffDistance, ST_FrechetDistance, ST_MinimumClearance, ST_MinimumClearanceLine)
  - Read `liblwgeom/measures.c` (core 2D distance algorithms), `liblwgeom/measures3d.c` (3D distance algorithms)
  - Cross-reference with regression tests: `regress/core/measures.sql`, `hausdorff.sql`, `frechet.sql`, `minimum_clearance.sql`, `minimum_bounding_circle.sql`, `regress_lrs.sql`
  - Write `openspec/specs/measurement-functions/spec.md` with 12-16 requirements
  - Validate every scenario has a test reference or is flagged untested

### Phase 3: Infrastructure

Cross-cutting concerns referenced by predicates and operations.

- [ ] **Extract `coordinate-transforms` spec**
  - Read `postgis/lwgeom_transform.c` (PG-level ST_Transform, pipeline transforms, PROJ context management, PJ caching)
  - Read `liblwgeom/lwgeom_transform.c` (lwgeom_transform_from_str, lwproj_from_PJ, CRS family detection, source_is_latlong)
  - Read `liblwgeom/liblwgeom.h.in` (LWPROJ struct, LW_CRS_FAMILY enum, SRID macros)
  - Read `postgis/postgis.sql.in` (ST_Transform definitions, spatial_ref_sys table DDL)
  - Cross-reference with regression tests: `regress/core/regress_proj_basic.sql`, `regress_proj_adhoc.sql`, `regress_proj_pipeline.sql`, `regress_proj_cache_overflow.sql`, `regress_proj_4890.sql`, `regress_crs_family.sql`, `regress_management.sql`
  - Write `openspec/specs/coordinate-transforms/spec.md` with 10-14 requirements
  - Validate every scenario has a test reference or is flagged untested

- [ ] **Extract `spatial-indexing` spec**
  - Read `postgis/gserialized_gist_2d.c` (GiST 2D: BOX2DF, consistent, union, compress, penalty, picksplit, same, distance)
  - Read `postgis/gserialized_gist_nd.c` (GiST nD: GIDX, N-dimensional bounding box ops)
  - Read `postgis/gserialized_spgist_2d.c`, `gserialized_spgist_3d.c`, `gserialized_spgist_nd.c` (SP-GiST quad-tree)
  - Read `postgis/brin_2d.c`, `brin_nd.c`, `brin_common.c` (BRIN summarization)
  - Read `postgis/gserialized_estimate.c` (selectivity estimation, 2D histograms, join selectivity)
  - Read `postgis/postgis.sql.in` (operator and operator class definitions), `postgis_brin.sql.in`, `postgis_spgist.sql.in`
  - Cross-reference with regression tests: `regress/core/regress_index.sql`, `regress_index_nulls.sql`, `regress_gist_index_nd.sql`, `regress_spgist_index_2d.sql`, `regress_spgist_index_3d.sql`, `regress_spgist_index_nd.sql`, `regress_brin_index.sql`, `regress_brin_index_3d.sql`, `regress_brin_index_geography.sql`, `regress_selectivity.sql`, `estimatedextent.sql`, `knn_recheck.sql`, `temporal_knn.sql`
  - Write `openspec/specs/spatial-indexing/spec.md` with 15-18 requirements
  - Validate every scenario has a test reference or is flagged untested

### Phase 4: Advanced Types

Geography builds on geometry-types; constructors/editors complete the SQL API surface.

- [ ] **Extract `geography-type` spec**
  - Read `postgis/geography.sql.in` (geography type definition, casts, operators)
  - Read `postgis/geography_inout.c` (geography I/O)
  - Read `postgis/geography_measurement.c` (distance, dwithin, intersects, covers, length, area, perimeter, azimuth, project)
  - Read `postgis/geography_measurement_trees.c` (tree-accelerated measurements)
  - Read `postgis/geography_centroid.c`, `geography_btree.c`, `geography_brin.sql.in`
  - Read `liblwgeom/lwgeodetic.c` (geodesic algorithms on sphere), `liblwgeom/lwspheroid.c` (spheroid calculations)
  - Cross-reference with regression tests: `regress/core/out_geography.sql`, `geography_centroid.sql`, `geography_covers.sql`, `bestsrid.sql`, `regress_lots_of_geographies.sql`, `regress_brin_index_geography.sql`
  - Write `openspec/specs/geography-type/spec.md` with 12-15 requirements
  - Validate every scenario has a test reference or is flagged untested

- [ ] **Extract `constructors-editors` spec**
  - Read `postgis/lwgeom_functions_basic.c` (ST_MakePoint, ST_MakeLine, ST_MakePolygon, ST_MakeEnvelope, ST_Collect, ST_Multi, ST_Force2D/3DZ/3DM/4D, ST_ForceCollection, ST_ForceCurve, ST_ForceSFS, ST_SetSRID, ST_Reverse, ST_FlipCoordinates, ST_SwapOrdinates, ST_Normalize, ST_Scroll, ST_SnapToGrid, ST_RemoveRepeatedPoints, ST_SetPoint, ST_RemovePoint, ST_AddPoint, ST_WrapX, ST_QuantizeCoordinates, all accessors: ST_X/Y/Z/M, ST_NPoints, ST_NRings, ST_NumGeometries, ST_GeometryN, ST_ExteriorRing, ST_InteriorRingN, ST_PointN, ST_StartPoint, ST_EndPoint, ST_GeometryType, ST_SRID, ST_CoordDim, ST_Dimension, ST_Envelope, ST_Summary, ST_IsEmpty, ST_IsClosed)
  - Read `postgis/lwgeom_dump.c` (ST_Dump, ST_DumpPoints, ST_DumpSegments, ST_DumpRings)
  - Read `postgis/lwgeom_ogc.c` (OGC-compliant accessors)
  - Read `postgis/lwgeom_geos.c` (ST_IsValid, ST_IsSimple, ST_IsRing, ST_MakeValid)
  - Cross-reference with regression tests: `regress/core/ctors.sql`, `dump.sql`, `dumppoints.sql`, `dumpsegments.sql`, `empty.sql`, `setpoint.sql`, `removepoint.sql`, `snaptogrid.sql`, `remove_repeated_points.sql`, `remove_irrelevant_points_for_view.sql`, `remove_small_parts.sql`, `reverse.sql`, `scroll.sql`, `swapordinates.sql`, `normalize.sql`, `quantize_coordinates.sql`, `wrapx.sql`, `point_coordinates.sql`, `iscollection.sql`, `isvaliddetail.sql`, `orientation.sql`, `filterm.sql`, `forcecurve.sql`, `summary.sql`, `letters.sql`
  - Write `openspec/specs/constructors-editors/spec.md` with 25-30 requirements
  - Validate every scenario has a test reference or is flagged untested

### Phase 5: Extensions

Separate PostgreSQL extensions with their own type systems and SQL APIs.

- [ ] **Extract `topology-model` spec**
  - Read `topology/sql/sqlmm.sql.in` (ISO SQL/MM topology functions: ST_AddEdgeNewFaces, ST_RemEdgeModFace, ST_ModEdgeSplit, ST_NewEdgesSplit, ST_AddIsoNode, ST_AddIsoEdge, ST_MoveIsoNode, ST_RemoveIsoNode, ST_RemoveIsoEdge, ST_GetFaceEdges, ST_GetFaceGeometry)
  - Read `topology/sql/populate.sql.in` (CreateTopology, DropTopology, TopologySummary, toTopoGeom, clearTopoGeom)
  - Read `topology/sql/predicates.sql.in` (topology spatial predicates)
  - Read `topology/sql/polygonize.sql.in` (face ring computation, ValidateTopology)
  - Cross-reference with regression tests in `regress/topology/` directory
  - Write `openspec/specs/topology-model/spec.md` with 15-20 requirements covering topology schema creation, node/edge/face primitives, TopoGeometry CRUD, validation, editing operations, snap tolerance
  - Validate every scenario has a test reference or is flagged untested

- [ ] **Extract `raster-core` spec**
  - Read `raster/rt_core/librtcore.h` (rt_raster, rt_band, pixel type enum, rt_context)
  - Read `raster/rt_core/rt_serialize.h` (raster WKB format)
  - Read `raster/rt_pg/rtpg_inout.c`, `rtpg_raster_properties.c`, `rtpg_band_properties.c`, `rtpg_pixel.c`, `rtpg_mapalgebra.c`, `rtpg_spatial_relationship.c`, `rtpg_geometry.c`, `rtpg_create.c`, `rtpg_gdal.c`
  - Read `raster/rt_pg/rtpostgis.sql.in` (SQL API surface)
  - Cross-reference with regression tests in `regress/raster/` directory
  - Write `openspec/specs/raster-core/spec.md` with 20-25 requirements covering raster struct, band model, pixel types, nodata, spatial reference, WKB round-trip, value access, map algebra, raster/vector clip, GDAL I/O
  - Validate every scenario has a test reference or is flagged untested

### Phase 6: Lifecycle

Extension packaging and upgrade mechanics.

- [ ] **Extract `extension-lifecycle` spec**
  - Read `extensions/postgis/` (control file, Makefile), `extensions/postgis_raster/`, `extensions/postgis_topology/`, `extensions/postgis_sfcgal/`
  - Read `extensions/postgis_extension_helper.sql.in` (helper functions)
  - Read `extensions/upgrade-paths-rules.mk`, `extensions/upgradeable_versions.mk`
  - Read `postgis/postgis_legacy.c` (legacy stubs), `sfcgal/postgis_sfcgal_legacy.c`, `raster/rt_pg/rtpg_legacy.c`
  - Read `postgis/common_before_upgrade.sql`, `postgis/common_after_upgrade.sql`
  - Read `utils/create_upgrade.pl` (upgrade SQL generation)
  - Read `postgis/lwgeom_functions_basic.c` (postgis_version, postgis_full_version, postgis_lib_version, etc.)
  - Cross-reference with upgrade regression tests
  - Write `openspec/specs/extension-lifecycle/spec.md` with 10-14 requirements covering CREATE EXTENSION, ALTER EXTENSION UPDATE, version discovery, upgrade SQL generation, legacy stub mechanism, drop-before pattern, dependency chain
  - Validate every scenario has a test reference or is flagged untested

### Phase 7: Validation

Cross-reference and gap analysis across all extracted specs.

- [ ] **Cross-reference all specs against regression tests**
  - For each spec, verify every "Validated by" reference actually exists and tests the claimed behavior
  - For each regression test in `regress/core/`, verify it is referenced by at least one spec scenario
  - Produce a coverage report listing: tested scenarios, untested scenarios, unreferenced regression tests

- [ ] **Identify test gaps**
  - Compile list of all scenarios flagged as "untested"
  - Prioritize by risk: scenarios covering error paths and edge cases for frequently-used functions
  - Document gap list in a summary file for potential future test authoring

- [ ] **Write missing scenarios for critical gaps**
  - For any requirement with fewer than 3 scenarios after initial extraction, add scenarios to meet the minimum
  - For any critical function (ST_Intersects, ST_Distance, ST_Transform, ST_Buffer) missing edge-case scenarios, add them
  - Update specs with the additional scenarios

- [ ] **Final consistency review**
  - Verify all cross-references between specs resolve correctly (no broken `See the X spec` references)
  - Verify all spec directory names match the design inventory
  - Verify naming conventions are consistent across all specs (requirement heading style, scenario format)
  - Run `openspec validate` on all new specs to check structural compliance
