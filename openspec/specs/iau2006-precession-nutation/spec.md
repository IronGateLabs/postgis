## Purpose

Full IAU 2006 precession and IAU 2000A nutation model for ECEF↔ECI coordinate transformations, backed by a vendored subset of the ERFA (Essential Routines for Fundamental Astronomy) library. Distinct handling for the three supported celestial frames (ICRF, J2000, TEME) with documented precision contracts.

## Requirements

### Requirement: Vendored ERFA subset

The system SHALL include a vendored subset of the ERFA library under `liblwgeom/erfa/` to provide the IAU 2006/2000A bias-precession-nutation model. ERFA is distributed under BSD-3-Clause and is the astropy project's GPL-compatible fork of IAU SOFA. Files SHALL be byte-identical to upstream.

#### Scenario: Vendored files preserve upstream license

- **WHEN** a reviewer inspects any file under `liblwgeom/erfa/`
- **THEN** the file SHALL retain its original ERFA copyright header and BSD-3-Clause license notice unchanged from upstream
- **AND** `liblwgeom/erfa/LICENSE` SHALL contain the full BSD-3-Clause license text
- **AND** `liblwgeom/erfa/ERFA-VERSION.txt` SHALL record the upstream release version, source URL, and vendored file list with per-file purpose

#### Scenario: Vendored subset compiles into liblwgeom.a

- **WHEN** `make -C liblwgeom` is executed on a clean checkout
- **THEN** the vendored `erfa/` sources SHALL compile cleanly into the existing `liblwgeom.a` archive
- **AND** compilation SHALL use `-Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable` scoped to the `erfa/` subdirectory only
- **AND** all other fork code SHALL retain `-Wall -Werror`
- **AND** the build SHALL NOT depend on any system ERFA package

#### Scenario: Vendored subset is excluded from fork tooling

- **WHEN** SonarCloud Scan, codespell, clang-format, or the trailing-blanks check runs
- **THEN** files under `liblwgeom/erfa/**` SHALL NOT be analyzed or modified
- **AND** `sonar-project.properties` `sonar.exclusions`, `.codespellrc`, `.pre-commit-config.yaml`, and `GNUmakefile.in check-no-trailing-blanks` SHALL all include an exclusion for `liblwgeom/erfa/`

### Requirement: IAU 2006/2000A celestial-to-terrestrial matrix

The system SHALL compute the full celestial-to-terrestrial rotation matrix using ERFA's `eraXy06`, `eraS06`, `eraC2ixys`, `eraPom00`, `eraEra00`, and `eraC2tcio` routines, implementing the IAU 2006 precession model combined with the IAU 2000A nutation series. The matrix SHALL be built once per geometry and amortized across all points.

#### Scenario: Per-geometry matrix amortization

- **WHEN** `lwgeom_transform_ecef_to_eci_eop()` is called on a multi-point geometry
- **THEN** the bias-precession-nutation matrix SHALL be computed exactly once for the geometry's epoch via `lweci_build_c2t_matrix`
- **AND** the matrix SHALL be applied to every point via `ptarray_apply_matrix3x3` without being rebuilt

#### Scenario: CIP corrections injected via X, Y offsets

- **WHEN** Earth Orientation Parameters include non-zero CIP offsets (`dx`, `dy` in arcseconds from `postgis_eop`)
- **THEN** the corrections SHALL be applied by adding `dx*ARCSEC_TO_RAD`, `dy*ARCSEC_TO_RAD` to the IAU 2006 CIP X, Y coordinates BEFORE building the celestial-to-intermediate matrix via `eraC2ixys`
- **AND** standalone Rx/Ry rotations for CIP corrections SHALL NOT be used because they do not compose correctly with the BPN matrix

#### Scenario: Two-part Julian date precision

- **WHEN** a date is passed to ERFA's matrix construction routines
- **THEN** the date SHALL be represented as a two-part JD `(jd1, jd2)` where `jd1 = 2400000.5` (MJD epoch) and `jd2 = MJD + fraction_of_day`
- **AND** `lweci_epoch_to_jd_two_part()` SHALL provide this conversion from decimal year
- **AND** the two-part form SHALL preserve ~16 bits of extra precision versus a single-double JD across many-year spans

### Requirement: Distinct handling for ICRF, J2000, and TEME frames

The system SHALL produce distinct, physically correct results for each of the three supported ECI frames. They SHALL NOT collapse to the same rotation.

#### Scenario: ICRF uses ERFA default celestial-to-terrestrial

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'ICRF')` is called
- **THEN** the transform SHALL use the matrix produced by `eraC2tcio(rc2i, eraEra00(jd_ut1), rpom)` without frame-specific post-adjustment
- **AND** the output frame SHALL be the Geocentric Celestial Reference Frame (GCRF/ICRF)

#### Scenario: J2000 applies frame bias on top of ICRF

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'J2000')` is called
- **THEN** the transform SHALL right-multiply the ICRF matrix by the inverse IAU 2000 frame bias built from `eraBi00`
- **AND** the result SHALL differ from the ICRF result by sub-meter at Earth radius (approximately 17 milliarcseconds rotation)

#### Scenario: TEME uses Greenwich Mean Sidereal Time

- **WHEN** `ST_ECEF_To_ECI(geom, epoch, 'TEME')` is called
- **THEN** the transform SHALL rebuild the matrix via `eraC2tcio(rc2i, eraGmst06(...), rpom)` using Greenwich Mean Sidereal Time instead of Earth Rotation Angle
- **AND** the result SHALL differ from the ICRF result by tens of kilometers at Earth radius (driven by the GMST vs ERA ~1 hour arc offset)

### Requirement: Precision contract

The system SHALL provide a documented precision contract for ECEF↔ECI transformations validated by ground-truth regression tests against reference values generated via pyerfa (the Python bindings to the same ERFA C library).

#### Scenario: Precision with EOP data

- **WHEN** EOP data (via `postgis_eop_interpolate`) is available for the requested epoch
- **THEN** ECEF↔ECI transformations SHALL match pyerfa reference values to within 1 micrometer at Earth radius scale (~6378 km)
- **AND** this accuracy SHALL hold for all three frames (ICRF, J2000, TEME)

#### Scenario: Precision without EOP data

- **WHEN** EOP data is NOT available
- **THEN** the transform SHALL fall back to zero-correction IAU 2006/2000A
- **AND** the result SHALL remain accurate to approximately 6 centimeters at Earth radius (which is the precision of pure IAU 2006/2000A without UT1, polar motion, or CIP refinements)
- **AND** the fallback SHALL NOT raise an error; it SHALL proceed silently with zero corrections

#### Scenario: Round-trip preserves coordinates

- **WHEN** `ST_ECI_To_ECEF(ST_ECEF_To_ECI(geom, epoch, frame), epoch, frame)` is called for any supported frame F
- **THEN** the round-trip result SHALL match the input geometry within 1 millimeter at Earth radius scale (representing the accumulated floating-point error of the 3x3 matrix build plus its transpose plus two matrix-vector multiplies)

### Requirement: Ground-truth regression tests

The system SHALL include regression tests that compare PostGIS transform output to reference values generated offline via pyerfa. Reference generation SHALL be reproducible.

#### Scenario: Reference values are regeneratable

- **WHEN** a developer wants to regenerate expected values after bumping the vendored ERFA version
- **THEN** running `python3 ci/generate_erfa_reference.py` SHALL emit inline SQL that can be pasted into `regress/core/ecef_eci_iau2006.sql`
- **AND** the script SHALL mirror `lweci_build_c2t_matrix` exactly in Python via the pyerfa package

#### Scenario: Regression test asserts sub-micron agreement

- **WHEN** `regress/core/ecef_eci_iau2006.sql` is run as part of `make check`
- **THEN** the test SHALL compare `ST_ECEF_To_ECI` and `ST_ECI_To_ECEF` output to pre-computed pyerfa reference values for a matrix of epochs (2000.0, 2024.5, 2030.0) and frames (ICRF, J2000, TEME)
- **AND** the absolute component-wise tolerance SHALL be 1e-6 meters
- **AND** the test SHALL verify that the three frames produce distinct results consistent with their physical differences
