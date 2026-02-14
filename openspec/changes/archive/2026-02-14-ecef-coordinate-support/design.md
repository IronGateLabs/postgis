## Context

PostGIS detects ECEF/geocentric CRS via `lwsrid_get_crs_family()` returning `LW_CRS_GEOCENTRIC` for SRIDs like 4978. Storage, ST_Transform, bounding boxes, and spatial_ref_sys all work correctly. However, spatial analysis functions (`ST_Distance`, `ST_Area`, `ST_Buffer`, `ST_Length`, `ST_DWithin`) have no CRS family awareness and operate on ECEF geometries as though they are 2D planar:

- `ST_Distance` calls `lwgeom_mindistance2d`, returning 2D distance that ignores Z. For ECEF coordinates (where X/Y/Z are all in meters from Earth's center), ignoring Z produces silently wrong results.
- `ST_Area` computes planar area from X/Y, which has no physical meaning in ECEF.
- `ST_Buffer` produces 2D planar buffers, meaningless in geocentric space.
- `ST_Length` calls `lwgeom_length2d`, ignoring Z.

The `lwsrid_get_crs_family()` function and `LW_CRS_GEOCENTRIC` enum value are available for dispatch but not yet used in these code paths.

## Goals / Non-Goals

**Goals:**
- Prevent silently incorrect results from spatial functions on ECEF geometries
- Make `ST_Distance` and `ST_Length` return correct 3D Euclidean results for ECEF without requiring users to remember to call `ST_3DDistance`/`ST_3DLength`
- Raise clear errors for functions where no correct ECEF behavior exists
- Keep changes minimal and localized to the specific function entry points

**Non-Goals:**
- Implementing geodesic-on-ellipsoid distance for ECEF (users should transform to geographic first)
- Adding ECEF-native `ST_Area` or `ST_Buffer` implementations
- Refactoring the entire spatial function dispatch system (that belongs in `multi-crs-type-system`)
- Modifying geography type behavior

## Decisions

### Decision 1: ST_Distance auto-dispatches to 3D Euclidean for ECEF

When both input geometries have a geocentric CRS family, `ST_Distance` SHALL call `lwgeom_mindistance3d` instead of `lwgeom_mindistance2d`. The same applies to `ST_DWithin` using the 3D tolerance check.

**Rationale:** ECEF coordinates are inherently 3D. A user calling `ST_Distance` on ECEF points expects the Cartesian distance in meters. Requiring them to know to call `ST_3DDistance` is a trap. The 2D result is never correct for ECEF, so auto-dispatch has no valid-use-case downside.

**Alternative considered:** Raise an error and require explicit `ST_3DDistance`. Rejected because the correct 3D Euclidean distance is well-defined for ECEF and easy to compute, and erroring would break simple workflows unnecessarily.

### Decision 2: ST_Length auto-dispatches to 3D length for ECEF

When the input geometry has a geocentric CRS family, `ST_Length` SHALL call `lwgeom_length_3d` instead of `lwgeom_length_2d`. Same reasoning as Decision 1.

### Decision 3: ST_Area and ST_Buffer raise errors for ECEF

These functions SHALL call `lwsrid_get_crs_family()` on the input SRID and raise `ereport(ERROR)` with a message like: `"ST_Area is not supported for geocentric coordinate systems (SRID %d). Transform to a projected CRS first."` This check happens early in the function, before any computation.

**Rationale:** Area and buffering have no meaningful definition in geocentric Cartesian space. Unlike distance, there is no simple correct answer to dispatch to. An explicit error is safer than a silent wrong answer.

**Alternative considered:** Silently return NULL. Rejected because NULL suggests missing data, not an unsupported operation, and hides the programming error.

### Decision 4: CRS family check via SRID lookup, not LWPROJ

The geocentric check in each function uses `lwsrid_get_crs_family(gserialized_get_srid(geom))` directly, rather than constructing an LWPROJ struct. This avoids the overhead of a full PROJ CRS resolution for a simple dispatch check.

**Rationale:** `lwsrid_get_crs_family()` already handles the PROJ lookup internally with caching. It is a lightweight call suitable for per-function checks.

### Decision 5: Pass-through for coordinate-agnostic functions

Functions that operate purely on coordinate values without spatial semantics (`ST_X`, `ST_Y`, `ST_Z`, `ST_AsText`, `ST_AsEWKT`, `ST_NPoints`, `ST_GeometryType`) require no changes. They work correctly on any coordinate system.

## Risks / Trade-offs

- **[Risk] Behavior change for existing ECEF users** -> Anyone currently calling `ST_Distance` on ECEF geometries will get different (correct) results. This is intentional but technically a breaking change. Mitigated by the fact that the previous results were wrong and ECEF usage in PostGIS is rare.
- **[Risk] Performance of SRID lookup in hot paths** -> `lwsrid_get_crs_family()` involves a PROJ lookup. If called on every `ST_Distance` invocation, this adds overhead. Mitigated by the PROJ SRID cache and by only performing the lookup for SRIDs that are not obviously geographic/projected (the function returns quickly for common SRIDs like 4326).
- **[Trade-off] Selective function coverage** -> Only the most commonly misused functions are addressed. Obscure functions (e.g., `ST_Centroid`, `ST_ConvexHull`) that work correctly in Cartesian space are left as pass-through. Functions that may produce subtly wrong results (e.g., `ST_Intersection` treating ECEF as 2D) are deferred to the `multi-crs-type-system` change.
- **[Risk] Incomplete function list** -> The set of functions needing error/dispatch treatment may not be exhaustive. Mitigated by focusing on the functions explicitly identified in the spec and gap analysis, with the broader audit tracked in `coordinate-system-taxonomy`.
