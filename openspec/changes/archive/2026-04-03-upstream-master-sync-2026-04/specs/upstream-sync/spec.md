## ADDED Requirements

### Requirement: Master branch tracks upstream
The fork's `master` branch SHALL be a fast-forward of `upstream/master` with zero divergent commits.

#### Scenario: Fast-forward sync
- **WHEN** `git merge-base --is-ancestor origin/master upstream/master` returns true
- **THEN** `git merge --ff-only upstream/master` on `master` SHALL succeed

### Requirement: Develop branch rebased onto updated master
The `develop` branch SHALL be rebased onto the updated `master` so that all fork-specific commits sit on top of the latest upstream.

#### Scenario: Clean rebase
- **WHEN** `git rebase master` is run on `develop`
- **THEN** all fork-specific commits SHALL replay cleanly (with manual conflict resolution if needed)
- **THEN** `git log master..develop` SHALL show only fork-specific commits

### Requirement: Merged feature branches deleted
Feature branches fully merged into `develop` SHALL be removed from the remote to reduce branch clutter.

#### Scenario: Branch cleanup
- **WHEN** a feature branch has zero commits not in `develop` (verified via `git log develop..<branch>`)
- **THEN** the branch SHALL be deleted from `origin` via `git push origin --delete <branch>`

### Requirement: Build verification after sync
The codebase SHALL compile and pass basic regression tests after the rebase.

#### Scenario: Post-rebase build
- **WHEN** the rebase is complete
- **THEN** `./configure && make` SHALL complete without errors
- **THEN** `make check` SHALL pass (or match upstream's known test state)

### Requirement: PR for traceability
The sync SHALL be submitted as a pull request on the fork for team review.

#### Scenario: PR creation
- **WHEN** the rebase is complete and pushed
- **THEN** a PR SHALL be created targeting `develop` with a summary of upstream changes incorporated
