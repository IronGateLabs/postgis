## 1. Build System -- Configure and Makefile

- [x] 1.1 Add `--with-metal` / `--without-metal` configure flag in `configure.ac` with `WITH_METAL` variable (default: `auto`)
- [x] 1.2 Add Metal detection block in `configure.ac` gated by `case "$host_os" in darwin*)`: run `xcrun --find metal` and `xcrun --find metallib`, set `HAVE_METAL`, `METAL_COMPILER`, `METALLIB_TOOL`, `METAL_LDFLAGS`
- [x] 1.3 Add Objective-C compiler verification in `configure.ac`: test `$(CC) -ObjC` can compile a trivial `#import <Foundation/Foundation.h>` program before enabling Metal
- [x] 1.4 Add `AC_SUBST` for `HAVE_METAL`, `METAL_COMPILER`, `METALLIB_TOOL`, `METAL_LDFLAGS` and `AC_DEFINE([HAVE_METAL], [1], [Apple Metal GPU support available])`
- [x] 1.5 Add Metal detection result to the configure summary output line: `Metal GPU (xcrun):     ${HAVE_METAL}`
- [x] 1.6 Add shader compilation rules in `liblwgeom/Makefile.in`: `.metal` -> `.air` via `$(METAL_COMPILER) -c`, `.air` -> `.metallib` via `$(METALLIB_TOOL)`, `.metallib` -> `_metallib.h` via `xxd -i`
- [x] 1.7 Add Objective-C compilation rule in `liblwgeom/Makefile.in`: `accel/gpu_metal.o` from `accel/gpu_metal.m` using `$(CC) -ObjC $(CPPFLAGS) $(CFLAGS)`
- [x] 1.8 Add conditional `ACCEL_OBJS += accel/gpu_metal.o` and `LDFLAGS += @METAL_LDFLAGS@` block in `liblwgeom/Makefile.in` under `ifeq (@HAVE_METAL@,yes)`
- [x] 1.9 Add dependency: `accel/gpu_metal.o` depends on `accel/gpu_metal_kernels_metallib.h`
- [x] 1.10 Add Metal artifacts (`.air`, `.metallib`, `_metallib.h`, `.o`) to the `clean` target

## 2. GPU Abstraction Layer Integration

- [x] 2.1 Add `LW_GPU_METAL = 4` to the `LW_GPU_BACKEND` enum in `liblwgeom/lwgeom_gpu.h`
- [x] 2.2 Add Metal function declarations in `liblwgeom/lwgeom_gpu.h` under `#ifdef HAVE_METAL`: `lwgpu_metal_init()`, `lwgpu_metal_rotate_z()`, `lwgpu_metal_rotate_z_m_epoch()`, `lwgpu_metal_shutdown()`, `lwgpu_metal_device_name()`
- [x] 2.3 Wire Metal init into the auto-detect priority order in `lwgpu_init()`: try Metal after oneAPI (CUDA > ROCm > oneAPI > Metal)
- [x] 2.4 Wire Metal dispatch functions into `lwgpu_rotate_z_batch()` and `lwgpu_rotate_z_m_epoch_batch()` switch blocks
- [x] 2.5 Wire `lwgpu_metal_shutdown()` into `lwgpu_shutdown()` switch block
- [x] 2.6 Add `"metal"` string mapping in `lwgpu_backend_name()` and the GUC `postgis.gpu_backend` enum parser

## 3. Metal Runtime -- gpu_metal.m

- [x] 3.1 Create `liblwgeom/accel/gpu_metal.m` with `#ifdef HAVE_METAL` and `#if defined(__APPLE__)` guards
- [x] 3.2 Implement file-scope statics: `metal_initialized` flag, `metal_device_name[256]` buffer, `MTLDevice`, `MTLCommandQueue`, `MTLLibrary`, three `MTLComputePipelineState` objects (one per kernel)
- [x] 3.3 Implement `lwgpu_metal_init()`: call `MTLCreateSystemDefaultDevice()`, create command queue, load embedded metallib via `[device newLibraryWithData:error:]`, create pipeline states for `rotate_z_uniform`, `rotate_z_m_epoch`, and `rad_convert` functions, cache device name string
- [x] 3.4 Implement `lwgpu_metal_rotate_z()`: compute `cos_t`/`sin_t` on CPU, create `MTLBuffer` from caller's `double*` (zero-copy if page-aligned, copy otherwise), create params struct buffer, encode compute command with `rotate_z_uniform` pipeline, dispatch threads (threadgroup size 256), commit and `waitUntilCompleted`, copy back if needed, return 1 on success / 0 on error
- [x] 3.5 Implement `lwgpu_metal_rotate_z_m_epoch()`: create `MTLBuffer` from caller's `double*`, create params struct buffer with stride/m_offset/direction, encode compute command with `rotate_z_m_epoch` pipeline, dispatch, wait, return status
- [x] 3.6 Implement `lwgpu_metal_shutdown()`: release all Metal objects (set to nil), reset `metal_initialized` to 0
- [x] 3.7 Implement `lwgpu_metal_device_name()`: return cached `metal_device_name` string
- [x] 3.8 Implement page-alignment check helper: verify pointer alignment and length are VM page size multiples for `newBufferWithBytesNoCopy` eligibility
- [x] 3.9 Add error handling: check all Metal API return values, log `NOTICE` on failure via `lwerror`/`lwnotice`, set disable flag on first command buffer error

## 4. Metal Compute Kernels -- gpu_metal_kernels.metal

- [x] 4.1 Create `liblwgeom/accel/gpu_metal_kernels.metal` with Metal Shading Language header and `#include <metal_stdlib>`
- [x] 4.2 Define `RotateZParams` constant struct: `uint stride_doubles`, `uint npoints`, `double cos_t`, `double sin_t`
- [x] 4.3 Implement `rotate_z_uniform` kernel: read params from `[[buffer(1)]]`, compute point index from `thread_position_in_grid`, bounds check, read x/y at stride offset, apply 2x2 rotation with `cos_t`/`sin_t`, write back in-place
- [x] 4.4 Define `RotateZMEpochParams` constant struct: `uint stride_doubles`, `uint npoints`, `uint m_offset`, `int direction`
- [x] 4.5 Implement inline `gpu_epoch_to_jd()` and `gpu_earth_rotation_angle()` functions using identical formulas to `gpu_cuda.cu` (JD: `2451545.0 + (epoch - 2000.0) * 365.25`, ERA: `2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du)`)
- [x] 4.6 Implement `rotate_z_m_epoch` kernel: read epoch from M offset, compute JD/ERA, apply Z-rotation per-thread
- [x] 4.7 Define `RadConvertParams` constant struct: `uint stride_doubles`, `uint npoints`, `double scale`
- [x] 4.8 Implement `rad_convert` kernel: multiply x and y by scale factor, write back in-place

## 5. Testing and Benchmarking

- [x] 5.1 Add Metal backend to the existing `liblwgeom/bench/bench_accel.c` benchmark harness: detect Metal availability, run `rotate_z` and `rad_convert` benchmarks at 1K, 10K, 100K, 1M, 10M point counts
- [x] 5.2 Add Metal vs NEON comparison output to the benchmark CSV and text-mode chart
- [x] 5.3 Add `--validate` mode for Metal: compare Metal output against scalar reference for all three kernels, fail if max absolute difference > 1e-10
- [x] 5.4 Add CUnit test for Metal initialization: verify `lwgpu_metal_init()` succeeds on macOS with Metal, returns 0 on non-Metal systems
- [x] 5.5 Add CUnit test for Metal Z-rotation correctness: generate known-answer test vectors, compare Metal output to scalar
- [x] 5.6 Add CUnit test for Metal M-epoch rotation correctness: verify ERA computation matches CPU implementation
- [x] 5.7 Add CUnit test for Metal rad_convert correctness: verify exact multiplication results
- [x] 5.8 Add CUnit test for Metal fallback: verify that when Metal dispatch returns 0 (simulated failure), the caller falls back to NEON without error
- [x] 5.9 Guard Metal tests in test Makefiles with `ifeq (@HAVE_METAL@,yes)` conditionals

## 6. Documentation

- [x] 6.1 Update `configure` summary section documentation to include the `Metal GPU (xcrun)` line
- [x] 6.2 Update `postgis.gpu_backend` GUC documentation to include `'metal'` as a valid value
- [x] 6.3 Add macOS build instructions noting Xcode Command Line Tools requirement for Metal support
- [x] 6.4 Document Metal dispatch threshold behavior and auto-calibration in the acceleration configuration section
- [x] 6.5 Add Metal backend to `postgis_accel_features()` output documentation
