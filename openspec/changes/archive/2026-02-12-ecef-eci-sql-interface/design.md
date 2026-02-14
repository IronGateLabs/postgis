## Context

PostGIS has C-level ECEF/ECI infrastructure implemented across several files:

- **`liblwgeom/liblwgeom.h.in`**: Defines `LW_CRS_FAMILY` enum (GEOGRAPHIC, PROJECTED, GEOCENTRIC, INERTIAL, TOPOCENTRIC, ENGINEERING, UNKNOWN), SRID macros (`SRID_ECI_ICRF=900001`, `SRID_ECI_J2000=900002`, `SRID_ECI_TEME=900003`), `SRID_IS_ECI()` macro, and `lwcrs_family_requires_epoch()` macro.
- **`liblwgeom/lwgeom_eci.c`**: Implements `lwgeom_transform_ecef_to_eci()` and `lwgeom_transform_eci_to_ecef()` using simplified IERS 2003 Earth Rotation Angle, plus `lwgeom_eci_compute_gbox()` for Cartesian bounding boxes.
- **`liblwgeom/lwgeom_transform.c`**: `LWPROJ` struct carries `source_crs_family` and `target_crs_family` fields; `lwproj_from_PJ` detects `PJ_TYPE_GEOCENTRIC_CRS`.

None of this is exposed at the SQL level. This design bridges the gap.

## Goals / Non-Goals

**Goals:**

- Expose all existing ECEF/ECI C infrastructure through SQL functions with correct volatility and parallel safety annotations
- Provide ECEF coordinate accessors that are IMMUTABLE (usable in index expressions, materialized views, continuous aggregates)
- Register ECI SRIDs so `spatial_ref_sys` queries and SRID-based workflows function correctly
- Create EOP infrastructure for future precision enhancement of frame conversions
- Package as a separate installable extension that does not modify core PostGIS
- Verify GiST 3D indexing correctness with ECEF/ECI coordinate ranges

**Non-Goals:**

- Modifying the existing C transform implementation or ERA computation
- Implementing full IAU 2006/2000A precession-nutation (future enhancement using EOP)
- Adding new PostGIS types — ECEF/ECI points remain standard `geometry(PointZ, srid)`
- Replacing `ST_Transform` — the new functions complement it for ECEF/ECI-specific workflows
- Supporting non-PostgreSQL databases or non-PostGIS spatial stacks

## Decisions

### Decision 1: TIMESTAMPTZ at SQL level, decimal-year internally

**Choice:** SQL frame conversion functions SHALL accept `TIMESTAMPTZ` epoch parameters. The C wrapper converts TIMESTAMPTZ to the decimal-year format expected by `lwgeom_transform_ecef_to_eci()` internally.

**Rationale:** PostgreSQL users work with TIMESTAMPTZ natively. Requiring decimal-year input would force users to perform error-prone manual conversion. The C layer's `lweci_epoch_to_jd()` already handles the conversion path; we just need TIMESTAMPTZ-to-decimal-year in the SQL/C boundary.

**Alternatives considered:**
- (A) Expose decimal-year at SQL level — rejected because it violates PostgreSQL idioms and creates user confusion
- (B) Modify C functions to accept TIMESTAMPTZ directly — rejected because it would couple `liblwgeom` to PostgreSQL types

### Decision 2: Single function with TEXT frame parameter

**Choice:** Use `ST_ECEF_To_ECI(geom, epoch, frame)` with a TEXT parameter (default 'ICRF') rather than separate functions per frame (`ST_ECEF_To_ICRF`, `ST_ECEF_To_J2000`, etc.).

**Rationale:** A single function with a frame parameter is more extensible — adding new ECI frames requires no new SQL function declarations. The frame parameter maps directly to SRID selection (ICRF=900001, J2000=900002, TEME=900003). The default is 'ICRF' since it is the IAU-recommended inertial frame.

**Alternatives considered:**
- (A) Per-frame functions (`ST_ECEF_To_ICRF`, `ST_ECEF_To_J2000`, `ST_ECEF_To_TEME`) — rejected because it creates combinatorial explosion as frames are added
- (B) Integer SRID parameter instead of TEXT — rejected because frame names are more user-friendly and self-documenting

### Decision 3: ECEF accessors as thin C wrappers with SRID validation

**Choice:** `ST_ECEF_X/Y/Z` SHALL be implemented as C functions that: (1) validate SRID is 4978, (2) extract coordinates using `gserialized_peek_first_point` (the same fast-path used by `ST_X`), and (3) are declared IMMUTABLE and PARALLEL SAFE.

**Rationale:** The key difference from `ST_X`/`ST_Y`/`ST_Z` is the IMMUTABLE volatility (vs. STABLE) and SRID validation. IMMUTABLE is safe because the coordinate values are deterministic for a given geometry binary — no external state is consulted. SRID validation prevents accidental use on geographic (lat/lon) geometries where "X" would mean longitude, not meters.

**Alternatives considered:**
- (A) SQL wrappers around `ST_X` with a check — rejected because SQL wrapper functions cannot be declared IMMUTABLE if they call STABLE functions
- (B) No SRID validation — rejected because silently returning lon/lat values when ECEF meters are expected would produce incorrect downstream calculations

### Decision 4: EOP table owned by extension, not core PostGIS

**Choice:** The `postgis_eop` table SHALL be created by and owned by the `postgis_ecef_eci` extension. It is not part of core PostGIS's schema.

**Rationale:** EOP data is only relevant for precision ECEF/ECI transforms. Including it in core PostGIS would add unnecessary schema objects for the vast majority of users. Extension ownership ensures clean install/uninstall lifecycle.

**Alternatives considered:**
- (A) Add EOP table to core PostGIS — rejected because it burdens all PostGIS users with space-domain schema objects
- (B) Store EOP in a separate extension — rejected because it fragments the install experience; users wanting ECEF/ECI would need two extensions

### Decision 5: Extension packaging follows postgis_sfcgal pattern

**Choice:** Package as `extensions/postgis_ecef_eci/` with a `.control.in` file, `Makefile.in`, and SQL install scripts following the exact structure of `extensions/postgis_sfcgal/`.

**Rationale:** The `postgis_sfcgal` extension is the established pattern for optional PostGIS functionality that shares the PostGIS `.so` library but has separate SQL declarations. This pattern is well-tested, supports `CREATE EXTENSION` / `ALTER EXTENSION UPDATE`, and integrates with the existing build system.

**Alternatives considered:**
- (A) Ship as a standalone extension with its own `.so` — rejected because the C functions live in `liblwgeom`/`postgis.so`; a separate `.so` would require duplicating or dynamically linking
- (B) Merge into core PostGIS extension — rejected because it forces all PostGIS users to install ECEF/ECI schema objects

### Decision 6: Simplified ERA initially; EOP-enhanced transforms as upgrade path

**Choice:** Initial frame conversion functions SHALL use the existing simplified ERA computation (`lweci_earth_rotation_angle`). EOP-enhanced precision is an upgrade path that uses the `postgis_eop` table when populated.

**Rationale:** The simplified ERA provides sub-kilometer accuracy for LEO objects, which is sufficient for many use cases. Full IAU precession-nutation requires EOP data that may not be available in all deployments. The function can check for EOP data availability and use it when present, degrading gracefully to simplified ERA when absent.

## Risks / Trade-offs

- **[SRID range conflicts]** SRIDs 900001-900003 are in the user-defined range (> 900000). Users with existing custom SRIDs in this range will conflict. → Mitigation: Document the reserved range; provide a migration guide for conflicting users.

- **[IMMUTABLE accessor correctness]** Declaring `ST_ECEF_X` IMMUTABLE means PostgreSQL may cache/fold results aggressively. If the function ever needs to consult external state, this would be incorrect. → Mitigation: The function is a pure coordinate extraction — no external state is consulted. This is safe as long as the implementation remains a direct memory read.

- **[EOP data freshness]** IERS publishes EOP data with a ~30-day prediction window. Stale EOP data degrades precision. → Mitigation: The refresh procedure can be scheduled via `pg_cron` or TimescaleDB's job scheduler. The interpolation function returns NULL for epochs outside the loaded range, forcing fallback to simplified ERA.

- **[Extension dependency chain]** `postgis_ecef_eci` requires `postgis >= 3.4`. Users on older PostGIS versions cannot use it. → Mitigation: PostGIS 3.4+ is a reasonable floor given the C infrastructure is only in the fork.

## Open Questions

1. Should `ST_ECEF_To_ECI` return SRID 900001 for all frames (ICRF default) or set the SRID to match the requested frame (900001/900002/900003)?
   - **Proposed answer**: Match the frame — ICRF=900001, J2000=900002, TEME=900003. This preserves frame identity in the geometry.

2. Should the EOP loader use `COPY ... FROM` or `pg_read_file()` for IERS data import?
   - **Proposed answer**: Provide both paths — `COPY` for file-based loading, and a PL/pgSQL procedure that accepts TEXT content for programmatic loading.

3. How should frame conversion handle epochs far from the EOP data range (e.g., historical dates before 1962)?
   - **Proposed answer**: Fall back to simplified ERA with a NOTICE-level warning. Never silently produce degraded results without informing the user.
