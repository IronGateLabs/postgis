## Context

PR #13 (`apple-metal-gpu-backend`) shipped the Metal backend with a correct but slow `rotate_z_m_epoch` path. The correctness fix moved the Earth Rotation Angle computation from the GPU kernel (where it produced ~900 km of error per point because float32 could not represent decimal-year epochs precisely enough) to a serial fp64 loop on the host. Benchmarks on Apple A18 Pro at 500K points:

| Path                                           | Throughput   |
|------------------------------------------------|--------------|
| Scalar fp64 (`ptarray_rotate_z_m_epoch_scalar`) | ~58 Mpts/s   |
| NEON fp64 (`ptarray_rotate_z_m_epoch_neon`)     | ~76 Mpts/s   |
| Metal fp32 with serial host ERA precompute      | ~70 Mpts/s   |
| Metal fp32 with **SIMD host ERA precompute**    | ~130 Mpts/s (target) |

The serial loop in `lwgpu_metal_rotate_z_m_epoch()` runs at ~58–60 Mpts/s (essentially the scalar fp64 throughput minus the GPU dispatch cost). NEON's 2-wide `float64x2_t` ERA computation runs at ~76 Mpts/s by itself; reused as a thetas-only helper (without the rotation step) it should run at roughly twice that — ~150 Mpts/s — because it's doing half the work per loop iteration. The GPU dispatch + rotate adds ~1 ms per call regardless of point count, so for 500K points the GPU portion is amortized to ~2 Mpts/s of overhead.

Combined throughput estimate: `1 / (1/150 + 1/very-large) ≈ 130 Mpts/s` for the SIMD-host + GPU-rotate path, restoring Metal as a clear ~1.7× win over NEON.

The existing NEON file `liblwgeom/accel/rotate_z_neon.c` already contains the ERA computation in 2-wide intrinsic form. Splitting it into a thetas-only helper is a refactor of existing logic, not new SIMD code. Risk is low.

## Goals / Non-Goals

**Goals:**
- Restore Metal `rotate_z_m_epoch` as a clear throughput win (≥1.5× NEON) at the existing 50K dispatch threshold on Apple A18 Pro
- Reuse the existing NEON ERA computation logic (no new SIMD intrinsics needed beyond what `rotate_z_neon.c` already has)
- Keep the precision contract bit-identical to PR #13: thetas are computed in fp64 and narrowed to float exactly once per point, matching the scalar reference within the FP32_EARTH_SCALE_TOLERANCE
- Add benchmark instrumentation that breaks down rotate_z_m_epoch into "host ERA precompute" vs "GPU kernel" so future tuning has visibility
- Add a new CUnit test that explicitly exercises the SIMD-accelerated path and not the scalar fallback

**Non-Goals:**
- Implementing AVX2 or AVX-512 variants of the ERA helper. Apple Silicon is the only platform where Metal matters; AVX would only be exercised on (discontinued) Intel Macs or in cross-build tests. A scalar fallback covers correctness on non-NEON hosts.
- Implementing on-GPU double-single (df64) precision emulation. Out of scope; tracked as a hypothetical future change in the multi-vendor-gpu-rollout roadmap.
- Re-enabling Metal dispatch for `rotate_z_uniform` or `rad_convert`. Both are correctly bandwidth-bound and NEON wins; the dispatch gating in `lwgeom_accel.c` should remain.
- Re-tuning the `effective_gpu_threshold()` 5x multiplier. May be appropriate after this change lands but should be a separate focused change with its own benchmark data.
- Touching the CUDA, ROCm, oneAPI backends. Those are FP64_NATIVE and don't have this precision/parallelism tradeoff.

## Decisions

### Decision 1: New helper, not a refactor of existing rotate_z_m_epoch_neon

**Choice:** Add a new function `lwgeom_accel_era_thetas_neon(const double *m_in, size_t stride_doubles, uint32_t npoints, size_t m_offset, int direction, float *thetas_out)` in a new file `liblwgeom/accel/era_thetas_neon.c`. The existing `ptarray_rotate_z_m_epoch_neon()` is left untouched.

**Rationale:** The existing NEON function does ERA + rotation in a single fused loop, which is optimal when the caller wants both. Splitting it into two phases would force everyone (including the standalone NEON path used when GPU is unavailable) to make two passes over the data — half a regression for the existing code path to fix the new one. A separate helper specifically for the GPU-feeding case keeps both paths at their optimum.

**Alternatives considered:**
- **(A) Refactor existing function with an output-mode flag.** Adds branching to a hot loop, slightly worse codegen, awkward API.
- **(B) Inline the ERA-only loop in `gpu_metal.m` directly.** Couples GPU code to NEON intrinsics; harder to test independently; not portable to future SIMD backends.

### Decision 2: Reuse `lwgeom_accel.h` dispatch table

**Choice:** Add a new function-pointer field `era_thetas` to the existing `LW_ACCEL_DISPATCH` struct in `liblwgeom/lwgeom_accel.h`. The existing `lwaccel_init()` function populates the field with the NEON variant, AVX2 variant (when built), or scalar fallback based on the same CPU detection it already does.

**Rationale:** The dispatch table is the established pattern for runtime SIMD selection. `gpu_metal.m` calls `lwaccel_get()->era_thetas(...)` and gets the right variant for the build/host. No new infrastructure needed.

### Decision 3: Output buffer ownership

**Choice:** The caller (`lwgpu_metal_rotate_z_m_epoch`) allocates the `float *thetas` buffer and passes it to the helper. The helper writes into the caller's buffer and does not allocate. This matches the existing `ptarray_*` SIMD function conventions.

**Rationale:** Keeps allocation policy at the dispatch site (where lifetime is clear) and avoids hidden mallocs in the inner loop. The SIMD helper can be unit-tested by passing a stack-allocated buffer.

### Decision 4: Direction parameter handling

**Choice:** The helper takes `int direction` and applies the sign multiplication inside the SIMD loop, not on the host pre/post. Passing `direction = -1` flips the sign of every reduced theta before writing it to the output buffer.

**Rationale:** Mirrors what `gpu_metal.m`'s current serial loop does (and what `lweci_earth_rotation_angle` does in the scalar reference). Applying direction inside the SIMD loop costs one negate per lane, negligible cost.

### Decision 5: Bench instrumentation breakdown

**Choice:** `bench_metal.c` adds a new "rotate_z_m_epoch_breakdown" benchmark mode (or sub-output) that times the host ERA precompute and the GPU kernel dispatch+execute as separate phases, in addition to the existing wall-clock total. CSV output gains two new columns: `host_era_us` and `gpu_kernel_us`.

**Rationale:** Without the breakdown, future regressions could hide in either phase. Reviewers can see at a glance whether the SIMD ERA helper is doing its job.

## Risks / Trade-offs

- **[Risk] SIMD ERA helper bug produces wrong thetas silently** → mitigated by the new CUnit test that compares Metal output (with SIMD helper in the dispatch path) against scalar reference within FP32_EARTH_SCALE_TOLERANCE. The existing tests would also catch any regression but the new dedicated test makes the assertion specific.
- **[Risk] NEON `float64x2_t` precision differs from scalar fp64 by more than 1 ULP** → not actually a risk in practice (NEON `float64x2_t` is bit-exact fp64 by spec), but the test catches it if some compiler bug exists.
- **[Trade-off] Two separate NEON code paths for ERA computation** (the existing fused `rotate_z_m_epoch_neon` and the new thetas-only helper). Slight code duplication. Considered acceptable because the alternatives (refactor with mode flag, or inline in gpu_metal.m) are worse on different axes.
- **[Trade-off] Adds an internal helper that's only used by Metal** for now. If a future GPU backend (e.g., a hypothetical mobile GPU with no fp64) needs the same precompute, the helper is already in the dispatch table and reusable.

## Migration Plan

1. Land this change after PR #13 merges to develop. (If PR #13 is rebased or revised, this change rebases onto it.)
2. Run `cu_tester metal` and `bench_metal` locally on Apple A18 Pro to verify both correctness and the target throughput.
3. If the target throughput (≥130 Mpts/s at 500K) is missed by more than 10%, investigate whether the SIMD helper is being selected and whether the GPU dispatch overhead is higher than expected. Do not merge until the perf goal is met or explicitly waived.
4. Update `effective_gpu_threshold()` documentation comment in `lwgeom_accel.c` with the new measured Metal performance, but do NOT change the 5× multiplier in this commit (separate change if appropriate).
5. Mark the *Trade-off: Host-side ERA precomputation serializes the hot path* entry in `apple-metal-gpu-backend/design.md` as resolved.

**Rollback:** If the SIMD helper has unexpected issues, revert this change. `gpu_metal.m` falls back to the scalar serial loop (the state after PR #13), which is correct but slow. No data corruption risk because the precision contract is unchanged.

## Open Questions

1. **Should the new helper also produce the per-point `cos_t` and `sin_t` instead of the raw theta?** — Could save the GPU one cos/sin call per point but adds CPU work. Defer until we have a benchmark showing it's worthwhile.
2. **Can the same SIMD helper apply to a future CUDA-on-ARM scenario** (DGX Spark)? CUDA's `gpu_cuda.cu` is FP64_NATIVE so the precision concern doesn't apply, but the SIMD helper might still be a perf win for very large arrays where moving fp64 epochs over PCIe is slower than computing them on-host. Out of scope here, flagged for the multi-vendor-gpu-rollout roadmap.
