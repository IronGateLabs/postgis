## ADDED Requirements

### Requirement: Living roadmap for multi-vendor GPU validation

The PostGIS fork SHALL maintain a single, OpenSpec-tracked, living roadmap document that captures the phased plan for validating GPU backends (Apple Metal, NVIDIA CUDA, AMD ROCm, Intel oneAPI) across vendor hardware. The roadmap SHALL be reviewable as a spec, SHALL document phase order with hardware dependencies, and SHALL serve as the canonical "where are we" reference for the multi-vendor effort.

The roadmap SHALL NOT be archived in the conventional OpenSpec sense when individual phases complete. Instead, it SHALL remain in `openspec/changes/multi-vendor-gpu-rollout/` indefinitely, with phases tracked via checkboxes and links to focused per-phase implementation OpenSpec changes added inline as those changes are spawned.

#### Scenario: Roadmap exists and is discoverable

- **WHEN** a maintainer wants to find the multi-vendor GPU rollout plan
- **THEN** they SHALL find it at `openspec/changes/multi-vendor-gpu-rollout/` with the standard OpenSpec artifact set (proposal.md, design.md, specs/, tasks.md)
- **AND** the proposal.md SHALL contain the at-a-glance phase table
- **AND** the design.md SHALL contain per-phase details, prerequisites, success criteria, and the rationale for phase ordering

#### Scenario: Phases are checked off as they complete

- **WHEN** a phase's success criteria are met (per design.md per-phase definitions)
- **THEN** the corresponding entry in `tasks.md` SHALL have its checkbox ticked
- **AND** a link to the focused implementation OpenSpec change that completed the phase SHALL be added inline next to the checked item
- **AND** the at-a-glance phase table in proposal.md SHALL be updated with the new status

#### Scenario: New phases SHALL spawn focused OpenSpec changes

- **WHEN** a phase becomes actionable (its prerequisites are met, hardware is available)
- **THEN** a separate focused OpenSpec change SHALL be created using the per-phase template defined in design.md Decision 2
- **AND** the focused change SHALL reference this roadmap in its proposal.md
- **AND** the focused change SHALL follow the standard OpenSpec lifecycle (propose → review → implement → archive)
- **AND** the roadmap SHALL be updated to link to the focused change

### Requirement: Per-phase template for focused implementation changes

Every focused OpenSpec change spawned from this roadmap to validate a specific GPU backend SHALL include the following elements, in addition to the standard OpenSpec artifact set.

#### Scenario: Focused change includes validation of existing source

- **WHEN** a focused per-phase change is created
- **THEN** the proposal SHALL state that the change validates the existing `liblwgeom/accel/gpu_<vendor>.{cu,m,cpp,hip}` source compiles, links, and produces correct output on the target hardware
- **AND** the proposal SHALL NOT propose source rewrites unless a real bug is found and documented

#### Scenario: Focused change asserts precision class via accessor

- **WHEN** a focused per-phase change implements its CUnit test suite
- **THEN** at least one test SHALL assert `lwgpu_backend_precision_class(LW_GPU_<VENDOR>) == LW_GPU_PRECISION_<EXPECTED_CLASS>` where the expected class matches the gpu-precision-classes spec
- **AND** for all current GPU backends except Metal, the expected class SHALL be `LW_GPU_PRECISION_FP64_NATIVE`

#### Scenario: Focused change uses shared tolerance constants

- **WHEN** a focused per-phase change writes its CUnit tests
- **THEN** the test files SHALL `#include "cu_accel_tolerances.h"` and use the appropriate constant for the backend's precision class
- **AND** FP64_NATIVE backends SHALL use `FP64_STRICT_TOLERANCE` (`1e-10`)
- **AND** FP32_ONLY backends SHALL use `FP32_EARTH_SCALE_TOLERANCE` (~6.4 m for Earth-scale ECEF)
- **AND** the focused change SHALL NOT define its own local tolerance `#define`s

#### Scenario: Focused change includes benchmark integration

- **WHEN** a focused per-phase change is implemented
- **THEN** it SHALL produce throughput numbers comparable to the existing NEON and Metal benchmarks
- **AND** the comparison SHALL be at the same point counts (1K, 10K, 50K, 100K, 500K) as `bench_metal` for direct comparison
- **AND** the throughput data SHALL be added to the per-phase entry in this roadmap's design.md once measured

#### Scenario: Focused change documents dispatch tuning

- **WHEN** a focused per-phase change measures performance
- **THEN** it SHALL determine the crossover point where the GPU backend beats the CPU SIMD baseline
- **AND** it SHALL set or recommend a per-backend threshold multiplier in `effective_gpu_threshold()` (similar to Metal's 5x multiplier)
- **AND** the measurement methodology SHALL match the metal-simd-era-precompute change's benchmark instrumentation pattern

### Requirement: Hardware availability tracking

The roadmap SHALL include a tracking table that lists each phase's hardware prerequisites and current availability status. The table SHALL be updated as hardware is acquired so future maintainers know which phases are actionable today.

#### Scenario: Hardware status table exists

- **WHEN** a maintainer wants to know which phases can be started today
- **THEN** they SHALL find a table in `proposal.md` (or `design.md`) listing each phase, its hardware requirement, and its current status (e.g., "available", "expected Q3", "not yet planned")
- **AND** the status SHALL be updated whenever hardware acquisition or loss occurs

#### Scenario: Status uses unambiguous terms

- **WHEN** a phase's status is recorded in the tracking table
- **THEN** it SHALL use one of: `Done`, `In progress`, `Mostly done`, `Available (not started)`, `Waiting on hardware access`, `Blocked`, `Deferred`
- **AND** any status containing "Blocked" SHALL be accompanied by a brief explanation

### Requirement: Roadmap supersedes ad-hoc planning documents

The system SHALL NOT have parallel roadmap or planning documents for the multi-vendor GPU rollout effort outside of OpenSpec. Markdown files in `docs/`, README sections, or notes in code comments SHALL NOT duplicate or contradict the roadmap captured in this OpenSpec change.

#### Scenario: Single source of truth

- **WHEN** any code comment, README, or other documentation references the multi-vendor GPU rollout plan
- **THEN** it SHALL link to `openspec/changes/multi-vendor-gpu-rollout/` rather than restate the plan
- **AND** if a piece of information about the plan needs to live in a non-OpenSpec location (e.g., a CLAUDE.md file for AI assistants), it SHALL include a "see openspec/changes/multi-vendor-gpu-rollout/ for the authoritative version" pointer
