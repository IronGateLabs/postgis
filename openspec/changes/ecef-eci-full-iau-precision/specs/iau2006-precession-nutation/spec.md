## ADDED Requirements

### Requirement: Vendored ERFA subset for precession-nutation

The system SHALL include a vendored subset of the ERFA (Essential Routines for Fundamental Astronomy) library under `liblwgeom/erfa/` to provide the IAU 2006/2000A bias-precession-nutation model. ERFA is distributed under BSD-3-Clause and is the astropy project's GPL-compatible fork of IAU SOFA.

#### Scenario: Vendored files preserve upstream license

- **WHEN** a reviewer inspects any file under `liblwgeom/erfa/`
- **THEN** the file SHALL retain its original ERFA copyright header and BSD-3-Clause license notice unchanged from upstream
- **AND** a top-level `liblwgeom/erfa/ERFA-VERSION.txt` SHALL record the upstream release version, source URL, and license summary

#### Scenario: Vendored subset compiles into liblwgeom.a

- **WHEN** `make -C liblwgeom` is executed on a clean checkout
- **THEN** the vendored `erfa/` sources SHALL compile cleanly into the existing `liblwgeom.a` archive
- **AND** no new shared library or installable artifact SHALL be produced
- **AND** the build SHALL NOT depend on any system ERFA package

#### Scenario: Vendored subset is excluded from SonarCloud analysis

- **WHEN** SonarCloud Scan runs on a PR that modifies files outside `liblwgeom/erfa/`
- **THEN** the scan SHALL NOT flag issues in `liblwgeom/erfa/**`
- **AND** the `sonar.exclusions` property in `sonar-project.properties` SHALL include `liblwgeom/erfa/**`

### Requirement: IAU 2006/2000A bias-precession-nutation matrix

The system SHALL compute the full celestial-to-terrestrial rotation matrix using ERFA's `eraPnm06a`, `eraEra00`, and `eraPom00` routines, implementing the IAU 2006 precession model combined with the IAU 2000A nutation series.

#### Scenario: Per-geometry matrix amortization

- **WHEN** `lwgeom_transform_ecef_to_eci_eop()` is called on a multi-point geometry
- **THEN** the bias-precession-nutation matrix SHALL be computed exactly once for the geometry's epoch
- **AND** the matrix SHALL be applied to every point in the geometry without rebuilding
- **AND** the per-geometry cost SHALL be dominated by matrix construction (~25,000 FLOPs), not by per-point transformation

#### Scenario: Matrix construction uses two-part Julian dates

- **WHEN** the bias-precession-nutation matrix is constructed for a given epoch
- **THEN** the date SHALL be passed to ERFA as a two-part Julian date `(jd1, jd2)` to preserve double-precision accuracy
- **AND** the `lweci_epoch_to_jd()` function SHALL return both components

#### Scenario: CIP corrections applied via X,Y offset injection

- **WHEN** Earth Orientation Parameters include non-zero CIP offsets (`dx`, `dy` from `postgis_eop`)
- **THEN** the corrections SHALL be applied by adding `dx`, `dy` to the X,Y components of the CIP before constructing the celestial-to-intermediate matrix via `eraC2ixys`
- **AND** applying CIP corrections as standalone Rx/Ry rotations SHALL NOT be used (this is an approximation that does not compose with the BPN matrix correctly)

### Requirement: Distinct frame handling for ICRF, J2000, and TEME

The system SHALL produce distinct results for ICRF, J2000, and TEME frame conversions, reflecting their differing definitions.

#### Scenario: ICRF uses ERFA default celestial-to-terrestrial matrix

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'ICRF')` is called
- **THEN** the transform SHALL produce output in the International Celestial Reference Frame using `eraC2t06a` directly
- **AND** the result SHALL match ERFA reference values to within 1 micrometer at Earth radius

#### Scenario: J2000 applies frame bias beyond ICRF

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'J2000')` is called
- **THEN** the transform SHALL produce output in the mean equator and equinox at J2000.0 frame
- **AND** the result SHALL differ from the ICRF result by the known frame bias offset (approximately 17 milliarcseconds)

#### Scenario: TEME uses Greenwich Mean Sidereal Time

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'TEME')` is called
- **THEN** the transform SHALL produce output in the True Equator, Mean Equinox frame used by SGP4/SDP4 satellite propagators
- **AND** the Earth rotation component SHALL be Greenwich Mean Sidereal Time (`eraGmst06`), not Earth Rotation Angle
- **AND** the result SHALL differ from the ICRF and J2000 results by an amount consistent with the TEME definition

#### Scenario: Same-frame round-trip within precision contract

- **WHEN** `ST_ECEF_To_ECI(g, epoch, F)` is followed by `ST_ECI_To_ECEF(result, epoch, F)` for any supported frame F
- **THEN** the round-trip result SHALL match the input geometry coordinate-wise to within 1 micrometer

### Requirement: Precision contract

The system SHALL provide a documented precision contract for ECEF/ECI transformations backed by IAU 2006/2000A computation.

#### Scenario: Precision with EOP data available

- **WHEN** EOP data (via `postgis_eop_interpolate`) is available for the requested epoch
- **THEN** ECEF↔ECI transformations SHALL be accurate to within 1 micrometer at Earth radius (approximately 6378 km scale) when compared to ERFA reference values
- **AND** this accuracy SHALL hold for epochs within the loaded EOP data range

#### Scenario: Precision without EOP data

- **WHEN** EOP data is NOT available for the requested epoch
- **THEN** the transform SHALL fall back to zero-correction IAU 2006/2000A and SHALL remain accurate to within approximately 6 centimeters at Earth radius (this is the accuracy of pure IAU 2006/2000A without UT1, polar motion, or CIP corrections)
- **AND** the fallback SHALL NOT raise an error; it SHALL proceed silently with zero corrections
