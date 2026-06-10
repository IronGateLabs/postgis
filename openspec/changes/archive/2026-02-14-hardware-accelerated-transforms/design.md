## Context

PostGIS coordinate transforms iterate points in scalar C loops with zero vectorization. The primary workload is space-domain: streaming ECI/ECEF transforms for objects ranging from debris (bolts) to satellites. The current per-point computation profile is:

- ECI rotation: ~14 FLOPs + 2 trig calls per point (pure C, no PROJ)
- PROJ transform: ~100+ FLOPs per point (rad/deg conversion loops are in PostGIS, core transform is in PROJ)
- Memory layout: contiguous `double` arrays with fixed stride (16/24/32 bytes per point) — already SIMD/GPU-friendly

Key constraint: streaming workloads produce small batches (1-100 points per query). GPU dispatch overhead (~30-150 us) makes naive per-query GPU dispatch a net loss. Batching via Valkey can accumulate enough points to cross the GPU breakeven threshold (~1K-10K points).

PG-Strom already provides CUDA-based GPU acceleration for PostgreSQL with limited PostGIS function support. Contributing ECI functions to PG-Strom's GPU-PostGIS layer is lower effort than building a new GPU runtime.

## Goals / Non-Goals

**Goals:**
- Add SIMD-accelerated code paths for ECI rotation and rad/deg conversion loops
- Runtime CPU feature detection (AVX2, AVX-512, NEON) with scalar fallback
- Evaluate and integrate PG-Strom for GPU-accelerated ECI queries
- Design a GPU batch dispatch layer with multi-vendor support (CUDA/ROCm/oneAPI)
- Design Valkey-based batching to aggregate streaming transforms for GPU dispatch
- Build a benchmarking harness to measure and compare all acceleration paths
- Maintain bit-identical numerical results across all code paths

**Non-Goals:**
- Porting PROJ internals to GPU (PROJ is a separate project)
- Replacing PG-Strom with a custom GPU runtime for general SQL acceleration
- Supporting GPU-only deployments (CPU scalar path is always available)
- Real-time GPU memory management or persistent GPU contexts across queries
- Modifying the POINTARRAY memory layout (it's already stride-based and suitable)

## Decisions

### Decision 1: Layered acceleration strategy

**Choice:** Three layers, each independently optional:
1. **SIMD** (CPU vectorization) — always-on if hardware supports it, zero dispatch overhead
2. **PG-Strom** (GPU query acceleration) — optional extension, accelerates WHERE-clause spatial ops
3. **GPU batch dispatch** (custom kernel) — optional, for bulk transforms via Valkey batching

**Rationale:** Streaming workloads (1-100 points per query) benefit most from SIMD because there's no dispatch overhead. GPU only wins for batched workloads (10K+ points). PG-Strom handles the query-level GPU acceleration that already exists. These layers are independent: a server with no GPU still benefits from SIMD; a server with PG-Strom still benefits from SIMD on non-query-accelerated paths.

**Alternatives considered:**
- (A) GPU-only approach — rejected because streaming workload produces too few points per query for GPU breakeven
- (B) OpenMP threading — considered for Level 1.5 but deferred; SIMD gives 4-8x on single core without thread overhead, and PostgreSQL's per-backend model complicates multi-threading

### Decision 2: SIMD implementation approach

**Choice:** Compile-time feature detection via `configure` flags (`--enable-avx2`, `--enable-avx-512`, auto-detect default) with runtime `cpuid` check before first use. Implement using compiler intrinsics (`_mm256_*` for AVX2, `vld1q_f64` for NEON) rather than auto-vectorization pragmas.

**Rationale:** Compiler intrinsics guarantee vectorization happens. Auto-vectorization with `#pragma omp simd` or `-ftree-vectorize` is unreliable for trig-heavy loops (compilers rarely vectorize `sin`/`cos`). For ECI rotation, the key insight is that when all points share the same epoch (uniform rotation), `cos(ERA)` and `sin(ERA)` are computed once and the rotation becomes pure multiply-add — ideal for SIMD.

**Implementation sketch:**
```
// Uniform epoch: compute sin/cos once, SIMD the multiply-add
__m256d cos_v = _mm256_set1_pd(cos_era);
__m256d sin_v = _mm256_set1_pd(sin_era);
for (i = 0; i < npoints; i += 4) {
    x = _mm256_load_pd(&pts[i].x);  // 4 x-values
    y = _mm256_load_pd(&pts[i].y);  // 4 y-values
    x_new = _mm256_fmadd_pd(x, cos_v, _mm256_mul_pd(y, sin_v));
    y_new = _mm256_fnmadd_pd(x, sin_v, _mm256_mul_pd(y, cos_v));
}

// Per-point M-epoch: sin/cos per point, less SIMD benefit but still
// vectorizes the multiply-add after each trig pair
```

Note: stride-based POINTARRAY layout (x,y,z,m interleaved) requires gather/scatter for SIMD. For maximum throughput, a temporary SoA (structure-of-arrays) transpose may be needed for large arrays.

**Alternatives considered:**
- (A) Auto-vectorization pragmas — rejected because compilers won't vectorize the trig+rotate pattern reliably
- (B) ISPC (Intel SPMD Program Compiler) — interesting but adds a build dependency and is less portable than intrinsics
- (C) Compiler built-in vector types (`__attribute__((vector_size))`) — viable for GCC/Clang but less control than intrinsics

### Decision 3: GPU abstraction layer

**Choice:** Thin C abstraction with compile-time backend selection:
```
// lwgeom_gpu.h
typedef enum { LW_GPU_NONE, LW_GPU_CUDA, LW_GPU_ROCM, LW_GPU_ONEAPI } LW_GPU_BACKEND;
int lwgpu_init(LW_GPU_BACKEND preferred);
int lwgpu_rotate_z_batch(double *xy_pairs, int npoints, double theta);
int lwgpu_rotate_z_m_epoch_batch(double *xyzm, int npoints, int direction);
void lwgpu_shutdown(void);
```

Each backend compiles to a separate `.cu` / `.hip` / `.cpp` file. `configure` detects which toolkits are available and compiles the corresponding backend. Only one backend is active at runtime.

**Rationale:** Multi-vendor GPU support requires an abstraction. SYCL/oneAPI promises cross-vendor support but Intel's SYCL implementation for AMD/NVIDIA GPUs is immature. A thin C API with backend-specific implementations is the most portable approach.

**Alternatives considered:**
- (A) SYCL-only — rejected because cross-vendor SYCL is not production-ready for all targets
- (B) OpenCL — rejected because OpenCL is deprecated by vendors in favor of CUDA/ROCm/oneAPI
- (C) Vulkan Compute — rejected because it's too low-level for this use case

### Decision 4: Valkey batching for streaming workloads

**Choice:** A PostgreSQL background worker accumulates transform requests in a Valkey list. When the batch size threshold (configurable, default 10K points) or time window (configurable, default 100ms) is reached, the batch is dispatched to GPU, and results are written back to Valkey for client pickup.

**Rationale:** Individual streaming queries produce 1-100 points — below GPU breakeven. Valkey (Redis-compatible) provides sub-millisecond queue operations. Batching across concurrent clients amortizes GPU dispatch overhead. This is optional infrastructure; without Valkey, transforms use the CPU SIMD path.

**Alternatives considered:**
- (A) PostgreSQL shared memory for batching — viable but harder to share across backends and scale across nodes
- (B) Kafka — too heavyweight for sub-millisecond batching requirements
- (C) In-process ring buffer — simpler but limited to single PostgreSQL instance

### Decision 5: PG-Strom integration approach

**Choice:** Evaluate PG-Strom's GPU-PostGIS layer, then contribute ECI transform functions as PG-Strom GPU device functions. This means writing CUDA kernels that PG-Strom can invoke when it detects ECI-related PostGIS functions in query plans.

**Rationale:** PG-Strom already handles GPU memory management, query plan interception, and result marshaling. Contributing ECI functions to PG-Strom's existing framework is dramatically less work than building a custom GPU query accelerator. PG-Strom is CUDA-only, which limits vendor support, but it's the most mature option.

**Alternatives considered:**
- (A) Build custom GPU query accelerator — rejected because PG-Strom already solves the hard problems (plan interception, memory management, JIT)
- (B) Ignore PG-Strom, GPU-only for batch — misses the query-level acceleration opportunity

## Risks / Trade-offs

- **[Risk] SIMD numerical divergence** — Different SIMD widths may produce slightly different floating-point results due to FMA vs separate multiply-add. Mitigation: use consistent FMA across all paths and validate with epsilon-based comparison tests, not exact bit equality.

- **[Risk] CUDA vendor lock-in via PG-Strom** — PG-Strom is CUDA-only. Mitigation: SIMD and custom GPU paths support all vendors; PG-Strom integration is additive, not required.

- **[Risk] Valkey dependency for GPU batching** — Adds infrastructure complexity. Mitigation: Valkey batching is optional; without it, the system falls back to CPU SIMD. Document clearly that Valkey is only needed for GPU-batched streaming workloads.

- **[Risk] AoS vs SoA performance cliff** — POINTARRAY uses AoS (array-of-structures: x,y,z,m interleaved). SIMD works best with SoA (separate x[], y[] arrays). A temporary transpose adds overhead that may negate SIMD gains for small arrays. Mitigation: benchmark the crossover point; only use SoA transpose above a threshold (likely ~64 points).

- **[Risk] GPU driver compatibility** — GPU compute requires specific driver versions. Mitigation: runtime detection with graceful fallback; compile-time guards for toolkit presence.

- **[Trade-off] Build complexity** — Supporting 3 GPU backends + 3 SIMD variants + optional Valkey adds significant `configure` / CMake complexity. Accept this as the cost of multi-architecture support. Keep each backend in a separate file to minimize coupling.

- **[Trade-off] PG-Strom is CUDA-only** — Limits GPU query acceleration to NVIDIA hardware. For AMD/Intel GPU users, the custom batch dispatch path provides an alternative, though without query-plan-level integration.
