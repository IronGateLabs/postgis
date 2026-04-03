## Why

Apple Silicon Macs (M1, M2, M3, M4, and future generations) ship with powerful GPU cores integrated into the SoC, yet PostGIS cannot use them. The existing GPU abstraction layer (`lwgeom_gpu.h`) supports CUDA (NVIDIA), ROCm/HIP (AMD), and oneAPI/SYCL (Intel), but has no Apple Metal backend. On macOS, the only hardware acceleration available is ARM NEON SIMD, which uses the CPU vector unit but leaves the GPU entirely idle.

Metal is Apple's native GPU compute framework and is the only GPU API available on macOS (no Vulkan compute, no OpenCL on Apple Silicon). For developers running PostGIS on Mac workstations, CI servers on Mac infrastructure (GitHub Actions macOS runners, Buildkite on Mac), or edge deployments on Mac Mini/Mac Studio hardware, the GPU sits unused during bulk coordinate transforms. Given that M-series GPUs have up to 40 GPU cores with high memory bandwidth to unified memory (no PCIe transfer overhead), Metal compute is a compelling target for the same batch operations already accelerated on CUDA/ROCm/oneAPI.

## What Changes

- Add a Metal compute shader backend to the existing GPU abstraction layer in `liblwgeom/lwgeom_gpu.h` and `liblwgeom/lwgeom_gpu.c`
- Introduce `LW_GPU_METAL = 4` in the `LW_GPU_BACKEND` enum and wire it into the init/dispatch/shutdown switch blocks
- Add Metal backend implementation files:
  - `liblwgeom/accel/gpu_metal.m` -- Objective-C bridge for Metal device init, command queue, buffer management, and compute pipeline setup
  - `liblwgeom/accel/gpu_metal_kernels.metal` -- Metal Shading Language compute kernels for Z-rotation (uniform theta and per-point M-epoch) and radian/degree bulk conversion
- Compile `.metal` shaders to `.metallib` at build time using Apple's `metal` and `metallib` toolchain (available in Xcode Command Line Tools)
- Add `configure.ac` detection for the Metal framework, `xcrun --find metal` compiler, and `xcrun --find metallib` linker; set `HAVE_METAL` define and `METAL_CFLAGS`/`METAL_LDFLAGS` substitutions
- Integrate into the auto-detect priority order in `lwgpu_init()`: CUDA > ROCm > oneAPI > Metal (Metal last because the other backends target discrete GPUs with higher throughput; Metal targets integrated GPUs where the advantage is zero-copy unified memory)
- Support `postgis.gpu_backend = 'metal'` GUC value for explicit selection
- Runtime detection: query `MTLCreateSystemDefaultDevice()` at init time; if no Metal-capable GPU exists, return failure so the dispatcher falls back to NEON SIMD
- All compilation of Metal code guarded by `#ifdef HAVE_METAL` / `#if defined(__APPLE__)` and wrapped in conditional Makefile rules; non-macOS platforms are completely unaffected
- Add benchmark targets comparing Metal GPU vs ARM NEON SIMD for Z-rotation and radian conversion at 10K, 100K, 1M, and 10M point counts on M-series hardware

## Scope

### In Scope

- **Z-rotation (uniform theta)**: The `lwgpu_rotate_z_batch()` operation, used for ECI/ECEF frame conversion at a single epoch. Metal compute shader performs sin/cos once, then applies 2x2 rotation matrix to all points in parallel.
- **Z-rotation (per-point M-epoch)**: The `lwgpu_rotate_z_m_epoch_batch()` operation, where each point's M coordinate determines its rotation angle. Metal compute shader reads M, computes per-thread sin/cos, applies rotation.
- **Radian/degree bulk conversion**: Batch multiply of x,y coordinates by a scale factor (degree-to-radian or radian-to-degree), used pre/post PROJ transforms. Simple but high-bandwidth operation that benefits from GPU parallelism at large point counts.
- **Build system integration**: `configure.ac` detection, Makefile rules for `.m` (Objective-C) and `.metal` (shader) compilation, conditional linking of `-framework Metal -framework Foundation`
- **GUC integration**: Extending `postgis.gpu_backend` enum to accept `'metal'` and auto-detection logic
- **Benchmarking**: Performance comparison harness for Metal vs NEON on Apple Silicon

### Out of Scope (Future Work)

- Spatial index building on GPU (R-tree bulk loading, GiST operations) -- requires deeper PostgreSQL integration
- General-purpose spatial function acceleration (ST_Distance, ST_Intersects) on Metal -- these are better served by PG-Strom-style query-level GPU offload
- iOS/iPadOS/visionOS targets -- this change targets macOS only
- Vulkan compute as an alternative cross-platform GPU path
- Metal raytracing or mesh shading capabilities

## Integration Points

### GPU Abstraction Layer (`lwgeom_gpu.h`)

The Metal backend follows the exact same pattern as CUDA/ROCm/oneAPI:

```c
#ifdef HAVE_METAL
int lwgpu_metal_init(void);
int lwgpu_metal_rotate_z(double *data, size_t stride, uint32_t n, double theta);
int lwgpu_metal_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
                                 size_t m_off, int dir);
void lwgpu_metal_shutdown(void);
const char *lwgpu_metal_device_name(void);
#endif
```

### Configure Detection

```
dnl --- Apple Metal detection ---
if test "x$WITH_METAL" != "xno"; then
  AC_MSG_CHECKING([for Apple Metal framework])
  case "$host_os" in
    darwin*)
      METAL_COMPILER=`xcrun --find metal 2>/dev/null`
      METALLIB_TOOL=`xcrun --find metallib 2>/dev/null`
      if test -n "$METAL_COMPILER" -a -n "$METALLIB_TOOL"; then
        HAVE_METAL=yes
        AC_DEFINE([HAVE_METAL], [1], [Apple Metal GPU support available])
        METAL_LDFLAGS="-framework Metal -framework Foundation"
      fi
      ;;
  esac
fi
```

### Metal Compute Architecture

Apple Silicon's unified memory architecture means Metal buffers can use `MTLResourceStorageModeShared`, avoiding explicit host-to-device copies. The PostGIS coordinate arrays are allocated in shared memory that both CPU and GPU can access directly. This eliminates the PCIe transfer bottleneck that affects discrete GPU backends and makes the GPU dispatch threshold potentially lower on Metal than on CUDA/ROCm.

The Metal compute pipeline:
1. At `lwgpu_metal_init()`: create `MTLDevice`, `MTLCommandQueue`, load precompiled `.metallib`, create `MTLComputePipelineState` objects for each kernel
2. At dispatch: wrap caller's `double*` buffer in a `MTLBuffer` (no-copy via `newBufferWithBytesNoCopy`), encode compute command, commit and wait
3. At `lwgpu_metal_shutdown()`: release device, queue, pipeline state objects

### Shader Compilation

Metal shaders are compiled ahead of time:
```makefile
%.air: %.metal
	xcrun metal -c $< -o $@

%.metallib: %.air
	xcrun metallib $< -o $@
```

The resulting `.metallib` is either embedded as a C byte array or installed alongside the PostGIS shared library and loaded at runtime.

### Conditional Compilation

All Metal code is strictly conditional:
- C preprocessor: `#ifdef HAVE_METAL` guards in `lwgeom_gpu.h` and `lwgeom_gpu.c`
- Platform check: `#if defined(__APPLE__)` as a secondary guard
- Objective-C files (`.m`) only compiled on macOS with Metal support
- `configure` only looks for Metal on `darwin*` host
- Makefile rules for `.metal` -> `.air` -> `.metallib` only active when `HAVE_METAL=yes`

## Impact

- **Build system**: `configure.ac` gains Metal framework detection; `liblwgeom/Makefile.in` gains rules for Objective-C compilation and Metal shader compilation; new `--with-metal` / `--without-metal` configure flag
- **Core libraries**: `liblwgeom/lwgeom_gpu.h` gains `LW_GPU_METAL` enum value and Metal function declarations; `liblwgeom/lwgeom_gpu.c` gains Metal dispatch branches; new `liblwgeom/accel/gpu_metal.m` and `liblwgeom/accel/gpu_metal_kernels.metal`
- **Dependencies**: Optional: Xcode Command Line Tools (provides Metal compiler, Metal framework headers). Only on macOS.
- **CI**: macOS CI runners can test the Metal backend; other platforms unaffected
- **Regression**: All existing tests must produce bit-identical results regardless of whether Metal, NEON, or scalar path is used
- **Configure summary output**: New line `Metal (xcrun):        ${HAVE_METAL}` in the acceleration section

## Capabilities

### New Capabilities
- `metal-gpu-backend`: Apple Metal compute shader backend for batch coordinate transforms on macOS with Apple Silicon, fitting into the existing GPU dispatch abstraction alongside CUDA, ROCm, and oneAPI

### Modified Capabilities
- `gpu-transform-dispatch`: GPU dispatch layer gains a fourth backend (Metal) with auto-detection on macOS and GUC-selectable override
- `simd-transform-acceleration`: On Apple Silicon, the acceleration stack now has two tiers -- NEON SIMD for small arrays and Metal GPU for large arrays -- with auto-calibrated threshold selection

## Open Questions

1. **Double precision on Metal**: Metal GPU cores natively support `float` (32-bit) but `double` (64-bit) support varies by generation. M1/M2 have limited double throughput. Should we provide a `float` fast-path with precision analysis, or require `double` throughout for correctness? Coordinate transforms need full double precision for geodetic accuracy, so a float-only path may not be viable.
2. **Threshold tuning**: The unified memory advantage (no PCIe copies) suggests Metal's crossover point vs NEON could be lower than CUDA's crossover vs AVX. The auto-calibration in `lwaccel_calibrate_gpu()` should handle this, but the initial default threshold may need to be different for Metal.
3. **Embedded vs external metallib**: Should the compiled `.metallib` be embedded as a C byte array in the binary (simpler deployment, no file path issues) or installed as a separate file (easier to update shaders without recompiling)?
