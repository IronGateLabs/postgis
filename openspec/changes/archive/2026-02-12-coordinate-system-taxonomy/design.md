## Context

PostGIS has an established CRS family classification system implemented in liblwgeom:

- `LW_CRS_FAMILY` enum in `liblwgeom.h.in` with 7 families (GEOGRAPHIC, PROJECTED, GEOCENTRIC, INERTIAL, TOPOCENTRIC, ENGINEERING, UNKNOWN)
- `lwcrs_family_from_pj_type()` mapping PROJ `PJ_TYPE` values to the enum
- `lwsrid_get_crs_family()` deriving family from SRID (with special-case for ECI SRIDs 900001-900099)
- `lwcrs_family_name()` for human-readable names
- `source_crs_family` and `target_crs_family` fields in the `LWPROJ` struct
- Comprehensive unit tests in `cu_crs_family.c` (~800 lines)

However, the broader PostGIS codebase has not been systematically audited for locations that assume geographic-only input, and no gap analysis exists documenting which spatial functions work correctly for each CRS family.

## Goals / Non-Goals

**Goals:**
- Produce a complete audit of geographic-assumption code paths in the C codebase
- Produce a capability gap matrix backed by tested evidence
- Verify existing enum and classification functions are complete and correct
- Identify priority items for future CRS-aware refactoring

**Non-Goals:**
- Refactoring code to be CRS-family-aware (that belongs in `multi-crs-type-system`)
- Adding new spatial function implementations for non-geographic CRS types
- Modifying the on-disk GSERIALIZED format
- Changing any existing public SQL API behavior

## Decisions

### Decision 1: Audit as structured markdown, not code

The audit report and gap matrix will be produced as structured markdown documents stored in the change directory, not as code comments or inline documentation. This keeps the analysis separate from implementation and makes it reviewable.

**Rationale:** Code comments get stale; a standalone document can be referenced by future changes (multi-crs-type-system, ecef-coordinate-support) as input for their task planning.

**Alternative considered:** Annotating each code location with `/* CRS_AUDIT: ... */` comments. Rejected because it would touch dozens of files for a purely analytical purpose.

### Decision 2: Gap matrix populated by actual testing

Each cell in the gap matrix will be filled by running a specific SQL query or C unit test against the relevant CRS type, rather than by code inspection alone. This eliminates assumptions about what "should" work.

**Rationale:** Code inspection can miss runtime behavior like PROJ fallbacks, implicit type coercions, or silent error swallowing.

### Decision 3: Verify enum completeness against PROJ 9.x PJ_TYPE list

The LW_CRS_FAMILY enum will be validated against the full `PJ_TYPE` enumeration from the linked PROJ version to ensure no CRS types are unmapped.

**Rationale:** PROJ may add new CRS types in newer versions. The current mapping covers the common types but should be verified for completeness.

## Risks / Trade-offs

- **[Risk] Audit scope creep** → Limit audit to files under `liblwgeom/` and `postgis/` directories; exclude raster, topology, and tiger geocoder modules
- **[Risk] Gap matrix tests may have false positives** → Distinguish between "function returns a result" and "function returns a correct result" in classifications
- **[Risk] PROJ version differences affect classification** → Document the PROJ version used for testing; note any version-dependent behavior
