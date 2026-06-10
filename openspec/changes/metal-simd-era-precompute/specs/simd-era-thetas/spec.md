## ADDED Requirements

### Requirement: SIMD-accelerated ERA thetas helper for GPU dispatch

The system SHALL provide a SIMD-accelerated helper that computes pre-reduced Earth Rotation Angle (ERA) values for an array of points and writes them as `float` (32-bit) into a caller-provided output buffer. The helper SHALL be selected at runtime via the existing `LW_ACCEL_DISPATCH` table based on detected CPU features (NEON, AVX2, AVX-512, scalar fallback). The helper SHALL be the canonical way for any FP32_ONLY GPU backend (currently only Metal) to obtain pre-computed rotation angles without serializing the host-side ERA computation.

The helper signature SHALL be:

```c
void lwgeom_accel_era_thetas(const double *data,
                             size_t        stride_doubles,
                             uint32_t      npoints,
                             size_t        m_offset,
                             int           direction,
                             float        *thetas_out);
```

For each point index `i` in `[0, npoints)`, the helper SHALL:

1. Read the M-epoch from `data[i * stride_doubles + m_offset]` as a `double`
2. Compute the Julian Date in fp64: `jd = 2451545.0 + (epoch - 2000.0) * 365.25`
3. Compute the Earth Rotation Angle in fp64 using the IERS 2010 linear approximation: `era = 2*PI * (0.7790572732640 + 1.00273781191135448 * (jd - 2451545.0))`
4. Reduce ERA to `[0, 2*PI)` via `fmod` and wrap-if-negative, in fp64
5. Apply the direction sign: `theta = direction * era`, in fp64
6. Narrow to float ONCE: `thetas_out[i] = (float)theta`

The fp64 stage of steps 2–5 SHALL be vectorized via SIMD intrinsics where available (NEON `float64x2_t` for ARM, AVX2 `__m256d` for x86-64 if AVX2 build is enabled). The narrowing in step 6 SHALL also be vectorized (`vcvt_f32_f64` for NEON, `_mm256_cvtpd_ps` for AVX2).

#### Scenario: Helper is registered in the dispatch table

- **WHEN** `lwaccel_init()` runs at process startup
- **THEN** the global `LW_ACCEL_DISPATCH` struct returned by `lwaccel_get()` SHALL contain a non-NULL `era_thetas` function pointer
- **AND** the function pointer SHALL be set to the NEON variant on ARM hosts where NEON is detected, the AVX2 variant on x86-64 hosts where AVX2 is detected (if compiled with AVX2 support), or the scalar fallback otherwise
- **AND** the function pointer SHALL never be NULL after `lwaccel_init()` completes successfully

#### Scenario: NEON helper is bit-equivalent to scalar reference (within fp64 rounding)

- **GIVEN** an array of N points with M-epoch values in `data[i * stride + m_offset]` (fp64)
- **WHEN** the NEON variant `lwgeom_accel_era_thetas_neon()` and the scalar reference `lwgeom_accel_era_thetas_scalar()` both process independent copies of the input
- **THEN** the outputs SHALL be identical within fp64 rounding error (each output element matches to the last bit of float32 representation, because the only narrowing happens at the last step and both variants narrow the same fp64 value)
- **AND** the maximum absolute difference between the two outputs SHALL be `0.0` (NOT a small epsilon, because both variants compute identical fp64 results before narrowing)

#### Scenario: SIMD helper feeds Metal kernel without precision loss

- **GIVEN** an input POINTARRAY with M-epoch column in fp64 (e.g., `bench_make_test_pa(500000)` which generates epochs in `[2025.0, 2026.0)`)
- **WHEN** `lwgpu_metal_rotate_z_m_epoch()` calls `lwaccel_get()->era_thetas(...)` to populate the thetas buffer instead of using the scalar serial loop, then dispatches the Metal kernel
- **THEN** the GPU output SHALL match the scalar reference `ptarray_rotate_z_m_epoch_scalar()` within `FP32_EARTH_SCALE_TOLERANCE` (~6.4 m for Earth-scale ECEF coordinates)
- **AND** the precision contract SHALL be unchanged from PR #13 — this is a pure performance refactor, not a precision change

#### Scenario: Direction parameter is applied per point inside the SIMD loop

- **WHEN** `lwgeom_accel_era_thetas` is called with `direction = -1`
- **THEN** every output element SHALL satisfy `thetas_out[i] = -fp64_reduced_era_at_point_i` (narrowed to float)
- **WHEN** the same call is made with `direction = +1`
- **THEN** every output element SHALL satisfy `thetas_out[i] = +fp64_reduced_era_at_point_i` (narrowed to float)
- **AND** the helper SHALL NOT skip the multiplication when `direction = +1` (a separate code path would not be more efficient and would risk divergence between the two directions)

#### Scenario: Output buffer is owned by caller

- **WHEN** `lwgeom_accel_era_thetas` is called
- **THEN** the helper SHALL NOT allocate or free `thetas_out`
- **AND** the helper SHALL write exactly `npoints * sizeof(float)` bytes to `thetas_out`
- **AND** the caller SHALL be responsible for ensuring `thetas_out` has at least that much space before the call

### Requirement: Throughput target for SIMD ERA helper

The NEON variant of `lwgeom_accel_era_thetas` SHALL achieve a throughput such that the combined `lwgpu_metal_rotate_z_m_epoch` end-to-end performance (host SIMD ERA precompute + GPU dispatch + GPU kernel) is at least 1.5× the standalone NEON `ptarray_rotate_z_m_epoch_neon` throughput at 500,000 points on Apple A18 Pro hardware.

#### Scenario: Performance target met on A18 Pro

- **GIVEN** the standalone `bench_metal` benchmark running on Apple A18 Pro at 500,000 points
- **WHEN** the rotate_z_m_epoch operation runs through the Metal path with the SIMD ERA precompute helper enabled
- **THEN** the measured throughput SHALL be at least `1.5 * 76 Mpts/s ≈ 114 Mpts/s` (target: ~130 Mpts/s)
- **AND** if measured throughput is below `114 Mpts/s`, the change SHALL NOT be merged without an explicit waiver documenting why

#### Scenario: Benchmark output breaks down host vs GPU phases

- **WHEN** `bench_metal` runs the rotate_z_m_epoch benchmark with SIMD ERA precompute enabled
- **THEN** the CSV output SHALL include two new columns: `host_era_us` (microseconds spent in `lwgeom_accel_era_thetas` for the SIMD precompute phase) and `gpu_kernel_us` (microseconds spent in the Metal kernel dispatch + execute phase)
- **AND** the existing `total_us` column SHALL approximately equal `host_era_us + gpu_kernel_us + small_overhead`
- **AND** the breakdown SHALL allow a reviewer to identify which phase is the bottleneck for any given workload size
