# Spec: Pre-commit Hooks

## Purpose

Define requirements for local pre-commit hook infrastructure that prevents new code quality issues from reaching the repository.

## Requirements

### HOOK-001: Pre-commit Framework Installation

- The repository MUST contain a `.pre-commit-config.yaml` at the project root.
- The configuration MUST be compatible with the [pre-commit](https://pre-commit.com/) framework version 3.x or later.
- A developer MUST be able to install hooks by running `pip install pre-commit && pre-commit install` from the repository root.
- The `CONTRIBUTING.md` or equivalent documentation MUST include setup instructions for the hooks.

### HOOK-002: clang-format Hook

- The hook MUST run `clang-format` on all staged `*.c` and `*.h` files.
- The hook MUST use the existing `.clang-format` configuration file at the repository root.
- The hook MUST operate in **check mode** (report violations, do not auto-fix). This means using `--dry-run --Werror` or the pre-commit `--check` flag.
- The hook MUST fail the commit if any staged C/H file does not conform to the `.clang-format` rules.
- The hook MUST exclude files matching these patterns:
  - `deps/**`
  - `regress/**`
  - `doc/**`
- The hook MUST respect the existing topology exception (2-space indent for `topology/*.{c,h}` and `liblwgeom/topo/**`) as defined by the project `.clang-format` or directory-level overrides.

### HOOK-003: cppcheck Hook (Advisory)

- The hook MUST run `cppcheck` on all staged `*.c` files.
- The hook MUST enable the `warning` and `performance` check categories.
- The hook MUST suppress `missingIncludeSystem` to avoid false positives from system headers.
- The hook MUST run in **advisory mode**: it reports findings to stderr but does NOT block the commit (exit code 0 regardless of findings).
- The hook MUST exclude the same file patterns as the clang-format hook.
- The hook SHOULD be configurable to switch to blocking mode by changing `--error-exitcode=0` to `--error-exitcode=1`.

### HOOK-004: Bypass Mechanism

- Developers MUST be able to bypass all hooks using the standard git mechanism: `git commit --no-verify`.
- Bypass is intended for emergency situations only (e.g., hotfix on a file with pre-existing violations).
- The CI quality gate (see sonarcloud-ci-gate spec) catches issues regardless of local hook bypass.

### HOOK-005: Make Lint Target

- The project MUST provide a `make lint` target at the top level.
- `make lint` MUST run the same clang-format and cppcheck checks as the pre-commit hooks, but across ALL source files (not just staged files).
- The target MUST scan files in: `liblwgeom/`, `postgis/`, `raster/`, `topology/`, `sfcgal/`, `loader/`, `libpgcommon/`.
- The target MUST NOT be part of the default `make` or `make all` build to avoid slowing regular development.
- The clang-format portion of `make lint` MUST return a non-zero exit code if violations are found.
- The cppcheck portion of `make lint` MUST report findings but MAY return exit code 0 (advisory).

### HOOK-006: Performance

- The pre-commit hooks MUST only process files staged for commit, NOT the entire codebase.
- Hook execution time for a typical commit (1-10 files) SHOULD complete in under 5 seconds on modern hardware.
- The `make lint` target (full codebase scan) SHOULD complete in under 60 seconds.

## Acceptance Criteria

1. Running `pre-commit install` succeeds and configures `.git/hooks/pre-commit`.
2. Committing a C file with intentional formatting violations is blocked by the clang-format hook.
3. Committing a C file with a cppcheck warning produces a warning on stderr but the commit succeeds.
4. `git commit --no-verify` bypasses both hooks.
5. `make lint` runs successfully and reports the same class of issues as the hooks.
6. The hooks do not interfere with non-C commits (e.g., SQL-only changes, documentation changes).
