## ADDED Requirements

### Requirement: configure.ac Metal framework detection
The system SHALL detect the Apple Metal GPU compute framework and shader toolchain in `configure.ac`, gated to `darwin*` hosts only.

#### Scenario: Auto-detection on macOS with Xcode CLI tools
- **GIVEN** the host OS is `darwin*` and Xcode Command Line Tools are installed
- **WHEN** `./configure` runs with `--with-metal=auto` (default)
- **THEN** the system SHALL:
  1. Run `xcrun --find metal` to locate the Metal shader compiler
  2. Run `xcrun --find metallib` to locate the Metal library linker
  3. If both are found, set `HAVE_METAL=yes`, define `HAVE_METAL=1` in `postgis_config.h`, and substitute `METAL_COMPILER`, `METALLIB_TOOL`, `METAL_LDFLAGS="-framework Metal -framework Foundation"`

#### Scenario: Auto-detection on non-macOS
- **GIVEN** the host OS is NOT `darwin*` (e.g., `linux-gnu`, `freebsd`)
- **WHEN** `./configure` runs
- **THEN** Metal detection SHALL be skipped entirely with no error or warning, and `HAVE_METAL` SHALL be `no`

#### Scenario: Explicit --with-metal on macOS without Xcode tools
- **GIVEN** the host OS is `darwin*` but `xcrun --find metal` fails
- **WHEN** `./configure` runs with `--with-metal`
- **THEN** the system SHALL emit `AC_MSG_ERROR` stating that Metal shader compiler was not found and Xcode Command Line Tools are required

#### Scenario: Explicit --without-metal
- **WHEN** `./configure` runs with `--without-metal`
- **THEN** Metal detection SHALL be skipped, `HAVE_METAL=no`, and no Metal-related flags SHALL be set

#### Scenario: Objective-C compiler check
- **WHEN** Metal is detected
- **THEN** `configure` SHALL verify that `$(CC) -ObjC` can compile a trivial Objective-C program (e.g., `#import <Foundation/Foundation.h>\nint main(void){return 0;}`) before enabling Metal; if this check fails, set `HAVE_METAL=no` with a warning

### Requirement: Configure summary output
The system SHALL include Metal detection results in the `configure` summary output.

#### Scenario: Metal detected
- **WHEN** `HAVE_METAL=yes`
- **THEN** the configure summary SHALL include a line: `Metal GPU (xcrun):     yes` in the acceleration features section

#### Scenario: Metal not detected
- **WHEN** `HAVE_METAL=no` (non-macOS or tools not found)
- **THEN** the configure summary SHALL include a line: `Metal GPU (xcrun):     no`

### Requirement: Configure substitution variables
The system SHALL export the following `AC_SUBST` variables for use in `Makefile.in` templates.

#### Scenario: Variable definitions
- **WHEN** Metal is detected
- **THEN** the following SHALL be substituted:
  - `HAVE_METAL` = `yes`
  - `METAL_COMPILER` = path to `metal` (e.g., `/usr/bin/metal`)
  - `METALLIB_TOOL` = path to `metallib` (e.g., `/usr/bin/metallib`)
  - `METAL_LDFLAGS` = `-framework Metal -framework Foundation`

### Requirement: Makefile.in Metal shader compilation rules
The system SHALL add Makefile rules in `liblwgeom/Makefile.in` for compiling Metal shaders to an embedded metallib.

#### Scenario: Shader compilation pipeline
- **WHEN** `HAVE_METAL=yes` and `make` is invoked
- **THEN** the build system SHALL execute:
  1. `$(METAL_COMPILER) -c accel/gpu_metal_kernels.metal -o accel/gpu_metal_kernels.air` (compile MSL to AIR intermediate)
  2. `$(METALLIB_TOOL) accel/gpu_metal_kernels.air -o accel/gpu_metal_kernels.metallib` (link AIR to metallib)
  3. `xxd -i accel/gpu_metal_kernels.metallib > accel/gpu_metal_kernels_metallib.h` (embed as C byte array)
  4. Compile `accel/gpu_metal.m` (which includes the generated header) to `accel/gpu_metal.o`

#### Scenario: Objective-C compilation
- **WHEN** `accel/gpu_metal.m` is compiled
- **THEN** the build system SHALL use `$(CC) -ObjC $(CPPFLAGS) $(CFLAGS) -c` to invoke the C compiler in Objective-C mode

#### Scenario: Conditional inclusion in ACCEL_OBJS
- **WHEN** `HAVE_METAL=yes`
- **THEN** `accel/gpu_metal.o` SHALL be added to `ACCEL_OBJS` and `METAL_LDFLAGS` SHALL be appended to `LDFLAGS`, following the pattern of `gpu_cuda.o`, `gpu_rocm.o`, and `gpu_oneapi.o`

#### Scenario: Metal rules inactive when not detected
- **WHEN** `HAVE_METAL=no`
- **THEN** no Metal-related compilation rules SHALL be triggered, no `.metal` files SHALL be compiled, and `ACCEL_OBJS` SHALL NOT include `gpu_metal.o`

### Requirement: Dependency tracking for generated headers
The system SHALL ensure that `accel/gpu_metal.o` depends on the generated `accel/gpu_metal_kernels_metallib.h` header.

#### Scenario: Rebuild on shader change
- **WHEN** `accel/gpu_metal_kernels.metal` is modified
- **THEN** `make` SHALL recompile the shader pipeline (`.metal` -> `.air` -> `.metallib` -> `.h`) and recompile `gpu_metal.m`

### Requirement: Clean target for Metal artifacts
The system SHALL remove Metal build artifacts on `make clean`.

#### Scenario: Clean removes generated files
- **WHEN** `make clean` is run
- **THEN** the system SHALL remove `accel/gpu_metal_kernels.air`, `accel/gpu_metal_kernels.metallib`, `accel/gpu_metal_kernels_metallib.h`, and `accel/gpu_metal.o`

### Requirement: Preprocessor guards for Metal code
All Metal-specific code in shared headers and source files SHALL be guarded by preprocessor conditionals.

#### Scenario: Header guards in lwgeom_gpu.h
- **WHEN** Metal function declarations appear in `lwgeom_gpu.h`
- **THEN** they SHALL be wrapped in `#ifdef HAVE_METAL` / `#endif`

#### Scenario: Platform double-guard in implementation
- **WHEN** `gpu_metal.m` is compiled
- **THEN** it SHALL include both `#ifdef HAVE_METAL` and `#if defined(__APPLE__)` as guards at the top of the file, so that accidental compilation on non-macOS platforms produces a clear error or empty translation unit

#### Scenario: No impact on non-macOS builds
- **WHEN** PostGIS is built on Linux or FreeBSD
- **THEN** `HAVE_METAL` SHALL NOT be defined, `gpu_metal.m` SHALL NOT be compiled, and no Metal headers or frameworks SHALL be referenced
