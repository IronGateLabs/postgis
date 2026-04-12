## Phase tracking checklist

This is a **living checklist**. Items get checked off as phases complete. Links to focused implementation PRs are added inline. This change does NOT itself archive — it stays in `openspec/changes/sonarcloud-cleanup/` as the canonical cleanup roadmap.

**Baseline (2026-04-11)**: 8,697 code smells, 161 bugs, 3 vulnerabilities, 152 blockers, 532 security hotspots, 62,399 min technical debt.

## Phase 0: SonarCloud web UI — disable Automatic Analysis

**Status**: COMPLETED 2026-04-11 (performed during PR #11 session; Automatic Analysis disabled in SC web UI, `continue-on-error` workaround reverted in commit `657f77186`).

- [x] 0.1 Logged in to SonarCloud with the `montge` account
- [x] 0.2 Navigated to project configuration
- [x] 0.3 Toggled off **Automatic Analysis** under Administration → Analysis Method
- [x] 0.4 Verified the legacy `SonarCloud Code Analysis` check no longer blocks; CI-driven `SonarCloud Scan` workflow is the authoritative check
- [x] 0.5 Verified `SonarCloud Scan` job in `sonar.yml` runs cleanly without the "Automatic Analysis is enabled" error
- [x] 0.6 Removed `continue-on-error: true` from `.github/workflows/sonar.yml` and the workaround comment block (commit `657f77186` on `feature/fix-topology-crash-latest`, shipped via PR #11)

## Phase 1: Path exclusions in sonar-project.properties

**Status**: COMPLETED 2026-04-12 via [PR #16](https://github.com/IronGateLabs/postgis/pull/16) (merged to develop).

- [x] 1.1 Opened focused PR against develop titled "SonarCloud Phase 1: exclude test fixtures and vendored code" (branch `feature/sonarcloud-phase1-exclusions`) → [PR #16](https://github.com/IronGateLabs/postgis/pull/16)
- [x] 1.2 Updated `sonar-project.properties` `sonar.exclusions` value to include TEST FIXTURE paths: `topology/test/**`, `raster/test/**`, `extras/ogc_test_suite/**`. `doc/html/images/styles.c` is already covered by the existing `doc/**` exclusion. All previous exclusions (`regress/**`, `**/cunit/**`, `**/test/**`, `fuzzers/**`, `extensions/**/sql/**`, `ci/**`, `deps/**`, `doc/**`) are preserved.
- [x] 1.3 Added VENDORED file exclusions: `liblwgeom/lookup3.c`, `liblwgeom/lwin_wkt_lex.c`, `liblwgeom/lwin_wkt_parse.c`, `loader/dbfopen.c`, `loader/getopt.c`
- [x] 1.4 CI verified on PR #16: `SonarCloud Scan` workflow passed cleanly, full CI matrix green (pg14-18, asan/usan, mingw, garden, CodeQL, CodeRabbit). Post-merge metric query deferred to 1.6.
- [x] 1.5 Merged PR #16 to develop on 2026-04-12 (`--merge` mode, branch deleted)
- [ ] 1.6 Query SonarCloud post-merge for new metric snapshot and record here: blocker count, bug count, code smell count, technical debt. Compare to 2026-04-11 baseline (8,697 smells, 161 bugs, 152 blockers, 532 hotspots, 62,399 min debt).

## Phase 2: False-positive NOSONAR markers

**Status**: Not started. Blocked on Phase 1 (so reviewers can see the reduced noise).

### 2a. The 3 hard-coded password "vulnerabilities"

- [ ] 2a.1 Open focused PR against develop titled "SonarCloud: mark XPath template false positives in lwgeom_in_gml.c"
- [ ] 2a.2 Add NOSONAR markers at `postgis/lwgeom_in_gml.c:267`, `:269`, `:278` with reason `XPath expression template, not a credential`
- [ ] 2a.3 Verify the 3 vulnerabilities disappear from the SonarCloud dashboard on the next analysis run
- [ ] 2a.4 Merge the PR

### 2b. Other false positives found during Phase 3 investigation

- [ ] 2b.1 The `postgis/gserialized_estimate_support.h:91` `c:S836` flag (FALSE POSITIVE per agent investigation — `size` is initialized in the loop body before use) — add NOSONAR with reason `size is assigned at line 90 in every loop iteration before use at line 91, SonarCloud data-flow is confused`
- [ ] 2b.2 Any additional false positives discovered during Phase 3 investigation of the remaining 41 critical/major bugs

## Phase 3: Memory-safety fixes

**Status**: Not started. Blocked on Phase 1.

### 3a. Confirmed NULL dereferences (HIGH priority, security-adjacent)

- [ ] 3a.1 Open focused PR against develop titled "Fix NULL pointer dereferences flagged by SonarCloud S2259"
- [ ] 3a.2 Fix `postgis/flatgeobuf.c:563`: move NULL check before `ctx->ctx` dereference and capture `flatgeobuf_agg_ctx_init` return value
- [ ] 3a.3 Fix `liblwgeom/optionlist.c:112`: add `return;` after `lwerror()` call so subsequent `*val = '\0';` does not execute when `val` is NULL
- [ ] 3a.4 Add CUnit regression tests demonstrating the pre-fix crash (crafted input that sets `ctx = NULL` for flatgeobuf; crafted option string without separator for optionlist)
- [ ] 3a.5 Run ASan build locally to verify no NULL derefs fire in the fixed code paths
- [ ] 3a.6 Merge the PR

### 3b. Remaining S2259 null-deref instances (6 more)

- [ ] 3b.1 Query SonarCloud for the full list of `c:S2259` issues (7 total, 2 covered by 3a above)
- [ ] 3b.2 Investigate each: verdict real-bug vs false-positive, fix the real ones, NOSONAR the false positives
- [ ] 3b.3 Open a second focused PR bundling the 3b fixes if there are ≥3 real bugs; otherwise add to 3a's scope

### 3c. Potential out-of-bounds issues (S3519 in non-vendored code)

- [ ] 3c.1 Investigate `postgis/gserialized_estimate.c:1012` and `:1035` — "Access `field.value[-1]`" — determine if the negative index access is real or the analyzer is misreading array arithmetic
- [ ] 3c.2 Investigate `raster/rt_core/rt_wkb.c:543` — `memcpy` OOB array element — determine if the memcpy length can actually exceed the source or destination bound
- [ ] 3c.3 Fix the real issues; NOSONAR the false positives
- [ ] 3c.4 Include in the same PR as 3a/3b if the diff is small, or open a separate focused PR

### 3d. Uninitialized value reads (S836)

- [ ] 3d.1 Query SonarCloud for the full list of `c:S836` issues (4 total, 1 confirmed false positive per Phase 2b)
- [ ] 3d.2 Investigate each remaining instance
- [ ] 3d.3 Fix the real ones; NOSONAR the false positives

## Phase 4: `strtok` → `strtok_r` thread-safety sweep

**Status**: Not started. Blocked on Phase 1 (for dashboard clarity). Can proceed in parallel with Phase 3.

- [ ] 4.1 Open focused PR against develop titled "Replace non-reentrant strtok with strtok_r throughout accel/loader/raster"
- [ ] 4.2 Update the 8 call sites mechanically:
  - `liblwgeom/optionlist.c:95`
  - `liblwgeom/optionlist.c:149`
  - `postgis/lwgeom_geos.c:1029`
  - `postgis/lwgeom_geos.c:1295`
  - `raster/loader/raster2pgsql.c:243`
  - `raster/loader/raster2pgsql.c:266`
  - `raster/rt_pg/rtpg_internal.c:176`
  - `raster/rt_pg/rtpg_internal.c:199`
- [ ] 4.3 For each site, add a `char *saveptr;` local and change `strtok(s, sep)` to `strtok_r(s, sep, &saveptr)` and subsequent `strtok(NULL, sep)` to `strtok_r(NULL, sep, &saveptr)`
- [ ] 4.4 Verify each saveptr is unique per logical invocation (not shared across unrelated tokenizations)
- [ ] 4.5 Run existing regression tests to confirm no behavior changes
- [ ] 4.6 Merge the PR

## Phase 5: Side-effect-in-logical-operator cleanups

**Status**: Not started. Can proceed in parallel with Phase 3 and Phase 4 after Phase 1.

- [ ] 5.1 Open focused PR against develop titled "SonarCloud S912: extract side effects from logical operators"
- [ ] 5.2 Fix the 7 known sites:
  - `postgis/lwgeom_in_kml.c:114`
  - `postgis/lwgeom_in_marc21.c:69`
  - `raster/loader/raster2pgsql.c:181`
  - `raster/loader/raster2pgsql.c:641`
  - `raster/rt_pg/rtpg_internal.c:83`
  - (plus 2 more TBD from full SonarCloud query)
- [ ] 5.3 For each site, move the side-effecting expression (typically an assignment or `getc()`/`fread()`) out of the short-circuit operand and into a preceding statement
- [ ] 5.4 Bundle with the `c:S3491` useless `&*` fixes at `raster/rt_core/rt_pixel.c:440-441` and the `c:S3584` potential memory leak at `raster/loader/raster2pgsql.c:863` (small, mechanical)
- [ ] 5.5 Run existing regression tests
- [ ] 5.6 Merge the PR

## Phase 6: Long-tail cleanup (rolling)

**Status**: Not started, no fixed end date. Runs alongside normal feature work.

- [ ] 6.1 Establish the convention: when touching a file for any reason, fix any SonarCloud issues in that file as part of the same PR if the fix is mechanical and low-risk
- [ ] 6.2 Pick rules for periodic targeted cleanup sprints:
  - `c:S1659` (multi-var declarations) — mechanical, 1,005 instances, can be automated with a script
  - `c:S1116` (empty statements `;` alone) — mechanical, 676 instances
  - `c:S5955` (local var in for-init) — mechanical, 191 instances
  - `c:S1854` (dead stores) — requires case-by-case review (some are intentional defensive init), 789 instances
  - `c:S125` (commented-out code) — requires case-by-case review (some are historical references), 614 instances
- [ ] 6.3 Skip rules that are mostly test-fixture noise (already excluded by Phase 1): `plsql:S1192`, `plsql:LiteralsNonPrintableCharactersCheck`, `plsql:OrderByExplicitAscCheck`
- [ ] 6.4 Defer rules with high case-by-case review cost: `c:S134` (nesting depth), `c:S3776` (cognitive complexity) — these are refactoring decisions, not mechanical sweeps
- [ ] 6.5 Track the remaining code-smell count at each major release and aim for "does not regrow" as the primary success metric

## Python helper script fixes (small, orthogonal)

- [ ] P.1 Investigate `raster/scripts/python/rtreader.py:116` — `python:S5644` "Object has no __getitem__" — may be a real bug in an unused helper
- [ ] P.2 Investigate `raster/scripts/python/pixval.py:76` — `python:S930` "exit() called with too many args"
- [ ] P.3 If scripts are still in use, fix; if they're stale and unmaintained, consider removing them

## Living document maintenance

- [ ] L.1 Update the phase tracking table whenever a phase status changes
- [ ] L.2 Add inline links from this checklist to spawned focused PRs as they are created
- [ ] L.3 Re-query SonarCloud metrics and update the baseline snapshot in design.md after Phase 1 and Phase 3
- [ ] L.4 Review the cleanup plan quarterly; remove completed items from the active list and promote Phase 6 sub-tasks as they become concrete
- [ ] L.5 If the OpenSpec validator ever flags this change as stale or requiring archive, point at design.md Decision 4 explaining why this change is intentionally never archived

## Cross-references

- **Related**: `multi-vendor-gpu-rollout` change — same living-roadmap pattern, also never archived
- **Builds on**: the 2026-04-11 agent-investigation findings summarized in design.md Findings snapshot section
- **Unblocks**: a clean SonarCloud dashboard that future code-quality work can actually use for triage
- **Depends on**: user action in the SonarCloud web UI (Phase 0) for the CI-driven deep analysis to run at all
