## Context

PostGIS now supports geocentric (ECEF, SRID 4978) and inertial (ECI, SRIDs 900001–900003) coordinate reference systems. A first wave of guards was added to five spatial functions that produce meaningless results on geocentric input: `ST_Area`, `ST_Buffer`, `ST_OffsetCurve`, `ST_Centroid`, `ST_BuildArea`. These all call `gserialized_check_crs_family_not_geocentric()` immediately after parameter extraction, raising `ERRCODE_FEATURE_NOT_SUPPORTED` before any computation.

Several functions remain unguarded and will silently compute nonsensical results on geocentric input. The `geometry → geography` cast path has a separate issue: `srid_check_latlong()` rejects non-geographic SRIDs, but the error message is generic rather than explaining the geocentric-specific problem.

On the indexing side, GiST 3D correctness tests cover ~189 ECEF and ~81 ECI points. No performance benchmarks exist at realistic scale.

## Goals / Non-Goals

**Goals:**
- Guard all spatial functions that produce meaningless results on geocentric input
- Improve the geography cast error message for geocentric input specifically
- Benchmark GiST 3D index performance with ECEF/ECI data at 10K–100K point scale
- Close gaps in spatial-index-verification: `ST_3DDistance` accuracy validation, mixed-SRID safety

**Non-Goals:**
- Adding geocentric dispatch paths (3D-aware implementations) for ST_Perimeter, ST_Azimuth, etc. — those are future work
- Benchmarking other index types (SP-GiST, BRIN) for ECEF data
- GPU-accelerated index operations
- Modifying the guard infrastructure itself — the existing `gserialized_check_crs_family_not_geocentric()` function is sufficient

## Decisions

### 1. Guard placement: early-exit before deserialization where possible

**Decision:** Place the guard call immediately after `PG_GETARG_GSERIALIZED_P()`, before `lwgeom_from_gserialized()` when the function structure allows it.

**Rationale:** The existing ST_Area pattern does this — the guard checks the SRID from the serialized header without deserializing the geometry, making it a fast-path rejection. Functions like `LWGEOM_perimeter_poly` currently deserialize before any checks; we can insert the guard before deserialization.

**Exception:** `LWGEOM_azimuth` extracts SRID after deserialization because it needs the LWPOINT cast. Use `srid_check_crs_family_not_geocentric(srid, ...)` there instead of the GSERIALIZED variant.

### 2. Geography cast: add geocentric-specific guard before srid_check_latlong

**Decision:** In `geography_from_geometry`, add `srid_check_crs_family_not_geocentric()` before the existing `srid_check_latlong()` call.

**Rationale:** `srid_check_latlong` already rejects geocentric SRIDs because they aren't geographic, but the error message is generic ("Only geographic spatial reference systems allowed"). A geocentric-specific guard gives a clearer message: "Operation is not supported for geocentric (ECEF) coordinates (SRID=4978). Transform to a geographic or projected CRS first." Since the geocentric check is cheaper (SRID-based lookup vs PROJ inspection for latlong), it adds negligible overhead.

**Alternative considered:** Just improve `srid_check_latlong`'s error message — rejected because the geocentric guard is a reusable pattern and the messages should be specific to the problem.

### 3. ST_Project: guard both variants with the same function name

**Decision:** Both `geometry_project_direction` and `geometry_project_geometry` use `"ST_Project"` as the function name in the guard error message.

**Rationale:** Users know the function as `ST_Project`. The internal C function names are implementation details.

### 4. GiST benchmark: SQL-based script, not C extension

**Decision:** Create a standalone SQL benchmark script rather than extending `bench_accel.c`.

**Rationale:** GiST indexing is a PostgreSQL-level feature — benchmarking it requires `CREATE INDEX`, `EXPLAIN ANALYZE`, and query execution, all of which are natural in SQL. The `bench_accel.c` harness benchmarks low-level C transform operations and would need significant restructuring to drive SQL queries. A SQL script follows the existing pattern of `regress/core/` tests and can reuse `generate_series` for point generation.

**Benchmark structure:**
- Generate ECEF points via `ST_Transform(ST_SetSRID(ST_MakePoint(lon, lat, 0), 4326), 4978)` at 10K, 50K, 100K scales
- Measure index build time via `\timing` or `clock_timestamp()` deltas
- Measure query throughput for `ST_3DDWithin` range queries and `&&` bounding box overlap
- Compare against geographic baseline (same points, SRID 4326, geography type with GiST index)

### 5. Mixed-SRID safety: test at SQL level only

**Decision:** Add regression tests that verify mixed-SRID queries (e.g., ECEF geometry compared against geographic geometry) raise errors rather than silently returning wrong results.

**Rationale:** The SRID mismatch check is already in the core spatial operators — we just need to verify it works for the new SRID ranges. No new C code needed.

## Risks / Trade-offs

**[Breaking change for geocentric misuse]** → Users who were passing geocentric geometries to ST_Perimeter/ST_Azimuth/ST_Project/ST_Segmentize and ignoring the nonsensical results will get errors. Mitigation: this is intentional — the error message tells them to transform first.

**[Geography cast double-check overhead]** → Adding a geocentric guard before `srid_check_latlong` means two CRS lookups for non-geocentric SRIDs. Mitigation: `srid_get_crs_family()` uses the per-session PROJ cache (128-item LRU), so the second lookup is a cache hit. Overhead is negligible.

**[Benchmark reproducibility]** → SQL-level timing depends on system load, shared buffers, etc. Mitigation: document that benchmarks should run on a quiet system with `synchronize_seqscans = off` and multiple iterations. The benchmark is for relative comparison (ECEF vs geographic), not absolute numbers.
