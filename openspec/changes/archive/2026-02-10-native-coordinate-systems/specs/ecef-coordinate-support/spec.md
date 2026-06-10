## ADDED Requirements

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

#### Scenario: ST_Distance on ECEF geometries returns Cartesian distance
- **WHEN** `ST_Distance` is called on two ECEF geometries (SRID 4978)
- **THEN** the system SHALL return the 3D Euclidean distance in meters (Cartesian distance between the two ECEF points)

#### Scenario: ST_Area on ECEF polygon raises error
- **WHEN** `ST_Area` is called on an ECEF polygon
- **THEN** the system SHALL raise an error indicating that area computation is not supported for geocentric coordinate systems

### Requirement: ECEF SRID in spatial_ref_sys
The `spatial_ref_sys` table SHALL include EPSG:4978 (WGS 84 geocentric) with correct `srtext` (WKT) and `proj4text` definitions.

#### Scenario: EPSG 4978 is available by default
- **WHEN** a user queries `SELECT * FROM spatial_ref_sys WHERE srid = 4978`
- **THEN** the result SHALL include auth_name='EPSG', auth_srid=4978, and valid srtext/proj4text definitions for WGS 84 geocentric CRS
