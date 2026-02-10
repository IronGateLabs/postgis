--
-- Tests for CRS family classification system
--

-- postgis_crs_family() function tests
SELECT 'CF1', postgis_crs_family(4326);
SELECT 'CF2', postgis_crs_family(32632);
SELECT 'CF3', postgis_crs_family(4978);
SELECT 'CF4', postgis_crs_family(3857);
SELECT 'CF5', postgis_crs_family(999999);
SELECT 'CF6', postgis_crs_family(0);
SELECT 'CF7', postgis_crs_family(4269);
SELECT 'CF8', postgis_crs_family(4979);

-- ST_Summary CRS family output for different CRS types
SELECT 'SUM1', ST_Summary('SRID=4326;POINT(0 0)'::geometry);
SELECT 'SUM2', ST_Summary('SRID=32632;POINT(500000 0)'::geometry);
SELECT 'SUM3', ST_Summary('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'SUM4', ST_Summary('POINT(0 0)'::geometry);

-- ECEF round-trip via ST_Transform
SELECT 'ECEF1', ST_AsText(ST_SnapToGrid(
  ST_Transform(
    ST_Transform('SRID=4326;POINTZ(0 0 0)'::geometry, 4978),
  4326), 0.0000001));

-- Known control point: (0,0,0) on WGS84 -> (6378137, 0, 0) in ECEF
SELECT 'ECEF2', ST_AsText(ST_SnapToGrid(
  ST_Transform('SRID=4326;POINTZ(0 0 0)'::geometry, 4978),
  0.001));

-- North pole: (0, 90, 0) -> (0, 0, 6356752.314)
SELECT 'ECEF3', ST_AsText(ST_SnapToGrid(
  ST_Transform('SRID=4326;POINTZ(0 90 0)'::geometry, 4978),
  0.001));

-- 3D round-trip with high altitude (GPS orbit)
SELECT 'ECEF4', ST_AsText(ST_SnapToGrid(
  ST_Transform(
    ST_Transform('SRID=4326;POINTZ(0 0 20200000)'::geometry, 4978),
  4326), 0.0000001));

-- Multi-point ECEF transform
SELECT 'ECEF5', ST_AsText(ST_SnapToGrid(
  ST_Transform(
    'SRID=4326;LINESTRINGZ(0 0 0, 90 0 0, 0 90 0, -90 0 0)'::geometry,
    4978),
  0.001));
