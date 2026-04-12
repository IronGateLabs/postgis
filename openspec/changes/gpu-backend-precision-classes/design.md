## Context

PostGIS has a layered hardware acceleration stack: scalar fp64, SIMD (NEON / AVX2 / AVX-512), and GPU (CUDA / ROCm / oneAPI / Metal). PR #13 (`apple-metal-gpu-backend`) shipped with a discovered constraint: Apple GPU shader cores have NO 64-bit floating-point ALUs, so the Metal backend cannot match the bit-level precision of the scalar fp64 reference. Every other backend in the stack uses fp64 hardware natively and IS bit-equivalent to the reference.

To articulate this asymmetry, PR #13's design.md introduced a `FP64_NATIVE` / `FP32_ONLY` two-class precision model. The model is sound but its physical location — buried inside `specs/metal-compute-kernels/spec.md` of one specific in-flight change — makes it discoverable only by people who already know about the Metal-specific issue. Future GPU backends, reviewers of future tests, and maintainers of the dispatch layer have no clean way to find or reference the classification.

This change extracts the precision class model into its own top-level capability spec so it can be linked from any backend's spec, any test's tolerance documentation, and any dispatch policy decision. It also adds a thin C-level accessor (`lwgpu_backend_precision_class(LW_GPU_BACKEND b)`) returning a compile-time constant per backend, so test code can assert the classification rather than embedding magic numbers.

## Goals / Non-Goals

**Goals:**
- Make the `FP64_NATIVE` / `FP32_ONLY` model **the** authoritative reference, located outside any single backend's spec
- Document each existing backend's classification in one place (scalar, NEON, AVX2, AVX-512, CUDA, ROCm, oneAPI, Metal)
- Define the test tolerance invariant once, not per-test-file
- Provide a tiny C accessor so future tests can verify class membership at runtime
- Establish the dispatch policy contract (what each class MAY and MUST do)
- Set up the surface that future backend additions (CUDA on DGX Spark, etc.) will declare against without re-deriving the model

**Non-Goals:**
- Changing any existing backend's behavior. This is documentation + a compile-time accessor.
- Implementing a runtime registry of operations and their minimum required precision class. Out of scope; a possible future enhancement.
- Defining additional precision classes beyond the two we have. The enum can be extended later if a third use case appears (e.g., "configurable precision" backend), but premature extension creates classification ambiguity.
- Changing the Metal `FP32_EARTH_SCALE_TOLERANCE` value or any other tolerance. Just moving it to a shared location.
- Touching the actual SIMD or GPU code. All changes are headers, the dispatch table file, and OpenSpec docs.

## Decisions

### Decision 1: New top-level capability, not a section in an existing spec

**Choice:** Create a new capability `gpu-precision-classes` with its own `spec.md`. Do not add the model as a section in `gpu-transform-dispatch` or any other existing capability.

**Rationale:**
- Cross-cutting concerns deserve their own spec — they apply to multiple capabilities (any spec touching GPU dispatch references this), so co-locating them with one parent capability creates artificial coupling.
- The precision class model is stable infrastructure (the two classes don't change as backends are added), so it makes sense as a foundational spec that other specs build on.
- Searchability: a developer looking for "what's the precision contract" can find a top-level capability immediately, instead of navigating into a backend-specific spec.

**Alternatives considered:**
- **(A) Section in `apple-metal-gpu-backend/specs/metal-compute-kernels/spec.md`** — current state, doesn't scale to other backends.
- **(B) Section in a hypothetical `gpu-transform-dispatch` capability** — couples the precision model to dispatch policy, which is not strictly the same concept (dispatch is about when/where; precision is about what the backend guarantees).
- **(C) Markdown file outside OpenSpec** — loses the validation and review structure that OpenSpec provides.

### Decision 2: Compile-time constant accessor, not runtime detection

**Choice:** `LW_GPU_PRECISION_CLASS lwgpu_backend_precision_class(LW_GPU_BACKEND b)` returns a constant for each enum value via a switch statement. The classification is determined at the source-code level, not by querying hardware capabilities at runtime.

**Rationale:**
- For the current backends, the classification is invariant: Apple Silicon GPUs are fp32-only forever (it's a hardware decision Apple made years ago), and CUDA/ROCm/oneAPI hardware has had fp64 for decades. There is no scenario where the same `LW_GPU_BACKEND` value maps to different precision classes at runtime.
- Compile-time classification enables test assertions like `STATIC_ASSERT(lwgpu_backend_precision_class(LW_GPU_CUDA) == LW_GPU_PRECISION_FP64_NATIVE)` if the C standard supported it (it doesn't quite, but the spirit is there).
- A future "configurable precision" backend (e.g., a backend that exposes both fp32 and fp64 modes via different `LW_GPU_BACKEND` values) would simply have two enum entries with different classifications — still no runtime detection needed.

**Alternatives considered:**
- **(A) Runtime detection via Metal/CUDA/ROCm device queries** — overkill, and not actually different from compile-time for the current set of backends.
- **(B) No accessor at all, just documentation** — loses the ability to write tests that fail at compile time / link time if a backend is misclassified.

### Decision 3: Shared tolerance header in `cu_accel_tolerances.h`

**Choice:** Create `liblwgeom/cunit/cu_accel_tolerances.h` containing `FP32_EARTH_SCALE_TOLERANCE` and (eventually) `FP64_STRICT_TOLERANCE = 1e-10`. Move the existing `FP32_EARTH_SCALE_TOLERANCE` definition out of `cu_metal.c` and into the header. Other future test files include the header.

**Rationale:**
- Multiple test files will need these constants (current Metal tests, future CUDA tests, future ROCm tests). Defining them once avoids drift.
- A single-file location for tolerance values means future tuning (e.g., if the FP32_EARTH_SCALE_TOLERANCE turns out to be too tight or too loose for a specific operation) happens in one place.
- Having `FP64_STRICT_TOLERANCE` and `FP32_EARTH_SCALE_TOLERANCE` side-by-side documents the contrast between the two classes' tolerance regimes.

### Decision 4: Backend classification as a table in the spec

**Choice:** The `gpu-precision-classes` spec includes a table with one row per current backend (scalar, NEON, AVX2, AVX-512, CUDA, ROCm, oneAPI, Metal) and columns for: precision class, hardware basis for the classification (e.g., "Apple Silicon shader cores have no fp64 ALUs"), expected error bound, and the dispatch policy that follows from the class.

**Rationale:** Tables are vastly easier to scan than a paragraph per backend, and a table makes it visually obvious which backend is the outlier (Metal is the only FP32_ONLY row).

## Risks / Trade-offs

- **[Risk] The new accessor adds a 1-line maintenance burden when a new backend is added** — anyone adding `LW_GPU_NEW_BACKEND` to the enum must also add a case to `lwgpu_backend_precision_class()`. **Mitigation**: the switch statement uses `default: return LW_GPU_PRECISION_FP64_NATIVE` as the safe-by-default fallback for SIMD-only backends, but explicit cases for each enum value are still recommended (and a `-Wswitch` warning catches missing entries). Reviewers should flag missing classifications during code review.

- **[Trade-off] Adding a placeholder accessor that nothing currently calls** — no production code uses `lwgpu_backend_precision_class()` today; only future tests will. Considered acceptable because the cost is ~25 lines of code and the alternative is "future tests have nowhere to assert against".

- **[Trade-off] Duplicates the FP32_EARTH_SCALE_TOLERANCE definition during the transition** — until cu_metal.c picks up the include, the constant is defined in both places. Single commit fixes this.

- **[Risk] Linking from the Metal spec to a separate capability spec** — OpenSpec has no first-class cross-spec link mechanism (just markdown links). The link from `metal-compute-kernels/spec.md` to `gpu-precision-classes/spec.md` is plain text. If the latter spec is ever renamed, the former breaks silently. **Mitigation**: tests should verify the linked spec exists at archive time. Out of scope here.

## Migration Plan

1. Land this change after PR #13 merges to develop. (If PR #13 is rebased or revised, this change rebases onto it.)
2. The new capability `gpu-precision-classes` is created at change-archive time as `openspec/specs/gpu-precision-classes/spec.md`.
3. The existing Metal spec is updated to reference the new top-level capability instead of defining its own model. The Metal spec retains a brief "Metal is FP32_ONLY because..." sentence for context, but the authoritative definitions live in the new capability.
4. The `cu_accel_tolerances.h` header is created with `FP32_EARTH_SCALE_TOLERANCE` (and the related `FP64_STRICT_TOLERANCE` for future reference). `cu_metal.c` is updated to include the header.
5. The accessor `lwgpu_backend_precision_class()` is added with explicit cases for every current `LW_GPU_BACKEND` enum value.
6. No tests change behavior; the new accessor has no callers in this commit. Subsequent OpenSpec changes (e.g., the CUDA validation work for DGX Spark) will be the first consumers.

**Rollback:** If the new spec or accessor causes any issue, revert this change. The Metal tests continue to pass because their tolerance definition is restored to `cu_metal.c`'s local `#define`. No runtime behavior changes.

## Open Questions

1. **Should the spec be named `gpu-precision-classes` or `accelerator-precision-classes`?** — Currently I'm using the former because GPU is where the classification matters in practice (every SIMD/scalar backend is FP64_NATIVE). But if a future SIMD path (e.g., a hypothetical fp16 SIMD batch) needs the same classification, the broader name would fit better. Defer until that hypothetical materializes.
2. **Should the accessor be `static inline` in the header or out-of-line in the .c file?** — Static inline gives the compiler the ability to fold the result at every call site, which is valuable for tests that branch on classification. Out-of-line is slightly easier to debug. Recommendation: static inline.
3. **Should this change extend to AVX/NEON/scalar dispatch precision too?** — All non-GPU backends are FP64_NATIVE so the classification is trivially "FP64_NATIVE", but the accessor `lwgpu_*` is GPU-specific. A parallel `lwaccel_backend_precision_class()` could be added but would always return FP64_NATIVE, which feels unnecessary. Leaving it out for now.
