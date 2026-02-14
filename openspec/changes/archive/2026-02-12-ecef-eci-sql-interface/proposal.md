## Why

PostGIS now has foundational C-level support for ECEF (Earth-Centered Earth-Fixed) and ECI (Earth-Centered Inertial) coordinate systems — a CRS family enum (`LW_CRS_FAMILY`), SRID macros for ECI frames (ICRF=900001, J2000=900002, TEME=900003), Earth Rotation Angle computation, and `lwgeom_transform_ecef_to_eci()`/`lwgeom_transform_eci_to_ecef()` functions in `liblwgeom`. However, none of this infrastructure is accessible from SQL. Users in satellite tracking, space situational awareness, geodetic research, and defense domains cannot:

- **Convert between ECEF and ECI frames** at the SQL level — the C functions exist but have no SQL wrapper
- **Extract ECEF Cartesian coordinates** with IMMUTABLE functions suitable for index expressions and materialized views — `ST_X`/`ST_Y`/`ST_Z` are STABLE, not IMMUTABLE, and perform no SRID validation for ECEF
- **Use ECI SRIDs** — frames ICRF (900001), J2000 (900002), and TEME (900003) are defined as C macros but not registered in `spatial_ref_sys`
- **Access Earth Orientation Parameters** for precision transforms — no EOP table, no loader, no interpolation infrastructure
- **Install ECEF/ECI support independently** — no separate extension packaging; users must build from source with the full PostGIS fork
- **Trust that GiST 3D indexes work correctly** with ECEF/ECI coordinate ranges (~6.4M meters vs. ±180/±90 degrees)

This change proposes a complete SQL-level interface for the existing C infrastructure, packaged as a separate `postgis_ecef_eci` extension.

## What Changes

- **SQL wrapper functions** for ECEF-to-ECI and ECI-to-ECEF frame conversion using TIMESTAMPTZ epoch parameters
- **ECEF coordinate accessor functions** (`ST_ECEF_X`, `ST_ECEF_Y`, `ST_ECEF_Z`) that are IMMUTABLE, PARALLEL SAFE, and validate SRID
- **ECI SRID registration** in `spatial_ref_sys` for ICRF (900001), J2000 (900002), and TEME (900003)
- **Earth Orientation Parameter infrastructure** — storage table, IERS Bulletin A/B loader, interpolation function, and refresh procedure
- **Extension packaging** as `postgis_ecef_eci` following the `postgis_sfcgal` pattern
- **GiST 3D spatial index verification** with ECEF/ECI geometries

## Capabilities

### New Capabilities
- `sql-frame-conversion`: SQL functions `ST_ECEF_To_ECI` and `ST_ECI_To_ECEF` wrapping the existing C transform functions, accepting TIMESTAMPTZ epoch and TEXT frame parameter
- `ecef-coordinate-accessors`: `ST_ECEF_X`, `ST_ECEF_Y`, `ST_ECEF_Z` functions that are IMMUTABLE, PARALLEL SAFE, and validate the input geometry has an ECEF SRID (4978)
- `eci-srid-registration`: `spatial_ref_sys` entries for ECI SRIDs 900001 (ICRF), 900002 (J2000), and 900003 (TEME) with appropriate WKT and proj4text definitions
- `earth-orientation-parameters`: `postgis_eop` table schema, IERS data loader procedure, interpolation function, and periodic refresh procedure
- `extension-packaging`: Separate `postgis_ecef_eci` PostgreSQL extension with control file, install/upgrade scripts, following the `postgis_sfcgal` packaging pattern
- `spatial-index-verification`: Verification that GiST 3D indexes (`gist_geometry_ops_nd`) work correctly with ECEF and ECI coordinate ranges

### Modified Capabilities
<!-- No existing openspec/specs/ capabilities are modified by this change -->

## Impact

- **C layer (`liblwgeom/`)**: No modifications. The existing `lwgeom_eci.c` functions, `LW_CRS_FAMILY` enum, and SRID macros in `liblwgeom.h.in` are used as-is. New C functions for coordinate accessors are thin wrappers around existing `gserialized_peek_first_point`.
- **SQL layer (`postgis/`)**: New SQL function declarations for frame conversion and coordinate accessors. C implementations added to a new source file (`postgis/lwgeom_ecef_eci.c`) or to `postgis/lwgeom_ogc.c` following existing accessor patterns.
- **`spatial_ref_sys`**: Three new INSERT statements for ECI SRIDs in the 900001-900003 range.
- **Extension packaging**: New directory `extensions/postgis_ecef_eci/` with control file, Makefile, and SQL scripts — following the `postgis_sfcgal` pattern exactly.
- **Spatial indexing**: No code changes; verification tests confirm existing `gist_geometry_ops_nd` operator class handles ECEF/ECI coordinate ranges correctly.
- **Dependencies**: Requires `postgis >= 3.4`. No new external dependencies. EOP loader uses `pg_read_file` or `COPY` for IERS data; no HTTP client required.
- **Downstream consumers**: The `postgis_ecef_eci` extension can be installed alongside stock PostGIS without affecting existing spatial operations.
