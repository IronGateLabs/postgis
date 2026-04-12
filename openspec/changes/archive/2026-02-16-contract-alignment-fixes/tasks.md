## 1. ECI Guard Fix

- [x] 1.1 Add `LW_CRS_INERTIAL` check to `srid_check_crs_family_not_geocentric()` in `libpgcommon/lwgeom_transform.c`, after the existing `LW_CRS_GEOCENTRIC` check
- [x] 1.2 Verify ECI-specific error message: "Operation is not supported for inertial (ECI) coordinates (SRID=N). Transform to a geographic or projected CRS first."
- [x] 1.3 Confirm all 12 existing call sites inherit the fix without per-site changes: ST_Area, ST_Buffer, ST_Centroid, ST_OffsetCurve, ST_BuildArea, ST_Perimeter, ST_Azimuth, ST_Project (direction), ST_Project (geometry), ST_Segmentize, geometry::geography, gserialized wrapper

## 2. EOP Signature Fix

- [x] 2.1 Add `CREATE OR REPLACE PROCEDURE postgis_refresh_eop(job_id INT, config JSONB)` overload in `postgis/ecef_eci.sql.in`
- [x] 2.2 Overload delegates to zero-arg version via `CALL @extschema@.postgis_refresh_eop()`
- [x] 2.3 Verify zero-arg `postgis_refresh_eop()` remains available for direct CALL usage

## 3. ST_Transform Volatility Fix

- [x] 3.1 Change `ST_Transform(geom geometry, to_srid integer, epoch timestamptz)` in `postgis/postgis.sql.in` from `IMMUTABLE` to `STABLE`
- [x] 3.2 Verify `ST_ECEF_To_ECI` remains `STABLE` (no change needed — already correct)

## 4. Regression Tests

- [x] 4.1 Add ECI guard test: `ST_Perimeter` on SRID 900001 polygon raises error containing "inertial"
- [x] 4.2 Add ECI guard test: `ST_Azimuth` on SRID 900001 points raises error containing "inertial"
- [x] 4.3 Add ECI guard test: `ST_Project` (direction variant) on SRID 900001 point raises error containing "inertial"
- [x] 4.4 Add ECI guard test: `ST_Segmentize` on SRID 900001 linestring raises error containing "inertial"
- [x] 4.5 Add ECI guard test: geography cast on SRID 900001 geometry raises error containing "inertial"

## 5. Extension SQL Update

- [x] 5.1 Verify `postgis_refresh_eop(INT, JSONB)` overload included in generated extension install SQL (`postgis_ecef_eci--3.7.0dev.sql`)
- [x] 5.2 Verify `ST_Transform` epoch volatility change reflected in `postgis.sql.in` (core extension, not ecef_eci)

## 6. Contract Alignment

- [x] 6.1 Update TimescaleDB interface contract §8.1 — mark ECI guards as resolved
- [x] 6.2 Update TimescaleDB interface contract §8.2 — mark EOP signature as resolved
- [x] 6.3 Update TimescaleDB interface contract §8.4 — mark ST_Transform volatility as resolved
