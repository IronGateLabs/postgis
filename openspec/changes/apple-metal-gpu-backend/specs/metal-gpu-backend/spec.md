## ADDED Requirements

### Requirement: Metal GPU backend initialization
The system SHALL support initializing an Apple Metal GPU compute backend on macOS systems with Metal-capable hardware, following the same lifecycle pattern as the CUDA, ROCm, and oneAPI backends.

#### Scenario: Successful Metal initialization
- **WHEN** `lwgpu_init(LW_GPU_METAL)` is called on a macOS system with a Metal-capable GPU
- **THEN** the system SHALL create an `MTLDevice` via `MTLCreateSystemDefaultDevice()`, create an `MTLCommandQueue`, load the precompiled `.metallib` containing compute kernels, create `MTLComputePipelineState` objects for `rotate_z_uniform`, `rotate_z_m_epoch`, and `rad_convert` kernels, and return 1 (success)

#### Scenario: No Metal device available
- **WHEN** `lwgpu_init(LW_GPU_METAL)` is called and `MTLCreateSystemDefaultDevice()` returns nil
- **THEN** the system SHALL return 0 (failure) without logging an error, allowing the dispatcher to fall back to the next backend or CPU SIMD

#### Scenario: Metallib load failure
- **WHEN** the Metal device is available but the embedded `.metallib` cannot be loaded (corrupted data or incompatible Metal version)
- **THEN** the system SHALL log a `NOTICE`-level message with the Metal error description, release the device, and return 0

#### Scenario: Lazy initialization
- **WHEN** Metal initialization has already succeeded (static `metal_initialized` flag is set)
- **THEN** subsequent calls to `lwgpu_metal_init()` SHALL be a no-op and return 1 immediately

### Requirement: Metal GPU backend enumeration
The system SHALL add `LW_GPU_METAL = 4` to the `LW_GPU_BACKEND` enum in `lwgeom_gpu.h`, and wire it into the init/dispatch/shutdown switch blocks in the GPU dispatch layer.

#### Scenario: Auto-detect priority order
- **WHEN** `lwgpu_init(LW_GPU_NONE)` is called (auto-detect) and multiple GPU backends are compiled
- **THEN** the system SHALL attempt backends in order: CUDA > ROCm > oneAPI > Metal, selecting the first that initializes successfully

#### Scenario: Explicit Metal selection via GUC
- **WHEN** a user sets `postgis.gpu_backend = 'metal'`
- **THEN** the system SHALL attempt only the Metal backend, ignoring other compiled backends

#### Scenario: Backend name reporting
- **WHEN** `lwgpu_backend_name()` is called with the Metal backend active
- **THEN** the system SHALL return a string like `"Metal (Apple M2 Pro)"` containing the MTLDevice name

### Requirement: Metal GPU dispatch for Z-rotation
The system SHALL dispatch Z-rotation operations to Metal GPU when the point count exceeds the GPU dispatch threshold and the Metal backend is initialized.

#### Scenario: Uniform-angle Z-rotation dispatch
- **WHEN** `lwgpu_rotate_z_batch()` is called with `npoints >= gpu_dispatch_threshold` and Metal is the active backend
- **THEN** the system SHALL wrap the caller's `double*` buffer in an `MTLBuffer` (zero-copy via `newBufferWithBytesNoCopy` if page-aligned, otherwise via `newBufferWithBytes`), encode a compute command using the `rotate_z_uniform` pipeline state, commit the command buffer, wait for completion, and return 1

#### Scenario: Per-point M-epoch Z-rotation dispatch
- **WHEN** `lwgpu_rotate_z_m_epoch_batch()` is called with `npoints >= gpu_dispatch_threshold` and Metal is the active backend
- **THEN** the system SHALL dispatch to the `rotate_z_m_epoch` Metal kernel with stride, m_offset, and direction parameters, wait for completion, and return 1

#### Scenario: Below-threshold fallback
- **WHEN** a Z-rotation is requested with `npoints < gpu_dispatch_threshold`
- **THEN** the system SHALL NOT dispatch to Metal and SHALL return 0, allowing the caller to use the CPU SIMD path

### Requirement: Metal unified memory model
The system SHALL exploit Apple Silicon unified memory to avoid explicit host-to-device and device-to-host memory copies.

#### Scenario: Zero-copy buffer for page-aligned data
- **WHEN** the caller's `double*` pointer is page-aligned and the total byte length is a multiple of the VM page size
- **THEN** the system SHALL create the `MTLBuffer` via `newBufferWithBytesNoCopy:length:options:deallocator:` with `MTLResourceStorageModeShared` and a nil deallocator, so no memory copy occurs

#### Scenario: Copy fallback for unaligned data
- **WHEN** the caller's `double*` pointer is NOT page-aligned or the length is not a page-size multiple
- **THEN** the system SHALL create the `MTLBuffer` via `newBufferWithBytes:length:options:` with `MTLResourceStorageModeShared`, which copies data into a Metal-managed buffer, and copy results back after kernel completion

#### Scenario: Results visible to CPU after kernel completion
- **WHEN** a Metal compute kernel completes on a `StorageModeShared` zero-copy buffer
- **THEN** the modified data SHALL be immediately visible to the CPU without any explicit synchronization or copy-back operation

### Requirement: Metal error handling and fallback
The system SHALL handle all Metal runtime errors gracefully, falling back to the CPU NEON SIMD path without crashing the PostgreSQL backend.

#### Scenario: Command buffer execution error
- **WHEN** a Metal command buffer reports an error status after `waitUntilCompleted`
- **THEN** the system SHALL log a `NOTICE` with the error description, set a session-level flag to disable future Metal dispatch attempts, and return 0 (triggering CPU fallback)

#### Scenario: Device lost during execution
- **WHEN** the Metal device is lost (e.g., eGPU disconnected) during kernel execution
- **THEN** the system SHALL catch the error, log a `NOTICE`, call `lwgpu_metal_shutdown()` to release all Metal objects, and return 0

#### Scenario: Pipeline state creation failure
- **WHEN** creating an `MTLComputePipelineState` for a kernel function fails (e.g., kernel not found in metallib)
- **THEN** the system SHALL log a `NOTICE` with the error description and return 0 from `lwgpu_metal_init()`

### Requirement: Metal backend shutdown
The system SHALL release all Metal objects (device, command queue, library, pipeline states) when `lwgpu_metal_shutdown()` is called.

#### Scenario: Clean shutdown
- **WHEN** `lwgpu_metal_shutdown()` is called after successful initialization
- **THEN** the system SHALL release (or set to nil) the MTLDevice, MTLCommandQueue, MTLLibrary, and all MTLComputePipelineState objects, and reset the `metal_initialized` flag to 0

#### Scenario: Shutdown without initialization
- **WHEN** `lwgpu_metal_shutdown()` is called but Metal was never initialized
- **THEN** the system SHALL be a no-op with no error

### Requirement: Metal function declarations in lwgeom_gpu.h
The system SHALL declare Metal backend functions in `lwgeom_gpu.h` under `#ifdef HAVE_METAL` guards, following the pattern of existing backends.

#### Scenario: Function signature consistency
- **WHEN** Metal backend functions are declared
- **THEN** the declarations SHALL match the pattern of existing backends:
  ```
  int lwgpu_metal_init(void);
  int lwgpu_metal_rotate_z(double *data, size_t stride, uint32_t n, double theta);
  int lwgpu_metal_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
                                   size_t m_off, int dir);
  void lwgpu_metal_shutdown(void);
  const char *lwgpu_metal_device_name(void);
  ```

## MODIFIED Requirements

### Requirement: GPU dispatch threshold tuning for Metal
The existing `postgis.gpu_dispatch_threshold` GUC SHALL apply to Metal dispatch. The auto-calibration function `lwaccel_calibrate_gpu()` SHALL benchmark Metal vs NEON to determine the optimal threshold per-device.

#### Scenario: Lower default threshold hint for Metal
- **WHEN** the Metal backend is active and `postgis.gpu_dispatch_threshold` is set to 0 (auto)
- **THEN** the auto-calibration SHALL use 5000 as the initial search midpoint (vs 10000 for discrete GPU backends), reflecting Metal's lower dispatch overhead due to unified memory

#### Scenario: User-specified threshold applies to Metal
- **WHEN** a user sets `postgis.gpu_dispatch_threshold = 2000`
- **THEN** Metal dispatch SHALL trigger for POINTARRAYs with 2000+ points, regardless of auto-calibration results
