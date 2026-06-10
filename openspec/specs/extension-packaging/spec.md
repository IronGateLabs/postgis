## Purpose

Packaging of ECEF/ECI SQL functions as a separate installable PostgreSQL extension.

## Requirements

### Requirement: Separate PostgreSQL extension
The ECEF/ECI SQL interface SHALL be packaged as a separate PostgreSQL extension named `postgis_ecef_eci` that can be installed independently via `CREATE EXTENSION`.

#### Scenario: Extension creation
- **WHEN** `CREATE EXTENSION postgis_ecef_eci` is executed after `CREATE EXTENSION postgis`
- **THEN** the extension SHALL be created successfully, installing all ECEF/ECI functions, ECI SRID registrations, and the EOP table

#### Scenario: Extension listed in pg_available_extensions
- **WHEN** `SELECT * FROM pg_available_extensions WHERE name = 'postgis_ecef_eci'` is queried
- **THEN** the result SHALL include one row with the extension name and version

### Requirement: PostGIS dependency
The extension SHALL declare `requires = postgis` in its control file, ensuring PostGIS is installed before the ECEF/ECI extension.

#### Scenario: Installation without PostGIS fails
- **WHEN** `CREATE EXTENSION postgis_ecef_eci` is executed without PostGIS installed
- **THEN** the system SHALL raise an error indicating that the `postgis` extension is required

#### Scenario: PostGIS version compatibility
- **WHEN** the extension is installed with PostGIS 3.4 or later
- **THEN** all ECEF/ECI functions SHALL work correctly with the installed PostGIS version

### Requirement: Control file following postgis_sfcgal pattern
The extension SHALL have a `.control.in` file with template variables for version substitution, following the `postgis_sfcgal` pattern.

#### Scenario: Control file contents
- **WHEN** the control file is processed during build
- **THEN** it SHALL contain: `comment = 'PostGIS ECEF/ECI coordinate system functions'`, `default_version` set to the build version, `relocatable = true`, and `requires = postgis`

### Requirement: Build system integration
The extension SHALL have a `Makefile.in` that integrates with PostgreSQL's PGXS build system and PostGIS's `configure` infrastructure.

#### Scenario: Build with make
- **WHEN** `make` is run in the `extensions/postgis_ecef_eci/` directory after `configure`
- **THEN** the extension control file and SQL scripts SHALL be generated from templates

#### Scenario: Install with make install
- **WHEN** `make install` is run
- **THEN** the control file and SQL scripts SHALL be installed to the PostgreSQL extension directory

### Requirement: Clean extension removal
The extension SHALL support clean removal via `DROP EXTENSION`, removing all installed objects.

#### Scenario: Drop extension removes all objects
- **WHEN** `DROP EXTENSION postgis_ecef_eci CASCADE` is executed
- **THEN** all functions (`ST_ECEF_To_ECI`, `ST_ECI_To_ECEF`, `ST_ECEF_X/Y/Z`), the `postgis_eop` table, and related objects SHALL be removed
- **THEN** ECI SRID entries in `spatial_ref_sys` SHALL remain (they are data, not schema objects, and may be referenced by existing geometries)

### Requirement: Extension upgrade support
The extension SHALL support version upgrades via `ALTER EXTENSION postgis_ecef_eci UPDATE`.

#### Scenario: Upgrade preserves data
- **WHEN** `ALTER EXTENSION postgis_ecef_eci UPDATE TO 'new_version'` is executed
- **THEN** EOP data in `postgis_eop` SHALL be preserved across the upgrade
- **THEN** existing geometries with ECI SRIDs SHALL remain valid

### Requirement: Install order independence with TimescaleDB
The extension SHALL work correctly regardless of whether it is installed before or after TimescaleDB.

#### Scenario: PostGIS -> ECEF/ECI -> TimescaleDB
- **WHEN** extensions are created in order: `postgis`, `postgis_ecef_eci`, `timescaledb`
- **THEN** all three extensions SHALL coexist without errors

#### Scenario: PostGIS -> TimescaleDB -> ECEF/ECI
- **WHEN** extensions are created in order: `postgis`, `timescaledb`, `postgis_ecef_eci`
- **THEN** all three extensions SHALL coexist without errors
