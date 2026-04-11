## Why

PostGIS has GPU-acceleration *infrastructure* in place (`liblwgeom/lwgeom_gpu.h`, `liblwgeom/accel/gpu_*.{cu,m,cpp,hip,metal}`, and the `LW_GPU_BACKEND` enum with five values: NONE / CUDA / ROCM / ONEAPI / METAL) but only one backend has been **validated end-to-end on real hardware**: Apple Metal, via PR #13. The other three backends (CUDA, ROCm, oneAPI) exist as source files committed to develop but have not been built, tested, or benchmarked on the corresponding vendor hardware. They are **unvalidated** in the literal sense — no human has confirmed they compile, link, dispatch, or produce correct output.

This change captures the strategic rollout plan to validate all four GPU backends across the hardware the user has access to (or will have access to), in a phased order that maximizes early signal and minimizes wasted effort. It is **not** itself an implementation change. It is a planning artifact that:

1. Enumerates the phases in concrete order with hardware dependencies, success criteria, and what gets unblocked when each phase completes
2. Identifies what is shared infrastructure across phases (so it gets built once, not five times)
3. Identifies what is phase-specific (so each phase has a clean, focused OpenSpec change)
4. Documents the user's access to hardware so future-self knows which phase is actionable today
5. Provides a tracking checklist for which phases have completed, are in flight, or are waiting on hardware availability

When each phase becomes actionable (hardware available, prerequisites met), a separate focused OpenSpec change SHOULD be created from the corresponding task in this roadmap. This change itself never gets archived in the normal sense — it stays in `openspec/changes/` as the living roadmap, with task checkboxes ticked off as phases complete and links added to the focused changes that implement them.

## What Changes

This change creates a single new "capability" that captures the rollout plan as a structured document. It does not modify any code, configure any backend, or change any test. It exists purely to make the strategic plan **reviewable as a spec** rather than as a side-channel document.

- Create a new capability `multi-vendor-gpu-rollout` that captures: phase order, per-phase prerequisites, per-phase success criteria, shared infrastructure, hardware dependencies, and a tracking table
- Document the relationship between this roadmap and the focused per-phase OpenSpec changes that will be spawned from it
- Establish the convention: this change is **not archived** when phases complete. Instead, individual phases get checked off in `tasks.md` and links to the implementing changes are added. This is unusual for OpenSpec but appropriate for a long-running plan.

## Capabilities

### New Capabilities

- `multi-vendor-gpu-rollout`: A living roadmap capability that defines the phased plan for validating PostGIS GPU backends across vendor hardware (Apple Metal, NVIDIA CUDA on ARM, NVIDIA CUDA on x86, AMD ROCm, Intel oneAPI). Specifies phase order, prerequisites, success criteria, hardware dependencies, and the relationship between this roadmap and focused per-phase OpenSpec changes.

### Modified Capabilities

None.

## Impact

- **Code**: zero. No source files touched.
- **Build system**: zero. No Makefiles or configure changes.
- **Tests**: zero. No new test files.
- **OpenSpec**: this change creates a new capability spec at archive time. The change itself stays in `openspec/changes/multi-vendor-gpu-rollout/` indefinitely as a living document.
- **Process**: establishes the convention that strategic roadmaps live as OpenSpec changes (not as side-channel docs), and that focused per-phase changes are spawned from the roadmap as hardware becomes available.

## Phases at a glance

| Phase | Backend           | Hardware                    | Status (2026-04-11)                                          |
|-------|-------------------|-----------------------------|--------------------------------------------------------------|
| 0     | Foundation        | n/a                         | **Done** — gpu-precision-classes (sibling change), liblwgeom/lwgeom_gpu.h abstraction layer, dispatch table |
| 1     | Apple Metal       | Apple Silicon (M-series, A18 Pro) | **Mostly done** — PR #13 (apple-metal-gpu-backend) merged-pending. Follow-up performance work in metal-simd-era-precompute (sibling change) |
| 2     | NVIDIA CUDA (ARM) | DGX Spark (Grace + Hopper)  | **Waiting on hardware access** (user has DGX Spark planned)   |
| 3     | NVIDIA CUDA (x86) | x86_64 Linux + RTX/A-series | **Waiting on hardware access**                                |
| 4     | AMD ROCm/HIP      | AMD Instinct or Radeon Pro  | **Waiting on hardware access**                                |
| 5     | Intel oneAPI      | Intel Arc / Data Center GPU | **Waiting on hardware access**                                |
| 6     | Cross-vendor benchmark harness | All of the above | **Waiting on phases 2-5 to complete**                         |

See `design.md` for per-phase details and `tasks.md` for the tracking checklist.

## Open Questions

1. **Should each phase get its own OpenSpec change as soon as hardware is available, or only when implementation actually starts?** — Recommendation: only when implementation starts. Pre-creating empty changes for unstarted phases creates stale artifacts that decay. The roadmap captures the plan; focused changes capture the work.
2. **Should phases 2 and 3 be merged into a single "CUDA validation" phase?** — Considered. They share the same source files (`gpu_cuda.cu`) and the validation goal is the same. Splitting into ARM vs x86 makes sense because (a) the user's hardware acquisition order is ARM first, x86 later, and (b) ARM CUDA exposes Grace CPU + Hopper GPU asymmetries that x86 builds don't see. Keep them split for now; they can collapse into one focused change at implementation time if it makes sense.
3. **Where does PG-Strom fit?** — Out of scope. PG-Strom is a separate query-level GPU offload project that uses CUDA for whole-query acceleration. PostGIS's GPU abstraction is for batch coordinate transforms, a much narrower scope. The two could coexist but are not in the rollout plan.
4. **Should the roadmap include WebGPU as a future portable backend?** — Out of scope. WebGPU compute support is still maturing and the precision/performance characteristics on each browser+OS combination are too unstable to plan against. Re-evaluate in 1-2 years.
5. **What about non-GPU accelerators (TPU, NPU, VPU)?** — Out of scope. The dispatch abstraction could in principle support them, but no current PostGIS workload would benefit and the testing burden is enormous. Document as a deferred consideration.
