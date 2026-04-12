DROP EVENT TRIGGER IF EXISTS trg_autovac_disable;
DROP FUNCTION IF EXISTS trg_test_disable_table_autovacuum();

-- Clean up ECEF/ECI objects loaded by ecef_eci.sql during prepare_spatial()
DROP TABLE IF EXISTS postgis_eop CASCADE;
DROP FUNCTION IF EXISTS ST_ECEF_To_ECI(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS ST_ECI_To_ECEF(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS _postgis_ecef_to_eci_eop(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS _postgis_eci_to_ecef_eop(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS ST_ECEF_To_ECI_EOP(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS ST_ECI_To_ECEF_EOP(geometry, timestamptz, text);
DROP FUNCTION IF EXISTS ST_ECEF_X(geometry);
DROP FUNCTION IF EXISTS ST_ECEF_Y(geometry);
DROP FUNCTION IF EXISTS ST_ECEF_Z(geometry);
DROP FUNCTION IF EXISTS postgis_eop_load(text);
DROP FUNCTION IF EXISTS postgis_eop_interpolate(timestamptz);
DROP PROCEDURE IF EXISTS postgis_refresh_eop();
DROP PROCEDURE IF EXISTS postgis_refresh_eop(int, jsonb);
DROP FUNCTION IF EXISTS postgis_accel_features();
DELETE FROM spatial_ref_sys WHERE srid IN (900001, 900002, 900003);
