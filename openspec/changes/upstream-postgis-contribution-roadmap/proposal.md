## Why

The stated long-term goal for the `IronGateLabs/postgis` fork (from the 2026-04-10 session's opening minutes) is to **eventually contribute this work upstream to `postgis/postgis`**. The fork has accumulated substantial value since its creation: native ECEF/ECI coordinate frame support, a multi-vendor GPU abstraction layer, an Apple Metal backend implementation, infrastructure fixes, bug fixes surfaced by SonarCloud, and extensive OpenSpec-driven documentation. None of that value is visible upstream today, and there is **no documented plan for how the fork's work eventually lands there**.

Without a living roadmap:

1. **Upstream-worthy fixes get buried in fork-specific commits.** The libtool fix (`03e644b1f`) shipped as part of PR #11's topology FK workaround removal is a real upstream bug that would fix mingw CI on `postgis/postgis` — but it's not tracked as "upstream-worthy" anywhere. The three build-env PRs already opened (`postgis/postgis-build-env#35/#36/#37`) are similarly orphaned from the main planning.
2. **Fork-specific work gets confused with upstream-worthy work.** The `remove-topology-fk-workaround` change was entirely fork-specific (the workaround only ever existed on the fork). Someone cataloging "what to contribute upstream" needs a way to distinguish.
3. **Planning PRs accumulate without an end-game.** We currently have 4 living-roadmap OpenSpec changes (`apple-metal-gpu-backend`, `multi-vendor-gpu-rollout`, `sonarcloud-cleanup`, and earlier ECEF/ECI archives). Each represents valuable work but none explicitly sequences "here's when this becomes an upstream PR". The upstream story floats in human memory.
4. **PR #10 is stuck in limbo.** `feature/codebase-spec-extraction` (PR #10) adds 12 foundational capability specs via reverse-engineering. The 2026-04-11 agent investigation recommended CHERRY-PICK + CURATE but no decision is recorded anywhere. Without a tracking mechanism, the decision keeps getting deferred.

This change fills those gaps by creating a **living roadmap openspec change** (same pattern as `multi-vendor-gpu-rollout` and `sonarcloud-cleanup`) that tracks the upstream contribution plan end-to-end: categorization of fork changes, phase ordering, per-phase templates, and explicit decision-tracking tasks for items that are currently in limbo (PR #10 being the immediate example).

## What Changes

This change is a **planning artifact only** — no source code modifications, no behavior changes. It creates a new capability spec and a living roadmap that subsequent focused implementation PRs will consume.

Concretely:

- Create a new capability `upstream-contribution-policy` defining:
  - The **four-way categorization** of every commit/change on the fork: `UPSTREAM_READY` (ship as-is), `UPSTREAM_AFTER_REFACTOR` (needs scoping or cleanup before upstream PR), `FORK_SPECIFIC` (will never go upstream — e.g., OpenSpec itself, fork's private CI config), `UPSTREAM_ALREADY_HAS` (redundant with what's already landed)
  - The **per-category action policy** (open upstream PR / refactor first / leave on fork / close as redundant)
  - The **phase ordering** for how work gets upstream (infra fixes → bug fixes → SIMD / acceleration infra → feature capabilities → multi-vendor GPU → benchmark data)
  - The **per-phase template** for what each upstream PR should include (reviewer-facing commit message, cross-vendor CI evidence, precision contract attestation, etc.)
- Capture a **baseline snapshot** of the fork's current state as of 2026-04-11: what's on develop, what's in-flight on feature branches, what's in archive, what's orphaned
- Establish the **living-roadmap convention** for this change (never archived; phases checked off as they complete; links to focused implementation PRs added inline)
- Enumerate the **immediate decision-tracking tasks** that need answers before specific phases can start:
  - PR #10 `codebase-spec-extraction` — triage the 12 foundational capability specs (CHERRY-PICK / MERGE / CLOSE per spec)
  - PR #13 `apple-metal-gpu-backend` (rebased) — merge timing and post-merge follow-ups
  - The sonarcloud-cleanup plan's Phases 2–5 — which of those phase PRs also need an upstream companion
- Cross-reference the existing `multi-vendor-gpu-rollout` change (which already defines its own Phase 7 for upstream contribution) and document how the two roadmaps coordinate — `multi-vendor-gpu-rollout` handles the GPU-specific upstream story, `upstream-postgis-contribution-roadmap` handles everything else

This change does not duplicate or supersede the GPU-specific plan. It complements it.

## Capabilities

### New Capabilities

- `upstream-contribution-policy`: A living-roadmap capability defining how fork changes are classified, ordered, and shipped to upstream `postgis/postgis`. Establishes the categorization model (UPSTREAM_READY / UPSTREAM_AFTER_REFACTOR / FORK_SPECIFIC / UPSTREAM_ALREADY_HAS), per-phase ordering, per-phase template for upstream PRs, and baseline snapshot of the fork's 2026-04-11 state. Serves as the single reviewable contract surface for all upstream contribution work.

### Modified Capabilities

None directly. Subsequent implementation PRs spawned from this roadmap may modify existing capabilities (e.g., when PR #10's specs get merged), but those modifications are out of scope for this planning artifact.

## Impact

- **Code**: zero. This change is documentation only.
- **Future work unblocked**:
  - Immediate: PR #10 gets an explicit triage decision instead of floating
  - Short-term: every future PR can self-classify against the four-way model in its own description, making "is this upstream-worthy" reviewable
  - Long-term: when you actually want to open the first upstream PR to `postgis/postgis`, the roadmap tells you exactly which focused change to start with (likely the libtool fix since it's a genuine upstream bug fix)
- **Cross-references to existing changes**: this change references and coordinates with `multi-vendor-gpu-rollout` (GPU-specific Phase 7 upstream) and `sonarcloud-cleanup` (Phase 3 memory-safety fixes that are upstream-worthy). No existing change is modified in-place; the coordination is declarative.
- **No new runtime behavior, no new dependencies, no new tests.**

## Open Questions

1. **Should upstream contribution happen PR-by-PR or as one mega-contribution?** — Recommendation: PR-by-PR. Upstream maintainers have limited bandwidth and review individual focused changes faster than they review sprawling feature branches. The phased ordering in this roadmap is designed around small atomic PRs.
2. **Does upstream PostGIS actually want any of this?** — Unknown until asked. The roadmap includes an early "announcement and scoping" conversation on the `postgis-devel` mailing list before the first upstream PR is submitted. Phase 0 of the roadmap.
3. **What happens to fork-specific capabilities (OpenSpec itself) when going upstream?** — They stay on the fork. Upstream doesn't need to adopt OpenSpec to benefit from the code-level fixes this fork has generated. The OpenSpec-driven workflow is an internal productivity choice, not part of the contribution.
4. **Does the roadmap need per-PR dependencies modeled as a DAG?** — Currently tracked as linear phases with notes about cross-phase dependencies. If the phase count grows beyond ~10 or dependencies become intricate, a more formal DAG model might help. For now, linear tracking is sufficient.
