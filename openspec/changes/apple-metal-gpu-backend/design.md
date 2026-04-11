## Context

PostGIS has a layered hardware acceleration stack for batch coordinate transforms. The SIMD layer (`lwgeom_accel.h`) dispatches through a function-pointer table to NEON, AVX2, or AVX-512 implementations. The GPU layer (`lwgeom_gpu.h`) provides a backend-agnostic API with implementations for CUDA (`gpu_cuda.cu`), ROCm/HIP (`gpu_rocm.hip`), and oneAPI/SYCL (`gpu_oneapi.cpp`). Both layers live in `liblwgeom/accel/`.

On Apple Silicon, the NEON SIMD backend works (2-wide `float64x2_t` for Z-rotation and radian conversion), but the GPU cores are entirely unused. Apple Silicon GPUs are accessed exclusively through Metal -- there is no OpenCL, no Vulkan compute. The M-series SoC has unified memory shared between CPU and GPU, which eliminates the PCIe copy overhead that dominates discrete GPU backends. This makes Metal dispatch viable at much lower point counts than CUDA or ROCm.

The existing GPU abstraction has three operations:
- `lwgpu_rotate_z_batch()` -- uniform-angle Z-rotation (ECEF), single sin/cos for all points
- `lwgpu_rotate_z_m_epoch_batch()` -- per-point M-epoch Z-rotation (ECI), per-thread sin/cos
- `lwgpu_shutdown()` -- release GPU resources

Each backend follows an identical pattern: `lwgpu_<backend>_init()`, `lwgpu_<backend>_rotate_z()`, `lwgpu_<backend>_rotate_z_m_epoch()`, `lwgpu_<backend>_shutdown()`, `lwgpu_<backend>_device_name()`. The Metal backend will add a fourth set matching this pattern exactly.

## Goals / Non-Goals

**Goals:**
- Add an Apple Metal GPU compute backend to `lwgeom_gpu.h` that follows the existing CUDA/ROCm/oneAPI pattern
- Exploit unified memory (zero-copy buffer sharing) to reduce dispatch overhead on Apple Silicon
- Compile Metal shaders at build time (`.metal` -> `.air` -> `.metallib`) using Xcode toolchain
- Detect Metal availability via `configure.ac` on `darwin*` hosts only; all other platforms unaffected
- Provide `rotate_z_uniform`, `rotate_z_m_epoch`, and `rad_convert` Metal compute kernels
- Fall back silently to NEON SIMD if Metal initialization fails or device is unavailable
- **Produce bounded-error results on Metal with a documented, user-visible precision contract.** Metal is the `FP32_ONLY` precision class (see Decision 9 below). Absolute error on Earth-scale ECEF coordinates (~6.4e6 m) is bounded to ≤2 m. Bit-identical equivalence to scalar/NEON/CUDA is **explicitly not a goal for Metal** because Apple GPU hardware cannot represent fp64.
- Gate Metal dispatch per operation so the float32 precision tradeoff is only taken when it is acceptable for the operation's domain

**Non-Goals:**
- Supporting Metal on iOS, iPadOS, or visionOS (macOS only)
- Implementing spatial index operations (R-tree, GiST) on Metal
- Supporting Vulkan compute as a cross-platform alternative
- Modifying PG-Strom integration (PG-Strom is CUDA-only)
- Implementing software double-precision emulation on Metal (double-single / df64 arithmetic). Possible future change if sub-meter precision on Apple GPUs becomes a requirement; not in scope here.
- Retrofitting existing CUDA/ROCm/oneAPI backends with precision classification annotations. They are all `FP64_NATIVE` by construction; the classification is introduced solely to document the Metal tradeoff.

## Decisions

### Decision 1: Architecture -- Metal as a fourth GPU backend

**Choice:** Add `LW_GPU_METAL = 4` to the `LW_GPU_BACKEND` enum in `lwgeom_gpu.h`. Implement the Metal backend in two new files:
- `liblwgeom/accel/gpu_metal.m` -- Objective-C runtime bridge (device init, pipeline setup, buffer management, dispatch)
- `liblwgeom/accel/gpu_metal_kernels.metal` -- Metal Shading Language compute kernels

The `.m` file uses Objective-C because the Metal API is an Objective-C framework (`<Metal/Metal.h>`). The `.metal` file uses MSL (C++14-based Metal Shading Language). Both are compiled only on macOS when `HAVE_METAL=1`.

**Rationale:** The existing backend pattern (one implementation file per GPU vendor) is well-established. Metal requires Objective-C for the host-side API and MSL for device-side kernels, which is analogous to CUDA requiring `.cu` and ROCm requiring `.hip`. Keeping the Metal code in `liblwgeom/accel/` alongside the other backends maintains the existing directory structure.

**Alternatives considered:**
- (A) Wrapping Metal in a pure-C API via Objective-C runtime functions (`objc_msgSend`) -- rejected because it adds complexity for no benefit; Objective-C compilation is straightforward on macOS
- (B) Using Metal-cpp (C++ bindings for Metal) -- rejected because it adds a header-only dependency and the Objective-C API is the primary, best-documented interface

### Decision 2: Memory model -- unified memory with zero-copy buffers

**Choice:** Use `newBufferWithBytesNoCopy:length:options:deallocator:` to wrap the caller's `double*` array as a Metal buffer without copying. Use `MTLResourceStorageModeShared` for shared CPU/GPU access. After kernel completion, results are already in the caller's memory -- no copy-back needed.

For safety, the deallocator block is `nil` (PostGIS owns the memory, Metal must not free it). The buffer is created per-dispatch and released immediately after the command buffer completes.

**Rationale:** Apple Silicon's unified memory architecture means CPU and GPU share the same physical DRAM. `StorageModeShared` with `newBufferWithBytesNoCopy` avoids all allocation and copy overhead, which is the primary advantage over discrete GPU backends. The CUDA backend (`gpu_cuda.cu`) requires `cudaMalloc` + `cudaMemcpy` host-to-device + kernel + `cudaMemcpy` device-to-host. Metal eliminates all three memory operations.

For page-alignment requirements, `newBufferWithBytesNoCopy` requires the pointer to be page-aligned and the length to be a multiple of page size. If the caller's buffer does not meet these requirements, fall back to `newBufferWithBytes:length:options:` which does a copy but is always safe.

**Alternatives considered:**
- (A) Always copy via `newBufferWithBytes` -- simpler but wastes the unified memory advantage
- (B) Use `MTLResourceStorageModeManaged` -- only needed for discrete GPUs (eGPU scenario); adds explicit synchronize calls with no benefit on integrated GPUs

### Decision 3: Build system -- configure.ac detection and shader compilation

**Choice:** Add a `--with-metal` / `--without-metal` configure flag (default: auto). Detection logic:
1. Check `$host_os` is `darwin*`
2. Run `xcrun --find metal` to locate the Metal shader compiler
3. Run `xcrun --find metallib` to locate the Metal library linker
4. If both found, set `HAVE_METAL=yes`, `AC_DEFINE([HAVE_METAL], [1], ...)`, substitute `METAL_COMPILER`, `METALLIB_TOOL`, `METAL_LDFLAGS="-framework Metal -framework Foundation"`

Shader compilation in `Makefile.in`:
```makefile
accel/gpu_metal_kernels.air: accel/gpu_metal_kernels.metal
	$(METAL_COMPILER) -c $< -o $@

accel/gpu_metal_kernels.metallib: accel/gpu_metal_kernels.air
	$(METALLIB_TOOL) $< -o $@

accel/gpu_metal.o: accel/gpu_metal.m accel/gpu_metal_kernels.metallib
	$(CC) -ObjC $(CPPFLAGS) $(CFLAGS) -c $< -o $@
```

The compiled `.metallib` is embedded as a C byte array via `xxd -i` at build time, stored in a generated header `accel/gpu_metal_kernels_metallib.h`. This avoids runtime file path resolution.

**Rationale:** Embedding the metallib eliminates deployment concerns (where to install the file, how to find it at runtime). The CUDA backend compiles kernels into the `.cu` object file; embedding the metallib achieves the same self-contained result. The `xxd -i` approach is well-established in graphics programming.

**Alternatives considered:**
- (A) Load `.metallib` from filesystem at runtime -- rejected because it requires knowing the install path and adds a failure mode
- (B) Compile shaders from source at runtime via `newLibraryWithSource:` -- rejected because runtime compilation is slow (~100ms) and Metal Shading Language source would need to be embedded anyway

### Decision 4: Runtime initialization -- lazy init with cached state

**Choice:** `lwgpu_metal_init()` performs:
1. Call `MTLCreateSystemDefaultDevice()` -- returns `nil` if no Metal-capable GPU exists
2. Query `[device name]` for the device name string (e.g., "Apple M2 Pro")
3. Create `MTLCommandQueue` from the device
4. Load the embedded `.metallib` byte array via `[device newLibraryWithData:error:]`
5. Create `MTLComputePipelineState` objects for each kernel function (`rotate_z_uniform`, `rotate_z_m_epoch`, `rad_convert`)
6. Cache all objects in file-scope statics (same pattern as `cuda_initialized` / `cuda_device` in `gpu_cuda.cu`)

Initialization is lazy: triggered on first GPU dispatch or explicit `lwgpu_init(LW_GPU_METAL)`. Thread safety relies on PostgreSQL's single-backend-per-process model (no concurrent init calls within a backend).

`lwgpu_metal_shutdown()` releases the device, command queue, library, and pipeline state objects by setting references to `nil` (ARC or explicit release).

**Rationale:** The CUDA backend uses the same pattern: `cuda_initialized` flag, `cuda_device` buffer, init on first call. Metal's Objective-C objects are cached between dispatches to avoid re-creating the pipeline state (~1ms overhead per creation).

### Decision 5: Kernel design -- three compute shaders

**Choice:** Three kernel functions in `gpu_metal_kernels.metal`:

1. **`rotate_z_uniform`** -- Uniform-angle Z-rotation:
   - Inputs: `device double *data`, `uint stride_doubles`, `uint npoints`, `double cos_t`, `double sin_t`
   - Each thread processes one point: reads `data[idx * stride + 0]` (x) and `data[idx * stride + 1]` (y), applies 2x2 rotation, writes back in-place
   - `cos_t` and `sin_t` are computed once on the CPU and passed as kernel arguments (same as CUDA backend)

2. **`rotate_z_m_epoch`** -- Per-point M-epoch Z-rotation:
   - Inputs: `device double *data`, `uint stride_doubles`, `uint npoints`, `uint m_offset`, `int direction`
   - Each thread reads `data[idx * stride + m_offset]` (epoch M), computes Julian Date and Earth Rotation Angle, then applies Z-rotation
   - ERA computation (`gpu_epoch_to_jd`, `gpu_earth_rotation_angle`) is inlined in MSL using the same formulas as `gpu_cuda.cu`

3. **`rad_convert`** -- Bulk radian/degree conversion:
   - Inputs: `device double *data`, `uint stride_doubles`, `uint npoints`, `double scale`
   - Each thread multiplies `data[idx * stride + 0]` (x) and `data[idx * stride + 1]` (y) by `scale`
   - This is a simple bandwidth-bound kernel; Metal's memory bandwidth (~200 GB/s on M2 Pro) provides throughput well above NEON

Thread dispatch: use `dispatchThreads:threadsPerThreadgroup:` with threadgroup size of 256 (matching CUDA/ROCm `threads = 256`). Metal handles incomplete threadgroups automatically via `dispatchThreads` (non-uniform dispatch).

**Rationale:** The three kernels exactly match the operations already implemented in CUDA (`gpu_cuda.cu` lines 42-73) and ROCm (`gpu_rocm.hip`). The ERA computation formulas (`2451545.0 + (decimal_year - 2000.0) * 365.25` for JD, `2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du)` for ERA) must be identical across all backends for numerical consistency.

### Decision 6: Dispatch threshold -- lower default for Metal

**Choice:** Reuse the existing `postgis.gpu_dispatch_threshold` GUC. The auto-calibration function `lwaccel_calibrate_gpu()` will benchmark Metal vs NEON and determine the crossover point dynamically. The initial default hint for Metal (before calibration) is 5000 points, compared to 10000 for discrete GPU backends.

**Rationale:** Discrete GPU dispatch (CUDA/ROCm) incurs ~30-150us of PCIe transfer overhead per dispatch. Metal on unified memory has near-zero transfer overhead -- the only cost is command buffer encoding (~5-10us) and kernel launch latency. This means the crossover point where GPU beats NEON is lower. Benchmarking on M2 Pro suggests 1000-5000 points depending on operation complexity. The auto-calibration handles this per-system; the lower default hint avoids pessimizing early dispatches.

### Decision 7: Error handling -- silent fallback to NEON

**Choice:** All Metal errors (device not found, library load failure, pipeline creation error, command buffer error) cause the Metal dispatch function to return 0 (failure). The caller in `lwgeom_gpu.c` then falls back to the CPU SIMD path. On first failure, log a `NOTICE`-level message with the Metal error description, then set a flag to skip future Metal dispatch attempts for the session.

**Rationale:** GPU errors must never crash the PostgreSQL backend. The CUDA backend already returns 0 on failure (e.g., `cudaMalloc` failure triggers `goto cleanup` returning 0). Metal follows the same convention. The NOTICE log helps debugging without disrupting query execution.

### Decision 8: Float32 precision on Metal (revised)

**Original (incorrect) position:** An earlier draft of this design specified `double` (64-bit) throughout all Metal kernels, claiming "Metal Shading Language supports `double` natively on Apple Silicon GPUs (M1+)". **That claim is factually wrong.** Apple GPU shader cores have no fp64 floating-point ALUs in any generation (M1 through M4, all A-series), and MSL does not expose `double` as a compute type in any version. The original rationale was based on a misreading of Apple documentation and has been superseded.

**Choice:** Use `float` (32-bit) throughout all Metal kernels. The host-side bridge (`gpu_metal.m`) converts `double*` input buffers to `float*` before dispatch and back to `double*` after. The kernel params structs pass `float` scalars (`cos_t`, `sin_t`, `scale`). The kernel source uses `device float *data` as the buffer type.

**Precision cost:** At Earth-scale ECEF coordinates (magnitudes ~6.4e6 meters), 1 ULP of float32 is `6.4e6 × 2⁻²³ ≈ 0.76 meters`. After a compute-heavy operation (cos + sin + multiply + add), accumulated error is on the order of 1–2 ULPs, i.e. **approximately 1–1.5 meters absolute** at Earth scale. Relative error is bounded at ~2.3e-7.

**Rationale:**
1. There is no alternative. Metal on Apple Silicon is float-only at the hardware level. We either ship float32 or we ship nothing for Apple GPU acceleration.
2. Dispatch gating (see Decision 6 and the `effective_gpu_threshold()` + operation-based routing in `lwgeom_accel.c`) ensures Metal is only invoked for operations and point counts where the float32 precision is acceptable given the throughput gain.
3. The precision contract is formalized in the `metal-compute-kernels` capability spec, not buried in code comments, so users can reason about it.
4. Future precision-sensitive Metal operations can either (a) opt out of Metal dispatch entirely, or (b) use a precision-preserving technique such as local origin translation or double-single (df64) arithmetic, as a separate follow-up change.

**Implementation invariants:**
- Every `double` value entering a Metal kernel is converted to `float` at the C/Obj-C boundary, not inside MSL.
- Every `float` value exiting a Metal kernel is converted back to `double` at the C/Obj-C boundary.
- Params struct layouts in the kernel source use `float` for scalar math and `uint`/`int` for indices and stride.
- Host-side tests (`cu_metal.c`) use scale-relative tolerances (`max_coord × 1e-6`) reflecting the float32 contract, NOT the 1e-10 or 1e-15 tolerances appropriate for fp64 backends.

**Alternatives considered:**
- **(A) Skip Metal entirely.** Rejected — forgoes Apple Silicon GPU acceleration with no mitigation for users who would accept the precision tradeoff.
- **(B) Double-single (df64) arithmetic emulation in the kernel.** Represent each double as `(hi, lo)` pair of floats, implement add/multiply/sin/cos as compensated sequences. Viable for restoring ~fp64 precision at ~4x slowdown vs raw float32. Rejected for this change as out of scope; listed as future work if demand arises.
- **(C) Local origin translation.** Subtract a reference point before dispatch, translate back after. Cheap and effective when input points are spatially clustered, but requires bookkeeping in the dispatch layer and does not help for globally-distributed inputs. Deferred as a potential optimization for specific operations.

### Decision 9: GPU backend precision classification (new)

**Choice:** Introduce a two-class precision model for GPU backends:

- **`FP64_NATIVE`** — Hardware has fp64 ALUs. Kernels compute in double precision. Output is bit-identical to the scalar reference within fp64 rounding. No scale-dependent precision loss. This class includes: **NEON**, **scalar**, **CUDA**, **ROCm/HIP**, **oneAPI** (on fp64-capable devices), and any future fp64-capable backend.
- **`FP32_ONLY`** — Hardware has only fp32 ALUs, or the API does not expose fp64 compute. Kernels compute in single precision. Output has bounded absolute error proportional to input magnitude. Scale-dependent precision loss is documented per-backend. This class includes: **Metal** (all Apple Silicon), and potentially future mobile/embedded GPU backends with similar constraints.

The classification is not represented by a runtime enum in the current implementation — it lives in the spec and design documentation because it affects dispatch policy, not runtime code. A future change may promote it to a runtime attribute on each backend if a generic safety guard is needed.

**Rationale:** Introducing this classification:
1. **Names the problem.** Instead of burying the Metal float32 tradeoff in a comment inside the kernel source, the distinction is visible in the GPU abstraction's design contract.
2. **Guides dispatch policy.** The dispatch layer treats `FP64_NATIVE` backends as drop-in replacements for the scalar reference (any operation is safe) and `FP32_ONLY` backends as opt-in per operation (each addition requires precision review).
3. **Reviewable contract.** When a future reviewer asks "what precision does this backend guarantee?", the answer is the backend's precision class plus the per-class contract in the spec, not a case-by-case archaeology exercise.
4. **Extensible.** If a future backend needs a third class (e.g., "configurable precision" for backends that expose both fp32 and fp64 via a runtime flag), the classification can be extended without restructuring the docs.

**Per-class contracts:**

| Class        | Absolute error bound                                | Equivalence to scalar       | Dispatch policy                        |
|--------------|-----------------------------------------------------|-----------------------------|----------------------------------------|
| FP64_NATIVE  | ≤ fp64 ULP of each operand (typically ≪ 1 mm)       | Bit-identical within rounding | Safe for all operations; gated by throughput threshold only |
| FP32_ONLY    | ≤ `max_coord × 1e-6` (~6 m at Earth scale, ~1 m typical) | NOT bit-identical             | Opt-in per operation; requires precision review before adding |

**Test tolerance invariant:** Regression and CUnit tests that compare a backend's output to the scalar reference SHALL use a tolerance appropriate to the backend's precision class. `FP64_NATIVE` tests use `1e-10` (or tighter) absolute tolerance. `FP32_ONLY` tests use `max_coord × 1e-6` scale-relative tolerance. This invariant is checked manually during code review; the build system does not enforce it mechanically.

**Alternatives considered:**
- **(A) No classification; document Metal as a one-off exception.** Rejected — creates a footgun for future backends. The next person adding a "mobile GPU" backend (e.g., a hypothetical WebGPU path, or an embedded Vulkan compute variant on devices with no fp64) would have no framework to articulate the same tradeoff.
- **(B) Runtime enum with per-backend precision query function.** Overkill for the current need. Defer until a generic safety guard actually needs it.
- **(C) Three-class model (`FP64_NATIVE`, `FP32_ONLY`, `MIXED`).** Premature. The two-class model covers all current and near-term backends. Add the third class when a real use case appears.

## Benchmark Results

### Apple A18 Pro (iPhone 16 Pro SoC class, macOS)

Benchmarks run with `bench_accel` harness at 1K, 10K, 50K, 100K, and 500K point counts.

**rotate_z_uniform (single sin/cos, 2x2 multiply per point):**
Metal loses to NEON at ALL point counts. The per-point work is too light to overcome Metal dispatch overhead (~150us for command buffer encoding) and memory round-trip costs. NEON processes this operation at near-memory-bandwidth speed with negligible dispatch cost.

| Points | NEON (Mpts/s) | Metal (Mpts/s) | Winner |
|--------|--------------|----------------|--------|
| 1K     | 450          | 6              | NEON   |
| 10K    | 480          | 55             | NEON   |
| 50K    | 490          | 210            | NEON   |
| 500K   | 495          | 380            | NEON   |

**rotate_z_m_epoch (per-point JD + ERA + sin/cos):**
Metal wins at 50K+ points. The per-point compute (epoch-to-JD, Earth Rotation Angle, sin, cos, 2x2 rotation) is heavy enough to amortize Metal dispatch overhead.

| Points | NEON (Mpts/s) | Metal (Mpts/s) | Winner | Speedup |
|--------|--------------|----------------|--------|---------|
| 1K     | 75           | 6              | NEON   | 0.08x   |
| 10K    | 77           | 48             | NEON   | 0.62x   |
| 50K    | 77           | 165            | Metal  | 2.1x    |
| 100K   | 77           | 210            | Metal  | 2.7x    |
| 500K   | 77           | 239            | Metal  | 3.1x    |

**rad_convert (multiply x,y by scale):**
Metal loses to NEON at all point counts. This is pure memory bandwidth with minimal ALU; NEON handles it at full bandwidth with no dispatch overhead.

### Recommendations implemented

1. **Per-backend threshold multiplier:** Metal applies a 5x multiplier to `postgis.gpu_dispatch_threshold` (default 10K becomes 50K for Metal). This is implemented in `effective_gpu_threshold()` in `lwgeom_accel.c`.

2. **Operation-based routing:** Uniform Z-rotation (`rotate_z_uniform`) and radian conversion (`rad_convert`) always skip Metal and use NEON. Only compute-heavy per-point epoch rotation (`rotate_z_m_epoch`) dispatches to Metal when above the effective threshold.

3. **Why not lower the global threshold?** Discrete GPU backends (CUDA, ROCm) with PCIe transfer overhead have different crossover characteristics. The per-backend multiplier keeps the global default correct for PCIe GPUs while adjusting for Metal's unified memory + higher dispatch overhead profile.

## Risks / Trade-offs

- **[Risk] `newBufferWithBytesNoCopy` alignment requirements** -- Metal requires page-aligned pointers and page-multiple lengths for zero-copy buffers. PostGIS `POINTARRAY` memory comes from PostgreSQL's `palloc`, which may not be page-aligned. Mitigation: attempt zero-copy first; if alignment check fails, fall back to `newBufferWithBytes` (which copies). Log a DEBUG message when copying so users can optimize if needed.

- **[Risk] Float32 precision at Earth scale** -- Metal kernels operate in 32-bit float because Apple GPU shader cores have no fp64 hardware. At Earth-scale ECEF coordinates (6.4×10⁶ m), 1 ULP of float32 is ~0.76 m, so rotation output has ~1–2 m absolute error. This is acceptable for satellite ephemeris, astronomy, and coarse-resolution mapping but NOT acceptable for surveying, property boundaries, or other sub-meter-precision applications. **Mitigation (already implemented):** operation-based dispatch gating in `lwgeom_accel.c` — `rotate_z_uniform` and `rad_convert` always skip Metal (NEON is faster anyway, so there is no throughput reason to take the precision loss); only `rotate_z_m_epoch` dispatches to Metal, and only at 50K+ points where the compute cost amortizes dispatch overhead. **Mitigation (user-facing):** precision contract documented in `proposal.md` and formalized as the `FP32_ONLY` backend class in Decision 9. Users needing stricter guarantees can set `postgis.gpu_dispatch_threshold = 0` to disable all GPU dispatch.

- **[Risk] Metal throughput variability across generations** -- Different M-series generations have different float throughput and memory bandwidth. M1 has lower sustained float throughput than M3/M4. The auto-calibration threshold handles this, but early M1 hardware may see the threshold pushed high enough that Metal provides little benefit over NEON. Mitigation: auto-calibration adapts per-device; document expected performance tiers in the benchmark results.

- **[Risk] Xcode Command Line Tools required** -- Building with Metal support requires `xcrun metal` and `xcrun metallib`, which come from Xcode Command Line Tools (~1.5 GB). Mitigation: Metal is optional (`--without-metal`); detection is auto with graceful skip. Document the dependency clearly.

- **[Risk] Objective-C compilation in a C project** -- PostGIS is C99. Mixing in Objective-C `.m` files requires the compiler to handle Objective-C syntax. On macOS, `clang` (the default `CC`) handles `.m` files natively. GCC on macOS does not support Objective-C well. Mitigation: use `$(CC) -ObjC` for `.m` files; `configure` verifies the compiler can compile a trivial Objective-C test before enabling Metal.

- **[Trade-off] Embedded metallib increases binary size** -- The compiled `.metallib` (typically 10-50 KB) is embedded in the binary via `xxd -i`. This is negligible compared to the overall liblwgeom size but does mean the shader code is compiled into the shared library. This is the same approach used for embedded SPIR-V in Vulkan projects.

- **[Trade-off] macOS-only code path** -- Metal is inherently macOS-only. This adds platform-specific code behind `#ifdef HAVE_METAL` and `#if defined(__APPLE__)` guards. Non-macOS builds have zero exposure to Metal code. The CI burden increases slightly (need a macOS runner to test Metal compilation).
