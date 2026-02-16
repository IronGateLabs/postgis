## Why

The TimescaleDB interface contract §8.3 identified that `postgis_refresh_eop()` is a
placeholder — it emits a RAISE NOTICE but does not fetch or load IERS data. Users must
manually download Bulletin A and call `postgis_eop_load()`. This blocks automated EOP
refresh in production deployments.

Additionally, the `postgis_eop_load()` parser has incorrect column offsets for several
IERS finals2000A fields, which would produce wrong EOP values if data were loaded
through the PostGIS parser directly.

## What Changes

### Architecture Decision: File-Based Refresh (Option D)

The EOP auto-refresh is implemented on the **TimescaleDB side** using a file-based
architecture:

1. **External scheduler** (cron/systemd timer) runs `scripts/fetch_iers_eop.sh` to
   download IERS Bulletin A (`finals2000A.all`) to a local file path
2. **TimescaleDB background job** (`ecef_eci.refresh_eop`) reads the file via
   `pg_read_file()`, parses it with the TimescaleDB parser (`load_eop_finals2000a`),
   and loads into `ecef_eci.eop_data`
3. **Cross-table sync** (`ecef_eci.sync_eop_to_postgis`) upserts from `eop_data`
   into PostGIS's `postgis_eop` table

This avoids:
- Adding HTTP extension dependencies (`pg_curl`, `http`) to PostGIS
- Using the PostGIS parser (which has column offset bugs)
- Requiring superuser for HTTP access

### PostGIS-Side Items (Separate from TimescaleDB Implementation)

1. **Fix `postgis_eop_load()` column offsets** — The parser in `ecef_eci.sql.in` uses
   incorrect SUBSTRING offsets for MJD (FROM 19 should be FROM 8), PM-x (FROM 35
   should be FROM 19), and PM-y (FROM 44 should be FROM 38). UT1-UTC is correct.
   This is a standalone bug fix, independent of the auto-refresh feature.

2. **Keep placeholder `postgis_refresh_eop()` as-is** — The TimescaleDB-side job
   handles all refresh logic. The PostGIS placeholder remains for documentation
   and forward compatibility.

## Capabilities

### Unchanged Capabilities

- `earth-orientation-parameters`: PostGIS's `postgis_eop` table schema and
  `postgis_eop_interpolate()` function are unchanged. The TimescaleDB sync
  function writes to this table using the existing schema.

### Known Issue

- `postgis_eop_load()` parser column offsets are incorrect. This does not affect
  the auto-refresh pipeline (data flows through TimescaleDB's parser), but should
  be fixed for users who load EOP data directly through PostGIS.

## Impact

- **No C code changes** required on PostGIS side for auto-refresh
- **SQL bug fix** needed in `postgis/ecef_eci.sql.in` for `postgis_eop_load()` offsets
- **Cross-repo coordination**: TimescaleDB contract updated to v0.4.0, §8.3 resolved
