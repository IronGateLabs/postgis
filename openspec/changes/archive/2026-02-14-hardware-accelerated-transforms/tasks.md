## 1. Build System and Feature Detection

- [x] 1.1 Add `configure.ac` checks for AVX2 (`-mavx2 -mfma`), AVX-512 (`-mavx512f`), and ARM NEON, setting `HAVE_AVX2`, `HAVE_AVX512`, `HAVE_NEON` defines
- [x] 1.2 Add `configure.ac` checks for CUDA toolkit (`nvcc`), ROCm/HIP (`hipcc`), and oneAPI/SYCL (`icpx`), setting `HAVE_CUDA`, `HAVE_ROCM`, `HAVE_ONEAPI`
- [x] 1.3 Add `--enable-avx2`, `--enable-avx512`, `--disable-simd`, `--with-cuda`, `--with-rocm`, `--with-oneapi` configure flags with auto-detect defaults
- [x] 1.4 Add `Makefile.in` rules for compiling `.cu` (CUDA), `.hip` (ROCm), and SYCL `.cpp` files in `liblwgeom/accel/`
- [x] 1.5 Add runtime `cpuid` detection function in `liblwgeom/lwgeom_accel.c` that probes CPU features at first use and caches the result

## 2. SIMD-Accelerated ECI Rotation

- [x] 2.1 Create `liblwgeom/lwgeom_accel.h` with acceleration dispatch API: function pointers for `ptarray_rotate_z_accel()`, `ptarray_rotate_z_m_epoch_accel()`, and `ptarray_rad_convert_accel()`
- [x] 2.2 Create `liblwgeom/accel/rotate_z_avx2.c` implementing uniform-epoch Z-rotation using AVX2 intrinsics (`_mm256_fmadd_pd`, gather/scatter for AoS layout)
- [x] 2.3 Create `liblwgeom/accel/rotate_z_avx512.c` implementing uniform-epoch Z-rotation using AVX-512 intrinsics (8-wide)
- [x] 2.4 Create `liblwgeom/accel/rotate_z_neon.c` implementing uniform-epoch Z-rotation using ARM NEON intrinsics (2-wide)
- [x] 2.5 Implement per-point M-epoch SIMD variant in AVX2: compute sin/cos per point, then SIMD the multiply-add
- [x] 2.6 Modify `ptarray_rotate_z()` in `lwgeom_eci.c` to dispatch through the acceleration API (SIMD if available, scalar fallback)
- [x] 2.7 Modify `ptarray_rotate_z_m_epoch()` in `lwgeom_eci.c` to dispatch through the acceleration API

## 3. SIMD-Accelerated Radian/Degree Conversion

- [x] 3.1 Create `liblwgeom/accel/rad_convert_avx2.c` implementing batch multiply-by-constant for rad↔deg conversion
- [x] 3.2 Modify `ptarray_transform()` in `liblwgeom/lwgeom_transform.c` to use SIMD radian/degree conversion for the pre/post PROJ loops
- [x] 3.3 Add ARM NEON variant of radian/degree conversion

## 4. GPU Abstraction Layer

- [x] 4.1 Create `liblwgeom/lwgeom_gpu.h` with the GPU abstraction API: `lwgpu_init()`, `lwgpu_rotate_z_batch()`, `lwgpu_rotate_z_m_epoch_batch()`, `lwgpu_shutdown()`, `lwgpu_available()`
- [x] 4.2 Create `liblwgeom/accel/gpu_cuda.cu` implementing the CUDA backend for Z-rotation kernel
- [x] 4.3 Create `liblwgeom/accel/gpu_rocm.hip` implementing the ROCm/HIP backend for Z-rotation kernel
- [x] 4.4 Create `liblwgeom/accel/gpu_oneapi.cpp` implementing the oneAPI/SYCL backend for Z-rotation kernel
- [x] 4.5 Add threshold-based dispatch in `ptarray_rotate_z()`: if `npoints >= threshold && lwgpu_available()`, dispatch to GPU; else use CPU SIMD
- [x] 4.6 Add GUC parameter `postgis.gpu_dispatch_threshold` (default 10000) and `postgis.gpu_backend` (default 'auto')

## 5. Valkey Batch Integration

- [x] 5.1 Add `configure.ac` check for Valkey/hiredis client library
- [x] 5.2 Create `postgis/lwgeom_valkey_batch.c` with background worker that monitors a Valkey list for transform requests
- [x] 5.3 Implement batch accumulation logic: flush when batch size threshold or time window (100ms default) is reached
- [x] 5.4 Implement GPU batch dispatch from the background worker and result write-back to Valkey
- [x] 5.5 Add GUC parameters: `postgis.gpu_valkey_url`, `postgis.gpu_batch_size` (default 10000), `postgis.gpu_batch_timeout_ms` (default 100)
- [x] 5.6 Add graceful fallback when Valkey is unavailable: synchronous CPU SIMD transform with warning

## 6. PG-Strom Integration

- [x] 6.1 Install PG-Strom in the Docker build environment and verify it works with PostGIS
- [x] 6.2 Benchmark PG-Strom with standard PostGIS functions (ST_Distance, ST_DWithin) on ECEF geometries: document which functions are GPU-accelerated
- [x] 6.3 Test ECI-specific functions (ST_ECEF_To_ECI, ST_ECI_To_ECEF, ST_Transform with ECI SRIDs) under PG-Strom: document GPU fallback behavior
- [x] 6.4 Write CUDA device function for ERA computation and Z-rotation (`gpu_eci_rotate.cu`) compatible with PG-Strom's device function interface
- [x] 6.5 Write test cases validating GPU device function numerical equivalence with CPU implementation
- [x] 6.6 Package as a PG-Strom contribution (patch or PR against `gpu_postgis.cu`)

## 7. Benchmarking Harness

- [x] 7.1 Create `liblwgeom/bench/bench_accel.c` benchmarking tool with `--operation`, `--backend`, `--validate` flags
- [x] 7.2 Implement ECI rotation benchmark: scalar, AVX2, AVX-512, NEON, CUDA, ROCm, oneAPI across point counts (1, 100, 1K, 10K, 100K, 1M, 10M)
- [x] 7.3 Implement PROJ transform benchmark: full `ptarray_transform()` pipeline with SIMD rad/deg conversion
- [x] 7.4 Implement GPU dispatch overhead benchmark: measure allocation, transfer, kernel launch separately
- [x] 7.5 Implement `--validate` mode: compare all backends against scalar, fail if max difference > 1e-10
- [x] 7.6 Add CSV output and text-mode comparison chart

## 8. SQL Interface and Reporting

- [x] 8.1 Add `postgis_accel_features()` SQL function returning detected SIMD, GPU, and Valkey capabilities
- [x] 8.2 Add regression tests verifying SIMD-accelerated transforms produce identical results to scalar
- [x] 8.3 Add regression tests for GPU dispatch threshold behavior (mock or skip if no GPU)
- [x] 8.4 Add regression tests for `postgis_accel_features()` output format
- [x] 8.5 Update `doc/ecef_eci.xml` with acceleration configuration section (GUC parameters, build flags)

## 9. Integration Testing

- [x] 9.1 Run full CUnit test suite (CRS_Family + ECI) with SIMD paths enabled, verify zero regressions
- [x] 9.2 Run `regress/core/ecef_eci.sql` regression tests with SIMD paths enabled
- [x] 9.3 Run benchmark harness with `--validate` on CI (at minimum: scalar + AVX2)
- [x] 9.4 If GPU available in CI: run GPU backend tests and validate numerical equivalence
- [x] 9.5 Document benchmark results in a results table comparing scalar vs SIMD vs GPU throughput
