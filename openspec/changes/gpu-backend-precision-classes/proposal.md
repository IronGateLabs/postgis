## Why

PR #13 (`apple-metal-gpu-backend`) introduced a two-class precision model for GPU backends — `FP64_NATIVE` and `FP32_ONLY` — to articulate why Metal is the only backend that cannot be bit-equivalent to the scalar fp64 reference. The classification lives **buried inside the Metal-specific spec** (`specs/metal-compute-kernels/spec.md`), which means:

1. Future GPU backends (CUDA on DGX Spark, ROCm on AMD, oneAPI on Intel) have no obvious place to declare their precision class. A reviewer of the next CUDA validation change would have to either (a) re-derive the classification from scratch or (b) chase a cross-reference into the Metal spec to find a one-paragraph definition.

2. The test-tolerance invariant ("FP64_NATIVE tests use ≤ 1e-10 absolute tolerance, FP32_ONLY tests use scale-relative `max_coord × 1e-6`") is not codified anywhere reviewable. Future tests will silently use whatever tolerance the author guessed, repeating the exact bug PR #13 just fixed.

3. The dispatch policy ("FP64_NATIVE backends are safe for any operation; FP32_ONLY backends require per-operation precision review") has no enforcement mechanism — it relies on humans reading design.md from one specific OpenSpec change.

4. The user's stated multi-phase rollout plan (Metal → CUDA on ARM → CUDA on x86 → ROCm → oneAPI) is going to add four more backends. Each addition will need to declare its precision class. Doing that against a cross-cutting capability is dramatically cleaner than amending the Metal spec.

This change formalizes the precision class concept as **its own top-level capability** so all current and future GPU backends can declare against it, the test invariants are reviewable, and the dispatch policy has a clear contract surface.

## What Changes

- Create a new capability `gpu-precision-classes` with an authoritative definition of `FP64_NATIVE` and `FP32_ONLY` backend classes, the per-class precision contracts, and the test-tolerance invariant
- Document the classification of each existing backend (scalar fp64, NEON, AVX2, AVX-512, CUDA, ROCm, oneAPI, Metal) — all FP64_NATIVE except Metal which is FP32_ONLY
- Define the dispatch-policy contract: FP64_NATIVE backends MAY be invoked for any operation gated only by performance threshold; FP32_ONLY backends MUST have per-operation review documenting that the precision contract is acceptable for the operation's domain
- Extract the FP32_EARTH_SCALE_TOLERANCE constant from `cu_metal.c` into a shared `liblwgeom/cunit/cu_accel_tolerances.h` header so future tests for any FP32_ONLY backend can reuse the same scale-relative bound (and the value can be tuned in one place)
- Add a placeholder `enum LW_GPU_PRECISION_CLASS { LW_GPU_PRECISION_FP64_NATIVE = 0, LW_GPU_PRECISION_FP32_ONLY = 1 }` and `LW_GPU_PRECISION_CLASS lwgpu_backend_precision_class(LW_GPU_BACKEND b)` accessor in `liblwgeom/lwgeom_gpu.h`. The accessor returns a constant per backend for now; in the future it could become a runtime query if needed (e.g., for a hypothetical "configurable precision" backend)
- Update the Metal `specs/metal-compute-kernels/spec.md` to reference the new top-level capability instead of duplicating the FP32_ONLY definition inline
- Update the existing `apple-metal-gpu-backend` design.md Decision 9 to point at the new capability spec as the authoritative source

## Capabilities

### New Capabilities

- `gpu-precision-classes`: Authoritative cross-backend definition of GPU precision classes (`FP64_NATIVE`, `FP32_ONLY`), per-class contracts (absolute/relative error bounds, dispatch policy, test tolerance invariants), and the classification of each backend currently in the GPU abstraction. Provides a single reviewable contract surface for all current and future backends.

### Modified Capabilities

None directly. The Metal spec gets light updates to reference the new top-level capability instead of defining its own precision model, but this is a documentation refactor — no behavior changes.

## Impact

- **Code**: small additions only.
  - `liblwgeom/lwgeom_gpu.h`: new enum + accessor declaration (~10 lines)
  - `liblwgeom/lwgeom_gpu.c`: accessor implementation (~15 lines, just a constant return per backend)
  - `liblwgeom/cunit/cu_accel_tolerances.h`: new header with FP32_EARTH_SCALE_TOLERANCE and (future) other shared tolerance constants
  - `liblwgeom/cunit/cu_metal.c`: include the new header instead of defining the constant locally
- **Specs**: new `openspec/specs/gpu-precision-classes/spec.md` (after this change archives) and a small update to the Metal change's spec to reference it
- **No behavior change.** This is documentation + a small accessor that returns a compile-time constant per backend. Existing tests and dispatch logic are untouched.
- **Cross-backend leverage**: the next time a GPU backend gets validated (CUDA on DGX Spark is the most likely first), the validator can write `CU_ASSERT(lwgpu_backend_precision_class(LW_GPU_CUDA) == LW_GPU_PRECISION_FP64_NATIVE)` and reference the established contract instead of re-litigating the precision question.

## Open Questions

1. **Should the dispatch layer enforce the precision policy at runtime?** — E.g., when a future operation registers itself, it could declare a "minimum required precision class" and the dispatch layer would refuse to invoke an FP32_ONLY backend for an operation that requires FP64. Out of scope for this change because no such operation registry exists yet, but it would be the natural next step if precision policy violations become a maintenance problem.
2. **Are there any backends that need a third class?** — A hypothetical "configurable" class where a single physical backend exposes both fp32 and fp64 modes via a runtime flag (some Intel iGPUs work this way via `cl_khr_fp64`, and CUDA can run kernels at fp32 for speed even when fp64 is available). For now, two classes cover all current and near-term backends; the enum can be extended later without breaking existing classifications.
3. **Should the precision class affect the dispatch threshold?** — FP32_ONLY backends have an inherent precision cost that scales with input magnitude, so it might make sense to apply a stricter threshold (or refuse dispatch entirely) for inputs at very large magnitudes. Currently the Metal `effective_gpu_threshold()` applies a uniform 5x multiplier; a magnitude-aware variant could be a future enhancement.
