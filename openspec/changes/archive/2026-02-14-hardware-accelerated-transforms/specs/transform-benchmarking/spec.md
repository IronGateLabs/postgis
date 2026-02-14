## ADDED Requirements

### Requirement: Performance benchmarking harness
The system SHALL include a benchmarking tool that measures transform throughput across acceleration backends and point counts, producing comparable results.

#### Scenario: Benchmark ECI rotation
- **WHEN** the benchmark harness is run with `--operation eci_rotate`
- **THEN** the system SHALL measure throughput (points/second) for scalar, SIMD (AVX2/AVX-512/NEON), and GPU paths across point counts: 1, 100, 1K, 10K, 100K, 1M, 10M

#### Scenario: Benchmark PROJ transform
- **WHEN** the benchmark harness is run with `--operation proj_transform`
- **THEN** the system SHALL measure throughput for the full `ptarray_transform()` pipeline (rad conversion + PROJ + deg conversion) across the same point count range

#### Scenario: Benchmark output format
- **WHEN** a benchmark run completes
- **THEN** the system SHALL output results as a CSV table with columns: operation, backend, point_count, throughput_pts_per_sec, latency_us, and optionally a text-mode comparison chart

#### Scenario: GPU dispatch overhead measurement
- **WHEN** the benchmark harness is run with `--operation gpu_overhead`
- **THEN** the system SHALL measure the fixed overhead of GPU dispatch (memory allocation, data transfer, kernel launch) separately from the per-point compute time

### Requirement: Regression correctness validation
The benchmarking harness SHALL validate that all acceleration paths produce numerically equivalent results.

#### Scenario: SIMD vs scalar equivalence
- **WHEN** the benchmark harness runs with `--validate`
- **THEN** the system SHALL compare SIMD output against scalar output for all test point sets and report maximum absolute difference per coordinate, failing if any difference exceeds 1e-10

#### Scenario: GPU vs scalar equivalence
- **WHEN** the benchmark harness runs with `--validate` and a GPU backend is available
- **THEN** the system SHALL compare GPU output against scalar output and report maximum absolute difference, failing if any difference exceeds 1e-10

### Requirement: Hardware capability reporting
The system SHALL report detected hardware capabilities relevant to transform acceleration.

#### Scenario: SQL-callable capability report
- **WHEN** a user calls `SELECT postgis_accel_features()`
- **THEN** the system SHALL return a text report including: CPU SIMD features (AVX2/AVX-512/NEON), GPU backend (CUDA/ROCm/oneAPI/none), GPU device name, GPU compute capability, and Valkey batching status (enabled/disabled)
