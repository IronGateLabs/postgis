## Context

PostGIS has a CRS family classification system fully integrated into the transformation pipeline:

- `LW_CRS_FAMILY` enum in `liblwgeom/liblwgeom.h.in` with 7 families (GEOGRAPHIC, PROJECTED, GEOCENTRIC, INERTIAL, TOPOCENTRIC, ENGINEERING, UNKNOWN)
- `source_crs_family` and `target_crs_family` fields in `LWPROJ`, populated during `lwproj_from_PJ()`
- `srid_get_crs_family()` in `libpgcommon/lwgeom_transform.c` for runtime lookup
- `gserialized_check_crs_family_not_geocentric()` guard function (exists but never called)
- `postgis_crs_family()` SQL function and CRS family in `ST_Summary` output

Two gaps remain. First, the `source_is_latlong` boolean in `LWPROJ` is still the primary branching mechanism for all downstream consumers (`lwproj_is_latlong()`, `srid_is_latlong()`, `srid_check_latlong()`, `srid_axis_precision()`). The CRS family fields are populated but not consumed for these decisions. Second, no spatial function calls the geocentric guard or dispatches based on CRS family.

## Goals / Non-Goals

**Goals:**
- Migrate all `source_is_latlong` consumers to use `source_crs_family`
- Deprecate `source_is_latlong` with a defined removal timeline
- Add CRS family error guards to spatial functions that cannot handle geocentric input
- Add CRS family dispatch to spatial functions where geocentric has a valid interpretation (3D Euclidean distance/length)
- Add CRS family mismatch detection to binary spatial functions

**Non-Goals:**
- Removing `source_is_latlong` in this change (kept for backward compatibility)
- Adding full spatial algorithm implementations for geocentric CRS (e.g., geodesic-on-ellipsoid for ECEF)
- Modifying the `GSERIALIZED` on-disk format
- Handling inertial (ECI) CRS in spatial functions (deferred to `eci-coordinate-support`)

## Decisions

### Decision 1: Deprecate source_is_latlong, do not remove yet

The `source_is_latlong` field will be marked with a `/* DEPRECATED */` comment and a note in the LWPROJ struct documentation. The field will continue to be populated correctly. All internal consumers will be migrated to use `source_crs_family == LW_CRS_GEOGRAPHIC`. Removal will occur in the next major version.

**Rationale:** External C extensions may reference `source_is_latlong` directly. A deprecation period avoids breaking third-party code. Since the field is already correctly populated, keeping it has zero runtime cost.

**Alternative considered:** Removing immediately and adding a compatibility macro `#define source_is_latlong (source_crs_family == LW_CRS_GEOGRAPHIC)`. Rejected because macros on struct fields are fragile and confusing.

### Decision 2: Error guards before algorithm dispatch

Spatial functions will check CRS family at the top of the function, before any computation. Functions that cannot handle geocentric input will call `gserialized_check_crs_family_not_geocentric()`, which already exists and produces a clear error message with function name and SRID.

**Rationale:** Fail-fast with a clear error is better than silent incorrect results. The guard function already exists and has a consistent error format.

**Alternative considered:** Silently transforming geocentric to projected before computing. Rejected because implicit transforms are expensive, lossy, and violate the principle of least surprise.

### Decision 3: Prioritize guard functions over dispatch

Phase 1 adds error guards to functions where geocentric is meaningless (ST_Buffer, ST_Area, ST_Centroid, ST_OffsetCurve, ST_BuildArea). Phase 2 adds 3D Euclidean dispatch to functions where geocentric has a natural interpretation (ST_Distance, ST_Length, ST_3DDistance). This ordering maximizes safety first.

**Rationale:** Guards prevent incorrect results immediately with minimal code. Dispatch requires new algorithm paths that need careful testing. The two phases can proceed independently.

### Decision 4: CRS family mismatch as SRID mismatch extension

Binary spatial functions already check that both inputs have the same SRID. CRS family mismatch checking will be added as an additional check in the same code path. If SRIDs match, families inherently match. The new check catches the case where SRIDs differ and families also differ, which indicates a likely user error.

**Rationale:** Reuses existing error-checking infrastructure. A geographic geometry (SRID 4326) and a geocentric geometry (SRID 4978) should never be compared directly -- the results would be meaningless.

### Decision 5: lwproj_is_latlong preserved as convenience wrapper

`lwproj_is_latlong()` will be retained as a convenience function but reimplemented to return `pj->source_crs_family == LW_CRS_GEOGRAPHIC`. This maintains backward compatibility for callers while migrating the implementation.

**Rationale:** The function name is semantically clear for its use case (geography type validation). Renaming it would be churn without benefit.

## Risks / Trade-offs

- **[Risk] Third-party extensions using source_is_latlong directly** -> Mitigated by deprecation period; field remains populated correctly during transition
- **[Risk] Performance regression from CRS family checks in hot paths** -> The check is a single enum comparison (integer equality), same cost as the boolean it replaces. Guard function calls in spatial functions add one SRID-to-family lookup per call, which hits the PROJ cache (O(1) hash lookup)
- **[Risk] Incomplete guard coverage** -> Start with the most commonly misused functions (ST_Buffer, ST_Area, ST_Distance); expand coverage based on user reports
- **[Trade-off] Error vs. silent fallback** -> Chose explicit errors over implicit transforms. Users must explicitly transform geocentric data to a projected CRS before using 2D spatial functions. This is more verbose but prevents silent correctness bugs.
