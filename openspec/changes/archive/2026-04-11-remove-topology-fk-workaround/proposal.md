## Why

PostGIS carries a workaround in `topology/sql/manage/CreateTopology.sql.in` that marks five
foreign-key constraints on the `node` and `edge_data` tables as `DEFERRABLE INITIALLY DEFERRED`
to dodge a PostgreSQL 19devel assertion failure in `ri_FastPathTeardown` (`rd_refcnt > 0`) that
fired when topology SPI operations like `toTopoGeom` and `TopoGeo_Add*` inserted edges with
immediate FK constraints. The workaround shipped in commit `a23712f27` with documentation in
`51e7d6202`, and the `latest` CI job was marked `continue-on-error` in `064cbe5c9` to
unblock merges while the root cause was investigated upstream.

Upstream PostgreSQL commit `34a3078629` ("Fix RI fast-path crash under nested C-level SPI"
by Amit Langote, 2026-04-10) addresses the root cause: the `query_depth > 0` guard in
`FireAfterTriggerBatchCallbacks` was deferring teardown to the outer query, by which time the
resource owner under which the PK relations were opened had been released, decrementing their
cached refcounts to zero. The fix scopes batch callbacks to the correct
`AfterTriggersQueryData` / `AfterTriggersData` list so teardown always runs under a live
resource owner. With the upstream fix merged to PG master, the PostGIS-side workaround is no
longer needed and should be removed so topology FK semantics match upstream PostGIS and
prior PG versions.

## What Changes

- Revert `a23712f27` (workaround) and `51e7d6202` (TODO comments), restoring immediate FK
  constraints on `node.face_exists`, `edge_data.start_node_exists`, `end_node_exists`,
  `left_face_exists`, and `right_face_exists`. The two pre-existing deferred constraints
  (`next_left_edge_exists`, `next_right_edge_exists`) are untouched — they were already
  deferred before the workaround and remain so.
- Remove the `topology_after_upgrade.sql.in` migration block that altered existing topology
  FK constraints to deferrable on upgrade. Existing installations that ran the workaround
  upgrade keep their deferred constraints (harmless, just slightly different semantics from
  a fresh install); a separate cleanup could re-alter them back later if desired.
- Re-enable the `latest` CI job by removing the `continue-on-error` flag added in `064cbe5c9`.
  The `latest` job pulls PG master, which now contains Amit's fix.
- Rebuild `postgis/postgis-build-env:latest` so CI containers pick up the fixed PG master.
- Verify via topology regression (`make check RUNTESTFLAGS="--extension --topology"`) against
  the rebuilt container that the `ri_FastPathTeardown` crash no longer occurs with immediate
  FK constraints.
- Document the tie-back to upstream PG commit `34a3078629` in the revert commit message so
  future archaeologists can trace the history.

Not a **BREAKING** change for users: topology FK semantics return to what they were before
the workaround, which matches all non-PG-19devel deployments and upstream PostGIS.

## Capabilities

### New Capabilities

- `topology-fk-constraints`: Formalizes the referential-integrity contract between
  topology `node`, `face`, and `edge_data` tables. Prior to this change the FK semantics
  were implicit in the SQL template; no spec captured which constraints are immediate vs.
  deferred or why. The workaround and its removal both hinge on these semantics, so
  making them an explicit capability gives future changes a contract to guard against.

### Modified Capabilities

None. No existing spec in `openspec/specs/` covers topology FK constraint semantics.

## Impact

- **Code**: `topology/sql/manage/CreateTopology.sql.in` (revert FK constraint changes),
  `topology/topology_after_upgrade.sql.in` (revert the 81-line upgrade block),
  `.github/workflows/ci.yml` (remove `continue-on-error` from the `latest` job and update
  the accompanying diagnostic comment).
- **Build infrastructure**: `postgis/postgis-build-env:latest` Docker image must be
  rebuilt so the baked PG master includes commit `34a3078629`.
- **Minimum PG 19devel requirement**: Once this change lands, PostGIS `develop` built
  against a PG 19devel snapshot older than 2026-04-10 will crash on topology SPI operations
  again. This is acceptable because PG 19devel is a moving target and consumers are
  expected to track its master; the CI job tracks PG master and will catch regressions.
- **No dependency version bumps**: the required PG version range in `configure.ac` is
  unchanged (PostgreSQL 12+). PG 19 is still unreleased.
- **Tracking issues**: closes `IronGateLabs/postgis#12` and `IronGateLabs/postgres#1`
  (referenced from the reverted TODO comments).
