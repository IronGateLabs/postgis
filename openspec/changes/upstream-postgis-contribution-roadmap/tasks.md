## Phase tracking checklist

This is a **living checklist**. Items get checked off as phases complete. Links to focused implementation PRs (against either `IronGateLabs/postgis` or `postgis/postgis`) are added inline. This change is intentionally **never archived** — it remains in `openspec/changes/upstream-postgis-contribution-roadmap/` indefinitely as the canonical upstream-contribution reference.

**Baseline (2026-04-11)**: see `design.md` "Baseline snapshot" section for the fork-vs-upstream delta and change categorization tables.

## Phase 0: Announcement and scoping on postgis-devel

**Status**: Not started. Blocks all subsequent phases (unless waived after 2 weeks of no list response).

- [ ] 0.1 Read the current `postgis/postgis` CONTRIBUTING.md (or equivalent) to understand upstream's preferred PR conventions, commit message format, sign-off requirements, and any DCO / CLA expectations
- [ ] 0.2 Read recent `postgis-devel` mailing list archives (last 3–6 months) to understand the maintainer team's current priorities and review bandwidth
- [ ] 0.3 Draft a scoping email for `postgis-devel@lists.osgeo.org` summarizing:
  - Who I am (montge) and why I forked
  - High-level inventory of the fork's work (ECEF/ECI, GPU abstraction, Metal backend, SIMD acceleration, bug fixes surfaced by SonarCloud)
  - Proposed upstream PR structure (phased per this roadmap)
  - Specific question: does upstream have capacity to review, and what PR structure is preferred?
  - Explicit "not asking for you to adopt OpenSpec or SonarCloud; these are my internal tools"
- [ ] 0.4 Send the scoping email
- [ ] 0.5 Wait for response (up to 2 weeks). Record the outcome in this task group.
- [ ] 0.6 Based on response, update this roadmap's phase plan if upstream prefers a different ordering

## Phase 1: Infrastructure fixes (UPSTREAM_READY, low-risk)

**Status**: Not started. Blocked on Phase 0 (unless waived).

These are small, self-contained, high-value fixes that upstream benefits from regardless of whether they take any feature work. Picked first because they build review-relationship reputation cheaply.

### 1a. libtool cunit Makefile fix

- [ ] 1a.1 Verify commit `03e644b1f` (from PR #11, now on develop) still applies cleanly against `upstream/master` via `git cherry-pick --no-commit`
- [ ] 1a.2 If clean cherry-pick, open a new branch `upstream/fix-cunit-libtool-wrapper` off `upstream/master`, cherry-pick the commit, and rewrite the commit message to be upstream-appropriate (no references to PR #11 or fork-internal OpenSpec changes)
- [ ] 1a.3 Open focused PR against `postgis/postgis:master` titled "Fix flaky mingw cunit failures: use libtool --mode=execute" with a description that explains the root cause and cites other cunit Makefiles (`liblwgeom/cunit`, `postgis/cunit`) that already use this pattern
- [ ] 1a.4 Track the upstream PR status; respond to review comments; iterate to merge

### 1b. postgis-build-env fixes (already in flight)

- [ ] 1b.1 Track status of `postgis/postgis-build-env#35` (arm64 ld.so.preload multiarch path)
- [ ] 1b.2 Track status of `postgis/postgis-build-env#36` (BUILD_THREADS auto-detect memory-aware default)
- [ ] 1b.3 Track status of `postgis/postgis-build-env#37` (undefined `DOCKER_CMAKE_BUILD_TYPE` in nlohmann/json block)
- [ ] 1b.4 Respond to review comments on each; iterate to merge

### 1c. Additional infrastructure candidates (identified opportunistically)

- [ ] 1c.1 If the sonarcloud-cleanup Phase 1 path exclusions (PR #16) reveal any exclusion patterns that would benefit `postgis/postgis` (e.g., new vendored code upstream hasn't marked), open a small upstream PR with the exclusion additions only

## Phase 2: Real bug fixes (UPSTREAM_READY, from sonarcloud-cleanup findings)

**Status**: Not started. Blocked on Phase 1 completion (build review relationship first) AND sonarcloud-cleanup Phase 3 completion (so the fixes actually exist in our fork first).

### 2a. NULL dereferences (flatgeobuf.c and optionlist.c)

- [ ] 2a.1 Confirm sonarcloud-cleanup Phase 3 has been executed and the fixes are in develop
- [ ] 2a.2 Extract the `postgis/flatgeobuf.c:563` NULL deref fix from develop into a minimal focused commit against `upstream/master`
- [ ] 2a.3 Extract the `liblwgeom/optionlist.c:112` missing-return fix
- [ ] 2a.4 Open focused upstream PR combining both NULL deref fixes (same root cause pattern — missing return after `lwerror`)
- [ ] 2a.5 Include regression test(s) in the PR: CUnit test that reproduces the NULL deref on upstream master and passes with the fix applied

### 2b. Memory-safety fixes from PR #8 (if extracted)

- [ ] 2b.1 Check if sonarcloud-cleanup Phase 3 extracted the three PR #8 commits (`47c20c68d`, `c133a2cfb`, `8df75747a`). If yes, include in 2a's upstream PR. If no, spawn a separate focused PR.

### 2c. Other real bugs identified during triage

- [ ] 2c.1 As sonarcloud-cleanup Phases 3/4/5 complete, review each commit for upstream applicability
- [ ] 2c.2 Group the upstream-ready commits by logical area (memory safety / thread safety / dispatch cleanups) and open focused upstream PRs per group

## Phase 3: SIMD and acceleration infrastructure (UPSTREAM_AFTER_REFACTOR, new subsystem)

**Status**: Not started. Blocked on Phase 2 (build more review capital first) AND a design discussion on postgis-devel about whether upstream wants a GPU abstraction layer at all.

- [ ] 3.1 Draft a design email to postgis-devel proposing the GPU abstraction layer (`lwgeom_gpu.h`) and SIMD dispatch table (`lwgeom_accel.h`). Include the FP64_NATIVE / FP32_ONLY precision class model as a motivating design decision.
- [ ] 3.2 Wait for upstream response. If rejected, re-scope to fork-specific and skip subsequent GPU phases.
- [ ] 3.3 If accepted in principle, extract the acceleration layer from develop in stages:
  - [ ] 3.3.1 `lwgeom_accel.h` dispatch table + scalar fallback only (no SIMD yet, no GPU yet)
  - [ ] 3.3.2 NEON backend addition
  - [ ] 3.3.3 AVX2 backend addition
  - [ ] 3.3.4 AVX-512 backend addition
  - [ ] 3.3.5 `lwgeom_gpu.h` abstraction layer (headers, enum, declarations, stubs only — no backend implementations)
- [ ] 3.4 Open one upstream PR per sub-step, each with its own tests and precision attestation

## Phase 4: ECEF/ECI core capability (UPSTREAM_AFTER_REFACTOR)

**Status**: Not started. Blocked on Phase 3.

- [ ] 4.1 Extraction spike: measure how much of the current ECEF/ECI implementation on develop depends on fork-specific infrastructure (OpenSpec, extension packaging, fork's CI matrix). Report what's actually portable.
- [ ] 4.2 Scoping conversation on postgis-devel: is ECEF/ECI the kind of capability upstream wants in core PostGIS, or should it stay in an extension?
- [ ] 4.3 Based on the scoping outcome, extract the core SRID registration, coordinate transform functions, and SQL interface as focused upstream PRs
- [ ] 4.4 Do NOT attempt to upstream the OpenSpec structure, the cleanup work, or the benchmark scripts. Those stay on the fork.

## Phase 5: Apple Metal GPU backend (UPSTREAM_AFTER_REFACTOR)

**Status**: Not started. Blocked on Phase 3 (GPU abstraction must be upstream first). Delegated to `multi-vendor-gpu-rollout/tasks.md` for GPU-specific details.

- [ ] 5.1 See `multi-vendor-gpu-rollout/tasks.md` Phase 7.1 for the upstream Metal PR task
- [ ] 5.2 Coordinate timing between this roadmap and the GPU-specific roadmap: Phase 5 here completes when the GPU-specific Phase 7 upstream PR merges

## Phase 6: Multi-vendor GPU validation (delegated)

**Status**: Not started. Blocked on Phase 5 AND user's acquisition of DGX Spark / AMD / Intel hardware. Delegated to `multi-vendor-gpu-rollout/tasks.md` Phases 2–5 which handle the validation work and Phase 7 which handles the upstream submission.

- [ ] 6.1 See `multi-vendor-gpu-rollout/tasks.md` Phases 2–7
- [ ] 6.2 Coordinate: Phase 6 here completes when all GPU vendor validations have been upstream

## Phase 7: Cross-vendor benchmark data (delegated)

**Status**: Not started. Blocked on Phase 6. Delegated to `multi-vendor-gpu-rollout/tasks.md` Phase 6.

- [ ] 7.1 See `multi-vendor-gpu-rollout/tasks.md` Phase 6 for the cross-vendor benchmark harness work
- [ ] 7.2 If the benchmark data is compelling enough, consider submitting a summary to the `postgis-devel` list as an informational post

## PR #10 triage task group

This task group answers the stalled question "what do we do with PR #10 (`feature/codebase-spec-extraction`)?" The 2026-04-11 agent investigation recommended CHERRY-PICK + CURATE. This task group captures the per-capability decisions.

**Methodology**: for each of the 12 capabilities PR #10 adds, decide:
- **ACCEPT** → spawn a focused OpenSpec change to ADD the capability to `openspec/specs/` with freshly-authored content (not wholesale commit copy). Then check off.
- **REFINE** → the spec needs editing before acceptance (e.g., scope narrower, scenarios need completion). Spawn a focused change that includes the refinements.
- **REJECT** → document the reason, check off.
- **DEFER** → not deciding today; come back later. Do not check off.

When all 12 sub-tasks have been decided (even if DEFERred), close PR #10 on GitHub with a summary comment.

### Agent recommendation summary

From the 2026-04-11 investigation (see `/Users/e/Development/GitHub/postgis/` session history):

- Phases 1–3 (foundation + core API + infrastructure) classified as **ESSENTIAL**
- Phase 4a (constructors-editors, geography-type, extension-lifecycle) classified as **VALUABLE**
- topology-model classified as **VALUABLE** (consolidates with existing `topology-fk-constraints`)
- raster-core classified as **OPTIONAL** (can defer)

### Per-capability triage sub-tasks

- [ ] T.1 Decide on `geometry-types` (15 types + 9 serialization formats) — agent: ESSENTIAL (Phase 1)
- [ ] T.2 Decide on `gserialized-format` (on-disk binary encoding) — agent: ESSENTIAL (Phase 1)
- [ ] T.3 Decide on `spatial-predicates` (13 DE-9IM predicates) — agent: ESSENTIAL (Phase 2 core ops)
- [ ] T.4 Decide on `spatial-operations` (22+ geometry functions) — agent: ESSENTIAL (Phase 2 core ops)
- [ ] T.5 Decide on `measurement-functions` (20+ distance/area/length functions) — agent: ESSENTIAL (Phase 2 core ops)
- [ ] T.6 Decide on `coordinate-transforms` (PROJ integration, SRID management) — agent: ESSENTIAL (Phase 3 infra)
- [ ] T.7 Decide on `spatial-indexing` (GiST/SP-GiST/BRIN architecture) — agent: ESSENTIAL (Phase 3 infra)
- [ ] T.8 Decide on `constructors-editors` — agent: VALUABLE (Phase 4a)
- [ ] T.9 Decide on `geography-type` — agent: VALUABLE (Phase 4a)
- [ ] T.10 Decide on `extension-lifecycle` — agent: VALUABLE (Phase 4a)
- [ ] T.11 Decide on `raster-core` — agent: OPTIONAL (can defer)
- [ ] T.12 Decide on `topology-model` — agent: VALUABLE (consolidates with existing `topology-fk-constraints`, requires a consolidation plan)
- [ ] T.13 Close PR #10 on GitHub once all 12 sub-tasks are decided (even if some are DEFERred)
- [ ] T.14 Delete the `feature/codebase-spec-extraction` branch on the fork after PR #10 is closed and any ACCEPT'd capabilities have landed via spawned focused changes

## Living document maintenance

- [ ] L.1 Update the at-a-glance phase status whenever a phase changes state
- [ ] L.2 Add inline links from this checklist to spawned upstream PRs as they are created
- [ ] L.3 Re-review the roadmap quarterly to update the baseline snapshot in `design.md` (the fork will drift from upstream over time)
- [ ] L.4 If the OpenSpec validator ever flags this change as stale, point at `design.md` Decision 5 explaining why this change is intentionally never archived
- [ ] L.5 If upstream `postgis/postgis` independently adds a capability the fork also has, reclassify the fork's version from `UPSTREAM_AFTER_REFACTOR` or `UPSTREAM_READY` to `UPSTREAM_ALREADY_HAS` and close the corresponding fork work

## Cross-references

- **Sibling living roadmaps**: `multi-vendor-gpu-rollout` (GPU-specific upstream delegation), `sonarcloud-cleanup` (feeds Phase 2 bug fixes)
- **Builds on**: PR #11's libtool cunit fix (already on develop, identified as `UPSTREAM_READY`), PR #15 `sonarcloud-cleanup` plan, PR #16 Phase 1 exclusions
- **Unblocks**: any future upstream contribution work; provides the single canonical "how do I contribute upstream" reference
- **Does not replace**: any existing roadmap. Complements and cross-references them.
