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

-- Geocentric guard tests: spatial functions should reject SRID 4978
-- ST_Buffer
SELECT 'GUARD1', ST_AsText(ST_Buffer('SRID=4978;POINTZ(6378137 0 0)'::geometry, 1000));
-- ST_Area
SELECT 'GUARD2', ST_Area('SRID=4978;POLYGONZ((6378137 0 0, 6378137 1000 0, 6378137 1000 1000, 6378137 0 1000, 6378137 0 0))'::geometry);
-- ST_Centroid
SELECT 'GUARD3', ST_AsText(ST_Centroid('SRID=4978;POINTZ(6378137 0 0)'::geometry));
-- ST_OffsetCurve
SELECT 'GUARD4', ST_AsText(ST_OffsetCurve('SRID=4978;LINESTRINGZ(6378137 0 0, 0 6378137 0)'::geometry, 100));
-- ST_BuildArea
SELECT 'GUARD5', ST_AsText(ST_BuildArea('SRID=4978;LINESTRINGZ(6378137 0 0, 0 6378137 0, 0 0 6356752, 6378137 0 0)'::geometry));

-- Geocentric ST_Distance should use 3D Euclidean
-- Two points at (1000,0,0) and (0,1000,0): distance = sqrt(2)*1000 = 1414.214
SELECT 'DIST1', round(ST_Distance(
  'SRID=4978;POINTZ(1000 0 0)'::geometry,
  'SRID=4978;POINTZ(0 1000 0)'::geometry)::numeric, 3);

-- 3D distance with Z: (0,0,0) to (3,4,0) = 5.0
SELECT 'DIST2', ST_Distance(
  'SRID=4978;POINTZ(0 0 0)'::geometry,
  'SRID=4978;POINTZ(3 4 0)'::geometry);

-- 3D distance: (0,0,0) to (1,1,1) = sqrt(3) = 1.732
SELECT 'DIST3', round(ST_Distance(
  'SRID=4978;POINTZ(0 0 0)'::geometry,
  'SRID=4978;POINTZ(1 1 1)'::geometry)::numeric, 3);

-- Geocentric ST_Length should use 3D Euclidean
-- Line from (1000,0,0) to (0,1000,0): length = sqrt(2)*1000 = 1414.214
SELECT 'LEN1', round(ST_Length(
  'SRID=4978;LINESTRINGZ(1000 0 0, 0 1000 0)'::geometry)::numeric, 3);

-- Multi-segment: (0,0,0)->(3,4,0)->(3,4,12) = 5 + 12 = 17
SELECT 'LEN2', ST_Length(
  'SRID=4978;LINESTRINGZ(0 0 0, 3 4 0, 3 4 12)'::geometry);

-- CRS family mismatch: geographic vs geocentric
SELECT 'MISMATCH1', ST_Intersects(
  'SRID=4326;POINT(0 0)'::geometry,
  'SRID=4978;POINTZ(6378137 0 0)'::geometry);

-- PROJ cache verification: calling postgis_crs_family twice should return same result
SELECT 'CACHE1', postgis_crs_family(4326) = postgis_crs_family(4326);
SELECT 'CACHE2', postgis_crs_family(4978) = postgis_crs_family(4978);

-- GSERIALIZED format: no changes to on-disk format
-- round-trip via pg_typeof confirms geometry type is preserved
SELECT 'COMPAT1', pg_typeof('SRID=4326;POINT(0 0)'::geometry);
SELECT 'COMPAT2', pg_typeof('SRID=4978;POINTZ(6378137 0 0)'::geometry);

-- ST_DWithin on ECEF uses 3D distance for tolerance check
-- Two points separated by 1000m in Z only: (0,0,0) and (0,0,1000)
SELECT 'DWITHIN1', ST_DWithin(
  'SRID=4978;POINTZ(0 0 0)'::geometry,
  'SRID=4978;POINTZ(0 0 1000)'::geometry, 500);
SELECT 'DWITHIN2', ST_DWithin(
  'SRID=4978;POINTZ(0 0 0)'::geometry,
  'SRID=4978;POINTZ(0 0 1000)'::geometry, 1500);

-- ST_Distance on ECEF points differing only in Z returns non-zero
SELECT 'DISTZ1', ST_Distance(
  'SRID=4978;POINTZ(6378137 0 0)'::geometry,
  'SRID=4978;POINTZ(6378137 0 1000)'::geometry);

-- ST_Distance matches ST_3DDistance for ECEF
SELECT 'DIST3D1', (ST_Distance(
  'SRID=4978;POINTZ(1000 0 0)'::geometry,
  'SRID=4978;POINTZ(0 1000 0)'::geometry) =
  ST_3DDistance(
  'SRID=4978;POINTZ(1000 0 0)'::geometry,
  'SRID=4978;POINTZ(0 1000 0)'::geometry));

-- ST_Length matches ST_3DLength for ECEF
SELECT 'LEN3D1', (ST_Length(
  'SRID=4978;LINESTRINGZ(1000 0 0, 0 1000 0)'::geometry) =
  ST_3DLength(
  'SRID=4978;LINESTRINGZ(1000 0 0, 0 1000 0)'::geometry));

-- ECEF geometry storage: X/Y/Z round-trip exactly
SELECT 'STORE1', ST_X(geom), ST_Y(geom), ST_Z(geom)
  FROM (SELECT 'SRID=4978;POINTZ(6378137 0 0)'::geometry AS geom) t;

-- Coordinate accessor functions work on ECEF without error
SELECT 'ACCESS1', ST_X('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'ACCESS2', ST_Y('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'ACCESS3', ST_Z('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'ACCESS4', ST_AsText('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'ACCESS5', ST_AsEWKT('SRID=4978;POINTZ(6378137 0 0)'::geometry);
SELECT 'ACCESS6', ST_NPoints('SRID=4978;POINTZ(6378137 0 0)'::geometry);

-- EPSG:4978 exists in spatial_ref_sys
SELECT 'SRS1', auth_name, auth_srid FROM spatial_ref_sys WHERE srid = 4978;

-- ECEF bounding box uses Cartesian metric ranges
SELECT 'BBOX1',
  round(ST_XMin(geom)::numeric, 1),
  round(ST_YMin(geom)::numeric, 1),
  round(ST_ZMin(geom)::numeric, 1),
  round(ST_XMax(geom)::numeric, 1),
  round(ST_YMax(geom)::numeric, 1),
  round(ST_ZMax(geom)::numeric, 1)
  FROM (SELECT 'SRID=4978;LINESTRINGZ(6378137 0 0, 0 6378137 0)'::geometry AS geom) t;

-- Negative tests: geographic and projected behavior unchanged
SELECT 'NEG_DIST1', round(ST_Distance(
  'SRID=4326;POINT(0 0)'::geometry,
  'SRID=4326;POINT(1 1)'::geometry)::numeric, 6);
SELECT 'NEG_AREA1', round(ST_Area(
  'SRID=32632;POLYGON((500000 0, 501000 0, 501000 1000, 500000 1000, 500000 0))'::geometry)::numeric, 0);
SELECT 'NEG_LEN1', round(ST_Length(
  'SRID=32632;LINESTRING(500000 0, 501000 0)'::geometry)::numeric, 0);
SELECT 'NEG_DWITHIN1', ST_DWithin(
  'SRID=4326;POINT(0 0)'::geometry,
  'SRID=4326;POINT(0.001 0)'::geometry, 0.01);

-- postgis_crs_family for geocentric returns 'geocentric'
SELECT 'META1', postgis_crs_family(4978);
-- ST_Summary includes CRS family for geocentric
SELECT 'META2', ST_Summary('SRID=4978;POINTZ(6378137 0 0)'::geometry);
