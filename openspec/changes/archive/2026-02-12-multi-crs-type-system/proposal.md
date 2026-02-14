## Why

PostGIS now carries CRS family metadata throughout its internal pipeline -- the `LWPROJ` struct stores `source_crs_family`/`target_crs_family`, the PROJ cache propagates them, `GSERIALIZED` derives family at runtime from SRID, and SQL-level metadata functions (`postgis_crs_family()`, `ST_Summary`) expose it. However, two of the six spec requirements remain incomplete:

1. **`source_is_latlong` is still the primary branching flag.** The old boolean field is still present in `LWPROJ` and all downstream consumers (`lwproj_is_latlong()`, `srid_is_latlong()`, `srid_check_latlong()`, `srid_axis_precision()`) use it exclusively. The CRS family fields exist in parallel but do not yet replace the boolean in any code path. This means geocentric coordinates are handled correctly only by accident (they happen to set `source_is_latlong = LW_FALSE`), not by intentional family-aware dispatch.

2. **No spatial function inspects CRS family.** The `gserialized_check_crs_family_not_geocentric()` guard function exists in `libpgcommon/lwgeom_transform.c` but is never called by any spatial function. Functions like `ST_Distance`, `ST_Area`, `ST_Buffer`, and `ST_Intersects` will silently produce incorrect results or crash when given geocentric input.

The other four requirements (LWPROJ metadata, PROJ cache, GSERIALIZED compatibility, metadata functions) are implemented and need only verification.

## What Changes

- Refactor `lwproj_is_latlong()` to use `source_crs_family == LW_CRS_GEOGRAPHIC` instead of the `source_is_latlong` boolean
- Mark `source_is_latlong` as deprecated in the `LWPROJ` struct; retain for one release cycle, then remove
- Update `srid_check_latlong()` to use CRS family internally while preserving its external contract (geography must be geographic CRS)
- Update `srid_axis_precision()` to branch on CRS family instead of the boolean
- Add `gserialized_check_crs_family_not_geocentric()` calls to spatial functions that cannot operate on geocentric coordinates: `ST_Buffer`, `ST_Area`, `ST_Centroid`, `ST_OffsetCurve`, `ST_BuildArea`
- Add CRS family dispatch to `ST_Distance` and `ST_Length` for geocentric inputs (3D Euclidean in meters)
- Add CRS family mismatch checking to binary spatial functions: `ST_Intersects`, `ST_Contains`, `ST_Within`, `ST_Distance`
- Verify the four already-implemented requirements with targeted regression tests

## Capabilities

### New Capabilities

(none -- this change completes an existing spec)

### Modified Capabilities

- `multi-crs-type-system` requirement 2: Complete the `source_is_latlong` deprecation by making all consumers branch on CRS family
- `multi-crs-type-system` requirement 5: Add CRS family dispatch and error guards to spatial functions

## Impact

- **Code**: `liblwgeom/liblwgeom.h.in` (deprecation annotation on `source_is_latlong`), `libpgcommon/lwgeom_transform.c` (`lwproj_is_latlong`, `srid_check_latlong`, `srid_axis_precision`), `postgis/lwgeom_geos.c` (ST_Buffer, ST_Area, ST_Centroid guards), `postgis/lwgeom_functions_basic.c` (ST_Distance dispatch), `postgis/geography_measurement.c` (ST_Length dispatch)
- **Tests**: New regression tests for geocentric error messages, CRS family mismatch errors, and 3D Euclidean distance correctness
- **Dependencies**: None -- all required infrastructure is already in place
- **Risk**: Low for error guards (additive). Medium for `source_is_latlong` deprecation -- must verify no third-party code depends on the field directly. Mitigated by keeping the field for one release cycle.
