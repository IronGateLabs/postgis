## 1. Merge ecef-eci Feature Branch Work

- [x] 1.1 Review diff between develop and `feature/ecef-eci-implementation` (12 files, ~1763 lines) for conflicts with current develop state
- [x] 1.2 Cherry-pick or merge SQL wrapper functions (`postgis/ecef_eci.sql.in`, `postgis/lwgeom_ecef_eci.c`) into develop
- [x] 1.3 Cherry-pick extension packaging (`extensions/postgis_ecef_eci/Makefile.in`, `postgis_ecef_eci.control.in`) and build system changes (`configure.ac`, `extensions/Makefile.in`, `postgis/Makefile.in`)
- [x] 1.4 Cherry-pick DocBook documentation (`doc/ecef_eci.xml`) and regression tests (`regress/core/ecef_eci.sql`, `regress/core/ecef_eci_expected`, `regress/core/tests.mk.in`)
- [x] 1.5 Cherry-pick ECI SRID registration INSERT statements for ICRF (900001), J2000 (900002), TEME (900003) in `spatial_ref_sys`
- [x] 1.6 Resolve any merge conflicts, particularly in `Makefile.in` and `.gitignore`
- [x] 1.7 Verify build succeeds with merged code: `./autogen.sh && ./configure && make && make check`

## 2. ST_Transform Epoch Integration

- [x] 2.1 Add SQL overload `ST_Transform(geometry, integer, timestamptz)` in `postgis/postgis.sql.in` with IMMUTABLE volatility and PARALLEL SAFE
- [x] 2.2 Implement C function for the new overload in `postgis/lwgeom_transform.c`: convert TIMESTAMPTZ to decimal-year, detect ECI CRS family, dispatch to `lwgeom_transform_ecef_to_eci()` or `lwgeom_transform_eci_to_ecef()`
- [x] 2.3 Modify existing `ST_Transform(geometry, integer)` C path to detect `LW_CRS_INERTIAL` source/target via `lwsrid_get_crs_family()` and extract per-point M coordinates as epoch
- [x] 2.4 Implement error handling: raise SQL error when ECI transform is attempted without epoch (no M coordinates, no explicit epoch parameter)
- [x] 2.5 Handle edge case: geometry has M coordinates but they are not valid epoch values (e.g., negative, extremely large) -- raise a warning or error
- [x] 2.6 Add `postgis_upgrade.sql.in` entry for the new `ST_Transform` overload

## 3. PROJ Version Gating Refinement

- [x] 3.1 Audit compile-time guards in `liblwgeom/liblwgeom.h.in` and `liblwgeom/lwgeom_eci.c` for any `POSTGIS_PROJ_VERSION` checks that gate ECI-specific functions
- [x] 3.2 Remove or relax PROJ version guards on ECI transform functions (pure C ERA math, no PROJ dependency)
- [x] 3.3 Ensure PROJ version guards remain on non-ECI time-dependent transform paths (dynamic datums, plate motion)
- [x] 3.4 Add code comment in `lwgeom_eci.c` documenting that ECI transforms are PROJ-independent
- [x] 3.5 Verify ECI transforms compile and pass tests when linked against PROJ 6.x, 7.x, 8.x, and 9.x (CI matrix)

## 4. Verification and Testing

- [x] 4.1 Run existing C unit tests: `cu_eci` suite covering ERA, round-trip transforms, bounding boxes, edge cases
- [x] 4.2 Run merged regression tests from feature branch: `regress/core/ecef_eci.sql`
- [x] 4.3 Add regression tests for `ST_Transform(eci_geom, 4978)` with M-coordinate epochs (per-point epoch extraction)
- [x] 4.4 Add regression tests for `ST_Transform(eci_geom, 4978, epoch)` with explicit TIMESTAMPTZ epoch parameter
- [x] 4.5 Add regression test for missing epoch error: `ST_Transform(eci_geom_no_m, 4978)` without M and without explicit epoch
- [x] 4.6 Add regression tests verifying `ST_Transform(ecef_geom, 900001, epoch)` (ECEF-to-ECI direction)
- [x] 4.7 Verify round-trip: `ST_Transform(ST_Transform(eci_geom, 4978, epoch), 900001, epoch)` recovers original coordinates within floating-point tolerance
- [x] 4.8 Verify ECI SRID entries queryable: `SELECT * FROM spatial_ref_sys WHERE srid IN (900001, 900002, 900003)`
- [x] 4.9 Verify `postgis_ecef_eci` extension installs cleanly: `CREATE EXTENSION postgis_ecef_eci`
- [x] 4.10 Run AddressSanitizer CI mode with ECI transform fuzzer (already on develop at `d190725f7`)
