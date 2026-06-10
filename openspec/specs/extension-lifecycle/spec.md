## Purpose

Defines the PostgreSQL extension packaging, installation, upgrade, and version management lifecycle for the PostGIS extension family. This covers CREATE EXTENSION installation, ALTER EXTENSION UPDATE upgrade paths, the postgis_extensions_upgrade() convenience function, legacy function stub mechanism for pg_upgrade compatibility, before/after upgrade hooks for safe signature migration, the version numbering scheme and its rules for API changes, upgrade script generation via create_upgrade.pl, and the extension dependency chain.

## ADDED Requirements

### Requirement: Extension control file definitions
Each PostGIS extension SHALL have a `.control` file specifying: `comment`, `default_version` (set to the build version), `relocatable` (false for postgis, postgis_raster, postgis_topology; true for postgis_sfcgal), and `requires` for dependency declaration. The `postgis` extension has no requires. `postgis_raster`, `postgis_topology`, and `postgis_sfcgal` all require `postgis`.

The `postgis_topology` extension SHALL set `schema = topology` to force installation into the topology schema.

#### Scenario: postgis extension control file
- **GIVEN** the postgis.control.in file
- **WHEN** the extension is built
- **THEN** it SHALL produce a control file with relocatable=false and no requires line
- **AND** module_pathname SHALL reference the shared library
- Validated by: extensions/postgis/postgis.control.in (build artifact)

#### Scenario: postgis_raster depends on postgis
- **GIVEN** the postgis_raster.control.in file
- **THEN** it SHALL contain `requires = postgis`
- **AND** `CREATE EXTENSION postgis_raster` without postgis installed SHALL fail
- Validated by: extensions/postgis_raster/postgis_raster.control.in

#### Scenario: postgis_topology uses fixed schema
- **GIVEN** the postgis_topology.control.in file
- **THEN** it SHALL contain `schema = topology`
- **AND** all topology objects SHALL be created in the 'topology' schema
- Validated by: extensions/postgis_topology/postgis_topology.control.in

### Requirement: CREATE EXTENSION installs PostGIS
`CREATE EXTENSION postgis` SHALL install all PostGIS types (geometry, geography, box2d, box3d), operators, operator classes, functions, and the spatial_ref_sys table into the target schema. The extension version installed SHALL match the `default_version` from the control file.

#### Scenario: Fresh install of postgis extension
- **WHEN** `CREATE EXTENSION postgis` is executed in a database with no prior PostGIS
- **THEN** the geometry and geography types SHALL be available
- **AND** `SELECT PostGIS_Full_Version()` SHALL return the installed version
- **AND** the spatial_ref_sys table SHALL be populated with standard EPSG entries
- Validated by: regress tests run with --extension flag

#### Scenario: Install postgis with specific schema
- **WHEN** `CREATE EXTENSION postgis SCHEMA public` is executed
- **THEN** all PostGIS objects SHALL be created in the public schema
- Validated by: regress tests run with --extension flag

#### Scenario: Install dependent extensions in order
- **WHEN** `CREATE EXTENSION postgis` then `CREATE EXTENSION postgis_topology` is executed
- **THEN** both extensions SHALL be installed successfully
- **AND** `\dx` SHALL show both extensions with correct versions
- Validated by: regress tests run with --extension --topology flag

### Requirement: ALTER EXTENSION UPDATE upgrades PostGIS
`ALTER EXTENSION postgis UPDATE TO 'version'` SHALL upgrade the extension by running the appropriate upgrade SQL script. The upgrade path uses a two-hop mechanism: `<extension>--<old_version>--ANY.sql` (a no-op tag file) followed by `<extension>--ANY--<new_version>.sql` (the actual upgrade script). This keeps the number of upgrade path files manageable.

#### Scenario: Upgrade from a prior version
- **GIVEN** postgis 3.5.0 is installed
- **WHEN** `ALTER EXTENSION postgis UPDATE TO '3.6.0'` is executed
- **THEN** the extension SHALL be upgraded to 3.6.0
- **AND** new functions introduced in 3.6.0 SHALL be available
- **AND** `PostGIS_Lib_Version()` SHALL return '3.6.0'
- Validated by: regress tests run with --upgrade --extension flag

#### Scenario: Two-hop upgrade path resolution
- **GIVEN** PostgreSQL extension infrastructure resolving upgrade paths
- **WHEN** upgrading from version X to version Y
- **THEN** PostgreSQL SHALL find the path X -> ANY -> Y using the symlinked files
- **AND** the ANY--Y.sql script SHALL contain the complete upgrade logic
- Validated by: extensions/upgrade-paths-rules.mk (build system)

#### Scenario: Upgrade preserves existing data
- **GIVEN** a database with geometry columns containing data
- **WHEN** the extension is upgraded
- **THEN** all existing geometry data SHALL remain accessible and unchanged
- **AND** existing spatial indexes SHALL remain functional
- Validated by: regress tests run with --upgrade flag

### Requirement: postgis_extensions_upgrade() convenience function
`postgis_extensions_upgrade(target_version text DEFAULT NULL)` SHALL upgrade all installed PostGIS extensions (postgis, postgis_raster, postgis_sfcgal, postgis_topology) to the specified target version (or the default version if NULL). It processes extensions in order of name length (postgis first). It handles both packaged (CREATE EXTENSION) and unpackaged installations.

#### Scenario: Upgrade all extensions at once
- **GIVEN** postgis and postgis_topology are installed at version 3.5.0
- **WHEN** `SELECT postgis_extensions_upgrade()` is called
- **THEN** both extensions SHALL be upgraded to the default (latest) version
- Validated by: regress tests with --upgrade --extension flag

#### Scenario: Upgrade with explicit target version
- **WHEN** `SELECT postgis_extensions_upgrade('3.6.0')` is called
- **THEN** all extensions SHALL be upgraded to exactly version 3.6.0
- Status: untested -- explicit version targeting not directly tested in regression

#### Scenario: Skip extensions not installed
- **GIVEN** only postgis is installed (not postgis_raster)
- **WHEN** postgis_extensions_upgrade() is called
- **THEN** only postgis SHALL be upgraded; no error for missing optional extensions
- Validated by: regress tests with --upgrade --extension flag

### Requirement: Legacy function stubs for pg_upgrade compatibility
Deprecated C functions SHALL NOT be deleted from the shared library. Instead, they SHALL be replaced with stub functions that raise an error with ERRCODE_FEATURE_NOT_SUPPORTED, the deprecated function name, the version it was deprecated in, and a hint to run `postgis_extensions_upgrade()`. Stubs are defined using the `POSTGIS_DEPRECATE(version, funcname)` macro.

Stub files:
- `postgis/postgis_legacy.c` for core postgis functions
- `sfcgal/postgis_sfcgal_legacy.c` for SFCGAL functions
- `raster/rt_pg/rtpg_legacy.c` for raster functions

#### Scenario: Calling deprecated function raises informative error
- **GIVEN** a database upgraded via pg_upgrade (binary upgrade) that still references old function names
- **WHEN** a query invokes a deprecated function like `area(geometry)`
- **THEN** an error SHALL be raised with message "A stored procedure tried to use deprecated C function"
- **AND** the errdetail SHALL name the function and deprecation version
- **AND** the errhint SHALL suggest running postgis_extensions_upgrade()
- Validated by: postgis/postgis_legacy.c (code review; runtime behavior after pg_upgrade)

#### Scenario: Legacy stubs cover all deprecated versions
- **GIVEN** the postgis_legacy.c file
- **THEN** it SHALL contain POSTGIS_DEPRECATE entries for functions deprecated in versions 2.0.0 through 3.0.0
- **AND** each entry SHALL compile to a valid PG_FUNCTION_INFO_V1 function
- Validated by: postgis/postgis_legacy.c (compilation)

#### Scenario: SFCGAL and raster legacy stubs exist
- **THEN** `sfcgal/postgis_sfcgal_legacy.c` SHALL contain stubs for SFCGAL functions moved from core
- **AND** `raster/rt_pg/rtpg_legacy.c` SHALL contain stubs for deprecated raster functions
- Validated by: sfcgal/postgis_sfcgal_legacy.c, raster/rt_pg/rtpg_legacy.c (compilation)

### Requirement: Before/after upgrade hooks for signature migration
The upgrade process SHALL execute `common_before_upgrade.sql` and extension-specific `*_before_upgrade.sql` scripts BEFORE applying new function definitions. These scripts drop functions with changed signatures using `_postgis_drop_function_by_identity()`. After new definitions are applied, `*_after_upgrade.sql` scripts clean up old aggregates, functions, and operators that are no longer needed.

#### Scenario: Before-upgrade drops changed function signatures
- **GIVEN** a function ST_AsX3D whose signature changed between versions
- **WHEN** the before-upgrade script runs
- **THEN** `_postgis_drop_function_by_identity('ST_AsX3D', old_args)` SHALL drop the old signature
- **AND** the new CREATE OR REPLACE shall succeed without conflict
- Validated by: postgis/postgis_before_upgrade.sql

#### Scenario: After-upgrade removes obsolete aggregates
- **WHEN** the after-upgrade script runs
- **THEN** deprecated aggregates like `memgeomunion`, `geomunion`, `collect` SHALL be dropped
- **AND** deprecated function signatures SHALL be removed
- Validated by: postgis/postgis_after_upgrade.sql

#### Scenario: _postgis_drop_function_by_identity handles missing functions
- **WHEN** the helper function attempts to drop a function that does not exist
- **THEN** it SHALL silently succeed (no error raised)
- Validated by: postgis/common_before_upgrade.sql (function definition handles EXCEPTION)

### Requirement: Version numbering and API change rules
PostGIS follows semantic versioning with Major.Minor.Patch:
- **Patch releases (3.x.Y):** No new SQL API functions, no new dependency requirements, no structure changes. Avoid removing/stubbing C API functions.
- **Minor releases (3.X.0):** May add SQL API functions, require newer dependency versions, stub deprecated C API functions.
- **Major releases (X.0.0):** May remove SQL API functions and C API functions outright.

SQL function definitions SHALL include `-- Availability:` comments marking when they were added, and `-- Changed:` comments for signature modifications. Functions with changed signatures SHALL include `-- Replaces` comments referencing the deprecated signature and version.

#### Scenario: Availability comments on SQL functions
- **GIVEN** a function added in version 2.0.0
- **THEN** its SQL definition SHALL contain `-- Availability: 2.0.0`
- Validated by: postgis/postgis.sql.in, topology/sql/sqlmm.sql.in (code review)

#### Scenario: Replaces comments for changed signatures
- **GIVEN** a function whose signature changed in version 3.6.0
- **THEN** its definition SHALL contain `-- Replaces <old_name>(<old_args>) deprecated in 3.6.0`
- **AND** create_upgrade.pl SHALL use this to generate appropriate DROP + CREATE OR REPLACE
- Validated by: topology/sql/sqlmm.sql.in (e.g., ST_AddIsoNode)

#### Scenario: Patch release does not add SQL functions
- **GIVEN** a patch release upgrade (e.g., 3.5.1 to 3.5.2)
- **THEN** no new CREATE FUNCTION statements SHALL appear in the upgrade script
- **AND** no new dependencies SHALL be required
- Status: untested -- enforced by project policy, not automated test

### Requirement: Upgrade script generation via create_upgrade.pl
The `utils/create_upgrade.pl` Perl script SHALL transform a full SQL install script into an upgrade script by:
1. Converting CREATE FUNCTION to CREATE OR REPLACE FUNCTION
2. Wrapping type/operator creation in conditional blocks based on `-- Availability:` comments
3. Parsing `-- Replaces` comments to generate DROP FUNCTION calls for old signatures
4. Parsing `-- Changed:` comments to conditionally recreate changed objects
5. Adding a version check to ensure the library version matches

#### Scenario: create_upgrade.pl generates valid upgrade SQL
- **GIVEN** the postgis.sql install script
- **WHEN** `perl utils/create_upgrade.pl postgis.sql` is run
- **THEN** the output SHALL contain CREATE OR REPLACE FUNCTION for all functions
- **AND** types with Availability newer than the upgrade source SHALL be conditionally created
- Validated by: build system (make generates upgrade scripts)

#### Scenario: Replaces comments generate DROP before CREATE
- **GIVEN** a function with `-- Replaces old_func(old_args) deprecated in 3.6.0`
- **WHEN** create_upgrade.pl processes it
- **THEN** the output SHALL contain a conditional DROP FUNCTION for old_func(old_args) when upgrading from pre-3.6.0
- Validated by: utils/create_upgrade.pl (script logic)

#### Scenario: Version mismatch check in upgrade script
- **WHEN** an upgrade script is run but the shared library version does not match
- **THEN** the script SHALL raise an error before applying changes
- Validated by: build system upgrade script output

### Requirement: Extension dependency chain
The PostGIS extension family forms a dependency chain: `postgis` is the base extension with no dependencies. `postgis_raster`, `postgis_topology`, and `postgis_sfcgal` all depend on `postgis`. Dropping `postgis` when dependents exist SHALL fail unless CASCADE is used. Upgrading `postgis` SHALL be done before upgrading dependent extensions.

#### Scenario: Cannot drop postgis with dependents installed
- **GIVEN** postgis and postgis_topology are installed
- **WHEN** `DROP EXTENSION postgis` is executed (without CASCADE)
- **THEN** an error SHALL be raised about dependent extensions
- Validated by: PostgreSQL extension infrastructure (standard behavior)

#### Scenario: CASCADE drop removes all dependent extensions
- **GIVEN** postgis and postgis_raster are installed
- **WHEN** `DROP EXTENSION postgis CASCADE` is executed
- **THEN** both postgis and postgis_raster SHALL be removed
- Status: untested -- standard PostgreSQL CASCADE behavior, not PostGIS-specific test

#### Scenario: Upgrade order respects dependencies
- **WHEN** postgis_extensions_upgrade() runs
- **THEN** postgis SHALL be upgraded first (shortest name, processed first)
- **AND** dependent extensions SHALL be upgraded after postgis completes
- Validated by: postgis/postgis.sql.in (postgis_extensions_upgrade function logic)

### Requirement: Upgradeable versions list
The build system SHALL maintain a list of all versions from which upgrades are supported in `extensions/upgradeable_versions.mk`. This list starts at version 2.0.0 and includes every released version through the current development version. The upgrade-paths-rules.mk SHALL generate symlinked upgrade path files for each upgradeable version.

#### Scenario: All released versions are upgradeable
- **GIVEN** the upgradeable_versions.mk file
- **THEN** it SHALL list every PostGIS release from 2.0.0 onward
- **AND** each version SHALL have a corresponding upgrade path file generated at install time
- Validated by: extensions/upgradeable_versions.mk (file contents)

#### Scenario: Upgrade from oldest supported version
- **GIVEN** an installation of PostGIS 2.0.0
- **WHEN** ALTER EXTENSION postgis UPDATE is executed
- **THEN** the upgrade SHALL succeed (path 2.0.0 -> ANY -> current exists)
- Status: untested -- oldest version upgrade not tested in CI

#### Scenario: Development version in upgradeable list
- **THEN** the current development version (e.g., 3.6.3dev) SHALL be in the upgradeable list
- **AND** this allows upgrading from a dev build to the next release
- Validated by: extensions/upgradeable_versions.mk (last entry)

### Requirement: Version discovery functions
PostGIS SHALL provide SQL functions to query installed versions: `PostGIS_Version()` returns a short version string, `PostGIS_Full_Version()` returns detailed version info including all library versions (GEOS, PROJ, GDAL, etc.), `PostGIS_Lib_Version()` returns the library version, `PostGIS_Scripts_Installed()` returns the installed SQL script version. These functions enable upgrade scripts to check version compatibility.

#### Scenario: PostGIS_Full_Version returns comprehensive info
- **WHEN** `SELECT PostGIS_Full_Version()` is called
- **THEN** the result SHALL contain the PostGIS version, GEOS version, PROJ version, and GDAL version
- **AND** it SHALL indicate whether optional features (raster, topology, sfcgal) are available
- Validated by: regress/core/regress_management.sql (indirectly)

#### Scenario: Version mismatch detection
- **GIVEN** the SQL scripts are version 3.6.0 but the C library is 3.5.0
- **WHEN** `PostGIS_Full_Version()` is called
- **THEN** a warning SHALL be included indicating the version mismatch
- Validated by: postgis/postgis.sql.in (PostGIS_Full_Version function logic)

#### Scenario: PostGIS_Lib_Version returns library version
- **WHEN** `SELECT PostGIS_Lib_Version()` is called
- **THEN** a version string in Major.Minor.Patch format SHALL be returned
- Validated by: regress/core/regress_management.sql
