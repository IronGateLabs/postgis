## ADDED Requirements

### Requirement: PG-Strom compatibility evaluation
The system SHALL include documentation and test results evaluating PG-Strom's GPU-PostGIS layer for ECI/ECEF workloads.

#### Scenario: PG-Strom install and baseline
- **WHEN** PG-Strom is installed alongside PostGIS with the ECI extension
- **THEN** the evaluation SHALL measure baseline GPU acceleration for standard PostGIS functions (ST_Distance, ST_DWithin, ST_Contains) on ECEF geometries and document which functions PG-Strom accelerates

#### Scenario: ECI function GPU support assessment
- **WHEN** ECI-specific functions (ST_ECEF_To_ECI, ST_ECI_To_ECEF, ST_Transform with ECI SRIDs) are used in WHERE clauses
- **THEN** the evaluation SHALL document whether PG-Strom can intercept these functions and whether they fall back to CPU

### Requirement: ECI device function contribution
The system SHALL provide CUDA device function implementations of ECI transform operations suitable for inclusion in PG-Strom's GPU-PostGIS extension.

#### Scenario: ERA computation device function
- **WHEN** PG-Strom's GPU executor encounters an ECI transform in a query plan
- **THEN** the contributed device function SHALL compute Earth Rotation Angle using the same IERS 2003 formula as the CPU implementation and apply Z-axis rotation on GPU

#### Scenario: Device function numerical equivalence
- **WHEN** the GPU device function processes the same input as the CPU function
- **THEN** the output SHALL be numerically equivalent within double-precision tolerance (max difference < 1e-10)

#### Scenario: PG-Strom contribution packaging
- **WHEN** the ECI device functions are ready for contribution
- **THEN** they SHALL be packaged as a patch or pull request against PG-Strom's `gpu_postgis.cu` with appropriate test coverage
