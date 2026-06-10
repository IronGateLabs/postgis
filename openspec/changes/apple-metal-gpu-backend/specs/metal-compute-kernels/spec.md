## ADDED Requirements

### Requirement: Metal backend precision class
The Metal GPU backend SHALL be classified as `FP32_ONLY` (see design.md Decision 9). The backend SHALL compute all coordinate arithmetic in 32-bit IEEE 754 float because Apple Silicon GPU shader cores physically lack 64-bit floating-point ALUs. Output SHALL NOT be required to match the scalar fp64 reference bit-for-bit. Instead, output SHALL satisfy a bounded absolute-error contract proportional to input coordinate magnitude.

#### Scenario: Absolute error bound
- **GIVEN** an input POINTARRAY with maximum absolute coordinate magnitude `M` (e.g., `M ≈ 6.4e6` for Earth-scale ECEF)
- **WHEN** any Metal compute kernel processes the input
- **THEN** the maximum absolute difference between Metal output and the scalar fp64 reference output SHALL be bounded by `M * 1e-6` (accommodating 1–2 ULPs of float32 rounding compounded across a small number of float ops, plus a small safety factor)
- **AND** for Earth-scale coordinates the bound SHALL be approximately `6 meters` worst-case, with typical errors of 1–2 meters after rotation-class operations

#### Scenario: Relative error bound
- **GIVEN** the same input as above
- **WHEN** any Metal compute kernel processes the input
- **THEN** the maximum relative difference between Metal output and the scalar fp64 reference output SHALL be bounded by `1e-6` (approximately `2^{-20}`), regardless of absolute coordinate magnitude

#### Scenario: Dispatch gating for FP32_ONLY backend
- **WHEN** a new operation is considered for Metal dispatch
- **THEN** the operation SHALL be reviewed against its application-domain precision requirements before being added to the Metal dispatch path in `lwgeom_accel.c`
- **AND** operations that require sub-`max_coord * 1e-6` precision (e.g., property-boundary transforms, sub-meter surveying) SHALL NOT dispatch to Metal
- **AND** operations that tolerate meter-level error at Earth scale (e.g., ECEF↔ECI rotation for satellite ephemeris, coarse thematic mapping) MAY dispatch to Metal subject to the `effective_gpu_threshold()` point-count gate

### Requirement: rotate_z_uniform Metal compute kernel
The system SHALL provide a Metal compute kernel that performs uniform-angle Z-axis rotation on an array of interleaved coordinate points. The kernel SHALL compute in 32-bit float as required by the FP32_ONLY precision class above. The host-side bridge (`gpu_metal.m`) SHALL convert the caller's `double*` buffer to `float*` before dispatch and back to `double*` after.

#### Scenario: Correct rotation output
- **GIVEN** an array of N points with interleaved doubles at a known stride, and a rotation angle theta
- **WHEN** the `rotate_z_uniform` kernel executes with `cos_t = cos(theta)` and `sin_t = sin(theta)` passed as float arguments (computed in fp64 on the host and narrowed to fp32 at the boundary)
- **THEN** for each point index `idx`, the kernel SHALL compute:
  - `x_new = x * cos_t + y * sin_t`
  - `y_new = -x * sin_t + y * cos_t`
  - where `x = data[idx * stride + 0]` and `y = data[idx * stride + 1]` (both read as `float`)
  - and write `x_new`, `y_new` back in-place (as `float`, then widened to `double` at the host-side boundary)

#### Scenario: Thread mapping
- **WHEN** the kernel is dispatched
- **THEN** each Metal thread SHALL process exactly one point, using `thread_position_in_grid` as the point index, and threads with index >= npoints SHALL early-return without memory access

#### Scenario: Numerical equivalence within FP32_ONLY precision contract
- **GIVEN** the same input POINTARRAY and theta value
- **WHEN** `rotate_z_uniform` (Metal) processes the data and `ptarray_rotate_z_scalar` (scalar fp64 reference) processes an independent copy
- **THEN** the maximum absolute difference between outputs SHALL satisfy the FP32_ONLY absolute error bound: `< max_coord * 1e-6` (approximately 6 meters for Earth-scale ECEF input, approximately 1 meter typical)
- **AND** this scenario SHALL NOT require bit-identical output, because the precision contract is FP32_ONLY

#### Scenario: Non-equivalence with FP64_NATIVE backends
- **GIVEN** the same input data and theta
- **WHEN** `rotate_z_uniform` (Metal, FP32_ONLY) processes the data and `rotate_z_kernel` (CUDA, FP64_NATIVE, `gpu_cuda.cu`) processes an independent copy
- **THEN** the outputs SHALL differ by up to the FP32_ONLY absolute error bound (`< max_coord * 1e-6`), NOT by fp64 rounding error alone
- **AND** this is the expected behavior — Metal is explicitly not bit-equivalent to fp64 backends, and regression tests comparing Metal to CUDA (or to NEON, or to scalar) SHALL use the FP32_ONLY tolerance, not the FP64_NATIVE tolerance

#### Scenario: Stride handling
- **WHEN** points have Z and/or M coordinates (stride > 2 floats)
- **THEN** the kernel SHALL only modify floats at offset 0 (x) and offset 1 (y) within each point's stride, leaving Z, M, and any padding bytes unchanged

### Requirement: rotate_z_m_epoch Metal compute kernel (host-precomputed thetas)
The system SHALL provide a Metal compute kernel that applies per-point Z-axis rotation using rotation angles pre-computed on the host. The Earth Rotation Angle (ERA) from each point's M-epoch SHALL be computed on the CPU in double precision, reduced modulo 2*pi, multiplied by the direction sign, and narrowed to float. The kernel SHALL accept these pre-reduced thetas as a second input buffer and SHALL NOT compute ERA from epoch in MSL.

**Why the kernel does not compute ERA from epoch:** an earlier iteration of this kernel read each point's M-epoch as float and computed ERA in MSL. This produced ~900 km of positional error at Earth scale because float32 ULP at 2025 (decimal year) is ~1.22e-4 year, which after `(epoch - 2000) * 365.25 * 2*pi * 1.00274` becomes ~0.14 rad of ERA error. The precision was lost at the double-to-float conversion of the raw epoch value, BEFORE the kernel ran -- no amount of in-kernel work could recover it. Moving the ERA computation to the host (where doubles are used natively) keeps the precision loss bounded to the float32 ULP of the REDUCED theta (in [0, 2*pi), ULP ~7e-7), which corresponds to sub-meter positional error at Earth scale.

#### Scenario: Host precomputes per-point thetas
- **GIVEN** an array of N points with interleaved x,y,z,m doubles, and a direction parameter (-1 or +1)
- **WHEN** `lwgpu_metal_rotate_z_m_epoch()` is called on the host
- **THEN** the host SHALL, for each point index `i`, execute the following in double precision:
  1. Read epoch from `data[i * stride_doubles + m_offset]` as a `double`
  2. Compute Julian Date: `jd = 2451545.0 + (epoch - 2000.0) * 365.25` (double)
  3. Compute Earth Rotation Angle: `era = 2.0 * PI * (0.7790572732640 + 1.00273781191135448 * (jd - 2451545.0))` (double)
  4. Reduce to `[0, 2*PI)` via `fmod(era, 2.0 * PI)` and wrap-if-negative (double)
  5. Compute `theta = direction * era` (double)
  6. Narrow to float: `thetas[i] = (float)theta`
- **AND** the helper functions used for steps 2–4 (`metal_epoch_to_jd`, `metal_earth_rotation_angle`) SHALL mirror the scalar reference implementations in `liblwgeom/lwgeom_eci.c` (`lweci_epoch_to_jd`, `lweci_earth_rotation_angle`) so that the host-computed thetas are bit-identical to the scalar path within double-precision rounding

#### Scenario: Kernel reads pre-reduced thetas from a separate buffer
- **GIVEN** the coordinate data buffer at `[[buffer(0)]]`, the params struct at `[[buffer(1)]]`, and a per-point theta array (N floats) at `[[buffer(2)]]`
- **WHEN** the `rotate_z_m_epoch` kernel executes for a thread with `id = thread_position_in_grid`
- **THEN** the kernel SHALL:
  1. Bounds-check: if `id >= params.npoints` return immediately
  2. Read `theta = thetas[id]` (already direction-adjusted and reduced to `[0, 2*PI)`)
  3. Compute `cos_t = cos(theta)` and `sin_t = sin(theta)` (float, accurate to ~1 ULP because theta is small)
  4. Read `x = data[id * stride + 0]` and `y = data[id * stride + 1]`
  5. Write `data[id * stride + 0] = x * cos_t + y * sin_t`
  6. Write `data[id * stride + 1] = -x * sin_t + y * cos_t`
- **AND** the kernel SHALL NOT read any epoch value, call any ERA or JD helper, or perform any modular reduction -- all precision-sensitive work happened on the host before dispatch

#### Scenario: rotate_z_m_epoch params struct is minimal
- **WHEN** the `RotateZMEpochParams` constant buffer is declared in both `gpu_metal_kernels.metal` and `gpu_metal.m`
- **THEN** it SHALL contain exactly two fields: `uint stride` and `uint npoints`
- **AND** it SHALL NOT contain `m_offset` or `direction` fields, because the host has already resolved both before passing the pre-computed thetas

#### Scenario: ERA numerical equivalence to scalar reference
- **GIVEN** the same input POINTARRAY
- **WHEN** `lwgpu_metal_rotate_z_m_epoch()` (Metal path, host precomputed + GPU rotate) and `ptarray_rotate_z_m_epoch_scalar()` (scalar fp64) both process independent copies
- **THEN** the maximum absolute difference between outputs SHALL satisfy the FP32_ONLY absolute error bound: `< max_coord * 1e-6` (approximately 6 meters for Earth-scale ECEF input)
- **AND** the expected error source SHALL be the float32 ULP of the REDUCED theta (~7e-7 rad) compounded across the cos/sin/multiply-add, NOT the double-to-float conversion of the raw epoch value

#### Scenario: Direction parameter applied host-side
- **WHEN** `direction = -1` (ECI to ECEF conversion) and `direction = +1` (ECEF to ECI conversion) are passed to `lwgpu_metal_rotate_z_m_epoch()`
- **THEN** the host SHALL apply the direction sign to the reduced ERA before narrowing to float: `theta = direction * era`
- **AND** the kernel SHALL receive the already-signed theta and apply it as-is -- the kernel SHALL NOT multiply by direction itself

#### Scenario: M coordinate preserved
- **WHEN** the kernel modifies x and y coordinates
- **THEN** the M coordinate at `data[idx * stride_doubles + m_offset]` SHALL NOT be modified

### Requirement: rad_convert Metal compute kernel
The system SHALL provide a Metal compute kernel that performs bulk radian-to-degree or degree-to-radian conversion by multiplying x and y coordinates by a scale factor.

#### Scenario: Scale multiplication
- **GIVEN** an array of N points and a scale factor (e.g., `M_PI/180.0` for deg-to-rad or `180.0/M_PI` for rad-to-deg)
- **WHEN** the `rad_convert` kernel executes
- **THEN** for each point index `idx`, the kernel SHALL compute:
  - `data[idx * stride_doubles + 0] *= scale`
  - `data[idx * stride_doubles + 1] *= scale`

#### Scenario: Z and M coordinates unchanged
- **WHEN** the kernel processes points with Z and/or M coordinates
- **THEN** only the x (offset 0) and y (offset 1) doubles SHALL be modified; Z, M, and other data SHALL remain unchanged

#### Scenario: Scale multiplication within FP32_ONLY precision contract
- **GIVEN** the same input data and scale factor
- **WHEN** `rad_convert` (Metal, FP32_ONLY) and `ptarray_rad_convert_neon` (NEON, FP64_NATIVE) both process independent copies of the data
- **THEN** the maximum absolute difference between outputs SHALL satisfy the FP32_ONLY absolute error bound: `< max_coord * 1e-6` (approximately 6 meters for Earth-scale inputs, typically well under 1 meter for single-multiply operations)
- **AND** because `rad_convert` is a single float multiply (no cos/sin/add compounding), the typical observed error is closer to 1 ULP (~0.8 m at Earth scale) rather than the worst-case compound bound

#### Scenario: rad_convert is NOT dispatched to Metal in the current implementation
- **WHEN** `ptarray_rad_convert` is called
- **THEN** the dispatch layer in `lwgeom_accel.c` SHALL route to NEON (or scalar) and SHALL NOT invoke the Metal `rad_convert` kernel, because benchmarks show NEON is faster for bandwidth-bound operations and the float32 precision cost is unnecessary
- **AND** the Metal `rad_convert` kernel SHALL still be compiled and tested for correctness as a reference implementation for future dispatch decisions, but no production code path SHALL invoke it

### Requirement: Metal Shading Language source conventions
The Metal compute kernels SHALL follow consistent coding conventions compatible with the Metal Shading Language specification.

#### Scenario: Floating-point precision
- **WHEN** kernel source is written
- **THEN** all coordinate arithmetic SHALL use `float` (32-bit IEEE 754), because Apple GPU shader cores have no 64-bit floating-point ALUs (Metal Shading Language does not expose `double` as a compute type in any version). The host-side bridge (`gpu_metal.m`) SHALL convert `double*` input buffers to `float*` before dispatch and back to `double*` after.
- **AND** the precision cost SHALL be documented as: at Earth-scale coordinates (max magnitude ~6.4e6 m), 1 ULP of float32 is `~0.8 meters`, so worst-case absolute error after a rotation-class operation is approximately `1–2 meters`. This is NOT "sub-millimeter" — an earlier iteration of this spec made that claim incorrectly, and it has been corrected here.
- **AND** the precision contract SHALL be formalized via the FP32_ONLY backend class (see the first requirement in this spec) so that regression tests, dispatch decisions, and downstream consumers can reason about it explicitly rather than relying on comments in kernel source

#### Scenario: Kernel function attributes
- **WHEN** kernel functions are declared
- **THEN** each SHALL use the `kernel` function qualifier and accept `device float *data` as a buffer argument with `[[buffer(0)]]` attribute binding

#### Scenario: Thread index access
- **WHEN** kernels determine which point to process
- **THEN** they SHALL use `uint idx [[thread_position_in_grid]]` as the thread index parameter

#### Scenario: Constants passed as kernel arguments
- **WHEN** scalar parameters are passed to kernels
- **THEN** they SHALL be passed via a constant buffer struct bound at `[[buffer(1)]]`, not as individual function arguments, to comply with Metal's argument table layout
- **AND** floating-point scalars (`cos_t`, `sin_t`, `scale`) SHALL be declared as `float` in the params structs (not `double`), matching the FP32_ONLY precision class
- **AND** integer fields (`stride`, `npoints`) SHALL be declared as `uint`
- **AND** the `rotate_z_m_epoch` params struct SHALL contain ONLY `stride` and `npoints` (no `m_offset`, no `direction`) because both are resolved host-side before dispatch. Per-point thetas are passed in a separate data buffer at `[[buffer(2)]]`, NOT via the constant params buffer
