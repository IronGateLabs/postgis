## Context

The TimescaleDB interface contract v0.3.0 was produced by auditing the PostGIS
`develop` branch against the integration requirements. Three gaps were identified
in sections 8.1, 8.2, and 8.4 of the contract. All three are small, isolated
fixes that resolve inconsistencies rather than adding new features.

## Goals / Non-Goals

**Goals:**
- Close contract gap 8.1: ECI input rejected by all geocentric-guarded functions
- Close contract gap 8.2: `postgis_refresh_eop` callable by TimescaleDB `add_job()`
- Close contract gap 8.4: `ST_Transform` epoch overload has consistent volatility

**Non-Goals:**
- Implementing actual EOP auto-fetch (contract gap 8.3 — remains open/deferred)
- Adding ECI-specific dispatch paths for any spatial functions
- Modifying the guard infrastructure beyond the `LW_CRS_INERTIAL` check
- Changing `ST_ECEF_To_ECI` volatility (it is correctly STABLE)

## Decisions

### 1. ECI guards: extend existing check function, not per-site guards

**Decision:** Add an `LW_CRS_INERTIAL` check to `srid_check_crs_family_not_geocentric()`
in `libpgcommon/lwgeom_transform.c`, immediately after the existing `LW_CRS_GEOCENTRIC`
check.

**Rationale:** The guard function is the single choke point for all 12 call sites.
Modifying it once covers ST_Area, ST_Buffer, ST_Centroid, ST_OffsetCurve, ST_BuildArea,
ST_Perimeter, ST_Azimuth, ST_Project (both variants), ST_Segmentize, and the
geometry-to-geography cast — without touching any of those functions' C code. The
error message is ECI-specific: "Operation is not supported for inertial (ECI)
coordinates (SRID=N)."

**Alternative considered:** Renaming the function to `_not_geocentric_or_inertial` —
rejected because the function name is already used at 12+ call sites and the semantic
intent ("not geocentric" in the broad sense) is clear enough. The function's behavior
now covers all non-terrestrial CRS families.

### 2. EOP refresh: overload, not rename

**Decision:** Add a `postgis_refresh_eop(job_id INT, config JSONB)` overload in
`postgis/ecef_eci.sql.in` that calls the zero-arg version. Keep the zero-arg version
for direct `CALL` usage.

**Rationale:** TimescaleDB's `add_job()` resolves the procedure by name and expects
`(INT, JSONB)` arguments. Adding an overload is the simplest fix — one-liner
delegation. The `job_id` and `config` parameters are accepted but unused because the
current implementation is a placeholder. When real EOP fetching is implemented, the
`config` parameter can carry the IERS URL and other options.

**Alternative considered:** TimescaleDB-side wrapper — rejected because it adds a
dependency direction (TimescaleDB must know about PostGIS internals) and the fix is
trivial on the PostGIS side.

### 3. ST_Transform epoch: downgrade to STABLE

**Decision:** Change `ST_Transform(geom, to_srid, epoch)` from `IMMUTABLE` to
`STABLE` in `postgis/postgis.sql.in`.

**Rationale:** The epoch overload calls the same underlying C function
(`transform_epoch` / `lwgeom_transform_ecef_to_eci`) as `ST_ECEF_To_ECI`, which is
declared `STABLE`. While the non-EOP ERA-only path is technically pure math
(deterministic for the same inputs), declaring it `IMMUTABLE` creates an exploitable
inconsistency: users could use `ST_Transform` in continuous aggregate definitions to
bypass the `STABLE` restriction on `ST_ECEF_To_ECI`. Making both `STABLE` is
conservative and consistent. If the non-EOP path is later separated into its own C
function, it can be made `IMMUTABLE` independently.

**Impact on continuous aggregates:** Users cannot use the epoch overload in cagg
definitions. This matches the behavior of `ST_ECEF_To_ECI` and is correct — frame
conversions depend on time and should not be materialized in caggs without explicit
epoch handling.
