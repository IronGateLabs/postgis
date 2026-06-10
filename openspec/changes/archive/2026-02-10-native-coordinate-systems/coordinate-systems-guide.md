# Native Coordinate Systems Support - User Guide

## Overview

PostGIS now supports multiple coordinate reference system (CRS) families as
first-class citizens, including ECEF (Earth-Centered Earth-Fixed) geocentric
coordinates and ECI (Earth-Centered Inertial) frames used in aerospace and
satellite applications.

---

## 1. CRS Family Taxonomy

PostGIS classifies every SRID into one of seven CRS families:

| Family | Enum | Example SRIDs | Description |
|--------|------|---------------|-------------|
| Geographic | `LW_CRS_GEOGRAPHIC` | EPSG:4326, EPSG:4269, EPSG:4979 | Lat/lon on ellipsoid |
| Projected | `LW_CRS_PROJECTED` | EPSG:32632, EPSG:3857, EPSG:2154 | Planar Cartesian from map projection |
| Geocentric | `LW_CRS_GEOCENTRIC` | EPSG:4978, EPSG:4936 | ECEF Cartesian (meters from Earth center) |
| Inertial | `LW_CRS_INERTIAL` | SRID:900001 (ICRF), 900002 (J2000), 900003 (TEME) | Non-rotating Earth-centered |
| Topocentric | `LW_CRS_TOPOCENTRIC` | User-defined | Local tangent plane (ENU/NED) |
| Engineering | `LW_CRS_ENGINEERING` | User-defined | Local/engineering CRS |
| Unknown | `LW_CRS_UNKNOWN` | Unresolvable SRIDs | Cannot determine family |

### SQL Function: `postgis_crs_family(srid integer)`

Returns the CRS family name for any SRID:

```sql
SELECT postgis_crs_family(4326);    -- 'geographic'
SELECT postgis_crs_family(32632);   -- 'projected'
SELECT postgis_crs_family(4978);    -- 'geocentric'
SELECT postgis_crs_family(900001);  -- 'inertial'
```

### Extended ST_Summary

`ST_Summary` now includes CRS family information when an SRID is set:

```sql
SELECT ST_Summary(ST_SetSRID(ST_MakePoint(0, 0, 6378137), 4978));
-- 'Point[ZS, crs_family=geocentric]'
```

---

## 2. ECEF (Geocentric) Coordinate Usage

### What is ECEF?

ECEF (Earth-Centered Earth-Fixed) is a 3D Cartesian coordinate system with:
- Origin at Earth's center of mass
- X axis pointing to the intersection of the equator and prime meridian
- Y axis pointing to the intersection of the equator and 90E longitude
- Z axis pointing to the North Pole
- Units in meters

The standard SRID for WGS 84 ECEF is **EPSG:4978**.

### Storage

ECEF coordinates are stored as GEOMETRY (not GEOGRAPHY) since they are Cartesian:

```sql
-- Create a table for ECEF positions
CREATE TABLE satellite_positions (
    id SERIAL PRIMARY KEY,
    name TEXT,
    position GEOMETRY(PointZ, 4978)
);

-- Insert a point on Earth's surface at the equator/prime meridian
INSERT INTO satellite_positions (name, position)
VALUES ('GPS Satellite', ST_SetSRID(ST_MakePoint(26600000, 0, 0), 4978));
```

### Transformation

Convert between geographic and ECEF coordinates using `ST_Transform`:

```sql
-- Geographic (4326) to ECEF (4978)
SELECT ST_AsText(
    ST_Transform(
        ST_SetSRID(ST_MakePoint(0, 0, 0), 4326),
        4978
    )
);
-- Returns a 3D point in meters from Earth center

-- ECEF (4978) to Geographic (4326)
SELECT ST_AsText(
    ST_Transform(
        ST_SetSRID(ST_MakePoint(6378137, 0, 0), 4978),
        4326
    )
);
-- Returns longitude/latitude/height
```

### Round-trip Precision

Geographic -> ECEF -> Geographic round-trips preserve sub-millimeter precision:

```sql
WITH original AS (
    SELECT ST_SetSRID(ST_MakePoint(-122.4194, 37.7749, 100), 4326) AS geom
),
roundtrip AS (
    SELECT ST_Transform(ST_Transform(geom, 4978), 4326) AS geom
    FROM original
)
SELECT
    ST_Distance(o.geom::geography, r.geom::geography) AS distance_m
FROM original o, roundtrip r;
-- distance_m < 0.001 (sub-millimeter)
```

### Spatial Function Behavior with ECEF

| Function | ECEF Behavior |
|----------|---------------|
| `ST_Transform` | Full support for ECEF <-> geographic/projected |
| `ST_Distance` | Returns 3D Euclidean distance in meters |
| `ST_Area` | Raises error (not meaningful for geocentric) |
| `ST_Buffer` | Raises error (not meaningful for geocentric) |
| `ST_Intersects` | CRS family mismatch error if mixed with geographic |
| GIST Index | Supported via standard Cartesian bounding boxes |

### Bounding Boxes

ECEF geometries use standard Cartesian GBOX computation. Coordinates range
from approximately -6,378,137 to +6,378,137 meters in X/Y and -6,356,752
to +6,356,752 meters in Z for points on Earth's surface.

---

## 3. ECI (Earth-Centered Inertial) Frame Support

### What is ECI?

ECI (Earth-Centered Inertial) frames are non-rotating reference frames with
the same origin as ECEF (Earth's center of mass). Unlike ECEF which rotates
with the Earth, ECI frames maintain a fixed orientation relative to the stars.

Common ECI frames:
- **ICRF** (International Celestial Reference Frame) - SRID 900001
- **J2000** (Julian 2000.0 epoch) - SRID 900002
- **TEME** (True Equator Mean Equinox) - SRID 900003, used by TLE/SGP4

### ECI SRID Registration

ECI frames use reserved SRIDs in the 900001-900099 range:

```sql
-- Register ECI frame in spatial_ref_sys
INSERT INTO spatial_ref_sys (srid, auth_name, auth_srid, srtext, proj4text)
VALUES (
    900001, 'CUSTOM', 900001,
    'LOCAL_CS["ICRF",LOCAL_DATUM["ICRF",0],UNIT["metre",1]]',
    '+proj=geocent +datum=WGS84 +units=m +no_defs'
);
```

### Epoch Parameterization

Converting between ECI and ECEF requires knowing the time (epoch) because
the Earth rotates. The epoch is specified as a decimal year (e.g., 2024.5
for mid-2024).

**M-coordinate as epoch**: For time series data, the M coordinate can store
the epoch for each point:

```sql
-- Satellite track with epoch in M coordinate
CREATE TABLE satellite_track (
    id SERIAL PRIMARY KEY,
    trajectory GEOMETRY(LinestringZM, 900001)
);

INSERT INTO satellite_track (trajectory)
VALUES (ST_GeomFromText(
    'LINESTRING ZM (
        6878137 0 0 2024.0,
        0 6878137 0 2024.0001,
        -6878137 0 0 2024.0002
    )', 900001
));
```

### ECI-ECEF Conversion

The conversion uses the Earth Rotation Angle (ERA) from the IERS 2003 model:

```
ECEF = Rz(-ERA) * ECI    (ECI to ECEF)
ECI  = Rz(+ERA) * ECEF   (ECEF to ECI)
```

where ERA = 2*pi*(0.7790572732640 + 1.00273781191135448 * Du) and
Du = Julian UT1 date - 2451545.0.

The C API provides:

```c
/* Convert ECI to ECEF at epoch 2024.5 */
lwgeom_transform_eci_to_ecef(geom, 2024.5);

/* Convert ECEF to ECI at epoch 2024.5 */
lwgeom_transform_ecef_to_eci(geom, 2024.5);
```

### ECI Bounding Boxes

ECI geometries use Cartesian bounding boxes (same as ECEF). When M values
represent epochs, the GBOX mmin/mmax fields track the temporal extent:

```c
GBOX gbox;
lwgeom_eci_compute_gbox(geom, &gbox);
/* gbox.xmin..xmax, ymin..ymax, zmin..zmax = spatial extent */
/* gbox.mmin..mmax = temporal (epoch) extent */
```

---

## 4. Migration Guide

### For Existing Users

1. **No breaking changes**: All existing geographic and projected CRS usage
   works exactly as before. The CRS family system is additive.

2. **SRID detection is automatic**: The CRS family for any SRID is derived
   at runtime from the PROJ database. No changes to `spatial_ref_sys` are
   needed for standard EPSG codes.

3. **On-disk format unchanged**: `GSERIALIZED` format is not modified.
   CRS family is determined at runtime from the SRID, not stored on disk.

### Starting with ECEF

```sql
-- 1. ECEF is already available via EPSG:4978
SELECT postgis_crs_family(4978);  -- 'geocentric'

-- 2. Transform existing geographic data to ECEF
SELECT ST_Transform(geom, 4978) FROM my_geographic_table;

-- 3. Create ECEF-native tables
CREATE TABLE ecef_points (
    id SERIAL PRIMARY KEY,
    position GEOMETRY(PointZ, 4978)
);
```

### Starting with ECI

```sql
-- 1. Register an ECI SRID
INSERT INTO spatial_ref_sys (srid, auth_name, auth_srid, srtext, proj4text)
VALUES (
    900001, 'CUSTOM', 900001,
    'LOCAL_CS["ICRF",LOCAL_DATUM["ICRF",0],UNIT["metre",1]]',
    '+proj=geocent +datum=WGS84 +units=m +no_defs'
);

-- 2. Create an ECI-native table with epoch in M
CREATE TABLE eci_positions (
    id SERIAL PRIMARY KEY,
    position GEOMETRY(PointZM, 900001)
);

-- 3. Insert positions with epoch
INSERT INTO eci_positions (position)
VALUES (ST_GeomFromText('POINT ZM (6878137 0 0 2024.5)', 900001));
```

---

## 5. C API Reference

### CRS Family Functions

```c
/* Get CRS family enum name */
const char* lwcrs_family_name(LW_CRS_FAMILY family);

/* Map PROJ PJ_TYPE to CRS family */
LW_CRS_FAMILY lwcrs_family_from_pj_type(PJ_TYPE pj_type);

/* Look up CRS family for an SRID */
LW_CRS_FAMILY lwsrid_get_crs_family(int32_t srid);

/* Check if CRS family requires an epoch */
lwcrs_family_requires_epoch(family)  /* macro */
```

### LWPROJ Epoch Support

```c
/* LWPROJ struct includes epoch field */
typedef struct LWPROJ {
    PJ* pj;
    bool pipeline_is_forward;
    uint8_t source_is_latlong;
    double source_semi_major_metre;
    double source_semi_minor_metre;
    LW_CRS_FAMILY source_crs_family;
    LW_CRS_FAMILY target_crs_family;
    double epoch;  /* decimal year, 0.0 = no epoch */
} LWPROJ;

#define LWPROJ_NO_EPOCH 0.0
```

### ECI Transform Functions

```c
/* ECI <-> ECEF transforms */
int lwgeom_transform_eci_to_ecef(LWGEOM *geom, double epoch);
int lwgeom_transform_ecef_to_eci(LWGEOM *geom, double epoch);

/* ERA computation */
double lweci_earth_rotation_angle(double julian_ut1_date);
double lweci_epoch_to_jd(double decimal_year);

/* ECI bounding box */
int lwgeom_eci_compute_gbox(const LWGEOM *geom, GBOX *gbox);
```

### ECI SRID Constants

```c
#define SRID_ECI_BASE   900001
#define SRID_ECI_ICRF   900001
#define SRID_ECI_J2000  900002
#define SRID_ECI_TEME   900003
#define SRID_ECI_MAX    900099
#define SRID_IS_ECI(srid) ((srid) >= SRID_ECI_BASE && (srid) <= SRID_ECI_MAX)
```

### Compile-Time Feature Macros

```c
/* Always 1 - ECI support uses pure C math */
#define POSTGIS_ECI_ENABLED 1

/* 1 if PROJ >= 9.2 provides advanced epoch APIs */
#define POSTGIS_PROJ_HAS_COORDINATE_EPOCH (0 or 1)
```
