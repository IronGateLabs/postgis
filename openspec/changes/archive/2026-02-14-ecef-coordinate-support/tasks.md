## 1. Spatial Function CRS Dispatch

- [x] 1.1 Add geocentric CRS check to `ST_Distance` in `postgis/lwgeom_functions_basic.c`: when both input SRIDs resolve to `LW_CRS_GEOCENTRIC`, call `lwgeom_mindistance3d` instead of `lwgeom_mindistance2d`
- [x] 1.2 Add geocentric CRS check to `ST_DWithin` in `postgis/lwgeom_geos_predicates.c`: use 3D tolerance check for ECEF input
- [x] 1.3 Add geocentric CRS check to `ST_Length` in `postgis/lwgeom_functions_basic.c`: call `lwgeom_length_3d` instead of `lwgeom_length_2d` for ECEF input
- [x] 1.4 Add geocentric CRS error to `ST_Area` in `postgis/lwgeom_functions_basic.c`: raise `ereport(ERROR)` with message identifying geocentric CRS and suggesting transform to projected CRS
- [x] 1.5 Add geocentric CRS error to `ST_Buffer` in `postgis/lwgeom_geos.c`: raise `ereport(ERROR)` with message identifying geocentric CRS

## 2. Verification of Implemented Requirements

- [x] 2.1 Verify ECEF CRS type detection: confirm `lwsrid_get_crs_family(4978)` returns `LW_CRS_GEOCENTRIC` and `lwcrs_family_from_pj_type(PJ_TYPE_GEOCENTRIC_CRS)` maps correctly (existing tests in `cu_crs_family.c`)
- [x] 2.2 Verify ECEF geometry storage: insert and retrieve ECEF point with SRID 4978, confirm X/Y/Z round-trip exactly
- [x] 2.3 Verify ST_Transform ECEF<->geographic: confirm `ST_Transform(geom, 4326)` and `ST_Transform(geom, 4978)` produce correct results within 1mm (existing regression tests)
- [x] 2.4 Verify ECEF bounding box computation: confirm GBOX for ECEF geometries uses Cartesian metric ranges, not unit-sphere normalization
- [x] 2.5 Verify EPSG:4978 in spatial_ref_sys: confirm `SELECT * FROM spatial_ref_sys WHERE srid = 4978` returns correct auth_name, auth_srid, srtext, and proj4text

## 3. Testing

- [x] 3.1 Add SQL regression test: `ST_Distance` on two ECEF points returns 3D Euclidean distance matching `ST_3DDistance`
- [x] 3.2 Add SQL regression test: `ST_Distance` on ECEF points differing only in Z returns non-zero result
- [x] 3.3 Add SQL regression test: `ST_DWithin` on ECEF points uses 3D distance for tolerance check
- [x] 3.4 Add SQL regression test: `ST_Area` on ECEF polygon raises error containing "geocentric"
- [x] 3.5 Add SQL regression test: `ST_Buffer` on ECEF geometry raises error containing "geocentric"
- [x] 3.6 Add SQL regression test: `ST_Length` on ECEF linestring returns 3D path length matching `ST_3DLength`
- [x] 3.7 Add SQL regression test: `ST_X`, `ST_Y`, `ST_Z`, `ST_AsText` work correctly on ECEF geometries (pass-through verification)
- [x] 3.8 Add negative test: confirm dispatch/error changes do not affect `ST_Distance`, `ST_Area` behavior on geographic (4326) and projected (32632) geometries
