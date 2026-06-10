## MODIFIED Requirements

### Requirement: Epoch-parameterized ECI-to-ECEF transformation uses IAU 2006/2000A

ECI↔ECEF transformations SHALL use the IAU 2006 precession model combined with the IAU 2000A nutation series, implemented via the vendored ERFA routines. The previous implementation used a simplified IERS 2003 Earth Rotation Angle (ERA) rotation alone and collapsed all three supported frames (ICRF, J2000, TEME) to the same Z-axis rotation. The new implementation SHALL produce distinct, correct results for each frame.

#### Scenario: ICRF, J2000, and TEME produce distinct results

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'ICRF')`, `ST_ECEF_To_ECI(geom, epoch, 'J2000')`, and `ST_ECEF_To_ECI(geom, epoch, 'TEME')` are called for the same input geometry and epoch
- **THEN** the three results SHALL be distinct from each other
- **AND** the difference between ICRF and J2000 SHALL reflect the frame bias matrix (~17 milliarcseconds rotation)
- **AND** the TEME result SHALL reflect Greenwich Mean Sidereal Time rotation rather than Earth Rotation Angle

#### Scenario: Transform matches ERFA reference values

- **WHEN** a test case compares `ST_ECEF_To_ECI(geom, epoch, frame)` output to reference values generated offline via the `erfa` Python package (the same library we vendor)
- **THEN** the ST_X, ST_Y, and ST_Z components SHALL match the reference values to within 1 micrometer at Earth radius
- **AND** this SHALL hold for any supported frame and for epochs within ±50 years of J2000.0

#### Scenario: ERA-only fallback path is removed

- **WHEN** `lwgeom_transform_ecef_to_eci()` is called
- **THEN** the function SHALL compute the full IAU 2006/2000A bias-precession-nutation matrix via ERFA, not a simplified Z-rotation
- **AND** the simplified Z-rotation code path SHALL NOT exist as a runtime option (the `lweci_earth_rotation_angle` function remains but is rewritten to call `eraEra00` internally, preserving its signature while producing identical results)
