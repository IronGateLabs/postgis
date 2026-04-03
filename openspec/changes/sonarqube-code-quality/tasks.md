# Tasks: SonarQube Code Quality Remediation

## Phase 1: Pre-commit Hooks and CI Gate

### Local Tooling

- [x] Create `.pre-commit-config.yaml` at project root with clang-format hook (check mode, exclude deps/regress/doc) and cppcheck hook (advisory mode, warning+performance categories)
- [x] Verify `.clang-format` config handles topology 2-space indent exception correctly; add directory-level override if needed
- [x] Add `make lint` target to top-level `Makefile.in` that runs clang-format check and cppcheck across all source directories (liblwgeom, postgis, raster, topology, sfcgal, loader, libpgcommon)
- [ ] Test pre-commit hooks locally: stage a C file with intentional formatting violation, confirm hook blocks commit; stage a clean file, confirm commit succeeds
- [ ] Test `git commit --no-verify` bypasses hooks successfully
- [ ] Test `make lint` runs end-to-end and reports findings

### CI Integration

- [x] Create `sonar-project.properties` at project root with project key `IronGateLabs_postgis`, org `irongatelabs`, sources, exclusions, and encoding settings
- [x] Create `.github/workflows/sonar.yml` GitHub Actions workflow: trigger on PRs to develop and pushes to develop, checkout with fetch-depth 0, run make lint, run SonarCloud scanner
- [ ] Add `SONAR_TOKEN` as a GitHub Actions repository secret (value: `527c819b53c81f7a233600a72a1091c82278295a`)
- [ ] Configure SonarCloud quality gate: 0 new bugs, 0 new vulnerabilities, 100% new hotspots reviewed, maintainability/reliability/security rating A on new code
- [ ] Configure SonarCloud to use `develop` as the main branch for analysis (not `master`)
- [ ] Add SonarCloud quality gate as a required status check on `develop` branch protection rules
- [ ] Verify PR decoration works: open a test PR, confirm SonarCloud posts status check and summary comment
- [ ] Document pre-commit hook setup in contributing guidelines or README

## Phase 2: High-Impact Mechanical C Fixes

### S1854: Dead Store Removal (789 issues)

- [ ] Query SonarCloud API to export all S1854 issues with file paths and line numbers
- [ ] Fix S1854 dead stores in `liblwgeom/` (~241 directory issues, S1854 subset): remove unused assignments, preserve side-effect function calls with `(void)` cast
- [ ] Fix S1854 dead stores in `postgis/`: remove unused assignments, verify no pointer aliasing reads
- [ ] Fix S1854 dead stores in `raster/rt_pg/` (~263 directory issues, S1854 subset)
- [ ] Fix S1854 dead stores in `topology/`
- [ ] Fix S1854 dead stores in `sfcgal/` and `loader/`
- [ ] Compile all changes with `-Wall -Werror` to verify no new warnings introduced
- [ ] Run `make check` full regression suite after all S1854 fixes

### S1116: Empty Statement Removal (676 issues)

- [ ] Fix S1116 empty statements in `liblwgeom/`: remove stray semicolons after control structures
- [ ] Fix S1116 empty statements in `postgis/`
- [ ] Fix S1116 empty statements in `raster/`
- [ ] Fix S1116 empty statements in `topology/`, `sfcgal/`, `loader/`
- [ ] Review and convert intentional empty loop bodies to `{ /* intentionally empty */ }` form
- [ ] Run `make check` full regression suite after all S1116 fixes

### S125: Commented-Out Code Removal (614 issues)

- [ ] Fix S125 in `liblwgeom/`: delete commented-out code blocks, preserve useful context as prose comments
- [ ] Fix S125 in `postgis/`
- [ ] Fix S125 in `raster/`
- [ ] Fix S125 in `topology/`, `sfcgal/`, `loader/`
- [ ] Run `make check` full regression suite after all S125 fixes

## Phase 3: Structural C Fixes

### S134: Nesting Depth Reduction (637 issues)

- [ ] Identify top 10 files by S134 issue count from SonarCloud
- [ ] Refactor nesting in top 10 files: apply early return/guard clause pattern where possible
- [ ] Refactor nesting in top 10 files: extract deeply nested blocks into static helper functions
- [ ] Refactor remaining S134 issues in `liblwgeom/` using condition merging and control flow restructuring
- [ ] Refactor remaining S134 issues in `postgis/`
- [ ] Refactor remaining S134 issues in `raster/rt_pg/`
- [ ] Refactor remaining S134 issues in `topology/`, `sfcgal/`, `loader/`
- [ ] Run `make check` after each file's refactoring to catch regressions early
- [ ] Manual code review of all S134 refactoring commits

### S1659: Multi-Variable Declaration Splitting (1,005 issues)

- [ ] Fix S1659 in `liblwgeom/`: split all multi-variable declarations into one-per-line, preserving original order
- [ ] Fix S1659 in `postgis/`
- [ ] Fix S1659 in `raster/`
- [ ] Fix S1659 in `topology/`, `sfcgal/`, `loader/`, `libpgcommon/`
- [ ] Verify no pointer-type ambiguity bugs (e.g., `int *a, b;` correctly split to `int *a;` and `int b;`)
- [ ] Run `make check` full regression suite after all S1659 fixes

## Phase 4: SQL Quality

### S1192: Duplicated String Literals (1,272 issues)

- [ ] Query SonarCloud API to export all S1192 issues, grouped by file
- [ ] Identify literals duplicated 5+ times as highest-value extraction targets
- [ ] Fix S1192 in `postgis/postgis_sql.in` and `postgis/geography_sql.in`: extract repeated literals into PL/pgSQL constants where appropriate
- [ ] Fix S1192 in `raster/rt_pg/rtpostgis_sql.in`
- [ ] Fix S1192 in `topology/sql/*.sql.in`
- [ ] Fix S1192 in `sfcgal/sfcgal_sql.in`
- [ ] Review remaining S1192 issues and mark inherently idiomatic duplication as acceptable (configure SonarCloud exclusions)
- [ ] Run `make check` full regression suite after all S1192 fixes

### OrderByExplicitAscCheck (319 issues)

- [ ] Fix OrderByExplicitAscCheck across all SQL files: add explicit `ASC` to ORDER BY clauses without a direction specifier
- [ ] Update any regression test expected output files (`*_expected`) that include ORDER BY in their output
- [ ] Run `make check` full regression suite after all ORDER BY fixes

## Phase 5: Bug Fixes and Security

### Bug Triage (161 bugs)

- [ ] Export all 161 bug issues from SonarCloud with severity, file path, and description
- [ ] Categorize bugs by severity: BLOCKER, CRITICAL, MAJOR, MINOR, INFO
- [ ] Categorize bugs by type: null dereference, resource leak, incorrect logic, integer overflow, other
- [ ] Prioritize: fork-owned code bugs first, then actively modified upstream code

### BLOCKER and CRITICAL Bug Fixes

- [ ] Fix all BLOCKER-severity bugs (null dereferences, resource leaks with security impact)
- [ ] Fix all CRITICAL-severity bugs
- [ ] Write or verify regression tests exist for each bug fix
- [ ] Manual code review of every bug fix commit
- [ ] Run `make check` after each individual bug fix

### Vulnerability Fixes (3 vulnerabilities)

- [ ] Investigate and fix all 3 identified vulnerabilities
- [ ] Verify fixes with targeted test cases
- [ ] Manual security review of vulnerability fix commits

### Security Hotspot Review (532 hotspots)

- [ ] Review all 532 security hotspots in SonarCloud
- [ ] For hotspots that are safe-by-design: mark as "Safe" in SonarCloud with justification comment
- [ ] For hotspots requiring code changes: create fix commits with security review
- [ ] For hotspots in upstream code: document findings and defer to upstream if appropriate
- [ ] Target: reduce unreviewed hotspots to 0

### MAJOR and MINOR Bug Fixes

- [ ] Fix MAJOR-severity bugs in fork-owned code
- [ ] Fix MAJOR-severity bugs in actively modified upstream code
- [ ] Triage MINOR-severity bugs: fix if low-risk, defer if high merge conflict risk
- [ ] Run `make check` full regression suite after all Phase 5 fixes

## Duplication Reduction (Cross-Phase)

- [ ] Identify top 20 duplicated code blocks from SonarCloud duplication analysis
- [ ] Extract shared helpers for LWGEOM type-switch boilerplate (blocks duplicated 3+ times)
- [ ] Extract shared helpers for error-checking sequences into macros
- [ ] Extract shared inline functions for serialized point extraction patterns
- [ ] Extract common SQL function preamble patterns into macros
- [ ] Extract raster band iteration helpers in `raster/rt_pg/`
- [ ] Verify duplication rate drops below 5% (from 7.7%)
- [ ] Run `make check` after each extraction

## Verification and Metrics

- [ ] Confirm SonarCloud quality gate passes on `develop` after all phases
- [ ] Verify total issue count is below 4,000 (from 8,861)
- [ ] Verify tech debt estimate is below 520 hours (from ~1,040)
- [ ] Verify reliability rating has improved from E toward C or better
- [ ] Verify no new issues introduced by remediation (SonarCloud delta is strictly negative)
- [ ] Document final metrics and remaining issue triage decisions
