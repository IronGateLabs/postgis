## Why

The IronGateLabs/postgis fork has **8,861 open SonarCloud issues** on master (SonarCloud project: `IronGateLabs_postgis`, org: `irongatelabs`):

- **8,697 code smells**, **161 bugs**, **3 vulnerabilities**, **532 security hotspots**
- Reliability rating **E**, Security rating **C**, **7.7% code duplication**, ~**1,040 hours** estimated tech debt

Top C rule violations:
| Rule | Description | Count |
|------|-------------|-------|
| S1659 | Multiple variable declarations per statement | 1,005 |
| S1854 | Dead stores (useless assignments) | 789 |
| S1116 | Empty statements (stray semicolons) | 676 |
| S134 | Excessive nesting depth | 637 |
| S125 | Commented-out code | 614 |

Top SQL rule violations:
| Rule | Description | Count |
|------|-------------|-------|
| S1192 | Duplicated string literals | 1,272 |
| OrderByExplicitAscCheck | Implicit ASC in ORDER BY | 319 |

Heaviest directories: `regress/core` (789), `topology/test/regress` (312), `raster/rt_pg` (263), `liblwgeom` (241).

There are no pre-commit hooks or local static analysis gates to prevent new issues from being introduced. Developers currently have no feedback until code reaches SonarCloud, making the problem steadily worse.

## What Changes

A phased remediation of SonarCloud issues combined with local tooling to prevent regressions.

### Phase 1: Pre-commit Hooks and CI Gate

- Add a `.pre-commit-config.yaml` with:
  - **clang-format** hook: auto-format staged C/H files using the existing `.clang-format` config. This catches formatting drift before it reaches CI.
  - **cppcheck** hook (optional/advisory): run static analysis on changed C files, warn on new issues without blocking.
- Add a `Makefile` target or script (`scripts/lint.sh`) for running the same checks in CI.
- Configure SonarCloud quality gate on the `develop` branch so new code must pass before merge.
- Document the setup in contributing guidelines so all fork contributors use the hooks.

### Phase 2: High-Impact C Fixes (Dead Stores, Empty Statements, Commented Code)

Target the three rules with the most issues and lowest risk of behavioral change:
- **S1854 (dead stores, 789)**: Remove useless assignments. These are pure deletions with no behavioral impact.
- **S1116 (empty statements, 676)**: Remove stray semicolons after control structures. Mechanical fix.
- **S125 (commented-out code, 614)**: Remove commented-out code blocks. The code is in git history if ever needed.

Estimated reduction: ~2,079 issues (~23% of total).

### Phase 3: Structural C Issues (Nesting Depth, Multi-Variable Declarations)

- **S134 (nesting depth, 637)**: Refactor deeply nested blocks using early returns, guard clauses, and helper function extraction. These are logic-preserving refactors but require careful review.
- **S1659 (multiple vars per declaration, 1,005)**: Split multi-variable declarations into one-per-line. Mechanical but touches many lines.

Estimated reduction: ~1,642 additional issues (~18% of total).

### Phase 4: SQL Quality

- **S1192 (duplicated string literals, 1,272)**: Extract repeated strings into SQL variables or constants where appropriate in test and extension SQL files.
- **OrderByExplicitAscCheck (319)**: Add explicit `ASC` to ORDER BY clauses.

Estimated reduction: ~1,591 additional issues (~18% of total).

### Phase 5: Bug Fixes and Security Hotspots

- **161 bugs**: Triage and fix actual bug-class issues identified by SonarCloud (null dereferences, resource leaks, incorrect logic).
- **3 vulnerabilities**: Address the three identified vulnerability findings.
- **532 security hotspots**: Review and either fix or mark as safe-by-design.

This phase requires the most careful review as changes may affect runtime behavior.

## Strategy

**Fork-first prioritization**: Focus remediation on code owned by our fork first:
1. `accel/` -- our GPU acceleration code (fully ours)
2. Fork-specific additions in `liblwgeom/`, `postgis/`, `topology/` (ECEF/ECI, geocentric guards)
3. Upstream code we actively maintain and modify
4. Upstream code we carry unchanged (lowest priority, highest upstream merge conflict risk)

**Commit discipline**: Per project conventions, style-only commits must be kept strictly separate from logic commits. Each phase will produce:
- Style-only commits (formatting, dead store removal, empty statement removal)
- Logic commits (nesting refactors, bug fixes) -- separate and individually reviewable

**Branch strategy**: Work on `develop`. Each phase gets its own feature branch merged via PR with SonarCloud gate passing.

## Capabilities

### New Capabilities
- Pre-commit hook infrastructure for code quality enforcement
- Local static analysis via cppcheck for C code
- CI quality gate integration with SonarCloud on develop branch

### Modified Capabilities
_No functional capabilities are modified. All changes are code quality improvements that preserve existing behavior._

## Impact

- **Code**: Touches files across the entire codebase, but phases 2-3 are mechanical/style changes with no behavioral impact. Phase 5 (bugs) may change runtime behavior intentionally.
- **Build**: No build system changes beyond adding optional tooling scripts.
- **CI**: New SonarCloud quality gate on develop branch. PRs that introduce new issues will be flagged.
- **Dependencies**: `pre-commit` framework (Python, dev-only), `cppcheck` (optional, dev-only). No runtime dependency changes.
- **Upstream merge risk**: Style changes to upstream files increase merge conflict surface. Mitigated by prioritizing fork-owned code and deferring upstream-only files.
- **Risk**: Low for phases 1-4 (tooling and mechanical fixes). Medium for phase 5 (bug fixes require behavioral verification via regression tests).

## Goals

- Pass SonarCloud quality gate on `develop` branch (zero new issues on new code)
- Reduce total tech debt by 50%+ (from ~1,040 hours to <520 hours)
- Reduce open issues from 8,861 to <4,000
- Improve reliability rating from E toward C or better
- Establish pre-commit hooks so new code maintains quality standards going forward
