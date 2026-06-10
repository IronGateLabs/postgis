## Why

SonarCloud currently reports **8,697 code smells, 161 bugs, 3 vulnerabilities, 152 blockers, and 532 security hotspots** on `IronGateLabs_postgis` master, with a technical-debt estimate of ~1,040 hours. That sounds overwhelming, but the raw numbers mask three important facts that a day of targeted investigation (spawned as parallel agent research on 2026-04-11) has now clarified:

1. **Most of the "scary" counts are noise** in one of three forms:
   - **Test-fixture intentional violations** — SQL test files containing `UPDATE` without `WHERE`, missing `VARCHAR` size constraints, non-printable characters in string literals, etc. SonarCloud flags them, but they exist on purpose to test the database engine's handling of those exact patterns. Roughly **96 of 152 blockers** fall in this bucket.
   - **Vendored / generated code** — `deps/flatgeobuf/`, `deps/wagyu/`, `liblwgeom/lookup3.c` (Bob Jenkins' public-domain hash), `liblwgeom/lwin_wkt_lex.c` (flex-generated), `liblwgeom/lwin_wkt_parse.c` (bison-generated), `loader/dbfopen.c` / `loader/getopt.c` (Shapelib / AT&T public domain). Roughly **34 of 152 blockers** are here. PostGIS should NOT hand-edit these files; upstream projects own the code.
   - **False positives** — SonarCloud's pattern matchers flagging non-bugs. Examples: all 3 "hard-coded password" vulnerabilities are actually XPath expression templates in `postgis/lwgeom_in_gml.c`; the 2 `c:S3519` "array out-of-bounds" in `loader/dbfopen.c` cannot fire because the function documents a permutation precondition that SonarCloud cannot verify.

2. **The real bugs are few and fixable.** Of the 152 blockers, only **22 are real issues in first-party non-test, non-vendored code**. Of the 44 critical/major bugs, at least **2 are confirmed real NULL-dereference bugs** in `postgis/flatgeobuf.c` and `liblwgeom/optionlist.c` that could crash the PostgreSQL backend under malformed input — small in number but security-adjacent and worth immediate attention.

3. **A configuration-level cleanup can cut the noise by ~80%** before any code changes land. Adding path-based exclusions for `regress/**`, `topology/test/**`, `raster/test/**`, `deps/**`, generated files, and `loader/dbfopen.c` / `loader/getopt.c` / `liblwgeom/lookup3.c` would drop the blocker count from 152 to roughly 22 and make the SonarCloud dashboard actually useful for triaging new issues.

This change proposes a **phased, classified cleanup initiative** that turns the overwhelming raw numbers into a manageable set of focused cleanup PRs, with a policy for classifying each issue (REAL / TEST FIXTURE / VENDORED / FALSE POSITIVE) and a clear ordering for the real fixes by risk and difficulty.

## What Changes

This change is a **planning artifact**, not an implementation. It creates the classification policy and phased tracking structure that subsequent focused implementation PRs will consume. It does not itself modify any source code.

Concretely:

- Create a new capability `sonarcloud-cleanup-policy` that defines:
  - The four-way classification (REAL / TEST FIXTURE / VENDORED / FALSE POSITIVE) with examples
  - The per-class action policy (fix, exclude, suppress, add NOSONAR marker)
  - The cleanup phase ordering (configuration first, then memory safety, then mechanical sweeps, then long-tail)
  - The per-phase success criteria and verification approach
- Document the findings from the 2026-04-11 investigation (the four agents' reports) so future-self has a snapshot of where things stood at the start of the initiative
- Enumerate the specific fixable issues discovered and assign each to a planned phase
- Establish the convention that this change is a **living roadmap** (like `multi-vendor-gpu-rollout`) that stays in `openspec/changes/` indefinitely with phases checked off as they complete

The change also implicitly documents a design decision already taken in PR #11: the SonarCloud CI workflow (`.github/workflows/sonar.yml`) was marked `continue-on-error: true` as a temporary workaround until the SonarCloud web-UI Automatic Analysis setting is disabled. Disabling Automatic Analysis is Phase 0 of this cleanup plan because it must happen before the CI-driven deep analysis (required for proper triage of the 22 real bugs) can run at all.

## Capabilities

### New Capabilities

- `sonarcloud-cleanup-policy`: A living cleanup roadmap capability that defines the classification model for SonarCloud-reported issues (REAL / TEST FIXTURE / VENDORED / FALSE POSITIVE), the action policy per class, phased cleanup ordering, and per-phase success criteria. Serves as the canonical "where are we" reference for the long-running code-quality cleanup effort.

### Modified Capabilities

None directly. The existing `sonar-project.properties` will be updated by Phase 1 (adding exclusion paths) but that's an implementation-level change, not a spec-level change.

## Impact

- **Code**: zero. This change is documentation only.
- **Future implementation PRs spawned from this plan**: expected 4–6 focused PRs over coming weeks or months:
  - Phase 1: SonarCloud exclusion paths + Automatic Analysis disable (configuration)
  - Phase 2: False-positive NOSONAR markers (3 vulns + 2 dbfopen blockers + others as found)
  - Phase 3: Memory-safety bug fixes (gserialized_estimate.c, rt_wkb.c, flatgeobuf.c, optionlist.c)
  - Phase 4: `strtok` → `strtok_r` thread-safety sweep (8 sites in 5 files)
  - Phase 5: Side-effect-in-logical-operator cleanups (7 sites)
  - Phase 6: Long-tail cleanup (the 8,000+ code smells triaged in rolling batches)
- **SonarCloud dashboard value**: after Phase 1 + 2, the dashboard should show roughly 20 real bugs and 0 vulnerabilities, making it actually useful for catching NEW regressions instead of drowning in pre-existing noise.
- **Baseline captured**: this change snapshots SonarCloud metrics as of 2026-04-11 so progress can be measured against a reference point. Future memory entries can track the trend (blocker count dropping over time, etc.).

## Resolved Questions

1. **Should we fix the ~9,000 code smells by hand?** — No. The long tail includes 1,272 SQL literal duplications (mostly in test fixtures), 1,005 multi-variable declarations (style-only), 789 dead stores (often intentional defensive initialization), 676 empty statements, 637 nesting-depth warnings. Hand-fixing these is weeks of work for minor benefit. The right approach is to (a) suppress categories that are noise via exclusions, (b) fix the 20–30 real bugs surgically, and (c) let new code be scanned against a clean baseline going forward.

2. **Should we carry a local patch against `loader/dbfopen.c`?** — No. It's vendored Shapelib code and the 2 "array OOB" flags are false positives (documented permutation contract). Better path: exclude the file from SonarCloud analysis, and if a real Shapelib bug is ever found, push the fix upstream to Shapelib rather than carrying a local divergence.

3. **Should the 3 "hard-coded password" vulnerabilities stay as `// NOSONAR` or get suppression rules in `sonar-project.properties`?** — NOSONAR. Inline comments keep the context visible at the offending line for human readers. Global suppression rules for `c:S2068` would hide real future password bugs.

4. **Is the SonarCloud Automatic-vs-CI analysis conflict blocking this work?** — Yes but only for deep analysis. The GitHub-integration Automatic Analysis is running and catches the basics. Disabling it (web UI action) is tracked as Phase 0 of this cleanup plan so the CI-driven build-wrapper analysis can take over for the deeper C/C++ checks needed to confirm the memory-safety fixes.

## Open Questions

1. **Should Phase 2 (NOSONAR markers) also add an inline comment documenting WHY each suppression is correct?** — Recommended yes, so future maintainers don't re-investigate the same false positives. The agent investigations that informed this change should be summarized in the NOSONAR comments.

2. **What's the right cadence for Phase 6 (long-tail cleanup)?** — Undecided. Options: weekly cleanup sprints targeting one rule at a time, or "when touching a file, fix SonarCloud issues in that file as part of the same PR". Probably both, with a bias toward the latter for active code and the former for infrequently-touched modules.

3. **Should test-fixture intentional violations be exposed via SonarCloud or hidden?** — This change proposes hiding them via path exclusions, reducing SonarCloud dashboard noise. Alternative: keep them visible and use SonarCloud's "Won't Fix" status per issue. Path exclusions are lower-maintenance and the loss (missing a REAL issue that accidentally lives in a test file) is tolerable.
