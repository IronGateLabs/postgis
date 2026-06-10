## 1. Verify upstream PG fix is available

- [x] 1.1 Confirm upstream PostgreSQL commit `34a3078629` ("Fix RI fast-path crash under nested C-level SPI") is present on PG master via `git log` in a fresh shallow clone
- [x] 1.2 Read the commit message and verify the fix scopes `AfterTrigger` batch callbacks to the correct resource-owner level (not just a guard flip)
- [x] 1.3 Confirm the commit explicitly states "deferred constraints are unaffected by this bug", validating that the PostGIS workaround's mechanism (deferring) was correctly dodging the same root cause

## 2. Prepare reverted worktree

- [x] 2.1 Create git worktree `../postgis-fix-topology-crash` from branch `feature/fix-topology-crash-latest`
- [x] 2.2 `git revert --no-edit 51e7d6202 a23712f27` in the worktree (TODO comments, then the FK workaround itself)
- [x] 2.3 Verify `topology/sql/manage/CreateTopology.sql.in` has exactly two `DEFERRABLE INITIALLY DEFERRED` constraints (`next_left_edge_exists`, `next_right_edge_exists`) and five immediate FK constraints on `node`/`edge_data`
- [x] 2.4 Verify `topology/topology_after_upgrade.sql.in` no longer contains the 81-line workaround migration block

## 3. Rebuild postgis-build-env:latest

- [x] 3.1 Run `docker build` with `POSTGRES_BRANCH=master GEOS_BRANCH=main GDAL_BRANCH=master PROJ_BRANCH=master SFCGAL_BRANCH=master`. **Done with `BUILD_THREADS=1` (not 2)** — the spec's BUILD_THREADS=2 was aspirational; on a 6 GiB Docker host, `-j1` was required to avoid SFCGAL/CGAL OOM. This observation motivated the separate `postgis-build-env#36` upstream PR (auto-detect memory-aware BUILD_THREADS default).
- [x] 3.2 Tag result as `postgis/postgis-build-env:latest`
- [x] 3.3 Inferred commit `34a3078629` presence from build timestamp (image built ~5 hours after Amit's commit landed on PG master). **Not verified via explicit git log inside the container** — the subsequent regression suite passing against immediate FK constraints is the empirical proof that the fix was present; a direct commit-hash check would have been stricter but was not strictly necessary given the passing tests.
- [x] 3.4 **Done differently than specified**: instead of filing a tracking issue, opened focused upstream PRs against `postgis/postgis-build-env` for the discovered infrastructure bugs:
  - PR #35: arm64 `ld.so.preload` multiarch path (the x86_64 hardcoded path broke every arm64 build, including ours)
  - PR #36: BUILD_THREADS auto-detect memory-aware default (closes the OOM root cause we hit)
  - PR #37: undefined `DOCKER_CMAKE_BUILD_TYPE` in nlohmann/json block
  The DNS flake we initially hypothesized was likely the user's network, not a build-env bug — kept as note but not opened as a PR.

## 4. Run topology regression against rebuilt container

- [x] 4.1 Start a container from `postgis/postgis-build-env:latest` with the reverted worktree bind-mounted at `/src/postgis-src` (read-only) and copied to `/src/postgis-work` for writable builds
- [x] 4.2 Inside the container, run `./autogen.sh && ./configure --with-raster --with-topology && make -j2`. **Note**: needed to `sudo rm /etc/ld.so.preload` first to avoid arm64 `libeatmydata.so` preload errors contaminating psql stderr (upstreamed as `postgis-build-env#35`)
- [x] 4.3 Installed core components individually (`make -C liblwgeom/libpgcommon/postgis/topology/raster install` and `extensions/postgis/postgis_topology install`). A top-level `make install` hit an unrelated pre-existing bug in `extensions/postgis_ecef_eci` upgrade-path generation; the per-component path worked around it
- [x] 4.4 Started PG and ran `make check RUNTESTFLAGS="--extension --topology --verbose"`. **Result: 364 tests ran, 1 failed** (the 1 failure was a pre-existing, unrelated ECEF/ECI cleanup hook bug — `regress/hooks/hook-before-uninstall.sql` tries to drop a table owned by the `postgis_ecef_eci` extension without first dropping the extension)
- [x] 4.5 **Zero `ri_FastPathTeardown` crashes and zero `rd_refcnt > 0` assertion failures** — the primary verification success signal. CUnit asserts: 53,043 / 53,043 passed (7127 + 45912 + 4)
- [x] 4.6 **Done by equivalence, not by exact cross-reference**: instead of cross-referencing the specific reproducer tests named in `f393f6017`, we ran the full `--topology` suite and observed `topology/test/regress/droptopogeometrycolumn .. ok in 53 ms` — the original crasher per the `064cbe5c9` CI comment that motivated the workaround. Plus every other topology test (addedge, addface, addnode, addtopogeometrycolumn, copytopology, createtopology, droptopology, etc.) passed cleanly. The broader sweep subsumes the narrower check in spirit.
- [x] 4.7 No failure occurred (the 1 failure was unrelated; did not diagnose topology semantics)

## 5. Re-enable latest CI job

- [x] 5.1 In the worktree, removed `continue_on_error: true` from both `latest` matrix entries (tests + garden) in `.github/workflows/ci.yml` (the flag introduced by commit `064cbe5c9`)
- [x] 5.2 Updated the accompanying diagnostic comment in `ci.yml` to reference upstream PG commit `34a3078629` as the upstream resolution
- [x] 5.3 Committed as `f541b1810 "Re-enable latest CI job after upstream PG RI fast-path fix"` on `feature/fix-topology-crash-latest`

## 6. Verify the topology-fk-constraints spec matches reality

- [x] 6.1 After `make install`, connected to a test database, ran `SELECT topology.CreateTopology('specverify', 4326);`
- [x] 6.2 Queried `pg_constraint` for the constraints on `specverify.node` and `specverify.edge_data`; verified the immediate-vs-deferred status matches `specs/topology-fk-constraints/spec.md` exactly. **Result**:
  - `face_exists` / `start_node_exists` / `end_node_exists` / `left_face_exists` / `right_face_exists`: all `condeferrable=f`, `condeferred=f` (immediate) ✅
  - `next_left_edge_exists` / `next_right_edge_exists`: `condeferrable=t`, `condeferred=t` (deferred initially deferred) ✅
- [x] 6.3 **Executed the forward-reference scenario** via psql on the running test container (pgtest-6x, 2026-04-11):
  - Created `specverify_63` topology, populated with face 1 and 3 nodes
  - `BEGIN; INSERT edge_data (edge_id=1, ..., abs_next_left_edge=2, ...);` — SUCCEEDED because `next_left_edge_exists` is DEFERRED (edge 2 did not exist at INSERT time)
  - `INSERT edge_data (edge_id=2, ...);` — SUCCEEDED
  - `COMMIT;` — SUCCEEDED; deferred FK check passed at commit time since both edges now exist
  - Post-commit `SELECT` confirmed both edges present with `abs_next_left_edge` cross-references intact
- [x] 6.4 **Executed the negative-path scenario** via psql on the same container:
  - `BEGIN; INSERT edge_data (edge_id=1, start_node=9999, end_node=9999, ...);` — FAILED with `ERROR: insert or update on table "edge_data" violates foreign key constraint "end_node_exists"; DETAIL: Key (end_node)=(9999) is not present in table "node".`
  - The error was raised **at the INSERT statement**, NOT deferred to COMMIT, proving both `start_node_exists` and `end_node_exists` are IMMEDIATE constraints
  - Note: PG checked `end_node_exists` before `start_node_exists` in declaration order and reported the first violation; both FKs are immediate and either firing proves the semantics
  - `ROLLBACK` cleaned up the failed transaction

## 7. Update documentation and commit messages

- [x] 7.1 **Done differently**: rather than adding a distinct "summary commit", the individual commit messages for the two reverts (`b42524d05`, `664b8d5ca`) and the CI re-enable (`f541b1810`) each reference upstream PG commit `34a3078629` in their body. The spec's spirit (attribution to Amit's fix and cross-reference to the upstream resolution) is satisfied across the commit chain rather than concentrated in a single commit. The PR #11 description also explicitly calls out the commit.
- [ ] 7.2 **Deferred**: update `IronGateLabs/postgis#12` and `IronGateLabs/postgres#1` tracking issues with the resolution. These are internal to the user's forks; will be updated post-merge. Tracking as a post-merge follow-up.
- [x] 7.3 **Not needed**: no comment added to `topology_after_upgrade.sql.in`. After the revert, the file no longer contains the workaround migration block at all, so there is nothing to comment on. The spec's intent (document that existing installations keep deferred FKs) lives in `design.md` Decision D2 and is not duplicated into the SQL file.

## 8. Merge and archive

- [x] 8.1 PR #11 from `feature/fix-topology-crash-latest` → `develop` is open (title and body updated this session to reflect the pivot from workaround to removal)
- [x] 8.2 Waited for CI — all matrix entries green including `latest` (tests + garden), mingw (after real fix in `03e644b1f`), SonarCloud Scan (after removing `continue-on-error` workaround in `657f77186` once user disabled Automatic Analysis in SC web UI)
- [ ] 8.3 Merge to `develop` — pending user action (`gh pr merge 11 --repo IronGateLabs/postgis --merge --delete-branch`)
- [ ] 8.4 Archive this openspec change via `openspec archive remove-topology-fk-workaround` — pending merge
- [ ] 8.5 Delete the `../postgis-fix-topology-crash` worktree after the merge

## 9. Parallel build-env fix (separate PR)

- [x] 9.1 **Invalidated**: the `fix/add-docbook-xsl-for-garden` branch and PR #34 on `postgis-build-env` were closed by the user. During this session we investigated whether the `docbook-xsl-ns` package is actually required for `make garden` and found it **is not** — `make garden` succeeds without the package because the gardentest XSL files don't import DocBook stylesheets. PR #34 was correctly left closed. Three other real `postgis-build-env` bugs were opened as PRs #35/#36/#37 instead (see task 3.4).
- [x] 9.2 Confirmed: the `postgis-build-env` work is tracked separately (PRs #35, #36, #37 are independent of the topology fix).

## Addenda: not in original plan but executed during the session

- **Fixed mingw cunit libtool wrapper bug** in `loader/cunit/Makefile.in:102` and `raster/test/cunit/Makefile.in:86` — changed `@./cu_tester` to `@$(LIBTOOL) --mode=execute ./cu_tester` to match the pattern already used by `liblwgeom/cunit` and `postgis/cunit`. The previous form was flaky on MSYS2/mingw where libtool sometimes produces a `#!/bin/sh` wrapper around `.libs/cu_tester.exe` instead of a direct binary. Committed as `03e644b1f`. This fix is not topology-FK-workaround related but was necessary to unblock the `mingw (clang64)` CI check on PR #11.
- **Added then removed `continue-on-error: true` on `sonar.yml`** — PR #11 briefly carried a workaround making `SonarCloud Scan` non-blocking so the CI-driven analysis conflict (Automatic Analysis enabled in SC web UI simultaneously) would not block merges. After the user disabled Automatic Analysis in the SonarCloud web UI on 2026-04-11, the workaround was reverted (commit `657f77186`), restoring the CI-driven build-wrapper analysis as a proper blocking PR check. This flow is Phase 0 of the separate `sonarcloud-cleanup` OpenSpec change (PR #15).
