# Spec: Code Quality Standards

## Purpose

Define the specific SonarCloud rules being enforced, their fix patterns, and the standards for remediation commits across the PostGIS codebase.

## Requirements

### RULE-001: S1854 -- Dead Store Removal

- All assignments where the assigned value is never subsequently read MUST be removed.
- Exception: If the right-hand side of the assignment has side effects (e.g., a function call), the function call MUST be preserved. Only the assignment is removed:
  ```c
  /* Before: dead store with side effect */
  int ret = close(fd);
  /* After: preserve call, discard return */
  (void)close(fd);
  ```
- Exception: If removing the initializer would trigger a compiler warning about use of an uninitialized variable on any supported compiler (GCC, Clang), the initializer MUST be kept.
- Fixes MUST be verified by compiling with `-Wall -Werror` to confirm no new warnings.
- Dead store fixes are classified as **style-only** changes and MUST be in separate commits from logic changes.

### RULE-002: S1116 -- Empty Statement Removal

- Stray semicolons after `if`, `for`, `while`, and `else` that create unintended empty statement bodies MUST be removed.
- If the empty body is **intentional** (e.g., a busy-wait loop `while (condition);`), the code MUST be rewritten with an explicit empty block and comment:
  ```c
  while (condition) {
      /* intentionally empty - waiting for condition */
  }
  ```
- Each fix MUST be manually verified to confirm the semicolon is truly stray. Automated regex matching alone is insufficient because some empty bodies are intentional.
- Empty statement fixes are classified as **style-only** changes.

### RULE-003: S125 -- Commented-Out Code Removal

- Blocks of commented-out code (code that would parse as valid C or SQL if uncommented) MUST be deleted.
- If the comment contains explanatory context about why code was removed or an alternative algorithm, it MUST be rewritten as a descriptive prose comment rather than deleted entirely. Example:
  ```c
  /* Before */
  /* result = lwgeom_force_3d(geom); */

  /* After - if context is useful */
  /* 3D forcing removed in v3.5 because ... */

  /* After - if no useful context */
  /* (deleted entirely) */
  ```
- Git history preservation is sufficient justification for deletion. The commit message SHOULD reference the original commit hash where the code was active if easily determined.
- Commented-out code removal is classified as **style-only** changes.

### RULE-004: S134 -- Nesting Depth Reduction

- Functions with control structure nesting deeper than 4 levels MUST be refactored to reduce depth.
- Acceptable refactoring techniques (in order of preference):
  1. **Early return / guard clause**: Invert conditions and return early to reduce indentation.
  2. **Extract helper function**: Move deeply nested logic into a `static` helper function with a descriptive name.
  3. **Merge conditions**: Combine adjacent `if` conditions using `&&` or `||`.
  4. **Restructure control flow**: Replace nested if/else chains with switch statements or lookup tables where appropriate.
- Extracted helper functions MUST be `static` (file-local scope) unless there is a documented reason for wider visibility.
- Nesting depth refactors are classified as **logic changes** (even if behavior-preserving) and MUST be in separate commits from style-only changes.
- Each refactored function MUST pass all existing regression tests before the commit is accepted.

### RULE-005: S1659 -- Single Variable Per Declaration

- Each variable declaration statement MUST declare exactly one variable.
- Multi-variable declarations MUST be split into separate statements:
  ```c
  /* Before */
  int a, b, c;
  /* After */
  int a;
  int b;
  int c;
  ```
- Special attention for pointer declarations where the type is ambiguous:
  ```c
  /* Before - b is int, not int* */
  int *a, b;
  /* After - types are explicit */
  int *a;
  int b;
  ```
- Variable declaration splits are classified as **style-only** changes.
- The order of declarations after splitting MUST preserve the original order.

### RULE-006: S1192 -- SQL String Literal Deduplication

- String literals that appear 3 or more times in a single SQL file SHOULD be extracted into a variable or constant.
- Extraction is NOT required when:
  - The literal is a type name used in type checks (e.g., `'POINT'`, `'POLYGON'`) and extraction would reduce readability.
  - The literal appears in different transactional contexts where a variable would not be in scope.
  - The literal is part of a generated/templated SQL file where the repetition is structural.
- For SQL files using PL/pgSQL, repeated literals SHOULD be extracted to `CONSTANT` declarations:
  ```sql
  DECLARE
      schema_name CONSTANT text := 'topology';
  ```
- For plain SQL files, consider whether the duplication is inherent to the SQL structure before refactoring.
- SQL deduplication fixes MUST pass the full regression test suite.

### RULE-007: OrderByExplicitAscCheck -- Explicit Sort Direction

- All `ORDER BY` clauses MUST specify an explicit sort direction (`ASC` or `DESC`).
- The fix is mechanical: add `ASC` where no direction is specified (since `ASC` is the SQL default).
- Expected output files in `regress/` that contain `ORDER BY` results MAY need updating if the regression test framework includes the query in its output.

### COMMIT-001: Commit Discipline

- **Style-only commits** (formatting, dead stores, empty statements, commented code removal, declaration splitting, explicit ASC) MUST be separate from **logic commits** (nesting refactors, bug fixes, deduplication involving new helper functions).
- Each commit MUST address a single rule within a single directory grouping. Example commit scopes:
  - "Remove dead stores in liblwgeom/ (S1854)"
  - "Fix empty statements in postgis/ (S1116)"
  - "Refactor nesting in lwgeom_geos.c (S134)"
- Commit messages MUST reference the SonarCloud rule ID.
- Commits MUST NOT mix remediation of different rules (e.g., do not fix S1854 and S1116 in the same commit).

### COMMIT-002: Verification Requirements

- All remediation commits MUST compile cleanly with the project's standard flags.
- All remediation commits MUST pass `make check` (the full regression test suite).
- Phase 3 (nesting refactors) and Phase 5 (bug fixes) commits MUST additionally receive manual code review before merge.
- Phase 2 (mechanical fixes) commits MAY be merged with automated review only if the CI suite passes.

### PRIORITY-001: Directory Prioritization

Remediation within each rule MUST follow this priority order:
1. Fork-owned code (`liblwgeom/accel/`, fork-specific additions)
2. Actively modified upstream code (files with fork commits)
3. Unmodified upstream code (defer if merge conflict risk is high)
4. Test files (`regress/`, `*/test/`, `*/cunit/`)

## Acceptance Criteria

1. Each rule has at least one commit demonstrating the fix pattern in a fork-owned file.
2. No remediation commit introduces new compiler warnings under `-Wall`.
3. No remediation commit causes a regression test failure.
4. Style-only commits contain zero logic changes; logic commits contain zero style-only changes.
5. SonarCloud issue count decreases after each phase is merged to `develop`.
