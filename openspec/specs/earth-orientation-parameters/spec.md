## Purpose

Storage, loading, and interpolation of IERS Earth Orientation Parameters for precision frame conversions.

## Requirements

### Requirement: EOP storage table
The system SHALL provide a `postgis_eop` table for storing Earth Orientation Parameters with columns for Modified Julian Date, polar motion (xp, yp), UT1-UTC offset (dut1), and celestial pole offsets (dx, dy).

#### Scenario: EOP table exists after extension installation
- **WHEN** `SELECT * FROM postgis_eop LIMIT 0` is queried after `CREATE EXTENSION postgis_ecef_eci`
- **THEN** the query SHALL succeed and the table SHALL have columns: `mjd` (FLOAT8, PRIMARY KEY), `xp` (FLOAT8), `yp` (FLOAT8), `dut1` (FLOAT8), `dx` (FLOAT8), `dy` (FLOAT8)

#### Scenario: EOP table owned by extension
- **WHEN** `DROP EXTENSION postgis_ecef_eci CASCADE` is executed
- **THEN** the `postgis_eop` table SHALL be dropped along with the extension

### Requirement: IERS data loader
The system SHALL provide a `postgis_eop_load(data TEXT)` function that parses IERS Bulletin A fixed-width format text and inserts or updates rows in the `postgis_eop` table.

#### Scenario: Load IERS Bulletin A data
- **WHEN** `SELECT postgis_eop_load(pg_read_file('/path/to/finals2000A.data'))` is called with valid IERS Bulletin A content
- **THEN** the `postgis_eop` table SHALL be populated with EOP values for each date in the bulletin, and the function SHALL return the number of rows inserted or updated

#### Scenario: Duplicate MJD values are updated
- **WHEN** `postgis_eop_load` is called twice with overlapping date ranges
- **THEN** existing rows SHALL be updated with the latest values (UPSERT behavior) rather than raising duplicate key errors

#### Scenario: Malformed data raises error
- **WHEN** `postgis_eop_load('not valid IERS data')` is called
- **THEN** the system SHALL raise an error indicating that the input does not match the expected IERS format

### Requirement: EOP interpolation
The system SHALL provide a `postgis_eop_interpolate(epoch TIMESTAMPTZ)` function that returns interpolated EOP values for a given epoch using linear interpolation between the two nearest MJD entries in the `postgis_eop` table.

#### Scenario: Interpolate at exact MJD
- **WHEN** `postgis_eop_interpolate('2025-01-01 00:00:00+00')` is called and MJD 60676.0 exists in the table
- **THEN** the function SHALL return the exact values stored for that MJD

#### Scenario: Interpolate between two MJDs
- **WHEN** `postgis_eop_interpolate('2025-01-01 12:00:00+00')` is called and both MJD 60676.0 and 60677.0 exist
- **THEN** the function SHALL return values linearly interpolated at the midpoint between the two entries

#### Scenario: Epoch outside loaded range
- **WHEN** `postgis_eop_interpolate('1900-01-01 00:00:00+00')` is called and no EOP data exists for that era
- **THEN** the function SHALL return NULL (not extrapolate beyond loaded data)

### Requirement: EOP refresh procedure
The system SHALL provide a `postgis_refresh_eop()` procedure suitable for scheduling with `pg_cron` or TimescaleDB's `add_job` to periodically update EOP data.

#### Scenario: Procedure callable by scheduler
- **WHEN** `CALL postgis_refresh_eop()` is executed
- **THEN** the procedure SHALL attempt to load or update EOP data from the configured source

#### Scenario: Compatible with TimescaleDB job scheduler
- **WHEN** `SELECT add_job('postgis_refresh_eop', schedule_interval => INTERVAL '1 day')` is executed
- **THEN** the job SHALL be created successfully (procedure signature is compatible with `add_job` requirements)

### Requirement: EOP data validation
The system SHALL validate EOP values during loading to prevent obviously incorrect data from being stored.

#### Scenario: Polar motion out of range
- **WHEN** EOP data with `xp = 5.0` (arcseconds) is loaded -- far outside the normal +/-0.5 arcsecond range
- **THEN** the system SHALL raise a warning or error indicating the value is outside expected bounds

#### Scenario: DUT1 out of range
- **WHEN** EOP data with `dut1 = 2.0` (seconds) is loaded -- outside the +/-0.9 second range maintained by leap seconds
- **THEN** the system SHALL raise a warning or error indicating the value is outside expected bounds
