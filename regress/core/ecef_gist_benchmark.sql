----------------------------------------------------------------------
-- GiST 3D Index Benchmark for ECEF/ECI Coordinates
--
-- This is a standalone benchmark script (not a regression test) that
-- measures GiST ND index performance with ECEF and ECI coordinate
-- data at various scales.  Run manually via psql:
--
--   psql -d your_db -f ecef_gist_benchmark.sql
--
-- Requires: postgis, postgis_ecef_eci extensions
----------------------------------------------------------------------

SET client_min_messages TO WARNING;
CREATE EXTENSION IF NOT EXISTS postgis;
CREATE EXTENSION IF NOT EXISTS postgis_ecef_eci;
RESET client_min_messages;

----------------------------------------------------------------------
-- 1. ECEF Point Generation (three scales)
----------------------------------------------------------------------

-- 10K points: ~100 lon x 100 lat grid on the WGS-84 ellipsoid (SRID 4978)
-- Step sizes: lon 3.6 deg (100 steps), lat 1.8 deg (100 steps)
CREATE TABLE bench_ecef_10k AS
SELECT row_number() OVER () AS id,
       ST_Transform(ST_SetSRID(ST_MakePoint(lon, lat, 0), 4326), 4978) AS geom
FROM generate_series(-180, 179, 3.6) AS lon,
     generate_series(-89, 89, 1.8) AS lat
LIMIT 10000;

-- 50K points: denser grid
-- Step sizes: lon 1.6 deg (225 steps), lat 0.8 deg (223 steps) ~ 50175
CREATE TABLE bench_ecef_50k AS
SELECT row_number() OVER () AS id,
       ST_Transform(ST_SetSRID(ST_MakePoint(lon, lat, 0), 4326), 4978) AS geom
FROM generate_series(-180, 179, 1.6) AS lon,
     generate_series(-89, 89, 0.8) AS lat
LIMIT 50000;

-- 100K points: densest grid
-- Step sizes: lon 1.14 deg (316 steps), lat 0.56 deg (318 steps) ~ 100488
CREATE TABLE bench_ecef_100k AS
SELECT row_number() OVER () AS id,
       ST_Transform(ST_SetSRID(ST_MakePoint(lon, lat, 0), 4326), 4978) AS geom
FROM generate_series(-180, 179, 1.14) AS lon,
     generate_series(-89, 89, 0.56) AS lat
LIMIT 100000;

SELECT 'Generated ECEF tables',
       (SELECT count(*) FROM bench_ecef_10k)  AS ecef_10k,
       (SELECT count(*) FROM bench_ecef_50k)  AS ecef_50k,
       (SELECT count(*) FROM bench_ecef_100k) AS ecef_100k;

----------------------------------------------------------------------
-- 2. GiST ND Index Creation with Timing
----------------------------------------------------------------------

-- 10K index build
DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE INDEX bench_ecef_10k_idx ON bench_ecef_10k USING gist(geom gist_geometry_ops_nd);
  t2 := clock_timestamp();
  RAISE NOTICE 'ECEF 10K index build: %', t2 - t1;
END $$;

-- 50K index build
DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE INDEX bench_ecef_50k_idx ON bench_ecef_50k USING gist(geom gist_geometry_ops_nd);
  t2 := clock_timestamp();
  RAISE NOTICE 'ECEF 50K index build: %', t2 - t1;
END $$;

-- 100K index build
DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE INDEX bench_ecef_100k_idx ON bench_ecef_100k USING gist(geom gist_geometry_ops_nd);
  t2 := clock_timestamp();
  RAISE NOTICE 'ECEF 100K index build: %', t2 - t1;
END $$;

ANALYZE bench_ecef_10k;
ANALYZE bench_ecef_50k;
ANALYZE bench_ecef_100k;

----------------------------------------------------------------------
-- 3. ECI Point Generation at 100K (converted from ECEF)
----------------------------------------------------------------------

DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE TABLE bench_eci_100k AS
  SELECT id,
         ST_ECEF_To_ECI(geom, '2024-01-01 00:00:00+00'::timestamptz, 'ICRF') AS geom
  FROM bench_ecef_100k;
  t2 := clock_timestamp();
  RAISE NOTICE 'ECI 100K generation (from ECEF): %', t2 - t1;
END $$;

SELECT 'Generated ECI table', count(*) AS eci_100k FROM bench_eci_100k;

-- ECI GiST ND index build
DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE INDEX bench_eci_100k_idx ON bench_eci_100k USING gist(geom gist_geometry_ops_nd);
  t2 := clock_timestamp();
  RAISE NOTICE 'ECI 100K index build: %', t2 - t1;
END $$;

ANALYZE bench_eci_100k;

----------------------------------------------------------------------
-- 4. Geographic Baseline at 100K
----------------------------------------------------------------------

CREATE TABLE bench_geog_100k AS
SELECT row_number() OVER () AS id,
       ST_SetSRID(ST_MakePoint(lon, lat), 4326)::geography AS geog
FROM generate_series(-180, 179, 1.14) AS lon,
     generate_series(-89, 89, 0.56) AS lat
LIMIT 100000;

SELECT 'Generated geography table', count(*) AS geog_100k FROM bench_geog_100k;

-- Geography GiST index build
DO $$
DECLARE t1 timestamptz; t2 timestamptz;
BEGIN
  t1 := clock_timestamp();
  CREATE INDEX bench_geog_100k_idx ON bench_geog_100k USING gist(geog);
  t2 := clock_timestamp();
  RAISE NOTICE 'Geography 100K index build: %', t2 - t1;
END $$;

ANALYZE bench_geog_100k;

----------------------------------------------------------------------
-- 5. Query Benchmarks
----------------------------------------------------------------------

-- Force index usage for benchmark queries
SET enable_seqscan = off;

-- 5a. ST_3DDWithin range query on ECEF 100K
-- Searches within 500 km of a point on the equator
EXPLAIN ANALYZE SELECT count(*) FROM bench_ecef_100k
WHERE ST_3DDWithin(geom,
      ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
      500000);

-- 5b. ST_3DDWithin range query on ECI 100K
EXPLAIN ANALYZE SELECT count(*) FROM bench_eci_100k
WHERE ST_3DDWithin(geom,
      (SELECT ST_ECEF_To_ECI(
          ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
          '2024-01-01 00:00:00+00'::timestamptz,
          'ICRF')),
      500000);

-- 5c. ST_DWithin range query on geography 100K (same 500 km radius)
EXPLAIN ANALYZE SELECT count(*) FROM bench_geog_100k
WHERE ST_DWithin(geog,
      ST_SetSRID(ST_MakePoint(0, 0), 4326)::geography,
      500000);

-- 5d. 3D bounding box overlap (&&& operator) on ECEF 100K
-- Expand query point into a 1,000 km cube
EXPLAIN ANALYZE SELECT count(*) FROM bench_ecef_100k
WHERE geom &&& ST_Expand(
      ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978)::geometry,
      1000000);

-- 5e. 3D bounding box overlap on ECI 100K
EXPLAIN ANALYZE SELECT count(*) FROM bench_eci_100k
WHERE geom &&& ST_Expand(
      (SELECT ST_ECEF_To_ECI(
          ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
          '2024-01-01 00:00:00+00'::timestamptz,
          'ICRF'))::geometry,
      1000000);

RESET enable_seqscan;

----------------------------------------------------------------------
-- 6. ST_3DDistance Accuracy: Index Scan vs Sequential Scan
--
-- Both paths should return identical distance sums, confirming the
-- GiST ND index does not alter query results.
----------------------------------------------------------------------

SET enable_indexscan = on;
SET enable_bitmapscan = on;
SET enable_seqscan = off;

SELECT 'with_index' AS mode,
       sum(ST_3DDistance(geom,
           ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978))::numeric(20,6)) AS dist_sum
FROM bench_ecef_10k;

SET enable_indexscan = off;
SET enable_bitmapscan = off;
SET enable_seqscan = on;

SELECT 'without_index' AS mode,
       sum(ST_3DDistance(geom,
           ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978))::numeric(20,6)) AS dist_sum
FROM bench_ecef_10k;

RESET enable_indexscan;
RESET enable_bitmapscan;
RESET enable_seqscan;

----------------------------------------------------------------------
-- 7. Cleanup
----------------------------------------------------------------------

DROP TABLE IF EXISTS bench_ecef_10k, bench_ecef_50k, bench_ecef_100k,
                     bench_eci_100k, bench_geog_100k;
