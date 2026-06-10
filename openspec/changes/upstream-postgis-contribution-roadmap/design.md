## Context

`IronGateLabs/postgis` is a fork of `postgis/postgis` (upstream) that has accumulated substantial divergence since its creation. The fork carries:

- **Native ECEF/ECI coordinate frame support** — 12 archived OpenSpec changes from February 2026 implementing the foundation, plus 12 capability specs in `openspec/specs/`
- **GPU abstraction layer** (`liblwgeom/lwgeom_gpu.h`, `liblwgeom/lwgeom_accel.h`) supporting CUDA, ROCm, oneAPI, and Metal backends, with SIMD acceleration on NEON, AVX2, AVX-512, and scalar paths
- **Apple Metal GPU backend implementation** (PR #13) with precision-fix and FP64_NATIVE/FP32_ONLY classification model
- **SonarCloud cleanup work** (PR #15 merged + PR #16 in flight) addressing 130 of 152 blockers via configuration changes and the rest as phased code fixes
- **Topology FK workaround removal** (PR #11 merged) — a fork-specific cleanup tied to upstream PG commit `34a3078629`
- **Infrastructure improvements** — libtool cunit fix, build-env PRs #35/#36/#37 upstream to `postgis-build-env`
- **OpenSpec-driven planning infrastructure** — this is fork-specific productivity tooling, not code intended for upstream

None of this is visible in `postgis/postgis:master`. The user has stated since the start of the 2026-04-10 session that the long-term goal is eventual upstream contribution. Without a living roadmap, "eventual" has no structure.

This design establishes that structure. It follows the same living-roadmap pattern as `multi-vendor-gpu-rollout` and `sonarcloud-cleanup`: a single OpenSpec change that stays in `openspec/changes/` indefinitely, with phases checked off in `tasks.md` as they complete, and cross-references to focused implementation PRs added inline.

## Goals / Non-Goals

**Goals:**

- Provide a single reviewable document for "how does the fork's work eventually reach `postgis/postgis`"
- Classify every existing change on the fork into one of four categories (UPSTREAM_READY, UPSTREAM_AFTER_REFACTOR, FORK_SPECIFIC, UPSTREAM_ALREADY_HAS) so future triage is mechanical
- Define the ordering of upstream PRs (which go first, which depend on which)
- Define the per-phase template for what each upstream PR should include
- Capture a baseline snapshot of the fork's 2026-04-11 state so progress can be measured
- Track immediate decisions that are currently blocking work (PR #10 triage being the main one)
- Coordinate with existing roadmaps (`multi-vendor-gpu-rollout`, `sonarcloud-cleanup`) without duplicating their content

**Non-Goals:**

- Opening any upstream PR in this change. That's what the phase-spawned focused changes do.
- Modifying any existing OpenSpec change. This change references them; it doesn't rewrite them.
- Deciding on PR #10 in this change. PR #10 triage is a task within this roadmap that gets answered when we act on it.
- Committing to any specific upstream-contribution calendar. This is opportunistic work, not a release plan.
- Promising that upstream will accept the contributions. That's a conversation for `postgis-devel`, not an OpenSpec change.
- Replacing the `multi-vendor-gpu-rollout` change's Phase 7 (upstream) — this roadmap complements it. GPU-specific upstream work lives in `multi-vendor-gpu-rollout`; everything else lives here.

## Decisions

### Decision 1: Four-way categorization of fork changes

**Choice:** Every commit, PR, and OpenSpec change on the fork SHALL be classifiable into exactly one of:

- **`UPSTREAM_READY`** — The change is valuable to upstream as-is, with no refactoring or scope changes needed. Example: the libtool cunit Makefile fix (`03e644b1f`). Ship directly as a focused upstream PR.
- **`UPSTREAM_AFTER_REFACTOR`** — The change addresses a real upstream problem but is scoped or packaged in a fork-specific way. Must be extracted, rescoped, or rewritten before upstream review. Example: the 3 memory-safety fixes identified by the 2026-04-11 agent investigation are currently commits inside a larger `sonarcloud-cleanup` phase — they need to be extracted as a standalone upstream PR with their own test coverage.
- **`FORK_SPECIFIC`** — The change addresses a fork-internal concern that upstream will never care about. Example: the OpenSpec workflow files, the `.github/workflows/sonar.yml` workflow, the `remove-topology-fk-workaround` change (which only makes sense in a fork that had the workaround to begin with). Stays on the fork forever.
- **`UPSTREAM_ALREADY_HAS`** — The change duplicates something upstream has already implemented. Example: any February 2026 ECEF/ECI work that matches what postgis/postgis subsequently added (to be determined — currently we have no overlap, but need to check at upstream-PR time).

**Rationale:** Without explicit categorization, every triage conversation starts from zero. A four-way label lets any contributor glance at a commit and know whether it's a candidate for upstream.

**Alternatives considered:**

- **Three-way (ready / refactor / fork-only).** Rejected because `UPSTREAM_ALREADY_HAS` is a real category — discovering duplication at contribution time is wasted effort, and pre-classifying it prevents wasted work.
- **Severity-based classification (bug / feature / cleanup).** Rejected because severity doesn't predict upstream-readiness. A small bug fix can be upstream-ready; a big feature might need extensive refactoring.
- **GitHub labels instead of OpenSpec categorization.** Rejected because labels are orthogonal to the structured planning work OpenSpec does. The categorization belongs alongside the phase planning.

### Decision 2: Phase ordering — infrastructure first, then fixes, then features

**Choice:** Upstream contribution proceeds in this order:

0. **`announcement-and-scoping`** — Reach out to `postgis-devel` mailing list summarizing what the fork has done, what the user proposes to contribute, what shape the upstream PRs would take. Get rough agreement on structure before submitting anything. *Blocking all subsequent phases.*
1. **`infrastructure-fixes`** — Small focused PRs that upstream's CI would benefit from regardless of whether they take anything else. Examples: libtool cunit fix, build-env docbook fix, build-env multiarch preload. Low risk, low reviewer effort, high reputation-building value.
2. **`real-bug-fixes`** — The 22 real SonarCloud-identified issues that the 2026-04-11 investigation surfaced. Includes the 2 NULL deref fixes in `flatgeobuf.c` and `optionlist.c`, the gserialized_estimate negative index, etc. Some of these are genuine security-adjacent bugs that upstream benefits from immediately.
3. **`simd-and-acceleration-infrastructure`** — The GPU abstraction layer (`lwgeom_gpu.h`), the SIMD dispatch table (`lwgeom_accel.h`), the scalar/NEON/AVX2 backends. This is a new subsystem, likely requires a design discussion with upstream before implementation PR.
4. **`ecef-eci-core`** — The native ECEF/ECI coordinate frame support. Requires careful scoping — only the core additions, not the benchmark scripts or the fork-specific configuration. Likely needs multiple focused PRs (core types, transforms, SRID registration, SQL interface).
5. **`metal-gpu-backend`** — Apple Metal backend (from PR #13 after it merges). Has the float32 precision contract; requires clear documentation of the tradeoff. May or may not be accepted upstream depending on whether upstream cares about Apple Silicon performance. Includes the SIMD ERA precompute follow-up.
6. **`multi-vendor-gpu-validation`** — CUDA/ROCm/oneAPI backends once the user has access to the corresponding hardware. Per `multi-vendor-gpu-rollout` Phases 2–5.
7. **`cross-vendor-benchmark-data`** — The apples-to-apples performance comparison data that validates the multi-vendor story. Per `multi-vendor-gpu-rollout` Phase 6.

**Rationale for ordering:**

- Phase 0 (mailing-list scoping) first because **submitting unsolicited large PRs to a mature open-source project rarely goes well**. A 20-minute conversation saves hours of rework later.
- Infrastructure fixes before bug fixes because they're the smallest, safest starting point. Getting "our contributor" successfully merged into upstream once gives reputation capital for the harder PRs later.
- Bug fixes before features because upstream is more likely to accept "here's a bug you have, here's the fix" than "here's a new subsystem, please adopt it".
- SIMD infrastructure before ECEF/ECI because ECEF/ECI uses the acceleration layer. Merging ECEF/ECI without the accel layer would require either duplicating logic or deferring upstream-side performance work.
- ECEF/ECI before Metal because Metal is one backend consuming the GPU abstraction; the GPU abstraction + SIMD + ECEF/ECI is the foundation Metal sits on.
- Multi-vendor GPU after Metal because it depends on the same foundation plus additional hardware access.
- Benchmark data last because it requires all the backends to be validated first.

**Alternatives considered:**

- **Big-bang contribution: one giant PR with everything.** Rejected. Upstream maintainers will not review it. Review fatigue alone kills large PRs.
- **Bug fixes first, skip infrastructure fixes.** Rejected. Infrastructure fixes warm up the review relationship cheaply — if they're rejected, the bug fixes wouldn't have been accepted either, so we save the bigger effort.
- **Feature-first ordering (ECEF/ECI first, then everything else).** Rejected. Submitting a large feature PR as the first upstream contribution is high-risk, and ECEF/ECI depends on infrastructure that doesn't exist upstream yet.

### Decision 3: Per-phase template for upstream PRs

**Choice:** Every upstream PR spawned from this roadmap SHALL include:

1. **Classification in the PR description** — which category from Decision 1 the change falls in, and which phase from Decision 2 it belongs to
2. **Fork-local verification evidence** — CI runs on our fork showing the change works (we already have this from the session's work)
3. **Isolation from fork-specific infrastructure** — the upstream PR's diff SHALL NOT touch `openspec/**`, `.github/workflows/sonar.yml`, or any other FORK_SPECIFIC file. Verify via `git diff upstream/master...HEAD` before opening the PR.
4. **Scoped commit message** — the first commit in the PR SHALL be a standalone, upstream-appropriate message. No references to internal OpenSpec changes, no "per the cleanup plan", no links to fork-internal issues. Upstream reviewers get a self-contained narrative.
5. **Precision and safety attestation** (for GPU/precision-sensitive changes) — explicit statement of what precision contract the change provides, what hardware it was verified on, what failure modes exist
6. **Regression test coverage** — a new test (CUnit, regress SQL, or bench) demonstrating the fix / feature works and catching regressions. For bug fixes, the test SHALL reproduce the bug on base and pass with the fix applied.
7. **A commit reference to the originating OpenSpec phase task** — in the commit body (not title), link back to `openspec/changes/upstream-postgis-contribution-roadmap/tasks.md` so future maintainers can trace the contribution

**Rationale:** Templating makes each upstream PR predictable. Reviewers know what to expect. Missing elements are obvious. Upstream reputation builds faster when every contribution is structured the same way.

### Decision 4: PR #10 triage as a task group within this roadmap (not a separate change)

**Choice:** PR #10 (`feature/codebase-spec-extraction`) adds 12 foundational capability specs via reverse-engineering. The 2026-04-11 agent investigation recommended CHERRY-PICK + CURATE. Rather than create a separate OpenSpec change to track the PR #10 decision, this roadmap's `tasks.md` includes an explicit **"PR #10 triage"** task group with one sub-task per capability: decide whether to accept, refine, or skip each of the 12 specs.

As decisions are made, each sub-task gets checked off and (if accepted) a focused OpenSpec change is spawned to add the capability to develop via `ADDED Requirements` delta. If an entire capability is rejected, it gets marked `[x] REJECTED — reason: ...`.

**Rationale:**

- PR #10 is a one-time decision, not a long-running initiative. A dedicated OpenSpec change would be heavy for what is essentially a triage.
- Embedding the triage in the upstream roadmap is natural because PR #10's specs are exactly the foundational layer that future upstream contributions reference. If we take any of them, they feed Phase 3 / 4 of the upstream roadmap.
- Keeping the decision tracked in one place (this roadmap's tasks.md) avoids scattering.

**Alternatives considered:**

- **Separate OpenSpec change `pr10-spec-extraction-decision`.** Rejected as too heavyweight for a triage. OpenSpec changes carry a proposal/design/specs/tasks structure that's overkill for "decide yes/no on 12 files".
- **GitHub issue tracking the PR #10 decision.** Rejected because GitHub issues aren't reviewable alongside the structured openspec planning. The user prefers OpenSpec for planning.
- **Close PR #10 outright.** Rejected based on agent findings — PR #10 has zero overlap with current develop specs and provides the foundational layer upstream will want to see. Discarding it wastes the work.

### Decision 5: Living roadmap, never archived

**Choice:** Same convention as `multi-vendor-gpu-rollout` and `sonarcloud-cleanup`. This change stays in `openspec/changes/upstream-postgis-contribution-roadmap/` indefinitely. Phases get checked off in `tasks.md` as they complete; links to spawned focused PRs are added inline.

**Rationale:** Upstream contribution is an open-ended process that spans months or years. A conventional archive-on-completion workflow would lose the "where are we on the upstream story" continuity.

### Decision 6: Coordination with `multi-vendor-gpu-rollout` Phase 7

**Choice:** `multi-vendor-gpu-rollout` already has a Phase 7 entry for upstream contribution of the GPU work. This roadmap does NOT duplicate that — it references `multi-vendor-gpu-rollout` Phase 7 as the authoritative source for GPU-specific upstream coordination. This roadmap handles the non-GPU work (infrastructure, bug fixes, ECEF/ECI, SIMD) and cross-references GPU work.

In `tasks.md`, the GPU-related phases (5, 6, 7) contain pointers like "see `multi-vendor-gpu-rollout/tasks.md` Phase 7 for the actual GPU upstream work; this entry is just a dependency marker".

**Rationale:** Avoids duplication. `multi-vendor-gpu-rollout` already has the detailed GPU rollout plan — no reason to restate it here. But this roadmap needs to sequence GPU work relative to non-GPU work, so it references the GPU plan as a dependency.

## Baseline snapshot (2026-04-11)

This section captures what's on the fork today so progress can be measured against a reference point.

### Fork-vs-upstream delta

| Dimension | Fork (`IronGateLabs/postgis`) | Upstream (`postgis/postgis`) |
|---|---|---|
| ECEF/ECI native support | ✅ Full — 12 capability specs + `lwgeom_eci.*` + extension + CUnit + regress | ❌ Not present |
| GPU abstraction layer | ✅ Full — `lwgeom_gpu.h` + CUDA/ROCm/oneAPI source stubs | ❌ Not present |
| Apple Metal backend | ✅ Complete implementation on PR #13 | ❌ Not present |
| SIMD acceleration | ✅ NEON / AVX2 / AVX-512 paths | ⚠️ Partial (scalar baseline only in some code paths) |
| SonarCloud quality gate | ✅ CI-driven deep analysis configured | ❌ Not configured |
| OpenSpec workflow | ✅ Full `openspec/**` directory with 14 archived changes + 4+ in-flight | ❌ Not present |
| libtool cunit fix | ✅ Landed via PR #11's `03e644b1f` | ❌ Has the bug — flaky mingw CI |
| Build-env fixes | ⚠️ 3 upstream PRs filed (#35/#36/#37) awaiting review | ❌ Has the bugs |
| Topology FK semantics | ✅ Match upstream (after PR #11's revert of the workaround) | ✅ Unchanged — we're now aligned |

### Change categorization snapshot

| Change / PR | Category | Rationale |
|---|---|---|
| PR #11 commits — revert workaround (b42524d05, 664b8d5ca) | `FORK_SPECIFIC` | Workaround only existed on fork; revert only makes sense on fork |
| PR #11 commit — CI re-enable (f541b1810) | `FORK_SPECIFIC` | `.github/workflows/ci.yml` fork-specific matrix |
| PR #11 commit — OpenSpec track (aa322a2c7) | `FORK_SPECIFIC` | OpenSpec never goes upstream |
| **PR #11 commit — libtool cunit fix (03e644b1f)** | **`UPSTREAM_READY`** | **Real upstream bug; same files in upstream master have the same bug** |
| PR #11 commit — sonar continue-on-error add/remove (2cf4d3c74, 657f77186) | `FORK_SPECIFIC` | Fork has no `sonar.yml` workflow upstream |
| PR #11 commit — tasks.md update (a9debe167) | `FORK_SPECIFIC` | OpenSpec bookkeeping |
| PR #13 — Apple Metal backend | `UPSTREAM_AFTER_REFACTOR` | Valuable to upstream but needs the GPU abstraction + SIMD infrastructure as prerequisites |
| PR #14 — GPU roadmap (3 planning changes) | `FORK_SPECIFIC` | Living roadmaps, OpenSpec-specific |
| PR #15 — SonarCloud cleanup plan | `FORK_SPECIFIC` | OpenSpec planning; SonarCloud only on fork |
| PR #16 — SonarCloud Phase 1 exclusions | `FORK_SPECIFIC` | SonarCloud-specific |
| PR #10 — codebase-spec-extraction | **TBD** | Pending triage — some specs are `UPSTREAM_READY` (foundational docs), others are `FORK_SPECIFIC` (would require upstream to adopt OpenSpec) |
| ECEF/ECI archived changes | `UPSTREAM_AFTER_REFACTOR` | Core value but heavily entangled with fork-specific infrastructure; needs careful scope extraction |
| Archived `remove-topology-fk-workaround` | `FORK_SPECIFIC` | The workaround never existed upstream |
| `sonarcloud-cleanup` Phase 3 memory-safety commits (when executed) | `UPSTREAM_READY` | Real bugs, upstream has the same code |
| `sonarcloud-cleanup` Phase 4 strtok sweep commits (when executed) | `UPSTREAM_READY` | Real thread-safety issue |
| `sonarcloud-cleanup` Phase 5 side-effect-in-operator commits (when executed) | `UPSTREAM_READY` | Real code quality issues upstream would also benefit from |
| Build-env PRs #35/#36/#37 (already filed upstream) | `UPSTREAM_READY` | Already in progress upstream |

The `UPSTREAM_READY` category already contains enough identified work to warrant the first few upstream PRs — we don't have to do any more investigation before starting Phase 1.

## Risks / Trade-offs

- **[Risk] Upstream maintainers don't engage on the `postgis-devel` scoping email.** Mitigation: if no response within 2 weeks, proceed with Phase 1 (the libtool cunit fix) as a trial balloon — it's a real bug fix, low review burden, and the reviewer's response tells us whether the broader conversation is welcome.

- **[Risk] Upstream adopts a different design for something we've built.** E.g., `postgis/postgis` could independently add a GPU abstraction that has a different API shape than ours. Mitigation: keep an eye on upstream's development; rebase/refactor our work to match upstream's shape if it's reasonable; keep fork-specific if not.

- **[Risk] `UPSTREAM_AFTER_REFACTOR` changes turn out to be bigger refactors than expected.** E.g., extracting just the ECEF/ECI core without the OpenSpec structure might reveal tight coupling we didn't see. Mitigation: each Phase 3+ focused upstream PR starts with a "extraction spike" task to measure refactor cost before committing to the full PR.

- **[Risk] Fork and upstream diverge over time, making rebases harder.** Mitigation: the existing `upstream-master-sync-2026-04` archived change shows we already periodically sync upstream into develop. Continue that cadence.

- **[Trade-off] Living roadmap never archives.** Accepted, documented in Decision 5. Same convention as sibling roadmaps.

- **[Trade-off] OpenSpec duplication between this roadmap and `multi-vendor-gpu-rollout`'s Phase 7.** Accepted — this roadmap cross-references rather than duplicates (see Decision 6).

- **[Trade-off] No fixed timeline.** Accepted — upstream contribution cadence depends on reviewer availability, user's hardware acquisition schedule, and upstream's own priorities. A timeline would be fiction.

## Migration Plan

This change has no migration (it is documentation). Each upstream PR spawned from this roadmap has its own migration plan captured in the per-PR task group.

**Rollback**: if this change itself needs to be withdrawn, revert its creation commit. No runtime impact; it's pure documentation.

## Open Questions

1. **Should Phase 0 (announcement and scoping) actually be done before any other upstream contact?** — Recommendation: yes. Upstream projects appreciate heads-up communication. The alternative ("just open a PR and see what happens") works for trivial fixes but scales poorly for features like GPU abstraction.

2. **Is there a way to batch multiple small `UPSTREAM_READY` fixes into one PR?** — Probably yes. E.g., the libtool cunit fix + any other trivial infrastructure fixes could go as one "mechanical mingw CI hygiene" PR. Decide per-phase when we reach that point.

3. **Should the roadmap include reputation-building signals?** — E.g., first comment on an upstream PR as a code review, first bug triage, first helpful answer on the mailing list. These build contributor reputation before we ask for our own contributions to be merged. Out of scope for this change but a possible Phase 0.5.

4. **Does `postgis/postgis` have a CONTRIBUTING.md with explicit PR conventions we should follow?** — Check at Phase 0 time and update the per-phase template (Decision 3) to match upstream conventions if they exist.

5. **What's the cadence for updating this roadmap?** — Living document, so whenever a phase status changes or a task is checked off. No fixed cadence.
