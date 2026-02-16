## Affected Specs

This change set modifies behavior defined in the following specs:

- **ecef-coordinate-support** (`openspec/specs/ecef-coordinate-support/spec.md`)
  - Extended: Guard behavior now covers `LW_CRS_INERTIAL` in addition to
    `LW_CRS_GEOCENTRIC`. All Error-class functions reject ECI input (SRID 900001+).
  - The spec's "Spatial functions error on unsupported ECEF operations" requirement
    is broadened to cover ECI coordinates via the shared guard function.

- **earth-orientation-parameters** (`openspec/specs/earth-orientation-parameters/spec.md`)
  - Extended: `postgis_refresh_eop` now has a `(job_id INT, config JSONB)` overload
    compatible with TimescaleDB's `add_job()` scheduler.
  - The spec's "Compatible with TimescaleDB job scheduler" scenario is now satisfied.

No new specs are introduced. The changes are strictly alignment fixes to close gaps
identified during the TimescaleDB interface contract v0.3.0 audit.
