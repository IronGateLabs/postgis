## MODIFIED Requirements

### Requirement: EOP-enhanced transforms apply full IAU 2006/2000A corrections

The `ST_ECEF_To_ECI_EOP()` and `ST_ECI_To_ECEF_EOP()` functions SHALL apply UT1-UTC, polar motion (`xp`, `yp`), AND Celestial Intermediate Pole (CIP) offset (`dx`, `dy`) corrections via the IAU 2006/2000A bias-precession-nutation matrix. Previously, these functions applied only UT1 and polar motion, silently ignoring the `dx`/`dy` columns returned by `postgis_eop_interpolate`.

#### Scenario: All five EOP values are applied

- **WHEN** `ST_ECEF_To_ECI_EOP(geom, epoch, frame)` is called and `postgis_eop_interpolate(epoch)` returns a row with all five corrections present
- **THEN** the transform SHALL pass `dut1`, `xp`, `yp`, `dx`, `dy` to the underlying C function
- **AND** the C function SHALL apply `dut1` to produce UT1 from UTC before computing ERA
- **AND** the C function SHALL apply `xp`, `yp` via `eraPom00` to build the polar motion matrix
- **AND** the C function SHALL apply `dx`, `dy` by injecting them as corrections to the X,Y components of the CIP before building the celestial-to-intermediate matrix
- **AND** the final result SHALL be accurate to within 1 micrometer at Earth radius versus ERFA reference values

#### Scenario: CIP corrections are no longer silently dropped

- **WHEN** a test inserts a row into `postgis_eop` with non-zero `dx` and `dy` values
- **AND** `ST_ECEF_To_ECI_EOP(geom, test_epoch, 'ICRF')` is called for the matching epoch
- **THEN** the result SHALL differ from the same call with `dx=0, dy=0` by an amount consistent with the CIP correction magnitude (micrometer-scale at Earth radius)
- **AND** the difference SHALL be verifiable via comparison against ERFA reference values

#### Scenario: Missing EOP row falls back to zero-correction IAU 2006/2000A

- **WHEN** `postgis_eop_interpolate(epoch)` returns NULL for the requested epoch
- **THEN** the SQL wrapper SHALL fall back to `ST_ECEF_To_ECI(geom, epoch, frame)` which uses zero corrections
- **AND** the fallback SHALL still compute the full IAU 2006/2000A bias-precession-nutation matrix (only the EOP corrections are zero, not the whole matrix)
- **AND** the fallback result SHALL be accurate to ~6 centimeters at Earth radius (the precision contract for pure IAU 2006/2000A without EOP refinements)
