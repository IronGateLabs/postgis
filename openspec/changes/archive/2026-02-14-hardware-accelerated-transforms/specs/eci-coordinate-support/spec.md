## MODIFIED Requirements

### Requirement: Epoch-parameterized ECI-to-ECEF transformation
The system SHALL support transformation between ECI and ECEF frames with an epoch parameter that accounts for Earth's rotation. The epoch MAY be provided as the M coordinate of `POINT4D` geometries or as an explicit parameter to `ST_Transform`. Transform implementations SHALL use the fastest available hardware acceleration (SIMD or GPU) with automatic fallback to scalar.

#### Scenario: ECI to ECEF with M-coordinate epoch
- **WHEN** `ST_Transform(eci_geom, 4978)` is called on an ECI geometry where each point's M coordinate contains the epoch (decimal year)
- **THEN** the transformation SHALL apply Earth rotation correction for each point's epoch, producing correct ECEF coordinates, using SIMD-accelerated rotation when available

#### Scenario: ECI to ECEF with explicit epoch parameter
- **WHEN** `ST_Transform(eci_geom, 4978, epoch => '2025-01-01T00:00:00Z')` is called with a single epoch for the entire geometry
- **THEN** the transformation SHALL apply Earth rotation correction at the specified epoch for all points, using SIMD-accelerated uniform rotation when available

#### Scenario: Missing epoch raises error
- **WHEN** an ECI-to-ECEF transformation is attempted without epoch information (no M coordinates and no explicit epoch parameter)
- **THEN** the system SHALL raise an error indicating that epoch is required for inertial-to-fixed frame transformations

#### Scenario: Dedicated SQL wrapper functions
- **WHEN** `ST_ECEF_To_ECI(geom, epoch, frame)` or `ST_ECI_To_ECEF(geom, epoch)` is called
- **THEN** the function SHALL perform the conversion using the C transform functions with the TIMESTAMPTZ epoch converted to decimal-year
