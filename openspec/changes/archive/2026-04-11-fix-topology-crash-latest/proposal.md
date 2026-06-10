# Fix PostgreSQL server crash in DropTopoGeometryColumn on latest Docker image

## Problem

The CI `latest` Docker image (bleeding-edge PostgreSQL + GEOS + PROJ) consistently crashes
during the `droptopogeometrycolumn` regression test. The crash occurs at line 19 of
`topology/test/regress/droptopogeometrycolumn.sql`:

```sql
INSERT INTO t5118f(g) SELECT toTopoGeom('LINESTRING(0 0, 10 0)', 't5118', 1);
```

The PostgreSQL backend terminates abnormally ("server closed the connection unexpectedly"),
causing the test harness to report failure. This blocks `latest/tests` and `latest/garden`
CI jobs on all PRs.

## Impact

- All 4 open PRs (#2, #8, #9, #10) show `latest` as failing
- Version-pinned CI jobs (pg14-18) all pass — the crash is specific to `latest`
- The test exercises ticket #5118: dropping a TopoGeometry column when the layer sequence
  has been manually dropped

## Investigation needed

1. **Reproduce locally** — build with the `postgis/postgis-build-env:latest` Docker image
   and run the specific test
2. **Identify crash location** — check if the server produces a core dump or log with
   backtrace info (the CI uses `logbt` for this)
3. **Determine root cause:**
   - Is this triggered by our fork changes (ECEF/ECI, CRS family, etc.)?
   - Is this a pre-existing upstream bug exposed by newer PG/GEOS?
   - Is the `latest` Docker image itself broken?
4. **Bisect** — check if the crash happens on upstream `master` with the same Docker image
5. **Fix or workaround:**
   - If our code: fix the bug
   - If upstream: report to PostGIS Trac and mark `latest` as `continue-on-error`
   - If Docker image: report to postgis/postgis-build-env

## Scope

- Investigate and fix (or document) the `toTopoGeom` crash
- Ensure `latest` CI jobs pass or are properly marked as non-blocking
- If fix is applicable to upstream, prepare a patch for contribution

## Success criteria

- `latest/tests` passes, or is marked `continue-on-error` with a documented reason
- No regression in pg14-18 test suites
