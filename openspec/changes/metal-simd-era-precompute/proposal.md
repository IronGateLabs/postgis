## Why

The Apple Metal GPU backend's `rotate_z_m_epoch` operation regressed from a (false) ~190 Mpts/sec at 500K points to a real ~70 Mpts/sec after PR #13 fixed the float32 precision bug. The fix moved Earth Rotation Angle (ERA) computation from the GPU kernel to a host-side serial fp64 loop, which is now the throughput bottleneck. NEON does the same work in 2-wide `float64x2_t` SIMD and ends up roughly tied with Metal (76 Mpts/s vs 70 Mpts/s) — defeating the entire purpose of GPU dispatch for this operation.

The fix is well-understood and the cost is modest: parallelize the host-side ERA precompute using SIMD intrinsics so the CPU does the ERA work in 2 (NEON) or 4 (AVX2) lanes per iteration instead of one. The reduced thetas array can then be passed to the Metal kernel as before, and the GPU does what it's actually good at — running thousands of cos/sin/multiply/add operations in parallel.

This is the smallest change that restores Metal as a real throughput win for `rotate_z_m_epoch` without re-introducing the precision bug. Future work (df64 emulation, on-GPU ERA with split coordinates, etc.) is still possible but not required.

## What Changes

- Add a new helper `lwgeom_accel_era_thetas_neon()` (and `_avx2`, `_scalar` fallbacks) in `liblwgeom/accel/` that takes an input array of doubles (the M-epoch column) and writes a parallel float array of pre-reduced rotation angles ready for Metal dispatch
- Add a corresponding entry in the `LW_ACCEL_DISPATCH` table (`liblwgeom/lwgeom_accel.h`) so the dispatch layer picks the right SIMD variant at runtime based on detected CPU features
- Refactor `lwgpu_metal_rotate_z_m_epoch()` in `liblwgeom/accel/gpu_metal.m` to call the dispatch table's ERA-thetas helper instead of doing the computation inline. The Metal kernel signature does NOT change — it still takes 3 buffers (data, params, thetas) at the same buffer indices.
- Add a new CUnit test `test_metal_rotate_z_m_epoch_simd_era` in `liblwgeom/cunit/cu_metal.c` that exercises the SIMD-accelerated path explicitly and verifies output matches the scalar reference within the existing `FP32_EARTH_SCALE_TOLERANCE` (~6.4 m for Earth-scale ECEF)
- Add benchmark instrumentation in `liblwgeom/bench/bench_metal.c` to break down the rotate_z_m_epoch timing into "ERA precompute (host)" vs "GPU kernel (device)" so future optimization can target the right phase
- Update the `apple-metal-gpu-backend` design.md *Trade-offs* section to mark the "Host-side ERA precomputation serializes the hot path" risk as resolved, with the new benchmark numbers
- Update `lwgeom_accel.c`'s comment block on `effective_gpu_threshold()` with the new measured crossover (Metal should now win at the existing 50K threshold by a healthy margin, not by 8% margin)

## Capabilities

### New Capabilities

None. The SIMD ERA helper is an internal implementation detail of the existing acceleration dispatch, not a new user-facing capability.

### Modified Capabilities

- `simd-transform-acceleration`: gains a new dispatch entry for `era_thetas` (the SIMD ERA precompute helper). The capability already covers SIMD-accelerated coordinate transforms; this adds one more operation to that surface. The existing dispatch detection logic (NEON / AVX2 / AVX-512 / scalar) is reused.

## Impact

- **Code**: new file `liblwgeom/accel/era_thetas_neon.c` (and conditional `era_thetas_avx2.c` for x86-64); new function in `liblwgeom/accel/rotate_z_common.h`; modified `liblwgeom/lwgeom_accel.h` (dispatch table entry); modified `liblwgeom/lwgeom_accel.c` (dispatch initialization); modified `liblwgeom/accel/gpu_metal.m` (call the dispatch helper instead of inline serial loop); modified `liblwgeom/cunit/cu_metal.c` (new test); modified `liblwgeom/bench/bench_metal.c` (timing breakdown).
- **Build system**: new files added to `liblwgeom/Makefile.in` under the existing SIMD compilation rules.
- **Performance**: target throughput at 500K points: Metal `rotate_z_m_epoch` ≥ 130 Mpts/s on A18 Pro (1.7× current 70 Mpts/s, 1.7× NEON 76 Mpts/s). On larger/newer Apple GPUs the relative win should be larger because the GPU portion scales while the SIMD-accelerated CPU portion scales linearly with cores.
- **Precision contract**: unchanged. Still FP32_ONLY at the kernel boundary; the SIMD helper computes thetas in fp64 (same `lweci_*` formulas as the scalar reference) and narrows to float exactly once per point. The existing `FP32_EARTH_SCALE_TOLERANCE` test bound applies.
- **Dispatch threshold**: may be re-tuned downward (from current 50K) once the SIMD ERA precompute lands and the crossover point shifts. Out of scope for this change but a likely follow-up.
- **No upstream PostGIS dependency changes** and **no new external dependencies**.

## Resolved Questions

1. **Why not df64 (double-single) arithmetic in the kernel?** — Considered. Estimated 4× slowdown per kernel thread vs raw float32 on the GPU side, which would still be slower than this SIMD-host approach for the M-epoch operation, AND would require maintaining a non-trivial compensated-arithmetic kernel implementation. SIMD-on-host is simpler, faster, and reuses existing well-tested NEON code paths. df64 remains a future option for operations where the host-precompute pattern doesn't apply.
2. **Why not local origin translation?** — Considered. Works for spatially clustered inputs but not for globally distributed ECI/ECEF data, and requires bookkeeping in the dispatch layer. The SIMD-host approach is universal and doesn't assume input clustering.
3. **Why a new helper instead of reusing `ptarray_rotate_z_m_epoch_neon`?** — The existing NEON function does *both* ERA computation and rotation in a single loop. Reusing it would require splitting it into two phases (ERA only, then rotation only) and threading state between them. Cleaner to add a new helper that just produces the thetas array, keeping the existing `ptarray_rotate_z_m_epoch_neon` untouched as the standalone-NEON code path.

## Open Questions

1. **AVX-512 variant needed?** — The Metal backend only matters on macOS (Apple Silicon = no AVX). The AVX2 helper would only be exercised in cross-compilation tests or on Intel Macs (which Apple has discontinued). Probably not worth the extra code; this change defaults to building only the NEON variant and the scalar fallback. Reviewer may disagree.
2. **Should the SIMD helper be exposed in the public C API?** — No, it's an internal helper for the dispatch layer. Static or `LWGEOM_INTERNAL` only.
