## Why

PostGIS has working ECEF (Earth-Centered Earth-Fixed) coordinate support for storage, transformation, bounding boxes, and CRS detection via SRID 4978. However, spatial analysis functions like `ST_Distance`, `ST_Area`, and `ST_Buffer` silently produce incorrect results when called on ECEF geometries. `ST_Distance` computes 2D Cartesian distance (ignoring Z), giving wrong answers for 3D geocentric coordinates. `ST_Area` and `ST_Buffer` operate as though coordinates are planar 2D, producing meaningless results. There is no CRS family check to warn users or dispatch to correct implementations.

## What Changes

- Add CRS family checks to spatial functions that produce incorrect results on geocentric input
- Make `ST_Distance` dispatch to 3D Euclidean distance for ECEF geometries instead of returning 2D-only results
- Make `ST_Area`, `ST_Buffer`, and other unsupported functions raise clear errors for ECEF input
- Verify the five already-implemented requirements (CRS detection, storage, ST_Transform, bounding boxes, spatial_ref_sys) pass their spec scenarios

## Capabilities

### New Capabilities

(none)

### Modified Capabilities

- `ecef-coordinate-support`: Refine requirement 5 (spatial function behavior) with implementation-specific scenarios based on gap analysis findings; all other requirements are verification-only

## Impact

- **Code**: `postgis/lwgeom_functions_basic.c` (ST_Distance dispatch), `postgis/lwgeom_geos.c` or equivalent (ST_Buffer, ST_Area error paths), `liblwgeom/liblwgeom.h.in` (CRS family query utilities)
- **SQL API**: No new functions; existing functions gain geocentric awareness (error or dispatch)
- **Dependencies**: Requires `lwsrid_get_crs_family()` from liblwgeom (already implemented)
- **Risk**: Medium - changing behavior of existing functions for a specific CRS family; must not regress geographic or projected CRS behavior
