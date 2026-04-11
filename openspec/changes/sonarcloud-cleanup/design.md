## Context

On 2026-04-11, while working on PR #11 (topology FK workaround removal), the `SonarCloud Scan` CI check was failing due to a configuration conflict between SonarCloud's Automatic Analysis and the CI-driven `sonar.yml` workflow. Investigating why the check was failing surfaced a broader truth: the IronGateLabs/postgis master branch carries a massive backlog of SonarCloud-reported issues (8,697 code smells, 161 bugs, 3 vulnerabilities, 152 blockers, 532 security hotspots, ~1,040 hours technical debt) that nobody has systematically triaged.

Rather than ignore the backlog or attempt a blind cleanup sweep, the approach taken was to spawn four parallel investigation agents on 2026-04-11 to sample the issue population, classify each into actionable buckets, and surface the real bugs hiding in the noise. The agent findings (summarized in the "Findings snapshot" section below) inform every decision in this plan.

The cleanup is framed as a **phased initiative** with an explicit classification model, so that each subsequent focused PR has clear scope and success criteria. The structure mirrors `multi-vendor-gpu-rollout`: this change is a living roadmap that tracks phases as checkboxes, spawns focused PRs for implementation, and is never archived in the conventional OpenSpec sense.

## Goals / Non-Goals

**Goals:**

- Establish a reviewable classification model (REAL / TEST FIXTURE / VENDORED / FALSE POSITIVE) so every SonarCloud issue has an action, not just a number
- Get the SonarCloud dashboard into a state where its signal is meaningful: new issues from new code should dominate the dashboard, not a backlog wall of pre-existing noise
- Fix the small number of real memory-safety bugs discovered in the investigation (currently ~3 confirmed: NULL deref in `flatgeobuf.c`, missing return in `optionlist.c`, investigate `gserialized_estimate.c` negative index) before any regression can compound them
- Establish a convention for triaging future SonarCloud-reported issues as they arise, so the backlog does not regrow
- Capture the 2026-04-11 findings as a durable artifact so future-self / future-me does not have to re-do the same investigation

**Non-Goals:**

- Hand-fixing all 8,697 code smells. Most of them are style-only and the cost/benefit does not justify the effort.
- Rewriting vendored dependencies (`deps/**`, Shapelib, Bob Jenkins' hash, flex/bison-generated scanners). Those are upstream's responsibility.
- Modifying SQL test fixtures to satisfy `plsql:*` rules. The test fixtures exist to test SQL behavior, including behavior SonarCloud would flag as "wrong".
- Replacing SonarCloud with a different static analyzer. SonarCloud is valuable — the backlog is a configuration and policy problem, not a tool-choice problem.
- Blocking PR #11 or any other in-flight work on this cleanup. The cleanup is orthogonal to feature work and can proceed on its own cadence.

## Decisions

### Decision 1: Four-way classification model

**Choice:** Every SonarCloud-reported issue SHALL be classified into exactly one of:

- **REAL** — A bug, code smell, or vulnerability in first-party, non-test, non-vendored source code that should be fixed. Default action: write a fix PR.
- **TEST FIXTURE** — An intentional violation in a test file (`regress/**`, `topology/test/**`, `raster/test/**`, `**/cunit/**`, `doc/html/images/styles.c`). The violation exists to test the database engine's handling of that exact pattern. Default action: exclude the path in `sonar-project.properties`.
- **VENDORED** — Code maintained by an upstream project that PostGIS vendors (`deps/**`, `liblwgeom/lookup3.c`, `liblwgeom/lwin_wkt_lex.c`, `liblwgeom/lwin_wkt_parse.c`, `loader/dbfopen.c`, `loader/getopt.c`). Default action: exclude the path and, if the issue is real, push the fix upstream to the vendor project. Do NOT carry local patches against vendored code unless absolutely necessary.
- **FALSE POSITIVE** — The analyzer's pattern matcher or abstract interpretation flagged code that is actually correct. Examples: XPath template strings flagged as hard-coded passwords; functions with documented preconditions that the analyzer cannot verify. Default action: add an inline `// NOSONAR - <reason>` comment documenting why the flag is wrong.

**Rationale:** Without classification, every issue looks equally important and the sheer count (8,697) paralyzes action. With classification, the action per issue is mechanical and each class has a different remediation pattern.

**Alternatives considered:**

- **(A) Three-way split (real / noise / vendored).** Rejected because "noise" conflates test fixtures (which need exclusions) with false positives (which need NOSONAR markers), and those have different handling.
- **(B) Severity-only triage (fix all blockers, ignore the rest).** Rejected because many blockers are false positives (e.g., `dbfopen.c` S3519) and some major/minor issues are real (e.g., the optionlist.c S2259 missing-return). Severity alone doesn't predict actionability.

### Decision 2: Configuration-level cleanup before code-level cleanup

**Choice:** Phase 1 of this initiative SHALL be a configuration-only PR that adds path exclusions to `sonar-project.properties` covering all identified TEST FIXTURE and VENDORED paths. It SHALL NOT modify any source code.

**Rationale:** Reducing the blocker count from 152 to ~22 via exclusions makes the remaining real issues actually visible in the SonarCloud dashboard. Without that noise reduction, reviewers looking at SonarCloud for a real PR drown in pre-existing unrelated flags and stop paying attention.

The exclusions also represent a deliberate policy decision: "these files are not ours to fix". Encoding that in version control via the properties file is more honest than relying on individual reviewers to know which directories are vendored.

**Implementation sketch** (for Phase 1's focused PR):

```properties
# sonar-project.properties additions
sonar.exclusions=regress/**,deps/**,doc/**,**/cunit/**,**/test/**,\
  extensions/**/sql/**,fuzzers/**,ci/**,\
  topology/test/**,raster/test/**,\
  liblwgeom/lookup3.c,liblwgeom/lwin_wkt_lex.c,liblwgeom/lwin_wkt_parse.c,\
  loader/dbfopen.c,loader/getopt.c,\
  extras/ogc_test_suite/**,doc/html/images/styles.c
```

### Decision 3: Memory-safety bugs fixed before mechanical sweeps

**Choice:** Phase 3 (memory-safety) SHALL happen before Phase 4 (strtok sweep) and Phase 5 (side-effect cleanups), even though Phases 4 and 5 touch more lines of code.

**Rationale:** The confirmed NULL dereferences (`flatgeobuf.c:563`, `optionlist.c:112`) can crash the PostgreSQL backend under crafted input. That's a correctness and availability issue. The `strtok` and side-effect fixes are thread-safety and maintainability improvements, respectively — valuable but not urgent.

Phases SHOULD NOT be bundled into one mega-PR. Each phase gets its own focused PR so review can concentrate on the specific class of change.

### Decision 4: Living roadmap, never archived

**Choice:** This change follows the `multi-vendor-gpu-rollout` convention: it stays in `openspec/changes/sonarcloud-cleanup/` indefinitely. Phases get checked off in `tasks.md` as they complete, and links to the focused implementation OpenSpec changes (or direct PRs if no OpenSpec change is needed) are added inline.

**Rationale:** A cleanup initiative that spans weeks of elapsed time with multiple focused PRs needs a single canonical "where are we" reference. Archiving this change when Phase 1 completes would lose the plan for Phases 2–6. Also consistent with the user's expressed preference for OpenSpec-driven planning.

### Decision 5: NOSONAR markers include investigation context

**Choice:** Every `// NOSONAR - <reason>` comment added by this cleanup SHALL include a one-phrase explanation of why the flag is incorrect, pointing at enough context that a future maintainer does not need to re-investigate.

**Examples:**

```c
/* XPath expression template, not a credential. SonarCloud S2068 pattern
 * matcher flags the '[@...id=\'...\']' string shape. */
id = lwalloc(xmlStrlen(href) + sizeof("//:[@:id='']") + 1); // NOSONAR
```

```c
/* DBFReorderFields documents that panMap must be a valid permutation
 * of [0, nFields-1]. SonarCloud S3519 cannot verify the contract. */
memcpy(pszRecordNew + panFieldOffsetNew[i], // NOSONAR
       pszRecord + psDBF->panFieldOffset[panMap[i]],
       psDBF->panFieldSize[panMap[i]]);
```

**Rationale:** Bare `// NOSONAR` comments are indistinguishable from "I don't know why SonarCloud is complaining and I gave up". Explanatory comments turn the marker into a contract with the analyzer: "yes, I read the flag, here's why it's wrong".

### Decision 6: Phase ordering summary

**Choice:** Phases execute in this order:

0. **Configuration conflict resolution** — disable Automatic Analysis in SonarCloud web UI (manual) so the CI-driven build-wrapper analysis can run
1. **Path exclusions** — add TEST FIXTURE and VENDORED paths to `sonar-project.properties`
2. **False positive NOSONAR markers** — 3 vulns + other confirmed FPs
3. **Memory-safety bug fixes** — NULL derefs, garbage values, potential OOB
4. **`strtok` → `strtok_r` sweep** — 8 sites, mechanical
5. **Side-effect-in-logical-operator cleanups** — 7 sites, mechanical
6. **Long-tail cleanup** — rolling batches of the 8,000+ remaining code smells, no fixed end date

**Rationale for ordering:**

- Phase 0 unblocks the deep CI-driven analysis that Phases 2–5 rely on for verification.
- Phase 1 is fastest and highest-impact (ratio of blocker-reduction to effort), so do it first.
- Phase 2 is near-zero risk (adding comments doesn't change behavior).
- Phase 3 is the highest urgency real bug work.
- Phases 4 and 5 are mechanical sweeps that can happen in any order or even in parallel.
- Phase 6 is open-ended and runs alongside normal feature work.

## Findings snapshot (2026-04-11 agent investigation)

This section captures the output of four parallel investigation agents run on 2026-04-11 so the plan has a durable, concrete starting point. Numbers are as of that date; the baseline will shift as cleanup progresses.

### Vulnerabilities: 3 total, all false positives

| Location | Rule | Verdict | Fix |
|---|---|---|---|
| `postgis/lwgeom_in_gml.c:267` | `c:S2068` | FALSE POSITIVE — XPath template `"//:[@:id='']"` in `sizeof()` calc | NOSONAR comment |
| `postgis/lwgeom_in_gml.c:269` | `c:S2068` | FALSE POSITIVE — XPath format string `"//%s:%s[@%s:id='%s']"` in `sprintf()` | NOSONAR comment |
| `postgis/lwgeom_in_gml.c:278` | `c:S2068` | FALSE POSITIVE — same XPath template as :267 | NOSONAR comment |

### Blockers: 152 total

| Classification | Count | % | Action |
|---|---|---|---|
| REAL | 22 | 14% | Fix in phases 3/4/5 |
| TEST FIXTURE | 96 | 63% | Exclude in Phase 1 |
| VENDORED | 34 | 22% | Exclude in Phase 1 |
| (FALSE POSITIVE) | 0 | 0% | — (subsumed into VENDORED for dbfopen cases) |

### Real blocker-level issues (22 total)

Memory safety (HIGH priority — Phase 3):

| File:line | Rule | Issue |
|---|---|---|
| `postgis/gserialized_estimate.c:1012` | `c:S3519` | Access `field.value[-1]` (negative index, size 1) |
| `postgis/gserialized_estimate.c:1035` | `c:S3519` | Access `field.value[-1]` (negative index, size 1) |
| `raster/rt_core/rt_wkb.c:543` | `c:S3519` | `memcpy` OOB array element |

Thread-safety `strtok` → `strtok_r` (Phase 4, 8 sites):

| File:line | Rule |
|---|---|
| `liblwgeom/optionlist.c:95` | `c:S1912` |
| `liblwgeom/optionlist.c:149` | `c:S1912` |
| `postgis/lwgeom_geos.c:1029` | `c:S1912` |
| `postgis/lwgeom_geos.c:1295` | `c:S1912` |
| `raster/loader/raster2pgsql.c:243` | `c:S1912` |
| `raster/loader/raster2pgsql.c:266` | `c:S1912` |
| `raster/rt_pg/rtpg_internal.c:176` | `c:S1912` |
| `raster/rt_pg/rtpg_internal.c:199` | `c:S1912` |

Side-effects in `&&`/`||` (Phase 5, 7 sites):

| File:line | Rule |
|---|---|
| `postgis/lwgeom_in_kml.c:114` | `c:S912` |
| `postgis/lwgeom_in_marc21.c:69` | `c:S912` |
| `raster/loader/raster2pgsql.c:181` | `c:S912` |
| `raster/loader/raster2pgsql.c:641` | `c:S912` |
| `raster/rt_pg/rtpg_internal.c:83` | `c:S912` |

Miscellaneous (Phase 5 or later):

| File:line | Rule | Note |
|---|---|---|
| `raster/rt_core/rt_pixel.c:440` | `c:S3491` | Useless `&*` pointer ops |
| `raster/rt_core/rt_pixel.c:441` | `c:S3491` | Useless `&*` pointer ops |
| `raster/loader/raster2pgsql.c:863` | `c:S3584` | Potential memory leak |
| `raster/scripts/python/rtreader.py:116` | `python:S5644` | Object has no `__getitem__` (possible real bug in helper) |
| `raster/scripts/python/pixval.py:76` | `python:S930` | `exit()` called with too many args |

### Critical/major bugs (44 total, agent investigated 3)

| File:line | Rule | Verdict |
|---|---|---|
| `postgis/flatgeobuf.c:563` | `c:S2259` | REAL — `ctx->ctx` dereferenced before NULL check; fix by moving NULL check first and capturing `flatgeobuf_agg_ctx_init` return value |
| `liblwgeom/optionlist.c:112` | `c:S2259` | REAL — `lwerror()` logs but does not return; missing `return;` before subsequent `*val = '\0';` |
| `postgis/gserialized_estimate_support.h:91` | `c:S836` | FALSE POSITIVE — `size` initialized at line 90 before use at line 91, SonarCloud tracking is confused by loop structure |

Remaining 41 critical/major bugs (7 more `c:S2259`, 3 more `c:S836`, 5 `plsql:S1764`, 4 `cpp:S6458`, 4 `plsql:CharVarchar`, etc.) not yet investigated — will be picked up during Phase 3.

### Top long-tail rules (Phase 6 targets, total ~8,000 code smells)

| Rule | Count | Category |
|---|---|---|
| `plsql:S1192` | 1,272 | SQL literal duplication (mostly test fixtures → excluded after Phase 1) |
| `c:S1659` | 1,005 | Multiple var declarations per line (style) |
| `c:S1854` | 789 | Dead stores (some defensive, case-by-case) |
| `c:S1116` | 676 | Empty statements `;` alone (style) |
| `c:S134` | 637 | Nesting depth > 3 (style) |
| `c:S125` | 614 | Commented-out code |
| `c:S3776` | 227 | Cognitive complexity > 15 |
| `c:S5955` | 191 | Local var declared in for-init |

After Phase 1 path exclusions, the test-fixture portion of `plsql:S1192` is expected to drop significantly (from ~1,272 toward the low hundreds). Similar noise reduction for `plsql:LiteralsNonPrintableCharactersCheck` (338 → ~0), `plsql:OrderByExplicitAscCheck` (319 → ~0), etc.

## Risks / Trade-offs

- **[Risk] Path exclusions hide real issues that happen to live in test files** → Mitigation: Phase 1's focused PR SHALL include a review step where the list of excluded paths is cross-checked against a sample of the excluded issues to confirm none of them are real bugs hiding in test code. If a real bug is found, adjust the exclusion pattern to be narrower.

- **[Risk] NOSONAR markers drift as code evolves** → Mitigation: every NOSONAR comment SHALL include a `- <reason>` explanation per Decision 5. A future maintainer can verify the reason still holds.

- **[Risk] Memory-safety fixes introduce regressions** → Mitigation: Phase 3's focused PR SHALL include a CUnit test demonstrating the pre-fix crash and the post-fix correctness. Requires either crafting an input that triggers the NULL deref or using a memory debugger (ASan) to verify the pointer is actually NULL at the flagged call site.

- **[Risk] `strtok` → `strtok_r` refactor introduces subtle behavior changes** → Mitigation: `strtok_r` has the same semantics as `strtok` except for reentrancy. The transform is purely mechanical: add a `char *saveptr;` local and pass `&saveptr` to every `strtok_r` call. Existing regression tests should catch any accidental semantic changes. Review should verify the `saveptr` is unique per logical invocation (not shared across unrelated calls).

- **[Trade-off] Living roadmap never archives** → Same as `multi-vendor-gpu-rollout`. Documented convention, accepted.

- **[Trade-off] Long tail (Phase 6) has no completion criterion** → By design. Phase 6 is "rolling cleanup during normal work", not a sprint. If the long tail ever grows rather than shrinks, re-evaluate.

- **[Risk] Upstream Shapelib patches become desirable** → If we discover real bugs in `loader/dbfopen.c` that Shapelib upstream has not fixed, we need to either carry a local patch (divergence risk) or push upstream (slow). Out of scope for this change; flag as a future concern.

## Migration Plan

This change itself has no migration (it is documentation). Each phase's focused implementation PR has its own migration plan captured in the corresponding tasks.

Phase 0 (SonarCloud web UI) is a one-time manual action by the user. Once done, Phase 1 (path exclusions) becomes unblocked.

Phases 1–5 are independent focused PRs that can land in any order after Phase 0. Recommended order is the natural dependency chain (1 → 2 → 3 → 4 → 5) but hard dependencies are only 1 → 2 (exclusions before the false-positive NOSONAR work so the reviewer can see the reduced noise) and 1 → 3 (exclusions before memory-safety work so the reviewer isn't drowning in noise from test fixtures).

Phase 6 is open-ended and interleaves with normal feature work.

**Rollback:** If Phase 1 exclusions accidentally hide a real bug, revert the exclusion and add a narrower path pattern. No data corruption risk; only dashboard visibility changes.

## Open Questions

1. **Should the SonarCloud project also gate new-code issues** (via the "New Code" quality gate in SonarCloud web UI) once the noise is cleaned up? — Recommended yes, after Phase 2 completes and the dashboard is actually readable. A new-code gate would block PRs that INTRODUCE new blockers or vulnerabilities, preventing the backlog from regrowing.

2. **Is there an automated way to convert the 152 blockers into GitHub issues** for tracking? — SonarCloud has an export API but it's verbose. For now, the tables in this design.md serve as the tracking list. If the long tail ends up with hundreds of issues that need individual tracking, consider per-rule or per-file umbrella issues.

3. **Should Phase 6 have a target date?** — Probably not. It's explicitly an ongoing effort, not a sprint. The right metric is "blocker count does not regrow", not "blocker count hits zero by date X".
