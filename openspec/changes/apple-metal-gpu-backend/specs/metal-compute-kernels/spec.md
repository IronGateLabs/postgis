## ADDED Requirements

### Requirement: rotate_z_uniform Metal compute kernel
The system SHALL provide a Metal compute kernel that performs uniform-angle Z-axis rotation on an array of interleaved coordinate points, using double-precision arithmetic.

#### Scenario: Correct rotation output
- **GIVEN** an array of N points with interleaved doubles at a known stride, and a rotation angle theta
- **WHEN** the `rotate_z_uniform` kernel executes with `cos_t = cos(theta)` and `sin_t = sin(theta)` passed as arguments
- **THEN** for each point index `idx`, the kernel SHALL compute:
  - `x_new = x * cos_t + y * sin_t`
  - `y_new = -x * sin_t + y * cos_t`
  - where `x = data[idx * stride_doubles + 0]` and `y = data[idx * stride_doubles + 1]`
  - and write `x_new`, `y_new` back in-place

#### Scenario: Thread mapping
- **WHEN** the kernel is dispatched
- **THEN** each Metal thread SHALL process exactly one point, using `thread_position_in_grid` as the point index, and threads with index >= npoints SHALL early-return without memory access

#### Scenario: Numerical equivalence with CUDA backend
- **GIVEN** the same input data and theta value
- **WHEN** `rotate_z_uniform` (Metal) and `rotate_z_kernel` (CUDA, `gpu_cuda.cu` line 42) both process the data
- **THEN** the outputs SHALL be identical within double-precision rounding (max absolute difference < 1e-15 for coordinate values in the range +/-1e7)

#### Scenario: Stride handling
- **WHEN** points have Z and/or M coordinates (stride > 2 doubles)
- **THEN** the kernel SHALL only modify doubles at offset 0 (x) and offset 1 (y) within each point's stride, leaving Z, M, and any padding bytes unchanged

### Requirement: rotate_z_m_epoch Metal compute kernel
The system SHALL provide a Metal compute kernel that performs per-point M-epoch Z-axis rotation, where each point's M coordinate determines its rotation angle.

#### Scenario: Per-point ERA computation
- **GIVEN** an array of N points with interleaved x,y,z,m doubles
- **WHEN** the `rotate_z_m_epoch` kernel executes with `m_offset` and `direction` parameters
- **THEN** for each point index `idx`, the kernel SHALL:
  1. Read epoch from `data[idx * stride_doubles + m_offset]`
  2. Compute Julian Date: `jd = 2451545.0 + (epoch - 2000.0) * 365.25`
  3. Compute Earth Rotation Angle: `era = 2.0 * PI * (0.7790572732640 + 1.00273781191135448 * (jd - 2451545.0))`
  4. Normalize ERA to `[0, 2*PI)` via `fmod`
  5. Compute `theta = direction * era`
  6. Apply Z-rotation: `x_new = x * cos(theta) + y * sin(theta)`, `y_new = -x * sin(theta) + y * cos(theta)`
  7. Write `x_new`, `y_new` back in-place

#### Scenario: Numerical equivalence with CUDA ERA computation
- **GIVEN** the same epoch value (e.g., 2024.5)
- **WHEN** the Metal kernel computes ERA and the CUDA `gpu_earth_rotation_angle` function (`gpu_cuda.cu` line 30) computes ERA
- **THEN** the ERA values SHALL be identical within double-precision rounding (the formulas are identical: both use the IERS 2010 linear approximation)

#### Scenario: Direction parameter
- **WHEN** `direction = -1` (ECI to ECEF conversion)
- **THEN** the rotation angle SHALL be `-era` (negative rotation)
- **WHEN** `direction = +1` (ECEF to ECI conversion)
- **THEN** the rotation angle SHALL be `+era` (positive rotation)

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

#### Scenario: Numerical equivalence with NEON implementation
- **GIVEN** the same input data and scale factor
- **WHEN** `rad_convert` (Metal) and `ptarray_rad_convert_neon` (NEON, `rad_convert_neon.c`) both process the data
- **THEN** the outputs SHALL be identical (this is a simple multiply, so no rounding divergence is expected)

### Requirement: Metal Shading Language source conventions
The Metal compute kernels SHALL follow consistent coding conventions compatible with the Metal Shading Language specification.

#### Scenario: Double-precision types
- **WHEN** kernel source is written
- **THEN** all coordinate arithmetic SHALL use `double` (64-bit IEEE 754), not `float` or `half`

#### Scenario: Kernel function attributes
- **WHEN** kernel functions are declared
- **THEN** each SHALL use the `kernel` function qualifier and accept `device double *data` as a buffer argument with `[[buffer(0)]]` attribute binding

#### Scenario: Thread index access
- **WHEN** kernels determine which point to process
- **THEN** they SHALL use `uint idx [[thread_position_in_grid]]` as the thread index parameter

#### Scenario: Constants passed as kernel arguments
- **WHEN** scalar parameters (cos_t, sin_t, scale, stride_doubles, npoints, m_offset, direction) are passed to kernels
- **THEN** they SHALL be passed via a constant buffer struct bound at `[[buffer(1)]]`, not as individual function arguments, to comply with Metal's argument table layout
