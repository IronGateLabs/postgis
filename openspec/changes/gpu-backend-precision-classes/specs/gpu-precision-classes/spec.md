## ADDED Requirements

### Requirement: Two precision classes for GPU backends

The PostGIS GPU abstraction layer SHALL classify every GPU backend into one of two precision classes:

- **`FP64_NATIVE`** — The backend's compute hardware exposes 64-bit IEEE 754 double-precision floating-point operations. Kernel arithmetic uses `double` throughout. Kernel output SHALL be bit-identical to the scalar fp64 reference implementation within the rounding error of normal fp64 arithmetic.
- **`FP32_ONLY`** — The backend's compute hardware exposes only 32-bit IEEE 754 single-precision floating-point operations. Kernel arithmetic uses `float` throughout. Kernel output SHALL satisfy a bounded absolute error contract proportional to input coordinate magnitude.

The classification SHALL be determined at the source-code level for each backend and SHALL NOT change at runtime for the same `LW_GPU_BACKEND` enum value. If a future use case requires a backend that exposes both fp32 and fp64 modes, that backend SHALL be represented by two separate enum values, each with its own classification.

#### Scenario: Each existing backend has exactly one precision class

- **WHEN** the `LW_GPU_BACKEND` enum is enumerated in `liblwgeom/lwgeom_gpu.h`
- **THEN** the following classifications SHALL hold:

  | Backend           | Enum value          | Class          | Hardware basis                                                                 |
  |-------------------|---------------------|----------------|--------------------------------------------------------------------------------|
  | scalar fp64       | (not in `LW_GPU_BACKEND`; treated as fp64 reference) | `FP64_NATIVE` | C `double` on host CPU                                                         |
  | NEON              | (not in `LW_GPU_BACKEND`; SIMD layer) | `FP64_NATIVE` | NEON `float64x2_t` is bit-exact fp64 by Arm spec                                |
  | AVX2              | (not in `LW_GPU_BACKEND`; SIMD layer) | `FP64_NATIVE` | AVX2 `__m256d` is bit-exact fp64 by Intel spec                                  |
  | AVX-512           | (not in `LW_GPU_BACKEND`; SIMD layer) | `FP64_NATIVE` | AVX-512 `__m512d` is bit-exact fp64 by Intel spec                               |
  | CUDA              | `LW_GPU_CUDA`       | `FP64_NATIVE`  | All NVIDIA discrete GPUs since Tesla (2008) have fp64 ALUs (variable throughput) |
  | ROCm/HIP          | `LW_GPU_ROCM`       | `FP64_NATIVE`  | All AMD GCN/CDNA GPUs have fp64 ALUs                                           |
  | oneAPI/SYCL       | `LW_GPU_ONEAPI`     | `FP64_NATIVE`  | Intel data-center GPUs (Ponte Vecchio, etc.) have fp64; consumer iGPU support varies but the backend is classified as FP64_NATIVE |
  | Apple Metal       | `LW_GPU_METAL`      | `FP32_ONLY`    | Apple Silicon GPU shader cores have NO 64-bit floating-point ALUs in any generation (M1–M4, all A-series). MSL does not expose `double` as a compute type in any version. |

- **AND** the only backend currently classified as `FP32_ONLY` SHALL be Metal
- **AND** any future GPU backend addition SHALL declare its classification in the same table and add a corresponding case to `lwgpu_backend_precision_class()` (see the accessor requirement below)

### Requirement: Per-class precision contracts

Each precision class SHALL have a documented absolute and relative error bound, equivalence guarantee against the scalar fp64 reference, and dispatch policy guideline. Tests for any backend SHALL use a tolerance appropriate to the backend's class.

| Class          | Absolute error bound                              | Relative error bound | Equivalence to scalar fp64 reference          | Dispatch policy                                                  |
|----------------|---------------------------------------------------|----------------------|-----------------------------------------------|------------------------------------------------------------------|
| `FP64_NATIVE`  | ≤ fp64 ULP of each operand (typically `≪ 1 mm` at Earth scale) | ≤ `2^{-52}` (~2.2e-16) | Bit-identical within fp64 rounding             | Safe for all operations; gated only by performance threshold      |
| `FP32_ONLY`    | ≤ `max_coord × 1e-6` (~6 m at Earth-scale ECEF, typically 1–2 m worst case) | ≤ `2^{-20}` (~1e-6)  | NOT bit-identical                              | Opt-in per operation; each new operation requires precision review |

#### Scenario: FP64_NATIVE test tolerance

- **WHEN** a regression or CUnit test compares an `FP64_NATIVE` backend's output to the scalar fp64 reference
- **THEN** the absolute tolerance SHALL be ≤ `1e-10` (a small constant absolute value, NOT scale-relative, because fp64 ULPs at Earth-scale ECEF are well below 1 nm)
- **AND** the test SHALL be allowed to fail if the difference exceeds this tolerance

#### Scenario: FP32_ONLY test tolerance

- **WHEN** a regression or CUnit test compares an `FP32_ONLY` backend's output to the scalar fp64 reference
- **THEN** the absolute tolerance SHALL be `max_coord × 1e-6` where `max_coord` is the maximum absolute coordinate magnitude in the input (e.g., for Earth-scale ECEF inputs at ~6.4e6 m, the tolerance is ~6.4 m)
- **AND** the test SHALL use the shared constant `FP32_EARTH_SCALE_TOLERANCE` from `liblwgeom/cunit/cu_accel_tolerances.h` for any test fixture using Earth-scale inputs (the standard fixture from `bench_helpers.h` is Earth-scale)
- **AND** the test SHALL NOT use the FP64_NATIVE tolerance (`≤ 1e-10`) because that bound is physically impossible for float32 kernels at Earth-scale magnitudes

#### Scenario: Adding a new operation to an FP32_ONLY backend requires review

- **WHEN** a contributor proposes adding a new dispatched operation to an `FP32_ONLY` backend (currently only Metal, via `lwgeom_accel.c` operation routing)
- **THEN** the change SHALL include a precision analysis demonstrating that the operation's precision cost (typically 1–2 m at Earth scale) is acceptable for the operation's application domain
- **AND** the change SHALL update the per-operation routing table in `lwgeom_accel.c` only after the precision analysis is reviewed
- **AND** operations requiring sub-meter precision (property boundary transforms, surveying) SHALL NOT be added to `FP32_ONLY` dispatch paths

### Requirement: Compile-time accessor for backend precision class

The system SHALL provide a C accessor `lwgpu_backend_precision_class(LW_GPU_BACKEND b)` that returns the precision class of a backend as a compile-time-constant value of type `LW_GPU_PRECISION_CLASS`.

The enum SHALL be defined in `liblwgeom/lwgeom_gpu.h`:

```c
typedef enum
{
    LW_GPU_PRECISION_FP64_NATIVE = 0,
    LW_GPU_PRECISION_FP32_ONLY   = 1
} LW_GPU_PRECISION_CLASS;
```

The accessor SHALL be implemented as a `static inline` function in `liblwgeom/lwgeom_gpu.h` (or equivalent compile-time mechanism) so the compiler can fold the result at every call site.

#### Scenario: Accessor returns FP32_ONLY only for Metal

- **WHEN** `lwgpu_backend_precision_class(LW_GPU_METAL)` is called
- **THEN** it SHALL return `LW_GPU_PRECISION_FP32_ONLY`
- **WHEN** the same accessor is called with `LW_GPU_NONE`, `LW_GPU_CUDA`, `LW_GPU_ROCM`, or `LW_GPU_ONEAPI`
- **THEN** it SHALL return `LW_GPU_PRECISION_FP64_NATIVE`

#### Scenario: Adding a new backend requires updating the accessor

- **WHEN** a future change adds a new value to the `LW_GPU_BACKEND` enum
- **THEN** the same change SHALL add a corresponding case to `lwgpu_backend_precision_class()` returning the appropriate class for that backend
- **AND** the build SHALL produce a `-Wswitch` warning if the new enum value is added without a matching switch case in the accessor (relying on the compiler's switch-case completeness check)

#### Scenario: Tests can assert classification at runtime

- **WHEN** a CUnit test for any backend wants to verify the backend's precision class
- **THEN** the test SHALL be able to write `CU_ASSERT_EQUAL(lwgpu_backend_precision_class(LW_GPU_CUDA), LW_GPU_PRECISION_FP64_NATIVE)` and have the assertion succeed
- **AND** if the classification is ever changed in the source, the test SHALL fail at runtime, alerting the maintainer

### Requirement: Shared tolerance constants for accelerator tests

The system SHALL provide a shared header `liblwgeom/cunit/cu_accel_tolerances.h` containing the standard tolerance constants used by accelerator regression and CUnit tests. The header SHALL define at minimum:

- `FP32_EARTH_SCALE_TOLERANCE`: scale-relative tolerance (`6378137.0 * 1e-6` ≈ 6.4 m) for Earth-scale FP32_ONLY backend tests
- `FP64_STRICT_TOLERANCE`: small absolute tolerance (`1e-10`) for FP64_NATIVE backend tests

#### Scenario: cu_metal.c uses the shared header

- **WHEN** `liblwgeom/cunit/cu_metal.c` references `FP32_EARTH_SCALE_TOLERANCE`
- **THEN** the constant SHALL be obtained via `#include "cu_accel_tolerances.h"`, NOT a local `#define`
- **AND** the local `#define FP32_EARTH_SCALE_TOLERANCE ...` in cu_metal.c SHALL be removed

#### Scenario: Future test files reuse the shared constants

- **WHEN** a future test file (e.g., `cu_cuda.c` for CUDA validation) is added
- **THEN** it SHALL include `cu_accel_tolerances.h` for its tolerance constants instead of defining its own
- **AND** if a new tolerance constant is needed (e.g., `FP32_LOCAL_SCALE_TOLERANCE` for tests using small coordinates), it SHALL be added to the shared header so all backends can use it
