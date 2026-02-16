## ADDED Requirements

### Requirement: Scalable synthetic dataset generation
The benchmark SHALL generate synthetic ECEF POINT Z datasets at configurable
scales (10K, 100K, 500K, 1M rows) with realistic orbital distribution across
LEO, MEO, and GEO altitude bands.  Each point SHALL have an associated
timestamp and object_id for partitioning.

#### Scenario: Generate 100K-point benchmark dataset
- **WHEN** the dataset generation script runs with scale=100K
- **THEN** it SHALL create a table with 100,000 ECEF POINT Z geometries
  (SRID 4978), timestamps, and object_ids, with points distributed across
  orbital altitude bands

### Requirement: GiST ND index build timing
The benchmark SHALL measure and report GiST ND index build time for each
dataset scale using `gist_geometry_ops_nd` operator class.

#### Scenario: Index build timing at 100K scale
- **WHEN** the benchmark builds a GiST ND index on 100K points
- **THEN** it SHALL report wall-clock build time in milliseconds using
  `clock_timestamp()` differencing

### Requirement: Standard query template suite
The benchmark SHALL include query templates for:
1. **Proximity search**: `ST_3DDWithin(geom, target, radius)` with index
2. **Range scan**: bounding box `&&&` operator over 3D extent
3. **k-NN search**: `ORDER BY geom <#> target LIMIT k` using index
4. **Aggregate with spatial filter**: COUNT/AVG over `ST_3DDWithin` results

#### Scenario: Proximity search benchmark
- **WHEN** the proximity search template runs against a 100K dataset with
  GiST ND index
- **THEN** it SHALL report query time in milliseconds, rows returned, and
  whether the index was used (via EXPLAIN output)

#### Scenario: k-NN benchmark
- **WHEN** the k-NN template runs with k=10 against a 100K dataset
- **THEN** it SHALL report query time and confirm the GiST index distance
  operator was used

### Requirement: Timing collection and reporting
The benchmark SHALL collect timing data from multiple runs (minimum 5 per query
template) and report median, p95, and standard deviation.  Results SHALL be
output in a format suitable for comparison across runs.

#### Scenario: Multi-run timing collection
- **WHEN** the benchmark runs a query template 5 times
- **THEN** it SHALL report median, p95, and stddev of wall-clock times,
  excluding the first run as warmup
