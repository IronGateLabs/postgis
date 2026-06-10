## 1. New capability spec

- [ ] 1.1 (Done by this OpenSpec change at archive time) The capability `gpu-precision-classes` is created at `openspec/specs/gpu-precision-classes/spec.md` from the spec delta in this change

## 2. Header additions

- [ ] 2.1 Add `LW_GPU_PRECISION_CLASS` enum to `liblwgeom/lwgeom_gpu.h` with values `LW_GPU_PRECISION_FP64_NATIVE = 0` and `LW_GPU_PRECISION_FP32_ONLY = 1`
- [ ] 2.2 Add `static inline LW_GPU_PRECISION_CLASS lwgpu_backend_precision_class(LW_GPU_BACKEND b)` to `liblwgeom/lwgeom_gpu.h` with explicit case for each existing enum value: NONE/CUDA/ROCM/ONEAPI return FP64_NATIVE, METAL returns FP32_ONLY
- [ ] 2.3 Add a comment block above the accessor pointing readers to the `gpu-precision-classes` capability spec for the authoritative definitions

## 3. Shared tolerance header

- [ ] 3.1 Create `liblwgeom/cunit/cu_accel_tolerances.h` containing `FP32_EARTH_SCALE_TOLERANCE` (computed as `6378137.0 * 1e-6`) and `FP64_STRICT_TOLERANCE` (`1e-10`)
- [ ] 3.2 Add a header comment explaining the rationale (cross-backend consistency, single point of tuning) and pointing at the `gpu-precision-classes` capability spec
- [ ] 3.3 Update `liblwgeom/cunit/cu_metal.c` to `#include "cu_accel_tolerances.h"` and remove the local `#define FP32_EARTH_SCALE_TOLERANCE`

## 4. Metal spec cross-reference update

- [ ] 4.1 In `openspec/changes/apple-metal-gpu-backend/specs/metal-compute-kernels/spec.md`, replace the standalone "Metal backend precision class" requirement with a reference to the new `gpu-precision-classes` capability. Keep a one-paragraph context note ("Metal is FP32_ONLY because Apple GPU shader cores have no fp64 ALUs; see gpu-precision-classes spec for the authoritative model") but remove the duplicated definition.
- [ ] 4.2 In `openspec/changes/apple-metal-gpu-backend/design.md` Decision 9, replace the inline class definitions with a one-line "see gpu-precision-classes capability spec" reference. Keep the Metal-specific rationale (why hardware forces fp32) but defer the class definitions to the new spec.

## 5. Verification

- [ ] 5.1 Run `openspec validate gpu-backend-precision-classes` and confirm valid
- [ ] 5.2 Run `openspec validate apple-metal-gpu-backend` after the cross-reference updates and confirm still valid
- [ ] 5.3 Build the project (`make -j2`) and confirm the new header changes compile cleanly
- [ ] 5.4 Run `cu_tester metal` and confirm all 6 existing tests still pass with the tolerance now coming from the shared header
- [ ] 5.5 (Optional verification) Add one test assertion `CU_ASSERT_EQUAL(lwgpu_backend_precision_class(LW_GPU_METAL), LW_GPU_PRECISION_FP32_ONLY)` to `test_metal_init` in cu_metal.c to demonstrate the accessor in action -- if the test passes, the cross-backend infrastructure is working as designed

## 6. Merge and archive

- [ ] 6.1 Open implementation PR from a future branch (e.g., `feature/gpu-precision-classes-impl`) to fork develop. The PR includes the header additions, shared tolerance header, Metal spec cross-reference updates, and the verification test.
- [ ] 6.2 Wait for CI green
- [ ] 6.3 Merge to develop
- [ ] 6.4 Archive this OpenSpec change via `openspec archive gpu-backend-precision-classes` -- this creates `openspec/specs/gpu-precision-classes/spec.md` from the deltas in this change

## 7. Follow-up consumers

After this change archives, future OpenSpec changes that add new GPU backends or new tests SHOULD reference the `gpu-precision-classes` capability spec rather than defining their own precision model. Specifically:

- [ ] 7.1 (Future) `cuda-gpu-validation-on-arm` (DGX Spark): include `cu_accel_tolerances.h`, declare CUDA as FP64_NATIVE, use `FP64_STRICT_TOLERANCE`
- [ ] 7.2 (Future) Any new ROCm or oneAPI validation change: same pattern
- [ ] 7.3 (Future) Any operation registry for runtime precision policy enforcement: build on the `LW_GPU_PRECISION_CLASS` accessor as the primitive
