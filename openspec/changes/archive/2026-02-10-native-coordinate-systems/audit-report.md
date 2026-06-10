# PostGIS CRS Hard-Coded Assumptions Audit Report

## Executive Summary

PostGIS has **5 categories of hard-coded CRS assumptions** that would produce incorrect results or outright failures for geocentric (ECEF) coordinate systems. The most critical are the `source_is_latlong` binary flag in `LWPROJ` and the geography measurement functions that interpret all input as lat/lon degrees.

---

## 1. `source_is_latlong` Flag (CRITICAL)

**File:** `liblwgeom/lwgeom_transform.c:77`

The flag is set only for `PJ_TYPE_GEOGRAPHIC_2D_CRS` or `PJ_TYPE_GEOGRAPHIC_3D_CRS`. Geocentric CRS (`PJ_TYPE_GEOCENTRIC_CRS`) gets `FALSE`, which then causes:

| Location | Function | Impact |
|----------|----------|--------|
| `libpgcommon/lwgeom_transform.c:566` | `spheroid_init_from_srid()` | Returns `LW_FAILURE` for geocentric SRIDs despite spheroid parameters being available from PROJ |
| `libpgcommon/lwgeom_transform.c:510-513` | `lwproj_is_latlong()` | Simply returns the flag -- used by `srid_is_latlong()` and `srid_check_latlong()` |
| `libpgcommon/lwgeom_transform.c:524-536` | `srid_check_latlong()` | Raises ERROR for any non-latlong SRID used with geography type |

**Root cause:** PostGIS conflates "geographic CRS" with "has spheroid parameters." ECEF has ellipsoid parameters but fails the geographic type check.

**Fix implemented:** Extended `LWPROJ` with `source_crs_family` / `target_crs_family` fields that carry `LW_CRS_GEOCENTRIC` classification alongside the existing `source_is_latlong` for backward compatibility.

---

## 2. `LWFLAG_GEODETIC` Flag Usage (HIGH)

**Files:** `liblwgeom/lwgeodetic.c`, `liblwgeom/gbox.c`, `liblwgeom/gserialized1.c`, `liblwgeom/gserialized2.c`

The geodetic flag is overloaded to mean both "uses geographic coordinates" and "has geocentric bounding box representation (unit sphere)":

| Pattern | Status | Locations |
|---------|--------|-----------|
| Z dimension gating (geodetic implies Z) | **SAFE** | `gbox.c:103`, `gbox.c:210`, `gbox.c:269` |
| Geodetic box comparison (Z-only for geodetic) | **UNSAFE** for ECEF | `gbox.c:297` (`gbox_overlaps`) |
| Geocentric-to-planar box conversion | **UNSAFE** for ECEF | `lwgeodetic.c:3713` (`gbox_geocentric_get_gbox_cartesian`) |
| GIDX encoding with geographic interpretation | **UNSAFE** for ECEF | `gserialized_gist_2d.c:823` (`gbox_encode_gidx`) |
| Distance functions assuming geodetic = geographic | **UNSAFE** for ECEF | `lwgeodetic.c:2091` (`lwgeom_distance_spheroid`) |

**Key insight:** The geodetic flag cannot be reused for ECEF because it triggers unit-sphere bounding box computation. ECEF coordinates must use the standard Cartesian code path with metric ranges.

---

## 3. Unit-Sphere Conversion Functions (MEDIUM)

**File:** `liblwgeom/lwgeodetic.c`

| Function | Line | Algorithm | ECEF Impact |
|----------|------|-----------|-------------|
| `geog2cart()` | 404 | `x = cos(lat)*cos(lon)`, `z = sin(lat)` on unit sphere | Not usable for ECEF (produces values in [-1,1] instead of meters) |
| `cart2geog()` | 418 | `lat = asin(z)`, `lon = atan2(y,x)` from unit sphere | Not usable for ECEF (expects unit-sphere input) |
| `ll2cart()` | 426 | Degrees-to-unit-sphere shortcut | Not usable for ECEF |

These functions are called by 80+ operations (distance, bearing, area, point-in-polygon) for GEOGRAPHY type. They are **correct for their purpose** (internal unit-sphere computations for geographic data) but **must not be applied to ECEF coordinates**.

**Fix approach:** ECEF conversions are delegated to PROJ (`+proj=cart`), not reimplemented in PostGIS. These functions remain unit-sphere-only.

---

## 4. GBOX Range Assumptions (HIGH)

**Files:** `liblwgeom/gbox.c`, `liblwgeom/lwgeodetic.c`, `postgis/gserialized_gist_*.c`

| Range Assumption | Location | ECEF Conflict |
|-----------------|----------|---------------|
| Geodetic GBOX uses unit-sphere coords (all in [-1, 1]) | `gbox.c` (throughout) | ECEF coords are ~6.4M meters |
| `longitude_degrees_normalize()` clamps to [-180, 180] | `lwgeodetic.c` | ECEF X/Y/Z are not angular |
| `latitude_degrees_normalize()` clamps to [-90, 90] | `lwgeodetic.c` | Same |
| GIDX serialization assumes degree-range floats | `gserialized_gist_2d.c` | ECEF metric ranges overflow |

**Fix approach:** ECEF geometries use GEOMETRY (not GEOGRAPHY) type and store as standard 3D Cartesian. Bounding boxes use metric ranges through the standard non-geodetic code path.

---

## 5. Geography Measurement Functions (CRITICAL)

**File:** `postgis/geography_measurement.c`

All geography functions assume input coordinates are lat/lon in degrees:

| Function | Line | What It Does | ECEF Behavior |
|----------|------|-------------|---------------|
| `geography_distance_uncached()` | 140 | Spheroid distance | Interprets ECEF meters as degrees: catastrophic error |
| `geography_distance_knn()` | 77 | k-NN with sphere | Same |
| `geography_distance_tree()` | 330 | Indexed distance | Same |
| `geography_dwithin_uncached()` | 386 | Distance-within | Same |
| `geography_area()` | 492 | Spheroid area | Same |
| `geography_length()` | ~550 | Spheroid length | Same |

**No coordinate range validation exists.** A user could store ECEF coordinates (values ~6M) in a geography column, and all functions would silently produce nonsensical results.

**Fix approach:** ECEF geometries SHALL be stored as GEOMETRY (not GEOGRAPHY). The CRS family check on spatial functions provides error messages for unsupported operations.

---

## 6. Serialization (gserialized1.c / gserialized2.c) (MEDIUM)

| Issue | Location | Impact |
|-------|----------|--------|
| Geodetic flag encoding uses 1 bit in `gflags` byte | `gserialized2.c:48-67` | No room for multi-value CRS family enum |
| No coordinate range validation on serialization | `gserialized2.c:87` | Invalid ECEF-in-geography data accepted |
| GBOX serialization format fixed at 6 floats for geodetic | `gserialized2.c:454` | Cannot accommodate ECEF metric ranges |

**Available bits in gflags (GSERIALIZED v2):**
- Bits 0-1: Version (2 bits, set to 01)
- Bit 2: HasZ
- Bit 3: HasM
- Bit 4: HasBBox
- Bit 5: IsGeodetic
- Bits 6-7: **Unused** (available for future extension)

**Fix approach:** CRS family is derived at runtime from SRID, not stored on disk. The 2 available bits could encode CRS family in a future GSERIALIZED v3 but this is not needed now.

---

## Gap Analysis Matrix

| Capability | Geographic | Projected | Geocentric (ECEF) | Inertial (ECI) |
|---|---|---|---|---|
| Storage | FULL | FULL | FULL (as GEOMETRY+Z) | FULL (as GEOMETRY+ZM) |
| ST_Transform | FULL | FULL | FULL (via PROJ) | NONE (needs epoch) |
| ST_Distance | FULL (spheroid) | FULL (Cartesian) | PARTIAL (Cartesian only) | NONE |
| ST_Area | FULL (spheroid) | FULL | ERROR (not meaningful) | NONE |
| ST_Buffer | FULL | FULL | ERROR (wrong metric) | NONE |
| GIST indexing | FULL | FULL | FULL (3D Cartesian) | NONE |
| ST_AsText/GeoJSON | FULL | FULL | FULL (same format) | FULL |
| CRS family detection | FULL | FULL | **NEW** (via LW_CRS_FAMILY) | NONE |
