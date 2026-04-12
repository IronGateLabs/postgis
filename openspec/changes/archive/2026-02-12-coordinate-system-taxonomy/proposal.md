## Why

PostGIS currently has a CRS family classification system (`LW_CRS_FAMILY` enum, `lwsrid_get_crs_family()`) implemented in liblwgeom, but lacks a formal audit of where the codebase still assumes geographic-only input and a gap analysis showing which spatial capabilities work correctly for each CRS family. Without this analysis, adding support for geocentric (ECEF), inertial (ECI), and other non-geographic CRS types risks introducing silent correctness bugs.

## What Changes

- Verify the existing `LW_CRS_FAMILY` enum covers all needed CRS families and aligns with PROJ `PJ_TYPE` values
- Produce a documented audit of every C code location that assumes geographic input (e.g., `source_is_latlong`, `LWFLAG_GEODETIC`, unit-sphere functions, coordinate wrapping)
- Produce a gap analysis matrix mapping CRS families against PostGIS capabilities (Storage, ST_Transform, ST_Distance, ST_Area, GiST indexing, serialization) with support levels: FULL, PARTIAL, PROXY, NONE, ERROR
- Verify `lwsrid_get_crs_family()` and `lwcrs_family_from_pj_type()` correctly classify all standard EPSG CRS types

## Capabilities

### New Capabilities

(none - this change refines and validates an existing spec)

### Modified Capabilities

- `coordinate-system-taxonomy`: Add audit results and gap analysis findings as test scenarios; verify enum coverage is complete against current PROJ version

## Impact

- **Code**: `liblwgeom/liblwgeom.h.in` (enum definition), `liblwgeom/lwgeom_transform.c` (classification functions), `liblwgeom/cunit/cu_crs_family.c` (tests)
- **Analysis artifacts**: Audit report documenting all geographic-assumption code paths; gap matrix documenting capability support per CRS family
- **Dependencies**: Requires PROJ headers for `PJ_TYPE` enumeration; no runtime dependency changes
- **Risk**: Low - primarily analysis and verification, minimal code changes expected
