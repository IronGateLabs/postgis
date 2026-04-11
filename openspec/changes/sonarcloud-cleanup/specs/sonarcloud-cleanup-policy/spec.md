## ADDED Requirements

### Requirement: Four-way SonarCloud issue classification

Every SonarCloud-reported issue on the IronGateLabs_postgis project SHALL be classifiable into exactly one of four buckets, and each bucket SHALL have a documented remediation action.

The four classes are:

- **`REAL`** — A bug, code smell, or vulnerability in first-party, non-test, non-vendored source code. Remediation: fix via a focused PR.
- **`TEST FIXTURE`** — An intentional violation in a test file where the "bug" is the test's purpose. Remediation: exclude the containing path in `sonar-project.properties`.
- **`VENDORED`** — Code maintained by an upstream project that PostGIS imports verbatim. Remediation: exclude the path; if a real bug is found, push the fix upstream rather than carry a local patch.
- **`FALSE POSITIVE`** — The analyzer's pattern matcher or abstract interpretation flagged correct code. Remediation: add an inline `// NOSONAR - <reason>` comment documenting why the flag is wrong.

#### Scenario: Classification is exhaustive

- **WHEN** any SonarCloud-reported issue on the project is triaged
- **THEN** the triage SHALL assign it to exactly one of `REAL`, `TEST FIXTURE`, `VENDORED`, or `FALSE POSITIVE`
- **AND** if an issue does not clearly fit one of these buckets, the triage SHALL add a classification case or split an existing bucket — not leave the issue unclassified

#### Scenario: Classification is recorded with the fix

- **WHEN** a focused cleanup PR addresses a SonarCloud issue
- **THEN** the commit message SHALL state the issue's classification explicitly (e.g., "classified REAL, fix by adding NULL check")
- **AND** `FALSE POSITIVE` classifications SHALL manifest as `// NOSONAR - <reason>` comments at the flagged line(s) with an explanation sufficient for a future maintainer to verify the reason still holds

### Requirement: Path exclusions for TEST FIXTURE and VENDORED code

The `sonar-project.properties` file SHALL list all paths containing TEST FIXTURE or VENDORED code under the `sonar.exclusions` directive. The list SHALL be maintained so that adding new test files or new vendored dependencies is accompanied by a corresponding exclusion entry.

#### Scenario: Test fixture directories are excluded

- **WHEN** `sonar-project.properties` is read
- **THEN** its `sonar.exclusions` value SHALL include patterns covering all of: `regress/**`, `topology/test/**`, `raster/test/**`, `**/cunit/**`, `**/test/**`, `fuzzers/**`, `extensions/**/sql/**`, `extras/ogc_test_suite/**`, `doc/html/images/styles.c`

#### Scenario: Vendored source files are excluded

- **WHEN** `sonar-project.properties` is read
- **THEN** its `sonar.exclusions` value SHALL include patterns covering all of: `deps/**`, `liblwgeom/lookup3.c`, `liblwgeom/lwin_wkt_lex.c`, `liblwgeom/lwin_wkt_parse.c`, `loader/dbfopen.c`, `loader/getopt.c`
- **AND** if a new vendored file is added to the project (e.g., importing a new upstream dependency into `deps/`), the same PR SHALL add a corresponding exclusion entry

#### Scenario: Exclusions do not hide real bugs

- **WHEN** Phase 1 of this cleanup adds path exclusions for the first time
- **THEN** the focused PR SHALL include a review step demonstrating that the excluded paths do not contain any REAL issues that would be silently hidden
- **AND** if a real bug is found in a path about to be excluded, the exclusion pattern SHALL be narrowed to preserve the flag, OR the bug SHALL be fixed before the exclusion lands

### Requirement: NOSONAR markers include explanatory comment

Every `// NOSONAR` marker added by this cleanup SHALL include a phrase explaining the reason SonarCloud's flag is incorrect, formatted as `// NOSONAR - <reason>`.

#### Scenario: Bare NOSONAR comments are rejected

- **WHEN** a cleanup PR adds a `// NOSONAR` comment
- **THEN** the comment SHALL NOT consist of only `// NOSONAR` with no reason
- **AND** code review SHALL reject the change if the reason phrase is missing

#### Scenario: Reason phrase is maintained when code changes

- **WHEN** a future PR modifies code that contains a `// NOSONAR - <reason>` comment
- **THEN** the modifier SHALL verify the reason still holds
- **AND** if the reason no longer holds, the modifier SHALL either remove the NOSONAR (letting SonarCloud re-flag) or update the reason to match the new code

### Requirement: Phased cleanup with explicit ordering

The cleanup initiative SHALL execute in phases with explicit dependencies. Phases SHALL NOT be bundled into a single mega-PR; each phase gets its own focused implementation PR (which may have its own OpenSpec change if the scope warrants).

#### Scenario: Phases execute in order with documented dependencies

- **WHEN** a new cleanup PR is opened
- **THEN** it SHALL correspond to exactly one phase defined in this change's `design.md`
- **AND** it SHALL NOT start before its documented dependencies have landed (e.g., Phase 3 depends on Phase 1, so memory-safety fixes cannot merge until path exclusions have reduced dashboard noise)
- **AND** the PR description SHALL reference this cleanup change and identify its phase

#### Scenario: Each phase has a success criterion

- **WHEN** a phase completes
- **THEN** the phase's entry in `tasks.md` SHALL be checked off
- **AND** a link to the merged focused PR SHALL be added inline next to the checked item
- **AND** the change itself SHALL NOT be archived — it remains in `openspec/changes/` as the living roadmap

### Requirement: Baseline capture and progress tracking

The cleanup initiative SHALL capture a reference baseline of SonarCloud metrics at the start (2026-04-11 values) and SHALL track progress against that baseline at each phase boundary.

#### Scenario: Baseline is documented in design.md

- **WHEN** `design.md` is reviewed
- **THEN** it SHALL contain a "Findings snapshot" section with the 2026-04-11 metric values (bugs, vulnerabilities, code smells, blockers, technical debt, top rules by count, top directories by count) and the agent-investigation findings (3 vulns, 22 real blockers, etc.)
- **AND** this snapshot SHALL serve as the reference point against which all subsequent progress is measured

#### Scenario: Progress is tracked at each phase completion

- **WHEN** a phase is marked complete in `tasks.md`
- **THEN** the phase's entry SHALL include the post-phase metric values (blocker count, bug count, etc.)
- **AND** the metric delta from the phase's start SHALL be visible (e.g., "blockers: 152 → 22 after Phase 1")
- **AND** if a phase's actual delta diverges significantly from the expected delta in the plan, a note SHALL be added explaining why

### Requirement: Future SonarCloud issues are triaged under the same policy

The classification model SHALL apply to all future SonarCloud-reported issues, not just the 2026-04-11 backlog snapshot. As new issues appear (either because new code is written or because SonarCloud adds new rules), they SHALL be classified using the same four-way model and added to the appropriate phase (typically Phase 3 or Phase 6 depending on severity).

#### Scenario: New code quality gate prevents backlog regrowth

- **WHEN** a new PR introduces a SonarCloud issue that classifies as REAL
- **THEN** the issue SHALL be fixed in the same PR, not deferred to the backlog
- **AND** the SonarCloud "New Code" quality gate SHOULD be configured to block PR merge on new REAL issues (pending Phase 0 completion so the CI-driven analysis can run)

#### Scenario: New vendored dependency triggers exclusion update

- **WHEN** a new upstream dependency is vendored into `deps/` or elsewhere
- **THEN** the PR that adds the dependency SHALL also add the exclusion pattern to `sonar-project.properties`
- **AND** the PR description SHALL note the classification reasoning
