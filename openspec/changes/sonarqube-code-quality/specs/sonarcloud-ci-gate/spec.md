# Spec: SonarCloud CI Quality Gate

## Purpose

Define requirements for continuous integration quality gate enforcement via SonarCloud, ensuring no new code quality regressions reach the `develop` branch.

## Requirements

### GATE-001: GitHub Actions Workflow

- A workflow file MUST exist at `.github/workflows/sonar.yml`.
- The workflow MUST trigger on:
  - Pull requests targeting the `develop` branch.
  - Pushes to the `develop` branch (for baseline analysis updates).
- The workflow MUST check out the repository with `fetch-depth: 0` (full git history) to enable SonarCloud blame and branch analysis.
- The workflow MUST run the `make lint` target before SonarCloud analysis so formatting issues are caught early and fail fast.

### GATE-002: SonarCloud Scanner Configuration

- A `sonar-project.properties` file MUST exist at the project root.
- The file MUST configure:
  - `sonar.projectKey=IronGateLabs_postgis`
  - `sonar.organization=irongatelabs`
  - `sonar.sources` set to: `liblwgeom,postgis,raster,topology,sfcgal,loader,libpgcommon`
  - `sonar.exclusions` set to: `regress/**,deps/**,doc/**,**/cunit/**,**/test/**,**/*_expected,extensions/**/*.sql`
  - `sonar.sourceEncoding=UTF-8`
- The workflow MUST use the `sonarsource/sonarcloud-github-action` official action for scanning.

### GATE-003: Authentication

- The SonarCloud token MUST be stored as a GitHub Actions repository secret named `SONAR_TOKEN`.
- The token MUST NOT appear in any committed file, log output, or workflow artifact.
- The workflow MUST pass the token to the scanner via the `SONAR_TOKEN` environment variable.
- An additional secret `GITHUB_TOKEN` (auto-provided by GitHub Actions) MUST be passed for PR decoration (status checks and comments).

### GATE-004: Quality Gate Thresholds

The following thresholds MUST be configured on the SonarCloud project quality gate for **new code** (code changed in the PR):

| Metric | Condition | Threshold |
|--------|-----------|-----------|
| New Bugs | is greater than | 0 |
| New Vulnerabilities | is greater than | 0 |
| New Security Hotspots Reviewed | is less than | 100% |
| Maintainability Rating on New Code | is worse than | A |
| Reliability Rating on New Code | is worse than | A |
| Security Rating on New Code | is worse than | A |

- The quality gate MUST apply only to new code, NOT to the entire codebase. This allows incremental improvement without blocking all PRs due to legacy issues.
- The "new code" definition MUST be set to "Previous version" mode, comparing PR code against the `develop` branch baseline.

### GATE-005: Branch Analysis

- SonarCloud MUST be configured to treat `develop` as the main analysis branch for the fork.
- PR analysis MUST compare new code against the `develop` baseline (not `master`, which tracks upstream).
- The `develop` branch analysis MUST update automatically on each push to `develop`.
- Long-lived feature branches (e.g., `feature/sonar-phase2-*`) SHOULD also be analyzed when PRs are opened against `develop`.

### GATE-006: Required Status Check

- The SonarCloud quality gate MUST be configured as a **required status check** on the `develop` branch protection rules in GitHub.
- PRs that fail the quality gate MUST NOT be mergeable (merge button blocked).
- Repository administrators MAY override the check in exceptional circumstances using GitHub's admin merge capability.

### GATE-007: PR Decoration

- SonarCloud MUST post a summary comment on each PR with:
  - Number of new issues by category (bugs, vulnerabilities, code smells, security hotspots).
  - Quality gate pass/fail status.
  - Link to the full analysis on SonarCloud.
- SonarCloud MUST set a commit status check visible in the PR checks tab.

### GATE-008: Workflow Performance

- The SonarCloud analysis workflow SHOULD complete in under 10 minutes for a typical PR.
- The workflow MUST NOT interfere with other CI workflows (build, test).
- The workflow SHOULD run in parallel with other CI jobs, not sequentially.

## Acceptance Criteria

1. Opening a PR against `develop` triggers the SonarCloud workflow.
2. The workflow runs `make lint` and then the SonarCloud scanner.
3. SonarCloud posts a quality gate status check on the PR.
4. A PR introducing a new bug or vulnerability is blocked from merging.
5. A PR with only clean new code passes the quality gate and is mergeable.
6. Pushing to `develop` updates the SonarCloud baseline analysis.
7. The SONAR_TOKEN secret is used but never exposed in logs.
