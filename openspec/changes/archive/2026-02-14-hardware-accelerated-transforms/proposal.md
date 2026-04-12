## Why

PostGIS coordinate transform loops (ECI/ECEF rotation, PROJ pre/post radian conversion, ptarray iteration) have zero vectorization or parallelization today. For space-domain workloads — tracking objects from bolts to satellites in ECI frames — streaming transforms through PostgreSQL leave significant hardware capability unused. Modern CPUs have SIMD (AVX2/AVX-512/NEON) and modern servers increasingly have GPUs (NVIDIA, AMD, Intel). PG-Strom already demonstrates that GPU-accelerated PostGIS queries are viable. This change adds hardware-aware acceleration to PostGIS's core transform pipeline, with runtime feature detection and graceful fallback.

## What Changes

- Add SIMD-accelerated paths for per-point ECI/ECEF Z-axis rotation (`ptarray_rotate_z`, `ptarray_rotate_z_m_epoch`) using compile-time and runtime CPU feature detection (AVX2, AVX-512, ARM NEON)
- Add SIMD-accelerated paths for the radian/degree conversion loops in `ptarray_transform()` (pre/post PROJ)
- Evaluate PG-Strom integration for GPU-accelerating PostGIS spatial functions in WHERE clauses, and document how ECI functions can be contributed to PG-Strom's GPU-PostGIS layer
- Add optional GPU batch dispatch for large POINTARRAYs (threshold-based: CPU for small, GPU for large), supporting CUDA, ROCm, and oneAPI/SYCL via a thin abstraction layer
- Add Valkey-based transform batching layer: accumulate streaming point transforms and dispatch as GPU-sized batches when a threshold is reached
- Add `configure` detection for CPU SIMD features, CUDA toolkit, ROCm, oneAPI, and PG-Strom
- Add performance benchmarking harness comparing scalar, SIMD, OpenMP, and GPU paths across point counts (1, 100, 10K, 1M, 10M)

## Capabilities

### New Capabilities
- `simd-transform-acceleration`: CPU SIMD (AVX2/AVX-512/NEON) acceleration for per-point transform loops with runtime feature detection and scalar fallback
- `gpu-transform-dispatch`: GPU batch dispatch for large coordinate transforms with multi-vendor support (CUDA/ROCm/oneAPI), threshold-based routing, and Valkey batching integration
- `transform-benchmarking`: Performance benchmarking harness for comparing acceleration backends across workload sizes and hardware configurations
- `pgstrom-eci-integration`: PG-Strom evaluation and ECI function contribution for GPU-accelerated spatial queries

### Modified Capabilities
- `eci-coordinate-support`: ECI transform functions gain SIMD and GPU-accelerated code paths while preserving identical numerical results

## Impact

- **Build system**: `configure.ac` gains detection for AVX2/AVX-512/NEON, CUDA toolkit, ROCm, oneAPI, PG-Strom headers
- **Core libraries**: `liblwgeom/lwgeom_eci.c` gains SIMD rotate_z variants; new `liblwgeom/lwgeom_accel.c` for dispatch logic
- **Dependencies**: Optional: CUDA toolkit, ROCm/HIP, oneAPI/SYCL, Valkey client library, PG-Strom headers
- **CI**: New matrix entries for SIMD feature flags and GPU runner (if available)
- **Regression**: All existing tests must produce bit-identical results regardless of acceleration path
