## Why

The TimescaleDB interface contract v0.3.0 audit (2026-02-15) identified three gaps
between the PostGIS fork's implementation and what the TimescaleDB integration layer
expects. These gaps block live integration testing:

1. Spatial function guards blocked geocentric (ECEF, SRID 4978) input but silently
   accepted inertial (ECI, SRID 900001+) input. Functions like `ST_Area` and
   `ST_Buffer` produce equally meaningless results on ECI coordinates.

2. `postgis_refresh_eop()` had a zero-argument signature, but TimescaleDB's
   `add_job()` passes `(job_id INT, config JSONB)` to scheduled procedures.
   Without a matching overload, scheduling fails at runtime.

3. `ST_Transform(geom, to_srid, epoch)` was declared `IMMUTABLE` while the
   functionally equivalent `ST_ECEF_To_ECI` was `STABLE`. The inconsistency
   could lead users to bypass safety checks by using `ST_Transform` in continuous
   aggregate definitions where `ST_ECEF_To_ECI` is correctly disallowed.

## What Changes

- Extend `srid_check_crs_family_not_geocentric()` in `libpgcommon/lwgeom_transform.c`
  to also check for `LW_CRS_INERTIAL` and raise `ERRCODE_FEATURE_NOT_SUPPORTED`.
  All 12 existing call sites (ST_Area, ST_Buffer, ST_Centroid, ST_OffsetCurve,
  ST_BuildArea, ST_Perimeter, ST_Azimuth, ST_Project x2, ST_Segmentize,
  geometry::geography) now block ECI input without any per-site changes.

- Add `postgis_refresh_eop(job_id INT, config JSONB)` overload in
  `postgis/ecef_eci.sql.in` that delegates to the zero-arg version.

- Change `ST_Transform(geom, to_srid, epoch)` in `postgis/postgis.sql.in` from
  `IMMUTABLE` to `STABLE`.

## Capabilities

### Modified Capabilities

- `ecef-coordinate-support`: Extend guard behavior from `LW_CRS_GEOCENTRIC` only
  to also cover `LW_CRS_INERTIAL`, making all Error-class functions reject ECI
  input (SRID 900001-900003) with a specific error message.

- `earth-orientation-parameters`: Add TimescaleDB-compatible procedure overload
  for `postgis_refresh_eop` so that `add_job()` works without a wrapper.

## Impact

- **C code**: `libpgcommon/lwgeom_transform.c` — added `LW_CRS_INERTIAL` check
  to `srid_check_crs_family_not_geocentric()` (single function, all 12 call sites
  inherit the fix)
- **SQL**: `postgis/ecef_eci.sql.in` — new 2-arg overload of `postgis_refresh_eop`;
  `postgis/postgis.sql.in` — `IMMUTABLE` to `STABLE` for `ST_Transform` epoch overload
- **Tests**: 5 new regression tests in `regress/core/ecef_eci.sql` covering ECI
  guards for ST_Perimeter, ST_Azimuth, ST_Project, ST_Segmentize, and geography cast
- **Existing behavior**: ECI geometries that were silently accepted by Error-class
  spatial functions now raise errors. This is intentionally breaking for misuse cases.
- **No new C functions**: All changes reuse existing infrastructure
