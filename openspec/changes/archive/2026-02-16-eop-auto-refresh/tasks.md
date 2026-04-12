## Tasks

### PostGIS Side

- [x] Fix `postgis_eop_load()` column offsets in `postgis/ecef_eci.sql.in`
  - MJD: change `FROM 19 FOR 9` to `FROM 8 FOR 8`
  - PM-x: change `FROM 35 FOR 9` to `FROM 19 FOR 9`
  - PM-y: change `FROM 44 FOR 9` to `FROM 38 FOR 9`
  - Add regression test with real IERS data snippet
  - **Priority**: Low (TimescaleDB pipeline bypasses this parser)

- [x] Verify `postgis_eop` table schema compatibility with TimescaleDB sync
  - Confirm columns: mjd (PK), xp, yp, dut1, dx, dy
  - Confirm `ON CONFLICT (mjd)` works for upsert

### TimescaleDB Side (Completed)

- [x] Replace placeholder `refresh_eop` with production version
  - Error handling: WARNING not EXCEPTION
  - File validation: minimum size check
  - Post-load staleness detection

- [x] Add `sync_eop_to_postgis()` function
  - INSERT...SELECT...ON CONFLICT from eop_data to postgis_eop
  - Graceful skip if postgis_eop table doesn't exist

- [x] Add `eop_staleness()` function
  - Returns gap_days, is_stale, load_age_hours
  - Configurable threshold (default 7 days)

- [x] Add `setup_eop_refresh()` convenience wrapper
  - Wraps add_job() with config construction

- [x] Create `scripts/fetch_iers_eop.sh`
  - Primary + mirror URL with fallback
  - Atomic download (tmp + mv)
  - File size validation

- [x] Create integration tests (`test/sql/postgis_ecef_eci/eop_refresh.sql`)

- [x] Update interface contract to v0.4.0, §8.3 resolved

### Cross-Repo Coordination

- [x] Verify end-to-end with both extensions installed
- [x] Update PostGIS contract copy to v0.4.0
