## Purpose

Registration of ECI reference frame SRIDs (ICRF, J2000, TEME) in PostGIS spatial_ref_sys.

## Requirements

### Requirement: ICRF SRID registration (900001)
The system SHALL register SRID 900001 in `spatial_ref_sys` representing the International Celestial Reference Frame (ICRF), the IAU-recommended quasi-inertial reference frame with origin at Earth's center.

#### Scenario: SRID 900001 exists after extension installation
- **WHEN** `SELECT srid, auth_name, auth_srid, srtext FROM spatial_ref_sys WHERE srid = 900001` is queried after `CREATE EXTENSION postgis_ecef_eci`
- **THEN** the result SHALL include a row with `auth_name='POSTGIS'`, `auth_srid=900001`, and a `srtext` containing 'ICRF' or 'International Celestial Reference Frame'

#### Scenario: Geometry with SRID 900001 is accepted
- **WHEN** `ST_SetSRID(ST_MakePoint(6378137, 0, 0), 900001)` is executed
- **THEN** the geometry SHALL be created successfully with SRID 900001

### Requirement: J2000 SRID registration (900002)
The system SHALL register SRID 900002 in `spatial_ref_sys` representing the J2000/EME2000 (Earth Mean Equator and Equinox of J2000.0) inertial reference frame.

#### Scenario: SRID 900002 exists after extension installation
- **WHEN** `SELECT srid, auth_name FROM spatial_ref_sys WHERE srid = 900002` is queried
- **THEN** the result SHALL include a row with `auth_name='POSTGIS'` and srtext referencing J2000 or EME2000

### Requirement: TEME SRID registration (900003)
The system SHALL register SRID 900003 in `spatial_ref_sys` representing the True Equator Mean Equinox (TEME) reference frame, the native output frame of the SGP4 orbit propagator.

#### Scenario: SRID 900003 exists after extension installation
- **WHEN** `SELECT srid, auth_name FROM spatial_ref_sys WHERE srid = 900003` is queried
- **THEN** the result SHALL include a row with `auth_name='POSTGIS'` and srtext referencing TEME

### Requirement: Conflict-safe registration
SRID registration SHALL use `INSERT ... ON CONFLICT DO NOTHING` (or equivalent) so that re-installing or upgrading the extension does not fail if SRIDs already exist in `spatial_ref_sys`.

#### Scenario: Extension reinstall does not error
- **WHEN** `DROP EXTENSION postgis_ecef_eci; CREATE EXTENSION postgis_ecef_eci;` is executed
- **THEN** the registration SHALL succeed without duplicate key errors, even if SRIDs remain from the previous installation

#### Scenario: User-defined SRID in range does not conflict
- **WHEN** a user has previously inserted a custom SRID 900004 and then installs the extension
- **THEN** the extension installation SHALL succeed -- only SRIDs 900001-900003 are registered, and user SRIDs outside this set are untouched

### Requirement: SRID consistency with C macros
The registered SRIDs SHALL match the SRID macros defined in `liblwgeom/liblwgeom.h.in`: `SRID_ECI_ICRF=900001`, `SRID_ECI_J2000=900002`, `SRID_ECI_TEME=900003`.

#### Scenario: SRID_IS_ECI macro matches registered SRIDs
- **WHEN** the C macro `SRID_IS_ECI(srid)` is evaluated for SRIDs 900001, 900002, 900003
- **THEN** all three SHALL return true, and these are exactly the SRIDs registered in `spatial_ref_sys`
