## Context

PostGIS has GPU-acceleration source files for four backends (CUDA, ROCm, oneAPI, Metal) committed to develop, but only Metal has been validated end-to-end on real hardware. The user has access to Apple Silicon today and will get DGX Spark (NVIDIA Grace + Hopper, ARM) and an x86_64 + NVIDIA workstation in subsequent phases of personal hardware acquisition. AMD and Intel hardware access is planned but not yet scheduled.

The challenge: keep the validation effort linear in the number of backends, not quadratic in the number of (backend × test-harness × hardware) combinations. Achieve this by building shared infrastructure once (Phase 0 — already mostly done via the gpu-precision-classes change) and applying the same per-phase validation template to each backend in turn.

This roadmap is a living document. As phases complete, the corresponding entries get checked off and links are added to the focused implementation changes that did the work. The roadmap itself is never archived in the conventional OpenSpec sense — instead, it remains in `openspec/changes/` as the canonical "where are we" reference for the multi-vendor effort.

## Goals / Non-Goals

**Goals:**
- Maintain a single, reviewable, OpenSpec-tracked plan for validating all four GPU backends across the user's accessible (and future-accessible) hardware
- Minimize duplicated effort across phases by identifying shared infrastructure (precision classes spec, dispatch table, test tolerances, benchmark harness, build-env Docker images)
- Define per-phase success criteria so "phase complete" is unambiguous
- Document the relationship between this roadmap and focused per-phase implementation OpenSpec changes
- Make hardware dependencies explicit so future-self knows which phases are actionable today
- Allow phases to be reordered, paused, or skipped without invalidating the roadmap

**Non-Goals:**
- Implementing any phase. This change is planning only.
- Defining the exact API of any backend. The API is in `liblwgeom/lwgeom_gpu.h` already; this roadmap validates that API on each backend, not redesigns it.
- Predicting calendar dates. Hardware acquisition is opportunistic; the roadmap orders phases by dependency, not by deadline.
- Replacing the standard OpenSpec workflow. Each phase still gets its own focused OpenSpec change at implementation time. This roadmap is the parent document.

## Decisions

### Decision 1: Phases ordered by hardware access, then by dependency

**Choice:** Phase order is:

0. **Foundation** (no hardware needed) — `gpu-precision-classes` capability, `cu_accel_tolerances.h`, `lwgpu_backend_precision_class()` accessor. Already proposed in a sibling OpenSpec change.
1. **Apple Metal** (Apple Silicon, available today) — `apple-metal-gpu-backend` change is at PR review (PR #13). Follow-up performance work in `metal-simd-era-precompute` (sibling change).
2. **NVIDIA CUDA on ARM** (DGX Spark, expected next) — validates that the existing `gpu_cuda.cu` source compiles and runs on Grace + Hopper.
3. **NVIDIA CUDA on x86_64** (later x86 workstation) — validates the same source on the more conventional x86 + discrete-GPU configuration.
4. **AMD ROCm** (TBD AMD hardware) — validates `gpu_rocm.hip` on Instinct or Radeon Pro.
5. **Intel oneAPI** (TBD Intel hardware) — validates `gpu_oneapi.cpp` on Arc or Data Center GPU.
6. **Cross-vendor benchmark harness** (requires all of above) — produces apples-to-apples benchmarks across all four GPU backends + scalar/SIMD references.

**Rationale:** Phases 2 and 3 are split because the user's hardware acquisition order is ARM-first. They could collapse into one change at implementation time. Phases 4 and 5 are independent; whichever vendor hardware lands first goes first. Phase 6 is the capstone that produces the data needed for the final upstream PR's "why this is worth it" pitch.

**Alternatives considered:**
- **(A) Phase order by vendor revenue / market share** (CUDA → ROCm → oneAPI → Metal). Rejected because hardware availability dominates: the user has Apple Silicon today and DGX Spark soon.
- **(B) All-CUDA first** (validate ARM and x86 CUDA together). Considered for collapsing but kept split because the ARM CUDA combination has unique characteristics (Grace ↔ Hopper memory architecture, Linux-on-ARM toolchain quirks) that x86 builds don't see.
- **(C) Skip x86 entirely** since most production deployments are x86. Rejected because PostGIS users do run on x86 + NVIDIA, and validating both ARM and x86 CUDA confirms the abstraction is genuinely portable, not accidentally ARM-tuned.

### Decision 2: Per-phase template — what each focused change includes

**Choice:** Every per-phase implementation OpenSpec change SHALL include:

1. **Validation against the existing source** — confirm the backend's `gpu_<vendor>.{cu,m,cpp,hip}` and supporting files compile, link, and produce correct output. No source rewrites unless a real bug is found.
2. **Precision class declaration** — assert via the `lwgpu_backend_precision_class()` accessor that the backend's class matches the gpu-precision-classes spec. For all backends except Metal this should be `FP64_NATIVE`.
3. **CUnit test suite** — `cu_<vendor>.c` modeled after `cu_metal.c`, with init/rotate_z_uniform/rotate_z_m_epoch/rad_convert/fallback/small/large tests. Reuse the shared `cu_accel_tolerances.h` constants (`FP64_STRICT_TOLERANCE` for FP64_NATIVE backends).
4. **Benchmark integration** — `bench_<vendor>` (or extension of `bench_accel`) that produces throughput numbers comparable to NEON and Metal.
5. **Dispatch tuning** — measure crossover point vs NEON, set the per-backend threshold multiplier in `effective_gpu_threshold()`.
6. **CI integration** — if a CI runner with the corresponding hardware exists, wire the backend into the matrix. If not, document the manual validation procedure.
7. **Documentation update** — update the gpu-precision-classes spec (or its successor) with a checkmark in the "validated on real hardware" column for the backend.

**Rationale:** Templating per-phase work means each focused change has a predictable shape. Reviewers know what to expect. Implementers know what's required. The template can be revised as we learn from each phase.

**Alternatives considered:**
- **(A) No template, each phase improvises** — risks each phase missing important validation steps.
- **(B) Stricter template requiring CI integration on every phase** — rejected because the user's hardware access is opportunistic and CI runners with vendor-specific GPUs are expensive. Manual validation procedure is acceptable when CI isn't possible.

### Decision 3: This roadmap change is never archived

**Choice:** `multi-vendor-gpu-rollout` stays in `openspec/changes/` indefinitely. Phases get checked off in `tasks.md` as they complete, and links to the focused implementation changes are added inline. The change itself does not graduate to `openspec/specs/` via `openspec archive`.

**Rationale:** OpenSpec's archive workflow is designed for changes that propose a discrete delta to the spec and then become part of the spec when the delta lands. A roadmap is different: it's a living plan that tracks ongoing work. Archiving it would lose the "where are we" function.

The risk is OpenSpec validators may eventually flag this as a stale change. We accept that risk and document the convention here so future maintainers don't try to "clean up" the roadmap by archiving it.

**Alternatives considered:**
- **(A) Archive the roadmap and create a new one each year** — loses continuity and creates archive sprawl.
- **(B) Keep the roadmap as a markdown file outside OpenSpec** — loses the validation, structure, and reviewability that OpenSpec provides. The user explicitly preferred OpenSpec for planning.
- **(C) Archive after each phase and create a new roadmap for the remainder** — too much overhead.

### Decision 4: Phase prerequisites are documented but not enforced

**Choice:** Each phase's `tasks.md` entry lists its prerequisites (e.g., Phase 2 depends on the user having SSH access to a DGX Spark). If a prerequisite is not met, the phase simply remains unchecked in the tracking table — there is no automated gate.

**Rationale:** The user is the only consumer of this roadmap right now. Self-discipline plus written prerequisites is sufficient. Automation would add complexity for no benefit.

## Phase details

### Phase 0: Foundation (precision classes, shared tolerances, accessor)

**Status**: Specified in sibling OpenSpec change `gpu-backend-precision-classes`. Implementation pending after the change is committed.

**Prerequisites**: None.

**Success criteria**:
- `openspec/specs/gpu-precision-classes/spec.md` exists (after the precision-classes change archives)
- `liblwgeom/cunit/cu_accel_tolerances.h` exists with `FP32_EARTH_SCALE_TOLERANCE` and `FP64_STRICT_TOLERANCE`
- `lwgpu_backend_precision_class(LW_GPU_BACKEND b)` accessor is in `lwgeom_gpu.h` and returns the correct class for every existing enum value
- `cu_metal.c` uses the shared header (no local `#define`)
- Build green, all existing tests pass

**Unblocks**: All subsequent phases. Without the precision class accessor and shared tolerances, every per-phase change would have to re-derive them.

### Phase 1: Apple Metal validation + perf

**Status**: PR #13 in flight on user's fork (`apple-metal-gpu-backend`). Performance follow-up planned in sibling change `metal-simd-era-precompute`.

**Prerequisites**: Apple Silicon hardware (user has A18 Pro and M-series Macs). Xcode Command Line Tools.

**Success criteria** (Phase 1a — already met by PR #13):
- `cu_tester metal` runs 6/6 tests passing on A18 Pro
- Metal kernels produce output within `FP32_EARTH_SCALE_TOLERANCE` of scalar reference
- The OpenSpec change `apple-metal-gpu-backend` is internally consistent (no float-vs-double contradictions)

**Success criteria** (Phase 1b — pending the metal-simd-era-precompute change):
- `bench_metal` shows `rotate_z_m/metal` ≥ 1.5× NEON throughput at 500K points on A18 Pro
- The perf regression from the precision fix (currently ~8% below NEON) is closed

**Unblocks**: Confidence in the shared GPU abstraction layer's ability to host a real backend end-to-end. This is the first proof that the Metal-specific work was correctly factored to be reusable.

### Phase 2: NVIDIA CUDA on ARM (DGX Spark)

**Status**: Not started. Hardware not yet acquired.

**Prerequisites**:
- DGX Spark hardware with SSH access (or equivalent ARM + NVIDIA Grace + Hopper system)
- CUDA Toolkit 12+ for ARM Linux
- Phase 0 complete

**Success criteria**:
- Existing `liblwgeom/accel/gpu_cuda.cu` compiles cleanly via `nvcc` on the target
- `lwgpu_cuda_init()` succeeds on the device
- New `cu_cuda.c` test suite runs all rotate_z and rad_convert tests with `FP64_STRICT_TOLERANCE` (`1e-10` absolute, NOT scale-relative — CUDA is FP64_NATIVE)
- CUDA output matches scalar reference within `FP64_STRICT_TOLERANCE`
- `bench_cuda` (or extension of bench_accel) produces throughput numbers
- `lwgpu_backend_precision_class(LW_GPU_CUDA) == LW_GPU_PRECISION_FP64_NATIVE` confirmed via test assertion
- A spawned focused OpenSpec change `cuda-gpu-validation-on-arm` is created, all 4 artifacts complete and validated, and merged to develop

**Unblocks**: Phase 3 (CUDA on x86 — most of the work is shared; only the toolchain and host CPU changes). Also unblocks the cross-vendor benchmark harness (Phase 6).

### Phase 3: NVIDIA CUDA on x86_64

**Status**: Not started. Hardware not yet acquired.

**Prerequisites**:
- x86_64 Linux workstation with NVIDIA discrete GPU (RTX, A-series, or similar)
- CUDA Toolkit 12+
- Phase 2 complete (so the source has been validated; this phase just confirms portability)

**Success criteria**: Same as Phase 2 but on x86 + discrete NVIDIA. The focused change `cuda-gpu-validation-on-x86` may be a small diff against the Phase 2 change if everything just works, OR a more substantial change if x86-specific issues arise.

**Unblocks**: Confidence that the CUDA backend is portable across host architectures. Most production NVIDIA deployments are x86; this is the validation that matters for upstream PostGIS adoption.

### Phase 4: AMD ROCm/HIP

**Status**: Not started. Hardware not yet acquired.

**Prerequisites**:
- AMD GPU with ROCm support (Instinct MI series, Radeon Pro VII / W6800+, or Radeon RX 7000 series)
- ROCm 6+ installed
- Phase 0 complete

**Success criteria**: Same template as Phases 2/3 but for `liblwgeom/accel/gpu_rocm.hip` and `LW_GPU_ROCM`. Spawn focused change `rocm-gpu-validation`.

**Unblocks**: Confidence the abstraction layer works on a third vendor. After this phase, the precision-class model has been verified against three FP64_NATIVE backends, all behaving identically.

### Phase 5: Intel oneAPI/SYCL

**Status**: Not started. Hardware not yet acquired.

**Prerequisites**:
- Intel GPU with oneAPI support (Arc, Data Center GPU Max, or Iris Xe with caveats)
- oneAPI Base Toolkit 2024+
- Phase 0 complete

**Success criteria**: Same template, for `liblwgeom/accel/gpu_oneapi.cpp` and `LW_GPU_ONEAPI`. Spawn focused change `oneapi-gpu-validation`.

**Caveat**: Some Intel iGPUs do NOT have fp64 hardware (consumer Iris Xe before Arc). If the only available test hardware is one of those, the precision class for oneAPI would have to be FP32_ONLY for THAT specific configuration — but the spec models the BACKEND class, not the device class. This may require a follow-up amendment to gpu-precision-classes if it becomes a real concern.

**Unblocks**: Confidence the abstraction layer works on all four major GPU vendors.

### Phase 6: Cross-vendor benchmark harness

**Status**: Not started. Requires all four backends validated.

**Prerequisites**: Phases 1, 2 (or 3), 4, 5 all complete.

**Success criteria**:
- A single benchmark harness that, given access to multiple backends, produces apples-to-apples throughput numbers across CUDA, ROCm, oneAPI, Metal, NEON, AVX, and scalar
- A summary table in `openspec/specs/gpu-precision-classes/spec.md` (or its successor) showing measured throughput per backend per operation per point count
- The data needed to write the upstream PostGIS PR's "why this is worth it" section

**Unblocks**: Upstream PR. With validated cross-vendor data, the upstream pitch is "PostGIS now has GPU acceleration on every major vendor with a reviewable precision contract" rather than "we have CUDA code that may or may not work".

## Risks / Trade-offs

- **[Risk] Hardware acquisition timeline is unpredictable** → mitigation: phase order is robust to delays. Phases 2-5 are independent; whichever hardware arrives first goes first.
- **[Risk] One backend has a fundamental issue that blocks the rollout** → mitigation: each phase is independent. A blocker on (say) ROCm doesn't stop CUDA or Metal validation. The roadmap explicitly does not require all phases to complete in order.
- **[Risk] Maintainer of the upstream PostGIS project rejects the multi-backend contribution as too large** → mitigation: structure the upstream PR(s) by backend, with the precision-classes spec landing first as a foundation, then each backend as its own PR. If upstream wants to take only a subset, they can.
- **[Trade-off] Living roadmap that never archives** → unconventional for OpenSpec. Documented in Decision 3 above. Convention may need revisiting if it causes confusion.
- **[Trade-off] Per-phase changes don't exist yet** → only the roadmap is created in this commit. Empty placeholder changes for future phases would create stale artifacts. Better to spawn focused changes when the work actually starts.

## Open Questions

1. **Should there be a Phase 7 for performance comparison vs CPU baselines on real GIS workloads** (not synthetic benchmarks)? — Could be valuable but requires real-world dataset access. Defer until cross-vendor harness exists.
2. **What about compute-shader-based backends like Vulkan or WebGPU?** — Out of scope. Re-evaluate if a vendor-neutral compute story matures.
3. **Should the precision-classes spec be promoted to a top-level PostGIS concept** (not just GPU)? — E.g., a future fp16 SIMD batch path would benefit from the same classification. Defer until that materializes.
4. **Does the upstream PostGIS project want this work at all?** — Unknown. Reach out to the postgis-devel list when Phase 1 (Metal) lands and use the discussion to shape Phases 2-5 priority.
