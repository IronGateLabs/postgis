## Context

Our fork (IronGateLabs/postgis) diverged from upstream (postgis/postgis) `master` while developing ECEF/ECI coordinate support, GPU acceleration, and geocentric spatial guards on the `develop` branch. Upstream has since landed 94 commits including new spatial functions, PG19 support, security hardening, and build modernization. Three feature branches that were merged into `develop` remain on the remote and need cleanup.

## Goals / Non-Goals

**Goals:**
- Fast-forward `origin/master` to match `upstream/master`
- Rebase `develop` cleanly onto the updated `master`
- Delete stale merged feature branches from the remote
- Verify no regressions in our ECEF/ECI/GPU code after rebase
- Submit the sync as a PR on the fork for traceability

**Non-Goals:**
- Submitting any work upstream to postgis/postgis (separate future effort)
- Resolving the `feature/ci-fixes` branch (has unmerged commits, handle separately)
- Modifying any upstream code — this is a pure sync

## Decisions

### 1. Fast-forward master, then rebase develop
**Decision**: Fast-forward `master` to `upstream/master`, then `git rebase master` on `develop`.

**Rationale**: Our `master` has no unique commits vs upstream, so fast-forward is clean. Rebasing `develop` (rather than merging) keeps our commit history linear and makes future upstream PRs cleaner.

**Alternative considered**: Merge `upstream/master` into `develop` directly. Rejected because it creates a merge commit that complicates future cherry-picks to upstream.

### 2. Delete only fully-merged feature branches
**Decision**: Delete `feature/ecef-eci-extension-test`, `feature/eop-enhanced-transforms`, `feature/eop-tests-and-docs`. Keep `feature/ci-fixes` (has 9 unmerged commits).

**Rationale**: Only clean up branches confirmed to have zero unique commits vs `develop`.

### 3. PR on fork for the sync
**Decision**: Create a PR from a dedicated branch (e.g., `sync/upstream-master-2026-04`) targeting `develop` after rebase, so the team can review conflict resolutions.

**Rationale**: Even though this is a sync, the rebase may introduce conflict resolutions that benefit from review.

## Risks / Trade-offs

- **[Rebase conflicts in shared files]** → Mitigation: Our ECEF/ECI work is mostly additive (new files). Conflicts likely limited to `Makefile.in`, CI configs, or shared C files. Resolve conservatively, keeping upstream changes and re-applying our additions.
- **[K&R style removal conflicts]** → Mitigation: If upstream changed `func()` → `func(void)` in files we also modified, adopt the upstream style in our code too.
- **[Build breakage after rebase]** → Mitigation: Run `./configure && make` after rebase to verify compilation. Run regression tests if CI is available.
- **[SSH auth not configured]** → Mitigation: Need to resolve SSH key access to push to `origin`. May need to switch to `gh` CLI auth or add SSH key.
