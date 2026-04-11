## Context

**Background.** PostGIS topology `node` and `edge_data` tables use foreign-key constraints
to enforce referential integrity between edges and their endpoints / bounding faces and
between edges' `next_left_edge` / `next_right_edge` links. Historically, all FKs were
immediate except for `next_left_edge_exists` and `next_right_edge_exists`, which have been
`DEFERRABLE INITIALLY DEFERRED` since well before the workaround because topology
construction code inserts edges whose forward links reference edges not yet inserted in the
same transaction.

**Current state.** On the `feature/fix-topology-crash-latest` branch, commit `a23712f27`
("Fix topology crash on PG 19devel: defer FK constraints to avoid RI fast-path bug") made
the remaining five FK constraints (`face_exists`, `start_node_exists`, `end_node_exists`,
`left_face_exists`, `right_face_exists`) also `DEFERRABLE INITIALLY DEFERRED`, and added an
81-line migration block to `topology_after_upgrade.sql.in` so existing topology schemas
would be altered to match on upgrade. Commit `51e7d6202` added TODO comments explaining the
workaround. Commit `064cbe5c9` marked the `latest` CI job `continue-on-error` because even
with the workaround, PG 19devel snapshots without Amit's fix could still crash in other
code paths.

**Upstream resolution.** On 2026-04-10, Amit Langote committed `34a3078629` ("Fix RI
fast-path crash under nested C-level SPI") to PostgreSQL master. Root cause per Amit's
commit message: the `query_depth > 0` guard in `FireAfterTriggerBatchCallbacks` suppressed
the callback at the nested SPI level, deferring teardown to the outer query's
`AfterTriggerEndQuery`. By then the resource owner active during the SPI call had been
released, decrementing the cached PK relations' refcounts to zero. When
`ri_FastPathTeardown` ran under the outer query's resource owner, it tripped the
`rel->rd_refcnt > 0` assert. The fix scopes batch callbacks to the correct
`AfterTriggersQueryData` (immediate constraints, fired by `AfterTriggerEndQuery`) or
`AfterTriggersData` (deferred constraints, fired at commit) list, replacing the
`query_depth > 0` guard with list-level scoping. Notably Amit's commit message confirms:
*"deferred constraints are unaffected by this bug"* — which is exactly why making the
PostGIS FKs deferred dodged the crash.

**Constraints.**
- PG 19 is still unreleased (PG master / 19devel is a moving target).
- The PostGIS `develop` branch must continue to build and test green against PG master.
- `postgis/postgis-build-env:latest` Docker image bakes PG master at build time, so it
  must be rebuilt to pick up commit `34a3078629`.
- Commit `a23712f27` modified two files (`CreateTopology.sql.in` and the upgrade script);
  both reverts must land together.
- This change lives on `feature/fix-topology-crash-latest`, parallel to `develop`. It
  merges into `develop` once verified.

## Goals / Non-Goals

**Goals:**
- Restore immediate FK constraint semantics on `node.face_exists` and the four
  `edge_data.*_exists` FKs that were changed by the workaround, matching pre-workaround
  PostGIS (and matching upstream `postgis/postgis` which never had the workaround).
- Verify empirically that Amit's PG commit `34a3078629` actually resolves the
  `ri_FastPathTeardown` crash for the PostGIS topology operations that originally triggered
  it, by running the topology regression suite against PG master built from source inside
  `postgis/postgis-build-env:latest`.
- Re-enable the `latest` CI job (remove `continue-on-error`) so PG master regressions are
  caught as blocking failures again.
- Keep the two pre-existing deferred FKs (`next_left_edge_exists`, `next_right_edge_exists`)
  untouched — they were deferred for a different, unrelated reason (forward edge links
  during topology construction).

**Non-Goals:**
- Does **not** benchmark immediate-vs-deferred FK performance for batch topology
  operations. If someone later measures a real win from deferring, that is a separate,
  data-driven change and should go upstream to `postgis/postgis`, not stay in this fork.
- Does **not** touch the two already-deferred FKs.
- Does **not** re-alter existing topology schemas on installations that ran the
  workaround's `topology_after_upgrade.sql.in` migration. Those schemas keep their
  deferred constraints; the behavioral difference (FK errors surface at commit instead of
  at the offending INSERT) is harmless, and forcing another ALTER on upgrade would slow
  topology-heavy upgrades for no correctness benefit.
- Does **not** bump PG or GEOS minimum version requirements.
- Does **not** create or modify any capability spec under `openspec/specs/`. This change
  touches implementation details (a SQL template and CI config), not any capability-level
  contract. The `specs/` directory for this change intentionally stays empty; see
  *Decisions* below.

## Decisions

### D1. Revert via `git revert`, not manual edits.
`git revert --no-edit 51e7d6202 a23712f27` was used to undo both commits on
`feature/fix-topology-crash-latest`. This preserves the history of *why* the workaround
was added and clearly marks its removal with revert commits that reference the originals.
**Alternatives considered.** A squashed manual commit ("Remove topology FK workaround
now that PG fix has landed") would be cleaner in `git log`, but loses the traceability
between the original workaround and its removal. For a workaround tied to a specific
upstream fix, traceability wins.

### D2. Leave the upgrade-path migration deleted, not inverted.
The reverted `topology_after_upgrade.sql.in` block only ran once per upgrade — it
already executed on any installation that upgraded to `feature/fix-topology-crash-latest`.
Inverting it (adding a new block that ALTERs the constraints back to immediate) would
(a) only affect installations that ran the workaround, (b) impose an ALTER cost on every
topology schema on upgrade even though the current deferred state is functionally
harmless, and (c) require us to track which installations need the inversion.
**Decision.** Skip the inversion. Document in the revert commit message that existing
installations keep deferred constraints; fresh installs get immediate.
**Alternatives considered.** A guarded ALTER that only runs if the constraint is
currently deferred — rejected because topology FK semantics are invisible to normal user
queries and the inconsistency is harmless.

### D3. Verify against a freshly rebuilt `postgis-build-env:latest`.
Rather than trusting a pinned PG master SHA, rebuild the Docker image with
`POSTGRES_BRANCH=master` at test time and verify post-build that the baked
`src/backend/utils/adt/ri_triggers.c` contains Amit's fix (or verify by PG commit log
inside the container). This catches the case where the PG master on the day of the test
does not yet include Amit's commit (e.g., if we run this before 2026-04-10 on a clock-
skewed box).
**Alternatives considered.** Pin `POSTGRES_BRANCH` to a specific SHA after `34a3078629`.
Rejected because it diverges from the build-env's philosophy of tracking upstream master
for the `latest` env; a pin would be a separate behavior change to negotiate with the
PostGIS project.

### D4. No `openspec/specs/` delta.
The `spec-driven` schema scaffolds a `specs/` directory and expects it to be populated.
For this change, no capability in `openspec/specs/` covers topology FK constraint
semantics (existing specs cover CRS taxonomy, ECEF/ECI accessors, spatial-index
verification, etc.). The workaround was never spec'd in the first place because it was an
implementation-detail workaround for a PG bug. Removing it is also an implementation
detail. Creating a stub spec solely to satisfy the schema would be ceremony without
content.
**Decision.** Leave `specs/` empty. `tasks.md` will note this explicitly so the validator
(and future archaeologists) see it's intentional, not an oversight. If the validator
hard-fails on empty `specs/`, we add a one-line placeholder file documenting the
intentional no-op.

### D5. Re-enable the `latest` CI job only after the regression passes locally.
Removing `continue-on-error` from the `latest` CI job is staged **after** the local
topology regression passes against the rebuilt image. This avoids turning an uncertain
fix into blocking CI failures for every unrelated PR.

## Risks / Trade-offs

- **Risk:** PG master drifts and a *different* regression lands between `34a3078629` and
  the day this change merges, causing the `latest` CI job to fail for unrelated reasons
  once `continue-on-error` is removed.
  **Mitigation:** Rebuild `postgis-build-env:latest` as close to merge time as possible
  and run the full regression (not just topology) before removing `continue-on-error`. If
  an unrelated PG master breakage is detected, keep `continue-on-error` temporarily and
  file a tracking issue.

- **Risk:** The topology regression suite doesn't actually exercise the crash path that
  `a23712f27` was originally fixing — i.e., the regression passes but a user running
  `toTopoGeom` at scale on PG 19devel still hits a different variant of the crash.
  **Mitigation:** Cross-check which specific topology tests previously crashed on the
  workaround branch (see `f393f6017` "Document thorough investigation"). Ensure those
  tests are exercised explicitly in the verification run. If the original crash repro was
  ad-hoc, construct a minimal SPI-in-a-function reproducer and run it against the rebuilt
  image before declaring the fix verified.

- **Risk:** Installations that ran the workaround's upgrade migration now have
  permanently deferred FK constraints on `node`/`edge_data`, divergent from fresh
  installs. A user's error-handling code might now observe FK violations at commit rather
  than at INSERT, causing a subtle behavior change.
  **Mitigation:** Document this explicitly in the revert commit message and the change
  notes. The behavior difference is consistent with standard PG deferred-constraint
  semantics and is not a correctness bug. Users who want to re-align can manually ALTER
  the constraints themselves; we do not force the migration.

- **Risk:** The `latest` Docker image rebuild hits the flaky `curl codeload.github.com`
  issue in the nlohmann/json step (already observed once during this change).
  **Mitigation:** Retry; if persistent, file a separate issue against
  `postgis-build-env` to pin the nlohmann/json version and fetch from a more reliable
  source (or vendor the tarball into the image build context). Does not block this
  change — a successful rebuild is all we need.

- **Trade-off:** We're trusting Amit's fix without adding our own PG-side regression
  test. If the fix regresses in a future PG master commit, we find out via our own
  topology CI, not via a targeted PG test.
  **Acceptable because:** PostGIS CI's `latest` job is exactly the signal we want, and
  once `continue-on-error` is removed, it becomes a blocking gate.

## Migration Plan

**Deployment order (PostGIS side):**
1. Rebuild `postgis/postgis-build-env:latest` with fresh PG master and verify the image
   contains Amit's commit.
2. In the `feature/fix-topology-crash-latest` worktree, run `make check RUNTESTFLAGS=
   "--extension --topology --verbose"` against the rebuilt image. Confirm no
   `rd_refcnt > 0` assert and no `ri_FastPathTeardown` crash.
3. If tests pass, the worktree's two revert commits (`b42524d05`, `664b8d5ca`) are the
   merge candidate. If tests fail, diagnose before proceeding — do not merge.
4. Remove `continue-on-error: ${{ matrix.name == 'latest' }}` (or equivalent) from
   `.github/workflows/ci.yml` in a follow-up commit on the same branch.
5. Rebase or merge `feature/fix-topology-crash-latest` into `develop`.
6. Push the rebuilt `postgis/postgis-build-env:latest` image (separate concern, handled
   by the build-env repo's CI / manual push).

**Rollback:** If post-merge issues appear on PG 19devel specifically, revert the two
revert commits (`git revert b42524d05 664b8d5ca`) to restore the workaround. The TODO
comments and migration block come back with it. CI's `continue-on-error` flag would also
need to be restored in the same rollback commit.

**No dependency version bumps**, no SQL API surface changes, no upgrade-path hooks to
add. Patch-release-safe in principle, though this change is scoped to `develop` because
the workaround only ever existed on the `feature/fix-topology-crash-latest` branch and
never shipped in a tagged release.

## Open Questions

- **Is there a PG master SHA we should pin the `latest` build-env to until `34a3078629`
  propagates into `stable_pg18`-style images?** Current `build.py` uses `PG=master` for
  `latest` which is fine, but if we want downstream stability we may want to pin. Flag
  to the user for decision.
- **Should the build-env `nlohmann/json` fetch be vendored / pinned?** Orthogonal to this
  change but was a real blocker during verification. Track as a separate issue.
- **Does the `develop` branch CI job separately need a rerun after the build-env image
  is pushed?** Yes, but that's standard CI hygiene, not a design question.
