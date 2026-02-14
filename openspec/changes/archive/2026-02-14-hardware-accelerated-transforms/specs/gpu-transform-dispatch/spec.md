## ADDED Requirements

### Requirement: GPU batch transform dispatch
The system SHALL support dispatching large coordinate transform batches to GPU when a compatible GPU is detected. GPU dispatch SHALL be threshold-based: arrays below the threshold use CPU (SIMD) paths; arrays above use GPU.

#### Scenario: Large batch GPU dispatch
- **WHEN** a POINTARRAY with 10,000+ points is transformed via `ptarray_rotate_z()` and a GPU backend is available
- **THEN** the system SHALL copy point data to GPU memory, execute the rotation kernel, copy results back, and return the transformed POINTARRAY

#### Scenario: Small batch CPU fallback
- **WHEN** a POINTARRAY with fewer than the GPU dispatch threshold points is transformed
- **THEN** the system SHALL use the CPU SIMD path without GPU dispatch overhead

#### Scenario: No GPU available
- **WHEN** a transform is requested and no GPU backend was detected at initialization
- **THEN** the system SHALL use the CPU SIMD path with no error or warning

#### Scenario: Configurable dispatch threshold
- **WHEN** a user sets `postgis.gpu_dispatch_threshold = 50000` via GUC
- **THEN** the system SHALL only dispatch to GPU for POINTARRAYs with 50,000+ points

### Requirement: Multi-vendor GPU backend support
The system SHALL support NVIDIA (CUDA), AMD (ROCm/HIP), and Intel (oneAPI/SYCL) GPUs through a common C abstraction layer. Only one backend is active at runtime.

#### Scenario: CUDA backend initialization
- **WHEN** PostGIS is compiled with CUDA support and an NVIDIA GPU is detected at runtime
- **THEN** the system SHALL initialize the CUDA backend and use it for GPU-dispatched transforms

#### Scenario: ROCm backend initialization
- **WHEN** PostGIS is compiled with ROCm support and an AMD GPU is detected at runtime
- **THEN** the system SHALL initialize the ROCm/HIP backend and use it for GPU-dispatched transforms

#### Scenario: oneAPI backend initialization
- **WHEN** PostGIS is compiled with oneAPI support and an Intel GPU is detected at runtime
- **THEN** the system SHALL initialize the oneAPI/SYCL backend and use it for GPU-dispatched transforms

#### Scenario: Backend priority
- **WHEN** multiple GPU backends are compiled and multiple GPUs are detected
- **THEN** the system SHALL select a backend based on the `postgis.gpu_backend` GUC (default: auto-detect in order CUDA > ROCm > oneAPI)

### Requirement: Valkey-based transform batching
The system SHALL support accumulating streaming transform requests in a Valkey queue and dispatching them as GPU-sized batches. This is optional infrastructure that requires a running Valkey instance.

#### Scenario: Batch accumulation
- **WHEN** multiple concurrent clients submit ECI/ECEF transforms and Valkey batching is enabled via `postgis.gpu_valkey_url`
- **THEN** the system SHALL queue transform requests in Valkey and dispatch a GPU batch when the batch size threshold or time window is reached

#### Scenario: Batch result delivery
- **WHEN** a GPU batch completes
- **THEN** the system SHALL write transformed coordinates back to Valkey and notify waiting clients, who receive their results within the batch latency window

#### Scenario: Valkey unavailable fallback
- **WHEN** Valkey batching is configured but Valkey is unreachable
- **THEN** the system SHALL fall back to synchronous CPU SIMD transforms and log a warning

#### Scenario: Batch timeout
- **WHEN** the batch time window (default 100ms) expires before the batch size threshold is reached
- **THEN** the system SHALL dispatch the partial batch to GPU rather than waiting indefinitely

### Requirement: GPU memory safety
The system SHALL handle GPU memory allocation failures and kernel errors without crashing the PostgreSQL backend.

#### Scenario: GPU out of memory
- **WHEN** GPU memory allocation fails during transform dispatch
- **THEN** the system SHALL fall back to CPU SIMD transform and log a warning, without raising a SQL error

#### Scenario: GPU kernel error
- **WHEN** a GPU compute kernel returns an error code
- **THEN** the system SHALL fall back to CPU SIMD transform, log the error details, and continue processing
