## EOP Auto-Refresh Architecture

### Data Flow

```
┌─────────────────────┐
│  External Scheduler  │  cron / systemd timer
│  (fetch_iers_eop.sh) │  runs daily
└──────────┬──────────┘
           │ curl + atomic mv
           ▼
┌─────────────────────┐
│  Local File          │  /var/lib/postgresql/eop/finals2000A.all
│  (finals2000A.all)   │
└──────────┬──────────┘
           │ pg_read_file()
           ▼
┌─────────────────────┐
│  TimescaleDB Job     │  ecef_eci.refresh_eop(job_id, config)
│  Background Worker   │  scheduled via add_job()
└──────────┬──────────┘
           │ load_eop_finals2000a()
           ▼
┌─────────────────────┐
│  ecef_eci.eop_data   │  TimescaleDB-managed EOP table
│  (TimescaleDB)       │  ~20 years of daily data
└──────────┬──────────┘
           │ sync_eop_to_postgis()
           ▼
┌─────────────────────┐
│  postgis_eop         │  PostGIS-managed EOP table
│  (PostGIS)           │  used by ST_ECEF_To_ECI_EOP()
└─────────────────────┘
```

### Parser Column Offset Bug

The PostGIS `postgis_eop_load()` function uses incorrect SUBSTRING offsets:

| Field   | IERS Spec   | TimescaleDB Parser | PostGIS Parser (BUG) |
|---------|-------------|-------------------|---------------------|
| MJD     | cols 8-15   | `FROM 8 FOR 8`   | `FROM 19 FOR 9`     |
| PM-x    | cols 19-27  | `FROM 19 FOR 9`  | `FROM 35 FOR 9`     |
| PM-y    | cols 38-46  | `FROM 38 FOR 9`  | `FROM 44 FOR 9`     |
| UT1-UTC | cols 59-68  | `FROM 59 FOR 10` | `FROM 59 FOR 10` (OK) |

The auto-refresh pipeline avoids this by using the TimescaleDB parser exclusively.
The PostGIS parser should be fixed separately as a standalone bug fix.

### Error Handling Strategy

The `refresh_eop` procedure uses `RAISE WARNING` (not `RAISE EXCEPTION`) for all
error conditions. This ensures the TimescaleDB background job scheduler does not
disable the job after transient failures (e.g., file not yet downloaded).

Error conditions handled:
- Missing `eop_file_path` config key
- File not found (download not yet run)
- Permission denied (wrong file permissions)
- File too small (corrupt/truncated download)
- Zero rows parsed (format mismatch)
- Post-load staleness (download script may have stopped)
