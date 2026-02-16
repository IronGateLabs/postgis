## Purpose

Core PostGIS support for ECEF (Earth-Centered Earth-Fixed) geocentric coordinate systems, including CRS detection, storage, transformation via PROJ, bounding box computation, and spatial function behavior.

## Requirements

### Requirement: ECEF CRS type detection
The system SHALL detect geocentric/ECEF coordinate reference systems by checking for `PJ_TYPE_GEOCENTRIC_CRS` when resolving SRIDs through PROJ, in addition to the existing `PJ_TYPE_GEOGRAPHIC_2D_CRS` and `PJ_TYPE_GEOGRAPHIC_3D_CRS` checks in `lwproj_from_PJ` (`liblwgeom/lwgeom_transform.c`).

#### Scenario: EPSG 4978 recognized as geocentric
- **WHEN** a geometry with SRID 4978 (WGS 84 geocentric) is passed to `ST_Transform`
- **THEN** the LWPROJ struct SHALL have its CRS family set to `LW_CRS_GEOCENTRIC` rather than falling through to the projected/unknown path

#### Scenario: Unknown SRID still handled gracefully
- **WHEN** a geometry with an unrecognized SRID is passed to `ST_Transform`
- **THEN** the system SHALL classify it as `LW_CRS_UNKNOWN` and proceed with current default behavior

### Requirement: ECEF geometry storage with 3D Cartesian coordinates
The system SHALL store ECEF geometries as standard `GEOMETRY` type with Z flag set, where X/Y/Z represent Earth-centered Cartesian coordinates in meters. No new PostgreSQL type is required.

#### Scenario: Insert ECEF point
- **WHEN** a user inserts `ST_SetSRID(ST_MakePoint(4510731, 4510731, 0), 4978)`
- **THEN** the geometry SHALL be stored with SRID 4978, X=4510731, Y=4510731, Z=0 in meters, with the Z flag set

#### Scenario: Round-trip ECEF coordinates
- **WHEN** a user inserts an ECEF point and then retrieves it with `ST_X`, `ST_Y`, `ST_Z`
- **THEN** the returned coordinates SHALL match the inserted values exactly (no unit conversion or normalization)

### Requirement: ECEF to geographic transformation via ST_Transform
The system SHALL support transformation from ECEF (SRID 4978) to geographic (SRID 4326) and vice versa using `ST_Transform`, delegating the coordinate math to PROJ.

#### Scenario: ECEF to WGS84 geographic
- **WHEN** `ST_Transform(ecef_geom, 4326)` is called on a geometry with SRID 4978
- **THEN** the result SHALL be a geographic geometry with longitude/latitude/height coordinates matching PROJ's geocentric-to-geographic conversion within 1mm precision

#### Scenario: WGS84 geographic to ECEF
- **WHEN** `ST_Transform(geog_geom, 4978)` is called on a geometry with SRID 4326
- **THEN** the result SHALL be a geocentric geometry with X/Y/Z in meters matching PROJ's geographic-to-geocentric conversion within 1mm precision

#### Scenario: ECEF via PROJ pipeline
- **WHEN** `ST_Transform(geom, '+proj=pipeline +step +proj=cart +ellps=WGS84', true, 4978)` is called
- **THEN** the result SHALL match the SRID-based transformation output

### Requirement: ECEF bounding box computation
The system SHALL compute bounding boxes for ECEF geometries using true Cartesian metric ranges (X/Y/Z in meters), not unit-sphere normalization.

#### Scenario: GBOX for ECEF point
- **WHEN** a bounding box is computed for an ECEF point at (6378137, 0, 0) (on equator, prime meridian, at surface)
- **THEN** the GBOX SHALL have xmin=xmax=6378137, ymin=ymax=0, zmin=zmax=0

#### Scenario: GBOX for ECEF linestring
- **WHEN** a bounding box is computed for an ECEF linestring spanning two surface points
- **THEN** the GBOX xmin/xmax/ymin/ymax/zmin/zmax SHALL reflect the actual metric coordinate ranges, not unit-sphere values

### Requirement: Spatial functions error on unsupported ECEF operations
Spatial functions that assume geographic or projected input (e.g., `ST_Distance` with geographic distance semantics, `ST_Area` on a spheroid) SHALL raise a clear error when called on ECEF geometries rather than returning silently incorrect results.

Functions are classified into three behavior categories for geocentric input:

1. **Dispatch**: Functions where a correct geocentric result exists and the function SHALL automatically produce it (e.g., `ST_Distance` dispatching to 3D Euclidean distance).
2. **Error**: Functions where no meaningful geocentric result exists and the function SHALL raise an error with a message containing "geocentric" and indicating the operation is not supported for this CRS family. Error-class functions include: `ST_Area`, `ST_Buffer`, `ST_OffsetCurve`, `ST_Centroid`, `ST_BuildArea`, `ST_Perimeter`, `ST_Azimuth`, `ST_Project`, `ST_Segmentize`.
3. **Pass-through**: Functions that are coordinate-system-agnostic and require no special handling (e.g., `ST_AsText`, `ST_X`, `ST_Y`, `ST_Z`, `ST_NPoints`).

The CRS family SHALL be determined by calling `lwsrid_get_crs_family()` on the geometry's SRID. A return value of `LW_CRS_GEOCENTRIC` triggers the dispatch or error behavior.

> **Note (contract alignment v0.2.0):** The guard function `srid_check_crs_family_not_geocentric()` now also blocks `LW_CRS_INERTIAL` (ECI SRIDs 900001+). This means error-class spatial functions raise the same guard errors for both geocentric and inertial CRS families. See the `eci-coordinate-support` spec for details on ECI guard behavior.

The `geometry → geography` cast SHALL raise a geocentric-specific error when the input geometry has a geocentric SRID, before the existing `srid_check_latlong` validation runs.

#### Scenario: ST_Distance on ECEF geometries returns 3D Euclidean distance
- **GIVEN** two ECEF points with SRID 4978: A at (6378137, 0, 0) and B at (0, 6378137, 0)
- **WHEN** `ST_Distance(A, B)` is called
- **THEN** the system SHALL return the 3D Euclidean distance (`sqrt((x2-x1)^2 + (y2-y1)^2 + (z2-z1)^2)`), approximately 9020047.8 meters
- **AND** the result SHALL match `ST_3DDistance(A, B)` exactly

#### Scenario: ST_Distance on ECEF geometries does not ignore Z
- **GIVEN** two ECEF points with SRID 4978 that share the same X and Y but differ in Z
- **WHEN** `ST_Distance(A, B)` is called
- **THEN** the result SHALL be non-zero, equal to the absolute Z difference

#### Scenario: ST_DWithin on ECEF uses 3D Euclidean distance
- **GIVEN** two ECEF points with SRID 4978 separated by 1000m in Z only
- **WHEN** `ST_DWithin(A, B, 500)` is called
- **THEN** the result SHALL be false (3D distance 1000 > tolerance 500)
- **AND WHEN** `ST_DWithin(A, B, 1500)` is called
- **THEN** the result SHALL be true

#### Scenario: ST_Area on ECEF polygon raises error
- **WHEN** `ST_Area` is called on a polygon with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and indicating that area computation is not supported for this CRS family

#### Scenario: ST_Buffer on ECEF geometry raises error
- **WHEN** `ST_Buffer` is called on a geometry with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and indicating that buffering is not supported for this CRS family

#### Scenario: ST_Length on ECEF linestring returns 3D length
- **GIVEN** an ECEF linestring with SRID 4978
- **WHEN** `ST_Length` is called
- **THEN** the system SHALL return the 3D Euclidean path length (sum of 3D segment lengths)
- **AND** the result SHALL match `ST_3DLength` for the same geometry

#### Scenario: Coordinate accessor functions work on ECEF without error
- **GIVEN** an ECEF point with SRID 4978 at (6378137, 0, 0)
- **WHEN** `ST_X`, `ST_Y`, `ST_Z`, `ST_AsText`, `ST_AsEWKT`, `ST_NPoints` are called
- **THEN** each function SHALL return the correct result without error (pass-through behavior)

#### Scenario: ST_Perimeter on ECEF polygon raises error
- **WHEN** `ST_Perimeter` is called on a polygon with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and the SRID value

#### Scenario: ST_Azimuth on ECEF points raises error
- **WHEN** `ST_Azimuth` is called on two points with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and the SRID value

#### Scenario: ST_Project on ECEF point raises error
- **WHEN** `ST_Project` is called on a point with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and the SRID value
- **AND** both the direction variant and the geometry variant of ST_Project SHALL raise the same error

#### Scenario: ST_Segmentize on ECEF linestring raises error
- **WHEN** `ST_Segmentize` is called on a linestring with SRID 4978
- **THEN** the system SHALL raise an error with a message containing "geocentric" and the SRID value

#### Scenario: Geography cast on ECEF geometry raises geocentric-specific error
- **WHEN** a geometry with SRID 4978 is cast to geography via `geom::geography`
- **THEN** the system SHALL raise an error with a message containing "geocentric" before the generic latlong validation runs
- **AND** the error message SHALL advise transforming to a geographic CRS first

#### Scenario: Guards do not affect non-geocentric input
- **WHEN** `ST_Perimeter`, `ST_Azimuth`, `ST_Project`, or `ST_Segmentize` is called on a geometry with SRID 4326 (geographic) or SRID 32632 (projected)
- **THEN** the function SHALL execute normally without error

### Requirement: ECEF SRID in spatial_ref_sys
The `spatial_ref_sys` table SHALL include EPSG:4978 (WGS 84 geocentric) with correct `srtext` (WKT) and `proj4text` definitions.

#### Scenario: EPSG 4978 is available by default
- **WHEN** a user queries `SELECT * FROM spatial_ref_sys WHERE srid = 4978`
- **THEN** the result SHALL include auth_name='EPSG', auth_srid=4978, and valid srtext/proj4text definitions for WGS 84 geocentric CRS
