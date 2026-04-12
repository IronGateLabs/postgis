## Phase tracking checklist

This is a **living checklist**. Items get checked off as phases complete. Links to focused implementation OpenSpec changes are added inline as those changes are spawned. This change does NOT itself archive when items complete.

## Phase 0: Foundation

- [ ] 0.1 OpenSpec change `gpu-backend-precision-classes` proposed (sibling of this change)
- [ ] 0.2 OpenSpec change `gpu-backend-precision-classes` approved and merged
- [ ] 0.3 Implementation: `LW_GPU_PRECISION_CLASS` enum and `lwgpu_backend_precision_class()` accessor in `liblwgeom/lwgeom_gpu.h`
- [ ] 0.4 Implementation: `liblwgeom/cunit/cu_accel_tolerances.h` with `FP32_EARTH_SCALE_TOLERANCE` and `FP64_STRICT_TOLERANCE`
- [ ] 0.5 Implementation: `cu_metal.c` updated to use shared header
- [ ] 0.6 Implementation: capability spec at `openspec/specs/gpu-precision-classes/spec.md` after archive
- [ ] 0.7 Verification: `cu_tester metal` still passes 6/6 with the shared header

## Phase 1: Apple Metal

### 1a. Initial validation (PR #13)

- [x] 1a.1 OpenSpec change `apple-metal-gpu-backend` proposed
- [ ] 1a.2 PR #13 (`rebase/apple-metal-gpu-backend-2026-04-10` → `develop`) merged on fork
- [x] 1a.3 `cu_tester metal` passes 6/6 on Apple A18 Pro
- [x] 1a.4 Metal output within `FP32_EARTH_SCALE_TOLERANCE` of scalar reference
- [x] 1a.5 OpenSpec change internally consistent (no double/float contradictions)

### 1b. Performance follow-up (`metal-simd-era-precompute` sibling change)

- [ ] 1b.1 OpenSpec change `metal-simd-era-precompute` proposed (sibling of this change, this batch)
- [ ] 1b.2 OpenSpec change `metal-simd-era-precompute` approved and merged
- [ ] 1b.3 Implementation: `lwgeom_accel_era_thetas_neon` helper in `liblwgeom/accel/era_thetas_neon.c`
- [ ] 1b.4 Implementation: `gpu_metal.m` refactored to use the helper
- [ ] 1b.5 Verification: `bench_metal` rotate_z_m/metal at 500K points ≥ 114 Mpts/s (1.5x NEON 76 Mpts/s)
- [ ] 1b.6 Update `apple-metal-gpu-backend/design.md` Risks section to mark perf trade-off as resolved

## Phase 2: NVIDIA CUDA on ARM (DGX Spark)

**Hardware status (2026-04-11)**: Waiting on user hardware acquisition.

**Prerequisites**:
- DGX Spark or equivalent Grace + Hopper system with SSH access
- CUDA Toolkit 12+ for ARM Linux
- Phase 0 complete

- [ ] 2.1 Hardware acquired and verified accessible
- [ ] 2.2 OpenSpec change `cuda-gpu-validation-on-arm` spawned from this roadmap
- [ ] 2.3 OpenSpec change has all 4 artifacts (proposal, design, specs, tasks) and validates
- [ ] 2.4 Implementation: existing `gpu_cuda.cu` builds via `nvcc` on the target
- [ ] 2.5 Implementation: `cu_cuda.c` test suite created (modeled after `cu_metal.c`, using `FP64_STRICT_TOLERANCE`)
- [ ] 2.6 Implementation: `cu_tester cuda` passes all tests on DGX Spark
- [ ] 2.7 Implementation: `lwgpu_backend_precision_class(LW_GPU_CUDA)` returns `FP64_NATIVE` (asserted in test)
- [ ] 2.8 Implementation: `bench_cuda` (or extension of bench_accel) produces throughput numbers
- [ ] 2.9 Implementation: dispatch threshold tuned for Grace + Hopper (likely lower than current 50K)
- [ ] 2.10 Verification: throughput beats NEON at the new threshold by ≥ 1.5x
- [ ] 2.11 OpenSpec change merged
- [ ] 2.12 OpenSpec change archived

## Phase 3: NVIDIA CUDA on x86_64

**Hardware status (2026-04-11)**: Waiting on user hardware acquisition.

**Prerequisites**:
- x86_64 Linux workstation with NVIDIA discrete GPU
- CUDA Toolkit 12+
- Phase 2 complete (so the source has been validated; this phase confirms portability)

- [ ] 3.1 Hardware acquired and verified accessible
- [ ] 3.2 OpenSpec change `cuda-gpu-validation-on-x86` spawned from this roadmap
- [ ] 3.3 If Phase 2 changes apply unmodified, this change may be small (just CI integration). If x86-specific issues arise, larger.
- [ ] 3.4 `cu_tester cuda` passes on the x86 target
- [ ] 3.5 `bench_cuda` produces x86 throughput numbers for comparison with ARM CUDA
- [ ] 3.6 OpenSpec change merged and archived

## Phase 4: AMD ROCm/HIP

**Hardware status (2026-04-11)**: Waiting on user hardware acquisition.

**Prerequisites**:
- AMD GPU with ROCm support (Instinct, Radeon Pro, Radeon RX 7000)
- ROCm 6+ installed
- Phase 0 complete

- [ ] 4.1 Hardware acquired and verified accessible
- [ ] 4.2 OpenSpec change `rocm-gpu-validation` spawned from this roadmap
- [ ] 4.3 Existing `gpu_rocm.hip` builds via `hipcc` on the target
- [ ] 4.4 `cu_rocm.c` test suite created (modeled after `cu_metal.c`/`cu_cuda.c`, using `FP64_STRICT_TOLERANCE`)
- [ ] 4.5 `cu_tester rocm` passes all tests
- [ ] 4.6 `lwgpu_backend_precision_class(LW_GPU_ROCM)` returns `FP64_NATIVE` (asserted)
- [ ] 4.7 `bench_rocm` produces throughput numbers
- [ ] 4.8 Dispatch threshold tuned
- [ ] 4.9 OpenSpec change merged and archived

## Phase 5: Intel oneAPI/SYCL

**Hardware status (2026-04-11)**: Waiting on user hardware acquisition.

**Prerequisites**:
- Intel GPU with oneAPI support (Arc, Data Center GPU Max, fp64-capable Iris Xe)
- oneAPI Base Toolkit 2024+
- Phase 0 complete

- [ ] 5.1 Hardware acquired and verified accessible
- [ ] 5.2 Verify the available Intel hardware actually supports fp64 compute (not all consumer iGPUs do). If not, escalate to spec authors as a "do we need a third precision class?" question
- [ ] 5.3 OpenSpec change `oneapi-gpu-validation` spawned from this roadmap
- [ ] 5.4 Existing `gpu_oneapi.cpp` builds via `icpx` on the target
- [ ] 5.5 `cu_oneapi.c` test suite created
- [ ] 5.6 `cu_tester oneapi` passes
- [ ] 5.7 `lwgpu_backend_precision_class(LW_GPU_ONEAPI)` returns `FP64_NATIVE` (asserted) — assuming the target hardware has fp64
- [ ] 5.8 `bench_oneapi` produces throughput numbers
- [ ] 5.9 OpenSpec change merged and archived

## Phase 6: Cross-vendor benchmark harness

**Status**: Waiting on phases 1b, 2 (or 3), 4, 5 to all be complete.

- [ ] 6.1 OpenSpec change `cross-vendor-benchmark-harness` spawned from this roadmap once at least three of {Metal, CUDA, ROCm, oneAPI} have been validated
- [ ] 6.2 Single benchmark harness that runs all available backends on the same input data and produces apples-to-apples throughput numbers
- [ ] 6.3 Summary table added to `openspec/specs/gpu-precision-classes/spec.md` (or its successor) with measured throughput per backend per operation per point count
- [ ] 6.4 Data captured for the eventual upstream PostGIS PR's "why this is worth it" pitch
- [ ] 6.5 OpenSpec change merged and archived

## Phase 7: Upstream contribution to postgis/postgis

**Status**: Waiting on Phase 6 (need cross-vendor data to write a compelling upstream pitch).

- [ ] 7.1 Reach out to postgis-devel mailing list with summary and benchmark data
- [ ] 7.2 Discuss preferred PR structure (one big PR vs phased PRs by backend)
- [ ] 7.3 Spawn OpenSpec change `upstream-postgis-gpu-contribution` capturing the agreed structure and review process
- [ ] 7.4 Submit upstream PR(s) following the agreed structure
- [ ] 7.5 Iterate based on upstream review feedback
- [ ] 7.6 Land at least the precision-classes capability and one backend (likely Metal or CUDA) upstream

## Living document maintenance

- [ ] L.1 Update the at-a-glance status table in `proposal.md` whenever a phase status changes
- [ ] L.2 Add inline links from this checklist to spawned focused OpenSpec changes as they are created
- [ ] L.3 Review the roadmap quarterly to update hardware status and re-prioritize as needed
- [ ] L.4 If the OpenSpec validator ever flags this change as stale or requiring archive, point at design.md Decision 3 explaining why this change is intentionally never archived
