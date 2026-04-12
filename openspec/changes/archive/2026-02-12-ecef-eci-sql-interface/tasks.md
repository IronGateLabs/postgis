## 1. SQL Frame Conversion Functions

- [x] 1.1 Implement C wrapper function `postgis_ecef_to_eci()` in `postgis/lwgeom_ecef_eci.c` that accepts `geometry, timestamptz, text` and calls `lwgeom_transform_ecef_to_eci()` after converting TIMESTAMPTZ to decimal-year
- [x] 1.2 Implement C wrapper function `postgis_eci_to_ecef()` in `postgis/lwgeom_ecef_eci.c` that accepts `geometry, timestamptz, text` and calls `lwgeom_transform_eci_to_ecef()`
- [x] 1.3 Add SRID validation to both wrappers: input SRID must be 4978 (ECEF) or 900001-900003 (ECI) as appropriate; raise error on mismatch
- [x] 1.4 Add frame parameter parsing: map 'ICRF' -> 900001, 'J2000' -> 900002, 'TEME' -> 900003; raise error on unknown frame name
- [x] 1.5 Set output geometry SRID to match the target frame (e.g., ST_ECEF_To_ECI with frame='J2000' returns SRID 900002)
- [x] 1.6 Declare SQL functions in `ecef_eci.sql.in`: `ST_ECEF_To_ECI(geometry, timestamptz, text DEFAULT 'ICRF')` and `ST_ECI_To_ECEF(geometry, timestamptz, text DEFAULT 'ICRF')` with STABLE volatility and PARALLEL SAFE
- [x] 1.7 Write regression tests: round-trip conversion (ECEF -> ECI -> ECEF) with known epoch, SRID mismatch error, unknown frame error, NULL geometry handling

## 2. ECEF Coordinate Accessors

- [x] 2.1 Implement C function `postgis_ecef_x()` in `postgis/lwgeom_ecef_eci.c` using `gserialized_peek_first_point` to extract X coordinate with SRID 4978 validation
- [x] 2.2 Implement C functions `postgis_ecef_y()` and `postgis_ecef_z()` following the same pattern
- [x] 2.3 Return NULL for empty geometries and non-point types (matching `ST_X` behavior)
- [x] 2.4 Declare SQL functions in `ecef_eci.sql.in`: `ST_ECEF_X(geometry)`, `ST_ECEF_Y(geometry)`, `ST_ECEF_Z(geometry)` with IMMUTABLE volatility and PARALLEL SAFE
- [x] 2.5 Write regression tests: extract coordinates from known ECEF point, SRID validation error for non-ECEF input, NULL on empty geometry, NULL on multipoint

## 3. ECI SRID Registration

- [x] 3.1 Create INSERT statements for SRID 900001 (ICRF) in `ecef_eci.sql.in` with srtext WKT and proj4text
- [x] 3.2 Create INSERT statements for SRID 900002 (J2000) with appropriate WKT and proj4text
- [x] 3.3 Create INSERT statements for SRID 900003 (TEME) with appropriate WKT and proj4text
- [x] 3.4 Use INSERT ... ON CONFLICT DO NOTHING to avoid errors if SRIDs already exist
- [x] 3.5 Write regression tests: verify SRIDs exist after extension creation, verify `ST_SetSRID(ST_MakePoint(0,0,0), 900001)` succeeds

## 4. Earth Orientation Parameters Infrastructure

- [x] 4.1 Define `postgis_eop` table schema in `ecef_eci.sql.in`: `mjd FLOAT8 PRIMARY KEY, xp FLOAT8, yp FLOAT8, dut1 FLOAT8, dx FLOAT8, dy FLOAT8`
- [x] 4.2 Create `postgis_eop_load(data TEXT)` PL/pgSQL function that parses IERS Bulletin A fixed-width format and inserts/updates rows
- [x] 4.3 Create `postgis_eop_interpolate(epoch TIMESTAMPTZ)` function that interpolates EOP values for a given epoch using linear interpolation between adjacent MJD entries
- [x] 4.4 Create `postgis_refresh_eop()` procedure suitable for scheduling with `pg_cron` or TimescaleDB `add_job`
- [x] 4.5 Add CHECK constraint or trigger to validate EOP value ranges (xp/yp within ±1 arcsec, dut1 within ±1 second)
- [x] 4.6 Write regression tests: insert sample EOP data, interpolate at known epoch, verify boundary conditions (before/after loaded range returns NULL)

## 5. Extension Packaging

- [x] 5.1 Create `extensions/postgis_ecef_eci/postgis_ecef_eci.control.in` with `requires = postgis`, `default_version = '@EXTVERSION@'`, `relocatable = true`
- [x] 5.2 Create `extensions/postgis_ecef_eci/Makefile.in` following `postgis_sfcgal` pattern with PGXS integration
- [x] 5.3 Create extension install SQL script that includes: SRID registration, EOP table creation, function declarations
- [x] 5.5 Verify `CREATE EXTENSION postgis_ecef_eci` installs all objects, `DROP EXTENSION postgis_ecef_eci CASCADE` cleans up
- [x] 5.6 Verify extension installs after `CREATE EXTENSION postgis` and does not interfere with `ALTER EXTENSION postgis UPDATE`

## 6. GiST 3D Spatial Index Verification

- [x] 6.1 Write test creating GiST index with `gist_geometry_ops_nd` on an ECEF geometry column (SRID 4978)
- [x] 6.2 Insert 1000+ ECEF points and verify index is used in `ST_3DDWithin` queries via `EXPLAIN`
- [x] 6.4 Verify `&&` (bounding box overlap) operator works correctly with ECEF coordinate ranges (values ~6.4M)

## 7. Documentation

- [x] 7.1 Document `ST_ECEF_To_ECI` and `ST_ECI_To_ECEF` function signatures, parameters, return types, and usage examples
- [x] 7.2 Document `ST_ECEF_X/Y/Z` accessor functions with IMMUTABLE semantics explanation and use cases (index expressions, materialized views)
- [x] 7.3 Document ECI SRID assignments (900001=ICRF, 900002=J2000, 900003=TEME) and their coordinate system definitions
- [x] 7.4 Document EOP infrastructure: table schema, data loading procedure, interpolation function, scheduling refresh
- [x] 7.5 Document extension installation and upgrade procedures
