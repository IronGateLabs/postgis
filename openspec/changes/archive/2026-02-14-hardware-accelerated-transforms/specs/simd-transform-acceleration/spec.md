## ADDED Requirements

### Requirement: SIMD-accelerated Z-axis rotation
The system SHALL provide SIMD-vectorized implementations of the Z-axis rotation used in ECI/ECEF transforms. SIMD paths SHALL produce numerically equivalent results to the scalar path (within double-precision FMA tolerance).

#### Scenario: AVX2 uniform-epoch rotation
- **WHEN** `ptarray_rotate_z(pa, theta)` is called on a system with AVX2 support and the POINTARRAY contains 4 or more points
- **THEN** the system SHALL process points in batches of 4 using 256-bit SIMD multiply-add operations, with a scalar tail loop for remaining points

#### Scenario: AVX-512 uniform-epoch rotation
- **WHEN** `ptarray_rotate_z(pa, theta)` is called on a system with AVX-512 support
- **THEN** the system SHALL process points in batches of 8 using 512-bit SIMD operations

#### Scenario: ARM NEON rotation
- **WHEN** `ptarray_rotate_z(pa, theta)` is called on an aarch64 system with NEON support
- **THEN** the system SHALL process points in batches of 2 using 128-bit NEON operations

#### Scenario: Scalar fallback
- **WHEN** `ptarray_rotate_z(pa, theta)` is called on a system without SIMD support or with fewer than the SIMD batch size points
- **THEN** the system SHALL use the existing scalar C implementation with identical results

### Requirement: SIMD-accelerated radian/degree conversion
The system SHALL provide SIMD-vectorized radian-to-degree and degree-to-radian conversion for the pre/post loops in `ptarray_transform()`.

#### Scenario: Batch radian conversion
- **WHEN** `ptarray_transform()` needs to convert geographic coordinates to radians before PROJ dispatch
- **THEN** the system SHALL multiply coordinate pairs by `M_PI/180.0` using SIMD operations when the POINTARRAY has 4+ points

#### Scenario: Batch degree conversion
- **WHEN** `ptarray_transform()` needs to convert PROJ output radians back to degrees
- **THEN** the system SHALL multiply coordinate pairs by `180.0/M_PI` using SIMD operations

### Requirement: Runtime CPU feature detection
The system SHALL detect available SIMD instruction sets at runtime and select the optimal code path. Detection SHALL occur once at library initialization.

#### Scenario: Feature detection on x86_64
- **WHEN** PostGIS loads on an x86_64 system
- **THEN** the system SHALL use `cpuid` to detect AVX2, AVX-512, and FMA support, and select the widest available SIMD path

#### Scenario: Feature detection on aarch64
- **WHEN** PostGIS loads on an aarch64 system
- **THEN** the system SHALL detect NEON support (guaranteed on ARMv8+) and use the NEON code path

#### Scenario: Feature detection result accessible
- **WHEN** a user calls `SELECT postgis_accel_features()`
- **THEN** the system SHALL return a text listing of detected acceleration features (e.g., "SIMD: AVX2, FMA; GPU: none")

### Requirement: Compile-time SIMD configuration
The build system SHALL support enabling or disabling specific SIMD instruction sets via configure flags, with auto-detection as the default.

#### Scenario: Auto-detect SIMD
- **WHEN** `./configure` is run without SIMD flags
- **THEN** the build system SHALL detect available SIMD instruction sets on the build host and enable them

#### Scenario: Explicit SIMD override
- **WHEN** `./configure --enable-avx2 --disable-avx512` is passed
- **THEN** the build system SHALL compile with AVX2 support and without AVX-512, regardless of build host capabilities

#### Scenario: Cross-compilation
- **WHEN** `./configure --disable-simd` is passed
- **THEN** the build system SHALL compile only the scalar code paths, producing a portable binary
