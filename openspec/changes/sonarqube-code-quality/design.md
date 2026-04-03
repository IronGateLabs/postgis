# Design: SonarQube Code Quality Remediation

## Overview

This design covers three interconnected systems: local pre-commit hooks that prevent new issues, a CI quality gate that enforces standards on PRs, and a phased remediation plan that systematically reduces the 8,861 existing SonarCloud issues. All changes preserve existing runtime behavior except Phase 5 bug fixes, which intentionally correct defects.

---

## 1. Pre-commit Hook Architecture

### Framework

Use the [pre-commit](https://pre-commit.com/) framework (Python-based, language-agnostic). Developers install once with `pip install pre-commit && pre-commit install`. The framework manages hook versions and execution automatically.

### Configuration: `.pre-commit-config.yaml`

Two hooks run on every commit:

#### 1.1 clang-format Hook

- **Source**: `https://github.com/pre-commit/mirrors-clang-format`
- **Scope**: Staged `*.c` and `*.h` files only
- **Behavior**: Checks formatting against the existing `.clang-format` config. Fails the commit if any file would be reformatted. Developer runs `git clang-format` to fix.
- **Mode**: Check-only (not auto-fix). This prevents surprises where the hook silently modifies staged content and ensures the developer reviews formatting changes.
- **Exclusions**: Files under `deps/`, `regress/`, and `doc/` are excluded since we do not own deps, regress SQL files are not C, and doc is documentation.
- **Topology exception**: The `.clang-format` already handles the 2-space indent for `topology/*.{c,h}` and `liblwgeom/topo/**` via directory-level `.clang-format` overrides or the existing config. No special hook logic needed.

#### 1.2 cppcheck Hook (Advisory)

- **Source**: `https://github.com/pocc/pre-commit-hooks` (cppcheck wrapper)
- **Scope**: Staged `*.c` files only
- **Behavior**: Runs `cppcheck --enable=warning,performance --suppress=missingIncludeSystem --error-exitcode=0` on changed files. Exit code 0 means it warns but does not block the commit.
- **Rationale**: cppcheck catches null dereferences, resource leaks, and dead code that clang-format cannot. Advisory mode prevents blocking developers on false positives in upstream code while still surfacing real issues.
- **Future**: Once issue count is low enough, switch `--error-exitcode=0` to `--error-exitcode=1` to make it blocking.

#### 1.3 Bypass Mechanism

Standard pre-commit bypass: `git commit --no-verify`. This is documented as an emergency escape hatch only, not for routine use. CI will still catch issues regardless.

### Makefile Target: `make lint`

A new top-level `Makefile` target that runs the same checks as the hooks but across all source files (not just staged ones). This is used in CI and for full-codebase sweeps.

```makefile
lint:
	@echo "Running clang-format check..."
	find liblwgeom postgis raster topology sfcgal loader libpgcommon \
		-name '*.c' -o -name '*.h' | \
		xargs clang-format --dry-run --Werror 2>&1 || \
		(echo "clang-format failures found. Run 'git clang-format' to fix." && exit 1)
	@echo "Running cppcheck..."
	find liblwgeom postgis raster topology sfcgal loader libpgcommon \
		-name '*.c' | \
		xargs cppcheck --enable=warning,performance \
		--suppress=missingIncludeSystem --quiet 2>&1
	@echo "Lint complete."
```

The `lint` target is not integrated into the default `make` build to avoid slowing down development builds.

---

## 2. SonarCloud CI Integration

### GitHub Actions Workflow: `.github/workflows/sonar.yml`

**Trigger**: On pull requests targeting `develop` and on pushes to `develop`.

**Steps**:
1. Checkout code with full history (`fetch-depth: 0` -- required for SonarCloud branch analysis and blame).
2. Run `make lint` (clang-format check). Fail fast if formatting is wrong.
3. Run the SonarCloud scanner using the official `sonarsource/sonarcloud-github-action`.
4. SonarCloud posts a status check and PR comment with the analysis results.

**Authentication**: The SonarCloud token is stored as a GitHub Actions secret `SONAR_TOKEN`. Value: the token from `~/.sonar/token`.

**Branch analysis**: SonarCloud analyzes `develop` as the main branch for the fork (not `master`, which tracks upstream). PR analysis compares new code against the `develop` baseline.

### SonarCloud Configuration: `sonar-project.properties`

```properties
sonar.projectKey=IronGateLabs_postgis
sonar.organization=irongatelabs

# Source directories
sonar.sources=liblwgeom,postgis,raster,topology,sfcgal,loader,libpgcommon

# Exclusions - test files, vendored deps, docs, generated files
sonar.exclusions=regress/**,deps/**,doc/**,**/cunit/**,**/test/**,**/*_expected,extensions/**/*.sql

# Test directories (for coverage context, not scanned for issues)
sonar.tests=regress

# C-specific settings
sonar.c.file.suffixes=.c,.h
sonar.c.errorRecoveryEnabled=true

# Encoding
sonar.sourceEncoding=UTF-8
```

### Quality Gate Configuration

Configure on SonarCloud UI (or via API) for the `IronGateLabs_postgis` project:

| Metric | Threshold | Scope |
|--------|-----------|-------|
| New Bugs | 0 | New code |
| New Vulnerabilities | 0 | New code |
| New Security Hotspots Reviewed | 100% | New code |
| Maintainability Rating | A | New code |
| Reliability Rating | A | New code |
| Security Rating | A | New code |

This gate applies only to **new code** (code changed in the PR). It does not block PRs due to existing legacy issues. The "new code" definition period is set to "Previous version" so each PR is measured against the develop baseline.

The quality gate is a **required status check** on the `develop` branch protection rules. PRs cannot merge if the gate fails.

---

## 3. Remediation Strategy Per Rule

### 3.1 S1854: Dead Stores (789 issues)

**Pattern**: A variable is assigned a value that is never subsequently read before the variable goes out of scope or is reassigned.

**Fix**: Remove the dead assignment. Common cases:
- `int ret = 0; ... ret = some_function(); return ret;` -- remove `= 0` if never read before reassignment.
- `result = compute(); result = compute_again();` -- remove first assignment.
- Return value assigned but never checked: `ret = close(fd);` -- if return is intentionally ignored, cast to `(void)` to signal intent: `(void)close(fd);`.

**Verification**: Each removal must confirm:
1. The assigned value has no side effects (e.g., function calls with side effects must remain, only the assignment is removed).
2. The variable is not read via pointer aliasing.
3. The removal does not trigger compiler warnings about uninitialized variables.

**Risk**: Low. Dead stores are by definition unused. The main risk is false positives where SonarCloud does not trace pointer-based reads.

### 3.2 S1116: Empty Statements (676 issues)

**Pattern**: A stray semicolon after a control structure creates an empty statement body.

```c
/* Before */
if (condition);    /* <-- empty body, next line always executes */
    do_something();

for (i = 0; i < n; i++);  /* <-- loop does nothing */
    process(i);

/* After */
if (condition)
    do_something();

for (i = 0; i < n; i++)
    process(i);
```

**Fix**: Remove the stray semicolon. In some cases the empty statement is intentional (e.g., `while (read(...) > 0);` for draining a buffer). These are marked with a comment: `while (read(...) > 0) { /* intentionally empty */ }`.

**Verification**: Each fix must confirm the semicolon is truly stray and not an intentional empty loop body. If intentional, convert to `{ /* intentionally empty */ }` to suppress the warning and clarify intent.

**Risk**: Low for stray semicolons. Must be careful with intentional empty loops.

### 3.3 S125: Commented-Out Code (614 issues)

**Pattern**: Blocks of code are commented out rather than deleted.

**Fix**: Delete the commented-out code. Git history preserves it. If the comment contains a useful note about *why* something was removed or an alternative approach, convert it to a descriptive comment rather than code.

**Heuristic for identification**: SonarCloud flags comments that parse as valid C/SQL statements. Manual review confirms each block is dead code rather than pseudocode or algorithm description.

**Risk**: Very low. No runtime impact. The only risk is losing useful context comments misidentified as code; manual review mitigates this.

### 3.4 S134: Excessive Nesting Depth (637 issues)

**Pattern**: Control structures nested more than 3-4 levels deep, making code hard to follow.

**Fix patterns** (in order of preference):

1. **Early return / guard clause**: Invert a top-level condition and return early.
   ```c
   /* Before */
   if (geom) {
       if (geom->type == POINTTYPE) {
           if (geom->npoints > 0) {
               /* deep logic */
           }
       }
   }

   /* After */
   if (!geom) return;
   if (geom->type != POINTTYPE) return;
   if (geom->npoints <= 0) return;
   /* logic at reduced depth */
   ```

2. **Extract helper function**: Move the inner block to a named helper that describes its purpose.
   ```c
   /* Before: 6 levels deep in lwgeom_split_line */
   /* After: inner 3 levels extracted to split_line_by_point() */
   ```

3. **Merge conditions**: Combine consecutive `if` checks with `&&`.
   ```c
   /* Before */
   if (a) { if (b) { do_thing(); } }
   /* After */
   if (a && b) { do_thing(); }
   ```

**Verification**: Each refactor must pass the existing regression test suite (`make check`). New helper functions must be `static` (file-local) unless they serve a broader API purpose.

**Risk**: Medium. Logic-preserving refactors can introduce subtle bugs if conditions are incorrectly inverted. Each change requires review and test verification.

### 3.5 S1659: Multiple Variable Declarations Per Statement (1,005 issues)

**Pattern**: `int a, b, c;` or `int *p, q;` (where `q` is not a pointer, a common source of bugs).

**Fix**: Split into one declaration per line.
```c
/* Before */
int a, b, c;
double x, y, z;
char *p, *q;

/* After */
int a;
int b;
int c;
double x;
double y;
double z;
char *p;
char *q;
```

**Verification**: Mechanical transformation. Automated via regex or script. Verify compilation succeeds after transformation.

**Risk**: Very low. Pure formatting change. The only subtle case is `int *p, q;` where splitting correctly assigns types (this is actually a bug fix).

### 3.6 S1192: Duplicated String Literals in SQL (1,272 issues)

**Pattern**: The same string literal appears 3+ times in a SQL file.

**Fix strategies**:
- **SQL variables**: For repeated literals within a single function or DO block, declare a variable at the top.
- **Shared definitions**: For literals repeated across files (e.g., error messages, schema names), consider defining them once in a shared SQL include or as a PL/pgSQL constant.
- **Acceptable duplication**: Some duplication in SQL is idiomatic (e.g., repeated `'POLYGON'` type checks). Mark these as acceptable via SonarCloud exclusions if the alternative would harm readability.

**Verification**: Run the regression test suite to confirm SQL changes produce identical results.

**Risk**: Low. SQL variable extraction is mechanical. Care needed to ensure variable scope does not cross transaction boundaries.

### 3.7 OrderByExplicitAscCheck (319 issues)

**Pattern**: `ORDER BY column` without explicit `ASC` or `DESC`.

**Fix**: Add explicit `ASC`:
```sql
-- Before
ORDER BY id
-- After
ORDER BY id ASC
```

**Verification**: No behavioral change -- `ASC` is the default. Run regression tests to confirm expected output files still match (some tests compare output order).

**Risk**: Very low. Pure explicitness improvement.

---

## 4. Directory Prioritization

Remediation proceeds in this priority order within each phase:

### Priority 1: Fork-Owned Code
- `liblwgeom/accel/` -- GPU acceleration code, fully owned by IronGateLabs
- Any new files added by the fork (ECEF/ECI code, geocentric guards)
- Lowest merge conflict risk since upstream does not have these files

### Priority 2: Actively Modified Upstream Code
- Files in `liblwgeom/`, `postgis/`, `topology/` that the fork has modified
- Identified by `git log --diff-filter=M develop..HEAD -- <file>` or files with fork-specific commits
- Moderate merge conflict risk, but we already accept this by modifying these files

### Priority 3: Unmodified Upstream Code
- Files carried from upstream without fork-specific changes
- Highest merge conflict risk since upstream may change these independently
- Only address if the file has BLOCKER/CRITICAL issues or is in a heavily-used code path
- Defer style-only fixes on these files to reduce merge noise

### Priority 4: Test Files
- `regress/`, `*/test/`, `*/cunit/`
- Lowest priority since they do not affect production behavior
- Address only if SonarCloud flags actual bugs in test logic

---

## 5. Duplication Reduction Strategy

Current duplication rate: 7.7% across the codebase.

### Identification

Use SonarCloud's duplication analysis to identify the top duplicated blocks. Focus on:
- Blocks duplicated 3+ times (highest value extraction targets)
- Blocks larger than 10 lines (worth the extraction overhead)
- Blocks within the same directory (easiest to refactor without cross-module dependencies)

### Common Duplication Patterns in PostGIS

1. **LWGEOM type-switch boilerplate**: Many functions have identical `switch(lwgeom->type)` blocks that dispatch to type-specific handlers. Extract a dispatch table or macro.

2. **Error checking sequences**: Repeated patterns of `if (!result) { lwerror("..."); return NULL; }`. Extract a macro like `LWCHECK_NULL(result, "context")`.

3. **SERIALIZED_POINT extraction**: Multiple functions repeat the same byte-manipulation sequence to extract coordinates from serialized geometries. Extract to a shared inline function.

4. **SQL function preamble**: Many PG_FUNCTION_INFO_V1 functions repeat identical argument extraction and validation. Extract common preamble patterns into macros or shared functions.

5. **Raster band iteration**: `raster/rt_pg/` has many functions with identical loops over raster bands. Extract a `for_each_band()` helper or macro.

### Extraction Rules

- New shared helpers go in the same module (e.g., liblwgeom helpers stay in liblwgeom)
- New helpers must be `static` unless needed across compilation units
- Macro helpers go in the appropriate `*_internal.h` header
- Each extraction is a separate commit with clear description of what was deduplicated
- Style-only deduplication (identical code blocks) is separate from logic deduplication (similar-but-not-identical blocks)

### Target

Reduce duplication from 7.7% to below 5%. This is achievable by addressing the top 20 duplicated blocks which likely account for the majority of the duplication percentage.

---

## 6. Rollback Safety

### Commit Series Structure

Each phase produces an independent series of commits on a feature branch:
- `feature/sonar-phase1-tooling`
- `feature/sonar-phase2-dead-stores-empty-stmts`
- `feature/sonar-phase3-nesting-declarations`
- `feature/sonar-phase4-sql-quality`
- `feature/sonar-phase5-bugs-security`

Each branch is merged to `develop` via a PR that must pass the SonarCloud quality gate.

### Revert Strategy

- **Phase 1 (tooling)**: Revert by removing config files. No code changes to revert.
- **Phases 2-4 (mechanical fixes)**: Each phase is a series of commits. If regressions appear after merge, `git revert --no-commit <first>..<last>` reverts the entire series cleanly since these are style-only or mechanical changes.
- **Phase 5 (bug fixes)**: Each bug fix is an individual commit with a regression test. If a fix causes issues, revert the individual commit.

### Regression Detection

- All phases must pass `make check RUNTESTFLAGS="--verbose --extension --raster --topology --sfcgal"` before merge.
- Phase 3 (nesting refactors) and Phase 5 (bug fixes) additionally require manual review of each PR.
- CI runs the full regression suite on every PR to `develop`.

### Upstream Sync Protection

Before each phase, rebase `develop` onto the latest upstream `master`. After each phase merges, verify no upstream merge conflicts by running `git merge --no-commit upstream/master` as a dry run. If conflicts are excessive, defer that phase's changes to unmodified upstream files.
