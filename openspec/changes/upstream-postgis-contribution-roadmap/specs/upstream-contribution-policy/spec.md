## ADDED Requirements

### Requirement: Four-way classification of fork changes

Every commit, PR, OpenSpec change, and other fork artifact SHALL be classifiable into exactly one of four categories with respect to upstream contribution. The four categories are:

- **`UPSTREAM_READY`** — The change is valuable to upstream `postgis/postgis` as-is, with no refactoring or scope changes needed. Default action: open a focused upstream PR.
- **`UPSTREAM_AFTER_REFACTOR`** — The change addresses a real upstream problem but is scoped or packaged in a fork-specific way. Must be extracted, rescoped, or rewritten before upstream review. Default action: start an extraction-spike task in this roadmap.
- **`FORK_SPECIFIC`** — The change addresses a fork-internal concern that upstream will never adopt (OpenSpec workflow, fork's private CI config, fork-specific revert commits). Default action: leave on fork; do not attempt upstream.
- **`UPSTREAM_ALREADY_HAS`** — The change duplicates something upstream has already implemented. Default action: close the fork's version as redundant once confirmed.

#### Scenario: Every change has exactly one category

- **WHEN** any commit, PR, or OpenSpec change on the fork is classified
- **THEN** the classification SHALL assign it to exactly one of `UPSTREAM_READY`, `UPSTREAM_AFTER_REFACTOR`, `FORK_SPECIFIC`, or `UPSTREAM_ALREADY_HAS`
- **AND** if a change does not clearly fit one category, the roadmap SHALL add explicit classification criteria or split the change into multiple smaller changes with independent classifications
- **AND** a change SHALL NOT be left unclassified once reviewed

#### Scenario: Classification is recorded where visible

- **WHEN** a fork change is categorized as `UPSTREAM_READY` or `UPSTREAM_AFTER_REFACTOR`
- **THEN** the change's tracking row in the roadmap's `Baseline snapshot` table (in `design.md`) SHALL include its category
- **AND** when the change is addressed by a spawned upstream PR, the PR body SHALL state the category explicitly
- **AND** the spawned PR body SHALL NOT reference fork-internal OpenSpec changes or fork-specific infrastructure

#### Scenario: Re-classification is allowed when circumstances change

- **WHEN** a `UPSTREAM_AFTER_REFACTOR` change's refactor is completed
- **THEN** the change SHALL be re-classified to `UPSTREAM_READY` and a focused upstream PR SHALL be spawned
- **WHEN** an `UPSTREAM_READY` change is discovered to have upstream-equivalent work already landed
- **THEN** the change SHALL be re-classified to `UPSTREAM_ALREADY_HAS` and closed without further action

### Requirement: Phased ordering for upstream contribution

Upstream contribution SHALL proceed in a defined phase ordering. The phases SHALL be executed sequentially with documented dependencies, not in parallel, except where explicitly marked as independent.

#### Scenario: Phase 0 — announcement and scoping — blocks all others

- **WHEN** no upstream contact has yet been made with the `postgis-devel` mailing list about the fork's work
- **THEN** Phase 0 SHALL execute first, consisting of a scoping email summarizing the fork's intended contributions and asking for preferred PR structure
- **AND** subsequent phases SHALL NOT open upstream PRs until Phase 0 completes OR explicitly waived after two weeks of no response from the list

#### Scenario: Infrastructure-fixes phase precedes bug-fixes phase

- **WHEN** Phase 0 is complete
- **THEN** Phase 1 (infrastructure fixes — libtool cunit fix, build-env fixes, mingw CI hygiene) SHALL precede Phase 2 (real bug fixes)
- **AND** Phase 1 PRs SHALL be small, low-risk, and self-contained — chosen to build review reputation cheaply
- **AND** if Phase 1 PRs are rejected or ignored, the roadmap SHALL pause before committing effort to Phase 2

#### Scenario: SIMD/acceleration infrastructure precedes feature capabilities

- **WHEN** Phase 2 is complete
- **THEN** Phase 3 (SIMD and acceleration infrastructure — the `lwgeom_gpu.h` abstraction, dispatch table, scalar/NEON/AVX backends) SHALL precede Phase 4 (ECEF/ECI core capability) and Phase 5 (Metal GPU backend)
- **AND** Phase 3 SHALL be preceded by a design discussion on `postgis-devel` because it introduces a new subsystem rather than a bug fix

#### Scenario: Feature phases reference the GPU-specific roadmap

- **WHEN** the upstream contribution phases reach Phase 5 (Metal) and beyond
- **THEN** the roadmap SHALL reference `multi-vendor-gpu-rollout/tasks.md` Phase 7 as the authoritative source for GPU-specific upstream work
- **AND** this roadmap SHALL NOT duplicate the GPU contribution plan
- **AND** this roadmap's GPU phase entries SHALL serve as dependency markers and cross-references only

### Requirement: Per-phase template for upstream PRs

Every upstream PR spawned from this roadmap SHALL conform to a standard template that makes PR review predictable for upstream maintainers.

#### Scenario: Upstream PR includes classification and phase in description

- **WHEN** an upstream PR is drafted from this roadmap
- **THEN** the PR description SHALL explicitly state its category (from the four-way classification) and its phase (from the phase ordering)
- **AND** the PR description SHALL NOT reference fork-internal OpenSpec changes, internal issue numbers, or fork-specific infrastructure

#### Scenario: Upstream PR is isolated from fork-specific infrastructure

- **WHEN** an upstream PR's diff is computed via `git diff upstream/master...HEAD`
- **THEN** the diff SHALL NOT touch any of: `openspec/**`, `.github/workflows/sonar.yml`, `sonar-project.properties`, `.sonar/**`, or any other file identified as `FORK_SPECIFIC` in this roadmap
- **AND** the PR SHALL be rejected before opening if fork-specific files appear in the diff
- **AND** the author SHALL split the change into fork-specific and upstream-ready halves before retrying

#### Scenario: Upstream PR includes regression test coverage

- **WHEN** an upstream PR is a bug fix (Phase 2 work)
- **THEN** the PR SHALL include a new test (CUnit, regress SQL, or equivalent) that reproduces the bug on upstream `master` and passes with the fix applied
- **WHEN** an upstream PR is a feature (Phase 3+ work)
- **THEN** the PR SHALL include new tests exercising the feature's contract, with passing test runs captured in the PR description

#### Scenario: Upstream PR for precision-sensitive changes attests precision

- **WHEN** an upstream PR touches GPU or acceleration code that has a non-trivial precision contract
- **THEN** the PR SHALL include an explicit precision statement: what hardware it was verified on, what error bound it provides, what failure modes exist
- **AND** the precision statement SHALL reference the `gpu-precision-classes` capability model if the work falls under FP64_NATIVE or FP32_ONLY classification

### Requirement: Baseline snapshot for progress tracking

This roadmap SHALL maintain a baseline snapshot of the fork's state at its creation time (2026-04-11) so progress can be measured against a reference point.

#### Scenario: Baseline is captured in design.md

- **WHEN** a reviewer wants to understand what the fork contained at roadmap creation
- **THEN** the roadmap's `design.md` SHALL contain a "Baseline snapshot (2026-04-11)" section with:
  - A fork-vs-upstream delta table showing which capabilities exist on each side
  - A change categorization table listing each known fork change and its category
  - Identified `UPSTREAM_READY` items that are candidates for the first upstream PR

#### Scenario: Progress is recorded at phase boundaries

- **WHEN** a phase completes
- **THEN** the phase's entry in `tasks.md` SHALL be checked off
- **AND** a link to the spawned upstream PR (if any) SHALL be added inline next to the checked item
- **AND** the baseline snapshot SHALL be annotated with the date the phase completed

### Requirement: Living roadmap discipline

This roadmap SHALL follow the living-roadmap convention used by sibling OpenSpec changes (`multi-vendor-gpu-rollout`, `sonarcloud-cleanup`). It SHALL NOT be archived via `openspec archive` when phases complete; it remains in `openspec/changes/upstream-postgis-contribution-roadmap/` indefinitely.

#### Scenario: Roadmap stays in openspec/changes

- **WHEN** a phase of this roadmap is completed
- **THEN** the phase's tasks SHALL be checked off in `tasks.md`
- **AND** the roadmap SHALL NOT be moved to `openspec/changes/archive/`
- **AND** if the OpenSpec validator flags this change as stale, the maintainer SHALL point at this requirement and the corresponding Decision 5 in `design.md` to explain why the change is intentionally never archived

#### Scenario: New phases can be added as upstream scope evolves

- **WHEN** upstream contribution uncovers work that does not fit any existing phase
- **THEN** a new phase SHALL be added to `tasks.md` with its own entry, dependency documentation, and category classification
- **AND** existing phases SHALL NOT be renumbered; new phases get inserted with fractional numbering or appended at the end

### Requirement: PR #10 triage as a task group

PR #10 (`feature/codebase-spec-extraction`) adds 12 foundational capability specs via reverse-engineering. This roadmap SHALL track the triage of each of the 12 specs as individual sub-tasks in the task group "PR #10 triage".

#### Scenario: Each PR #10 capability has its own triage sub-task

- **WHEN** the roadmap's `tasks.md` is reviewed
- **THEN** the "PR #10 triage" section SHALL contain one sub-task per capability in PR #10's feature branch: `geometry-types`, `gserialized-format`, `spatial-predicates`, `spatial-operations`, `measurement-functions`, `coordinate-transforms`, `spatial-indexing`, `constructors-editors`, `geography-type`, `extension-lifecycle`, `raster-core`, `topology-model`
- **AND** each sub-task SHALL have a checkbox and space for a decision (ACCEPT / REFINE / REJECT / DEFER with reason)

#### Scenario: Accepted PR #10 capabilities spawn focused OpenSpec changes

- **WHEN** a PR #10 triage sub-task is decided as ACCEPT
- **THEN** a new focused OpenSpec change SHALL be spawned to `ADD` the capability to `openspec/specs/<capability>/spec.md`
- **AND** the focused change SHALL NOT copy PR #10's commits wholesale; instead, it SHALL extract the relevant spec content from PR #10's feature branch and author it as a fresh ADDED Requirements delta
- **AND** the triage sub-task SHALL then be checked off with a link to the spawned focused change

#### Scenario: Rejected PR #10 capabilities document the reason

- **WHEN** a PR #10 triage sub-task is decided as REJECT
- **THEN** the sub-task SHALL be checked off with the decision "REJECTED — reason: ..." and the reason SHALL be specific enough for a future maintainer to understand
- **AND** the capability SHALL NOT be added to `openspec/specs/` from PR #10

#### Scenario: PR #10 is closed after triage completes

- **WHEN** all 12 triage sub-tasks have been decided (not necessarily ACCEPT, but a decision recorded)
- **THEN** PR #10 on GitHub SHALL be closed with a comment summarizing which capabilities were accepted (and which focused changes implement them) and which were rejected
- **AND** the `feature/codebase-spec-extraction` branch MAY be deleted after PR #10 close; the spec content is preserved via the spawned focused changes

### Requirement: Coordination with sibling roadmaps

This roadmap SHALL coordinate with the existing living-roadmap OpenSpec changes on the fork (`multi-vendor-gpu-rollout`, `sonarcloud-cleanup`) without duplicating their content. Specifically, GPU-related upstream work is delegated to `multi-vendor-gpu-rollout` Phase 7, and SonarCloud real-bug-fix upstream work is delegated to `sonarcloud-cleanup` Phases 3–5.

#### Scenario: GPU upstream phases reference multi-vendor-gpu-rollout

- **WHEN** this roadmap's GPU-related phases (Phase 5 Metal, Phase 6 multi-vendor, Phase 7 benchmark data) are documented
- **THEN** each entry SHALL cross-reference `multi-vendor-gpu-rollout/tasks.md` Phase 7 as the authoritative source
- **AND** this roadmap SHALL NOT duplicate the GPU rollout's per-phase content

#### Scenario: SonarCloud real-bug fixes feed Phase 2 as they complete

- **WHEN** a phase of `sonarcloud-cleanup` produces a `UPSTREAM_READY` bug fix (Phases 3, 4, 5 are expected candidates)
- **THEN** this roadmap's Phase 2 (real bug fixes) SHALL include a sub-task pointing at the specific commits from the completed sonarcloud-cleanup phase
- **AND** the upstream PR for that bug fix SHALL be spawned from this roadmap's Phase 2 task, not from the sonarcloud-cleanup task
