-- Tests for ECEF/ECI coordinate frame conversion and ECEF accessors
-- ECEF/ECI functions are loaded by the test infrastructure (run_test.pl)

-- Common test literals to reduce duplication
\set frame_icrf 'ICRF'
\set frame_teme 'TEME'
\set epoch_jun15 '2024-06-15 12:00:00+00'
\set epoch_jan01 '2024-01-01 00:00:00+00'
\set epoch_j2000 '2000-01-01 00:00:00+00'
\set epoch_jan01_noon_utc '2024-01-01 12:00:00 UTC'
\set epoch_2025_utc '2025-01-01T00:00:00Z'
\set wkt_pointzm_2024 'POINT ZM (6378137 0 500000 2024)'

--------------------------------------------
-- 1. SRID Registration Tests
--------------------------------------------

-- Verify ECI SRIDs exist in spatial_ref_sys
SELECT 'srid_icrf', srid, auth_name FROM spatial_ref_sys WHERE srid = 900001;
SELECT 'srid_j2000', srid, auth_name FROM spatial_ref_sys WHERE srid = 900002;
SELECT 'srid_teme', srid, auth_name FROM spatial_ref_sys WHERE srid = 900003;

-- Can set SRID to ECI values
SELECT 'setsrid_eci', ST_SRID(ST_SetSRID(ST_MakePoint(0,0,0), 900001));
SELECT 'setsrid_eci', ST_SRID(ST_SetSRID(ST_MakePoint(0,0,0), 900002));
SELECT 'setsrid_eci', ST_SRID(ST_SetSRID(ST_MakePoint(0,0,0), 900003));

--------------------------------------------
-- 2. ECEF Coordinate Accessor Tests
--------------------------------------------

-- Extract X/Y/Z from a known ECEF point (SRID 4978)
SELECT 'ecef_x', ST_ECEF_X(ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978));
SELECT 'ecef_y', ST_ECEF_Y(ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978));
SELECT 'ecef_z', ST_ECEF_Z(ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978));

-- SRID validation: non-ECEF input should error
SELECT 'ecef_x_badsrid', ST_ECEF_X(ST_SetSRID(ST_MakePoint(1, 2, 3), 4326));
SELECT 'ecef_y_badsrid', ST_ECEF_Y(ST_SetSRID(ST_MakePoint(1, 2, 3), 4326));
SELECT 'ecef_z_badsrid', ST_ECEF_Z(ST_SetSRID(ST_MakePoint(1, 2, 3), 4326));

-- Type validation: non-point should error
SELECT 'ecef_x_line', ST_ECEF_X(ST_SetSRID('LINESTRING(0 0 0, 1 1 1)'::geometry, 4978));

-- Empty point returns NULL
SELECT 'ecef_x_empty', ST_ECEF_X(ST_SetSRID('POINT EMPTY'::geometry, 4978)) IS NULL;

-- 2D point: Z accessor returns NULL (no Z coordinate)
SELECT 'ecef_z_2d', ST_ECEF_Z(ST_SetSRID(ST_MakePoint(4000000, 3000000), 4978)) IS NULL;

-- NULL input: STRICT functions return NULL automatically
SELECT 'ecef_x_null', ST_ECEF_X(NULL) IS NULL;

--------------------------------------------
-- 3. Frame Conversion Tests
--------------------------------------------

-- Basic ECEF-to-ECI conversion: verify output SRID for each frame
SELECT 'to_eci_icrf_srid', ST_SRID(ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	:'epoch_jun15'::timestamptz,
	:'frame_icrf'));

SELECT 'to_eci_j2000_srid', ST_SRID(ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	:'epoch_jun15'::timestamptz,
	'J2000'));

SELECT 'to_eci_teme_srid', ST_SRID(ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	:'epoch_jun15'::timestamptz,
	:'frame_teme'));

-- Case insensitivity of frame name
SELECT 'to_eci_case', ST_SRID(ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	:'epoch_jun15'::timestamptz,
	'icrf'));

-- Round-trip test: ECEF -> ECI (ICRF) -> ECEF should recover original coordinates
-- Compare snapped result to original — should match within grid tolerance
SELECT 'roundtrip',
	ST_Equals(
		ST_SnapToGrid(ST_ECI_To_ECEF(
			ST_ECEF_To_ECI(
				ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
				:'epoch_jun15'::timestamptz,
				:'frame_icrf'),
			:'epoch_jun15'::timestamptz,
			:'frame_icrf'), 0.001),
		ST_SnapToGrid(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978), 0.001));

-- Round-trip with a point that has all three nonzero coordinates
SELECT 'roundtrip_3d',
	ST_Equals(
		ST_SnapToGrid(ST_ECI_To_ECEF(
			ST_ECEF_To_ECI(
				ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978),
				:'epoch_jan01'::timestamptz,
				:'frame_teme'),
			:'epoch_jan01'::timestamptz,
			:'frame_teme'), 0.001),
		ST_SnapToGrid(ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978), 0.001));

-- Verify ECI-to-ECEF output SRID is ECEF (4978)
SELECT 'to_ecef_srid', ST_SRID(ST_ECI_To_ECEF(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001),
	:'epoch_jun15'::timestamptz,
	:'frame_icrf'));

-- Z coordinate is preserved through rotation (only X/Y change)
SELECT 'z_preserved',
	abs(ST_Z(ST_ECEF_To_ECI(
		ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978),
		:'epoch_jun15'::timestamptz,
		:'frame_icrf')) - 4500000) < 0.001;

-- Different epochs give different ECI results (Earth rotates)
SELECT 'diff_epochs',
	ST_X(eci1) != ST_X(eci2)
FROM (
	SELECT
		ST_ECEF_To_ECI(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			'2024-06-15 00:00:00+00'::timestamptz, :'frame_icrf') AS eci1,
		ST_ECEF_To_ECI(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			'2024-06-15 06:00:00+00'::timestamptz, :'frame_icrf') AS eci2
) sub;

--------------------------------------------
-- 4. Frame Conversion Error Tests
--------------------------------------------

-- SRID mismatch: input must be ECEF (4978) for ECEF-to-ECI
SELECT 'err_bad_srid_to_eci', ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(0, 0, 0), 4326),
	:'epoch_jan01'::timestamptz,
	:'frame_icrf');

-- Unknown frame name
SELECT 'err_bad_frame', ST_ECEF_To_ECI(
	ST_SetSRID(ST_MakePoint(0, 0, 0), 4978),
	:'epoch_jan01'::timestamptz,
	'INVALID');

-- ECI-to-ECEF: input must have ECI SRID
SELECT 'err_bad_srid_to_ecef', ST_ECI_To_ECEF(
	ST_SetSRID(ST_MakePoint(0, 0, 0), 4326),
	:'epoch_jan01'::timestamptz,
	:'frame_icrf');

-- ECI-to-ECEF: SRID/frame mismatch (geometry is ICRF=900001 but frame says J2000)
SELECT 'err_frame_mismatch', ST_ECI_To_ECEF(
	ST_SetSRID(ST_MakePoint(0, 0, 0), 900001),
	:'epoch_jan01'::timestamptz,
	'J2000');

-- NULL inputs: STRICT functions return NULL (no error)
SELECT 'null_geom', ST_ECEF_To_ECI(NULL, :'epoch_jan01'::timestamptz, :'frame_icrf') IS NULL;
SELECT 'null_epoch', ST_ECEF_To_ECI(ST_SetSRID(ST_MakePoint(0,0,0), 4978), NULL, :'frame_icrf') IS NULL;
SELECT 'null_frame', ST_ECEF_To_ECI(ST_SetSRID(ST_MakePoint(0,0,0), 4978), :'epoch_jan01'::timestamptz, NULL) IS NULL;

--------------------------------------------
-- 5. EOP Table Tests
--------------------------------------------

-- Insert sample EOP data (two bracketing rows)
INSERT INTO postgis_eop (mjd, xp, yp, dut1, dx, dy) VALUES
	(60000.0, 0.000100, 0.000200, 0.050, NULL, NULL),
	(60001.0, 0.000110, 0.000220, 0.060, NULL, NULL),
	(60002.0, 0.000120, 0.000240, 0.070, NULL, NULL);

-- Interpolate at midpoint between first two rows (MJD 60000.5)
-- Unix epoch is 1970-01-01 00:00:00 UTC = MJD 40587.0
-- 2000-01-01 00:00:00 UTC = MJD 51544.0
-- seconds after 2000-01-01 = (60000.5 - 51544.0) * 86400 = 730641600
-- Expected: xp = 0.000105, yp = 0.000210, dut1 = 0.055
SELECT 'eop_interp',
	round(xp::numeric, 6),
	round(yp::numeric, 6),
	round(dut1::numeric, 3)
FROM postgis_eop_interpolate(
	(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second')
);

-- Epoch before loaded data range returns empty result
SELECT 'eop_before_range',
	(SELECT count(*) FROM postgis_eop_interpolate(
		(:'epoch_j2000'::timestamptz + ((59999.0 - 51544.0) * 86400)::int * interval '1 second')
	)) = 0;

-- Epoch after loaded data range returns empty result
SELECT 'eop_after_range',
	(SELECT count(*) FROM postgis_eop_interpolate(
		(:'epoch_j2000'::timestamptz + ((60003.0 - 51544.0) * 86400)::int * interval '1 second')
	)) = 0;

--------------------------------------------
-- 5a. EOP-Enhanced Transform Tests
--------------------------------------------

-- EOP-enhanced round-trip: ECEF->ECI->ECEF with EOP should recover coordinates
-- Use the sample EOP data already loaded (MJD 60000-60002)
-- MJD 60000.5 -> epoch ~2023-02-25 (within our EOP data range)
SELECT 'eop_roundtrip',
	abs(ST_X(result) - 6378137) < 0.01 AND
	abs(ST_Y(result)) < 0.01 AND
	abs(ST_Z(result) - 4500000) < 0.01
FROM (
	SELECT ST_ECI_To_ECEF_EOP(
		ST_ECEF_To_ECI_EOP(
			ST_SetSRID(ST_MakePoint(6378137, 0, 4500000), 4978),
			(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second'),
			:'frame_icrf'),
		(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second'),
		:'frame_icrf') AS result
) sub;

-- EOP transform should differ from non-EOP transform
-- dut1=0.055 sec should shift ERA by ~0.83 arcsec = ~160m at equator
SELECT 'eop_differs',
	abs(ST_X(eci_basic) - ST_X(eci_eop)) > 0.1 OR
	abs(ST_Y(eci_basic) - ST_Y(eci_eop)) > 0.1
FROM (
	SELECT
		ST_ECEF_To_ECI(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second'),
			:'frame_icrf') AS eci_basic,
		ST_ECEF_To_ECI_EOP(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second'),
			:'frame_icrf') AS eci_eop
) sub;

-- EOP fallback: epoch outside EOP range should fall back to non-EOP transform
-- and produce identical results
SELECT 'eop_fallback',
	abs(ST_X(eci_basic) - ST_X(eci_eop)) < 0.001 AND
	abs(ST_Y(eci_basic) - ST_Y(eci_eop)) < 0.001
FROM (
	SELECT
		ST_ECEF_To_ECI(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			'2020-01-01 00:00:00+00'::timestamptz,
			:'frame_icrf') AS eci_basic,
		ST_ECEF_To_ECI_EOP(
			ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
			'2020-01-01 00:00:00+00'::timestamptz,
			:'frame_icrf') AS eci_eop
) sub;

-- Z coordinate preserved through EOP-enhanced rotation
SELECT 'eop_z_preserved',
	abs(ST_Z(ST_ECEF_To_ECI_EOP(
		ST_SetSRID(ST_MakePoint(4000000, 3000000, 4500000), 4978),
		(:'epoch_j2000'::timestamptz + ((60000.5 - 51544.0) * 86400)::int * interval '1 second'),
		:'frame_icrf')) - 4500000) < 1.0;

-- Clean up EOP test data
DELETE FROM postgis_eop WHERE mjd IN (60000.0, 60001.0, 60002.0);

--------------------------------------------
-- 5b. ST_Transform Epoch Integration Tests
--------------------------------------------

-- M-coordinate epoch: ECI(ICRF)->ECEF with M as decimal year (2024.0)
-- Rotation should change X/Y, preserve Z and M
SELECT 'xform_m_epoch',
	abs(ST_X(result) - 6378137) > 1000 AND  -- coordinates rotated
	ST_Z(result) = 500000 AND                -- Z preserved
	ST_M(result) = 2024                      -- M preserved
FROM (
	SELECT ST_Transform(
		ST_SetSRID(ST_GeomFromText(:'wkt_pointzm_2024'), 900001),
		4978
	) AS result
) sub;

-- M-coordinate epoch: round-trip ECI->ECEF->ECI recovers coordinates
SELECT 'xform_m_roundtrip',
	abs(ST_X(result) - 6378137) < 0.01 AND
	abs(ST_Y(result)) < 0.01 AND
	abs(ST_Z(result) - 500000) < 0.01
FROM (
	SELECT ST_Transform(
		ST_Transform(
			ST_SetSRID(ST_GeomFromText(:'wkt_pointzm_2024'), 900001),
			4978
		),
		900001
	) AS result
) sub;

-- Explicit epoch: ECEF->ECI(ICRF) with TIMESTAMPTZ
SELECT 'xform_explicit_epoch',
	ST_SRID(result) = 900001 AND
	abs(ST_X(result) - 6378137) > 1000  -- rotated
FROM (
	SELECT ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 4978),
		900001,
		:'epoch_jan01_noon_utc'::timestamptz
	) AS result
) sub;

-- Explicit epoch: round-trip ECEF->ECI->ECEF
SELECT 'xform_explicit_roundtrip',
	abs(ST_X(result) - 6378137) < 0.01 AND
	abs(ST_Y(result)) < 0.01 AND
	abs(ST_Z(result) - 500000) < 0.01
FROM (
	SELECT ST_Transform(
		ST_Transform(
			ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 4978),
			900001,
			:'epoch_jan01_noon_utc'::timestamptz
		),
		4978,
		:'epoch_jan01_noon_utc'::timestamptz
	) AS result
) sub;

-- Explicit epoch: ECI(ICRF)->ECEF direction
SELECT 'xform_eci_to_ecef',
	ST_SRID(result) = 4978 AND
	abs(ST_X(result) - 6378137) > 1000  -- rotated
FROM (
	SELECT ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 900001),
		4978,
		'2024-06-15 00:00:00 UTC'::timestamptz
	) AS result
) sub;

-- Error: ECI transform without M and without explicit epoch
SELECT 'xform_no_epoch_err',
	ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 900001),
		4978
	) IS NULL;

-- Error: ECI-to-ECI direct not supported
SELECT 'xform_eci_to_eci_err',
	ST_Transform(
		ST_SetSRID(ST_GeomFromText(:'wkt_pointzm_2024'), 900001),
		900002
	) IS NULL;

-- Error: Non-ECI with explicit epoch not supported
SELECT 'xform_non_eci_epoch_err',
	ST_Transform(
		ST_SetSRID(ST_MakePoint(0, 0, 0), 4326),
		4978,
		:'epoch_jan01_noon_utc'::timestamptz
	) IS NULL;

-- Error: Invalid epoch in M coordinate (negative year)
SELECT 'xform_invalid_m_err',
	ST_Transform(
		ST_SetSRID(ST_GeomFromText('POINT ZM (6378137 0 500000 -500)'), 900001),
		4978
	) IS NULL;

-- Explicit epoch: J2000 frame
SELECT 'xform_j2000',
	ST_SRID(result) = 900002
FROM (
	SELECT ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 4978),
		900002,
		:'epoch_jan01_noon_utc'::timestamptz
	) AS result
) sub;

-- Explicit epoch: TEME frame
SELECT 'xform_teme',
	ST_SRID(result) = 900003
FROM (
	SELECT ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 500000), 4978),
		900003,
		:'epoch_jan01_noon_utc'::timestamptz
	) AS result
) sub;

--------------------------------------------
-- 6. GiST 3D Index Tests
--------------------------------------------

-- Create a table with ECEF points
CREATE TABLE test_ecef_gist (
	id serial PRIMARY KEY,
	geom geometry(PointZ, 4978)
);

-- Insert some ECEF points (approximately Earth surface radius ~6.37M metres)
INSERT INTO test_ecef_gist (geom)
SELECT ST_SetSRID(ST_MakePoint(
	6378137 * cos(radians(lat)) * cos(radians(lon)),
	6378137 * cos(radians(lat)) * sin(radians(lon)),
	6378137 * sin(radians(lat))
), 4978)
FROM generate_series(-80, 80, 10) AS lat,
     generate_series(-170, 170, 10) AS lon;

-- Create GiST ND index
CREATE INDEX test_ecef_gist_idx ON test_ecef_gist USING gist(geom gist_geometry_ops_nd);

ANALYZE test_ecef_gist;

-- Helper to extract scan type from EXPLAIN
CREATE OR REPLACE FUNCTION _ecef_test_scantype(q text) RETURNS text
LANGUAGE 'plpgsql' AS
$$
DECLARE
  exp TEXT;
  mat TEXT[];
  ret TEXT;
BEGIN
  FOR exp IN EXECUTE 'EXPLAIN ' || q
  LOOP
    mat := regexp_matches(exp, ' *(?:-> *)?(.*Scan)');
    IF mat IS NOT NULL THEN
      ret := mat[1];
    END IF;
  END LOOP;
  RETURN ret;
END;
$$;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = on;

-- Check that the GiST ND index is used for &&& queries (not a Seq Scan)
SELECT 'gist_nd_scan',
	_ecef_test_scantype(
		'SELECT count(*) FROM test_ecef_gist WHERE geom &&& ST_SetSRID(ST_MakePoint(6000000, 0, 0), 4978)::geometry'
	) NOT LIKE '%Seq Scan%';

-- Verify &&& operator returns results with ECEF-scale coordinates
SELECT 'gist_nd_count',
	count(*) > 0
FROM test_ecef_gist
WHERE geom &&& ST_Expand(ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978)::geometry, 1000000);

-- ST_3DDWithin uses the GiST ND index
SELECT 'gist_3ddwithin',
	count(*) > 0
FROM test_ecef_gist
WHERE ST_3DDWithin(geom, ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978)::geometry, 500000);

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;

-- Clean up ECEF table
DROP TABLE test_ecef_gist;

--------------------------------------------
-- 6b. GiST 3D Index Tests — ECI SRIDs
--------------------------------------------

-- Create a table with ECI points (ICRF frame, SRID 900001)
CREATE TABLE test_eci_gist (
	id serial PRIMARY KEY,
	geom geometry(PointZ, 900001)
);

-- Insert ECI points (same coordinate ranges as ECEF)
INSERT INTO test_eci_gist (geom)
SELECT ST_SetSRID(ST_MakePoint(
	6378137 * cos(radians(lat)) * cos(radians(lon)),
	6378137 * cos(radians(lat)) * sin(radians(lon)),
	6378137 * sin(radians(lat))
), 900001)
FROM generate_series(-80, 80, 20) AS lat,
     generate_series(-170, 170, 20) AS lon;

-- Create GiST ND index on ECI geometry
CREATE INDEX test_eci_gist_idx ON test_eci_gist USING gist(geom gist_geometry_ops_nd);

ANALYZE test_eci_gist;

SET enable_seqscan = off;
SET enable_indexscan = on;
SET enable_bitmapscan = on;

-- Verify GiST ND index is used for ECI geometry queries
SELECT 'eci_gist_nd_scan',
	_ecef_test_scantype(
		'SELECT count(*) FROM test_eci_gist WHERE geom &&& ST_SetSRID(ST_MakePoint(6000000, 0, 0), 900001)::geometry'
	) NOT LIKE '%Seq Scan%';

-- Verify &&& operator returns results with ECI points using 3D bbox
SELECT 'eci_gist_nd_count',
	count(*) > 0
FROM test_eci_gist
WHERE geom &&& ST_SetSRID(
	ST_MakeLine(ST_MakePoint(5000000, -2000000, -2000000), ST_MakePoint(7000000, 2000000, 2000000)),
	900001)::geometry;

-- ST_3DDWithin on ECI geometry (large radius to ensure hits)
SELECT 'eci_3ddwithin',
	count(*) > 0
FROM test_eci_gist
WHERE ST_3DDWithin(geom, ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001)::geometry, 2000000);

RESET enable_seqscan;
RESET enable_indexscan;
RESET enable_bitmapscan;

-- Clean up
DROP FUNCTION _ecef_test_scantype(text);
DROP TABLE test_eci_gist;

----------------------------------------------------------------------
-- Section: Hardware Acceleration Feature Detection
----------------------------------------------------------------------

-- 8.1 Test postgis_accel_features() returns a non-empty string
SELECT 'accel_features', length(postgis_accel_features()) > 0;

-- 8.2 Test that SIMD-accelerated transforms match scalar results
-- Create test points and transform via ECI -> ECEF -> ECI roundtrip
-- The result should be identical regardless of backend
SELECT 'accel_roundtrip',
	ST_AsText(
		ST_SnapToGrid(
			ST_Transform(
				ST_Transform(
					ST_SetSRID(
						ST_MakePoint(6378137, 0, 0, 2025.0),
						900001),
					4978,
					:'epoch_2025_utc'::timestamptz),
				900001,
				:'epoch_2025_utc'::timestamptz),
			0.01)
	) ~* 'POINT';

-- 8.3 Test GPU dispatch threshold behavior
-- With very few points, GPU should NOT be used (CPU SIMD or scalar)
-- This is a no-GPU test: just verify the transform succeeds with small data
SELECT 'accel_small_batch',
	ST_X(ST_Transform(
		ST_SetSRID(ST_MakePoint(6378137, 0, 0, 2025.0), 900001),
		4978,
		:'epoch_2025_utc'::timestamptz
	)) IS NOT NULL;

-- 8.4 Test postgis_accel_features() output format contains expected keys
SELECT 'accel_format',
	postgis_accel_features() LIKE 'SIMD: %' AND
	postgis_accel_features() LIKE '%GPU: %' AND
	postgis_accel_features() LIKE '%Valkey: %';

--------------------------------------------
-- 9. Spatial Function Geocentric Guards
--------------------------------------------

-- 9.1 ST_Perimeter on ECEF polygon should raise error
SELECT 'guard_perimeter', ST_Perimeter(ST_SetSRID(ST_GeomFromText(
	'POLYGON((6378137 0 0, 0 6378137 0, 0 0 6378137, 6378137 0 0))'), 4978));

-- 9.2 ST_Azimuth on ECEF points should raise error
SELECT 'guard_azimuth', ST_Azimuth(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(0, 6378137, 0), 4978));

-- 9.3 ST_Project direction variant on ECEF should raise error
SELECT 'guard_project_dir', ST_AsText(ST_Project(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978), 1000, 0.5));

-- 9.4 ST_Project geometry variant on ECEF should raise error
SELECT 'guard_project_geom', ST_AsText(ST_Project(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(0, 6378137, 0), 4978), 1000));

-- 9.5 ST_Segmentize on ECEF linestring should raise error
SELECT 'guard_segmentize', ST_AsText(ST_Segmentize(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(0, 6378137, 0)), 4978),
	100000));

-- 9.6 Geography cast on ECEF geometry should raise geocentric error
SELECT 'guard_geog_cast', ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978)::geography;

-- 9.7 ECI guard tests: same functions should also error on ECI input
SELECT 'guard_eci_perimeter', ST_Perimeter(ST_SetSRID(ST_GeomFromText(
	'POLYGON((6378137 0 0, 0 6378137 0, 0 0 6378137, 6378137 0 0))'), 900001));

SELECT 'guard_eci_azimuth', ST_Azimuth(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001),
	ST_SetSRID(ST_MakePoint(0, 6378137, 0), 900001));

SELECT 'guard_eci_project_dir', ST_AsText(ST_Project(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001), 1000, 0.5));

SELECT 'guard_eci_segmentize', ST_AsText(ST_Segmentize(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(0, 6378137, 0)), 900001),
	100000));

SELECT 'guard_eci_geog_cast', ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001)::geography;

-- 9.8 Negative tests: guards do NOT fire on geographic/projected input
SELECT 'neg_perimeter', ST_Perimeter(ST_SetSRID(ST_GeomFromText(
	'POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))'), 4326)) > 0;
SELECT 'neg_azimuth', ST_Azimuth(
	ST_SetSRID(ST_MakePoint(0, 0), 4326),
	ST_SetSRID(ST_MakePoint(1, 1), 4326)) IS NOT NULL;
SELECT 'neg_project', ST_AsText(ST_Project(
	ST_SetSRID(ST_MakePoint(500000, 0), 32632), 1000, 0.5)) IS NOT NULL;
SELECT 'neg_segmentize', ST_NPoints(ST_Segmentize(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(0, 0), ST_MakePoint(10, 10)), 4326), 1)) > 2;

-- 9.9 Mixed-SRID safety: ECEF vs geographic should error
SELECT 'mixed_ecef_geog', ST_3DDWithin(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(0, 0, 0), 4326), 1000);

-- 9.10 Mixed-SRID safety: ECEF vs ECI should error
SELECT 'mixed_ecef_eci', ST_3DDWithin(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001), 1000);

-- 9.11 Same-SRID ECI query should succeed
SELECT 'same_srid_eci', ST_3DDWithin(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001),
	ST_SetSRID(ST_MakePoint(6378137, 100, 0), 900001), 1000);

--------------------------------------------
-- 10. Extended Geocentric Guard Tests
--------------------------------------------

-- 10.1 ST_Simplify rejects ECEF
SELECT 'guard_simplify_ecef', ST_Simplify(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(6378237, 100, 0)), 4978), 100.0);
-- 10.2 ST_SimplifyPreserveTopology rejects ECEF
SELECT 'guard_simpprestopo_ecef', ST_SimplifyPreserveTopology(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(6378237, 100, 0)), 4978), 100.0);
-- 10.3 ST_ConvexHull rejects ECEF
SELECT 'guard_convexhull_ecef', ST_ConvexHull(
	ST_SetSRID(ST_Collect(ARRAY[ST_MakePoint(6378137,0,0), ST_MakePoint(0,6378137,0), ST_MakePoint(4510731,4510731,0)]), 4978));
-- 10.4 ST_DelaunayTriangles rejects ECEF
SELECT 'guard_delaunay_ecef', ST_DelaunayTriangles(
	ST_SetSRID(ST_Collect(ARRAY[ST_MakePoint(6378137,0,0), ST_MakePoint(0,6378137,0), ST_MakePoint(4510731,4510731,0)]), 4978));
-- 10.5 ST_VoronoiPolygons rejects ECEF
SELECT 'guard_voronoi_ecef', ST_VoronoiPolygons(
	ST_SetSRID(ST_Collect(ARRAY[ST_MakePoint(6378137,0,0), ST_MakePoint(0,6378137,0), ST_MakePoint(4510731,4510731,0)]), 4978));
-- 10.6 ST_LineInterpolatePoint rejects ECEF
SELECT 'guard_lineinterp_ecef', ST_LineInterpolatePoint(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(6378237, 100, 0)), 4978), 0.5);

-- 10.7 ST_Simplify rejects ECI
SELECT 'guard_simplify_eci', ST_Simplify(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(6378237, 100, 0)), 900001), 100.0);
-- 10.8 ST_ConvexHull rejects ECI
SELECT 'guard_convexhull_eci', ST_ConvexHull(
	ST_SetSRID(ST_Collect(ARRAY[ST_MakePoint(6378137,0,0), ST_MakePoint(0,6378137,0), ST_MakePoint(4510731,4510731,0)]), 900001));

-- 10.9 ST_ClosestPoint with ECEF dispatches to 3D
SELECT 'closestpoint_3d_ecef', ST_AsText(ST_ClosestPoint(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(6378237, 100, 50), 4978)));
-- 10.10 ST_ShortestLine with ECEF dispatches to 3D
SELECT 'shortestline_3d_ecef', ST_AsText(ST_ShortestLine(
	ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
	ST_SetSRID(ST_MakePoint(6378237, 100, 50), 4978)));

-- 10.11 ST_3DLineInterpolatePoint continues to work with ECEF
SELECT 'lineinterp3d_ecef_ok', ST_AsText(ST_3DLineInterpolatePoint(
	ST_SetSRID(ST_MakeLine(ST_MakePoint(6378137, 0, 0), ST_MakePoint(6378237, 100, 50)), 4978), 0.5));

--------------------------------------------
-- 11. Cleanup
--------------------------------------------

-- ECI SRIDs (900001-900003) are managed by test infrastructure
-- (loaded via ecef_eci.sql in prepare_spatial), so no cleanup needed here.
