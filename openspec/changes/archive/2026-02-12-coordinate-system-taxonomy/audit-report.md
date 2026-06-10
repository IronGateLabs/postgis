# CRS Assumption Audit Report

**PostGIS develop branch** (commit 94fe6ccb5)
**Date:** 2026-02-12
**Scope:** `liblwgeom/`, `libpgcommon/`, `postgis/` (excludes raster, topology, tiger geocoder)

---

## 1. CRS Family Enum Completeness

### 1.1 LW_CRS_FAMILY Enum

Defined in `liblwgeom/liblwgeom.h.in:46-54`:

| Enum Value | Integer | Description |
|---|---|---|
| `LW_CRS_UNKNOWN` | 0 | Unclassified or unresolvable |
| `LW_CRS_GEOGRAPHIC` | 1 | Latitude/longitude on ellipsoid (e.g. EPSG:4326) |
| `LW_CRS_PROJECTED` | 2 | Planar Cartesian from map projection (e.g. EPSG:32632) |
| `LW_CRS_GEOCENTRIC` | 3 | Earth-Centered Earth-Fixed Cartesian (e.g. EPSG:4978) |
| `LW_CRS_INERTIAL` | 4 | Earth-Centered Inertial (ICRF/J2000/TEME) |
| `LW_CRS_TOPOCENTRIC` | 5 | Local tangent plane (ENU/NED) |
| `LW_CRS_ENGINEERING` | 6 | Local/engineering CRS with no geodetic datum |

### 1.2 PJ_TYPE Mapping (`lwcrs_family_from_pj_type`)

`liblwgeom/lwgeom_transform.c:52-72`:

| PROJ PJ_TYPE | PostGIS Mapping | Status |
|---|---|---|
| `PJ_TYPE_GEOGRAPHIC_2D_CRS` | `LW_CRS_GEOGRAPHIC` | Mapped |
| `PJ_TYPE_GEOGRAPHIC_3D_CRS` | `LW_CRS_GEOGRAPHIC` | Mapped |
| `PJ_TYPE_PROJECTED_CRS` | `LW_CRS_PROJECTED` | Mapped |
| `PJ_TYPE_GEOCENTRIC_CRS` | `LW_CRS_GEOCENTRIC` | Mapped |
| `PJ_TYPE_ENGINEERING_CRS` | `LW_CRS_ENGINEERING` | Mapped |
| `PJ_TYPE_COMPOUND_CRS` | Recurse into horizontal component | Special-cased in `lwcrs_family_from_pj()` |
| `PJ_TYPE_VERTICAL_CRS` | `LW_CRS_UNKNOWN` | Correct (not spatial) |
| `PJ_TYPE_TEMPORAL_CRS` | `LW_CRS_UNKNOWN` | Correct (not spatial) |
| `PJ_TYPE_BOUND_CRS` | `LW_CRS_UNKNOWN` | Consider unwrapping to inner CRS |
| `PJ_TYPE_OTHER_CRS` | `LW_CRS_UNKNOWN` | Correct |
| Non-CRS types (ELLIPSOID, DATUM, etc.) | `LW_CRS_UNKNOWN` | Correct |

**Assessment:** All 5 primary spatial CRS types are mapped. COMPOUND_CRS is handled via recursion. Two minor gaps identified:

1. **`PJ_TYPE_DERIVED_PROJECTED_CRS`** (PROJ 9.x) - Should map to `LW_CRS_PROJECTED`. Derived projected CRS instances exist in the EPSG registry (e.g. rotated grids). Currently falls to UNKNOWN.
2. **`PJ_TYPE_BOUND_CRS`** - Should be unwrapped to inner CRS in `lwcrs_family_from_pj()`, similar to COMPOUND_CRS handling. Needs PJ object access (cannot be handled in the type-only function).
3. **`PJ_TYPE_GEODETIC_CRS`** - Low priority. PROJ docs say `proj_get_type()` can return this for non-geographic geodetic CRS. In practice PROJ returns `PJ_TYPE_GEOCENTRIC_CRS` instead. Defensive mapping to `LW_CRS_GEOCENTRIC` optional.

**Inertial/Topocentric:** These have no PROJ `PJ_TYPE` equivalent. `LW_CRS_INERTIAL` is detected via SRID range (900001-900099) in `lwsrid_get_crs_family()`. `LW_CRS_TOPOCENTRIC` has no detection path yet.

### 1.3 Enum Stability Test

Added `test_lwcrs_family_enum_stability()` to `liblwgeom/cunit/cu_crs_family.c` asserting all integer values are stable.

---

## 2. source_is_latlong Audit

### 2.1 Population

`source_is_latlong` is set in `liblwgeom/lwgeom_transform.c:170-181` within `lwproj_from_str()`:
- Only set to `LW_TRUE` when `str_in == str_out` (null-transform) AND `pj_type` is `GEOGRAPHIC_2D` or `GEOGRAPHIC_3D`
- All other cases default to `LW_FALSE`, including pipeline transforms

### 2.2 All Call Sites

| File | Function | Line | Usage | CRS Family Augmented | Geocentric Risk |
|---|---|---|---|---|---|
| `liblwgeom/lwgeom_transform.c` | `lwproj_from_str()` | 147, 180-181, 233 | Population | N/A | N/A |
| `liblwgeom/lwgeom_transform.c` | `lwproj_from_str_pipeline()` | 271 | Explicit `LW_FALSE` | N/A | N/A |
| `libpgcommon/lwgeom_transform.c` | `lwproj_is_latlong()` | 510-513 | Returns `pj->source_is_latlong` | **No** | Low (used via null-transform lookup) |
| `libpgcommon/lwgeom_transform.c` | `srid_is_latlong()` | 516-522 | Calls `lwproj_is_latlong()` via null-transform | **No** | Low |
| `libpgcommon/lwgeom_transform.c` | `srid_check_latlong()` | 525-536 | Errors if not latlong (geography guard) | **No** | Low (correct rejection) |
| `libpgcommon/lwgeom_transform.c` | `srid_axis_precision()` | 549 | Adds 5 decimals for latlong | **No** | Low |
| `libpgcommon/lwgeom_transform.c` | `spheroid_init_from_srid()` | 579-580 | **AUGMENTED:** Allows geocentric via family check | **Yes** | None |
| `postgis/lwgeom_out_marc21.c` | `ST_AsMARC21()` | 79 | Rejects non-latlong | **No** | Correct behavior |

### 2.3 Refactored vs Remaining

| Call Site | Status | Priority |
|---|---|---|
| `spheroid_init_from_srid()` | **Refactored** - uses both boolean and CRS family | Low |
| `lwproj_is_latlong()` | **Remaining** - boolean only | High |
| `srid_is_latlong()` | **Remaining** - boolean only | High |
| `srid_check_latlong()` | **Remaining** - boolean only | Medium (works for geography guard) |
| `srid_axis_precision()` | **Remaining** - boolean only | Medium |
| `ST_AsMARC21()` | **Remaining** - boolean only, correct behavior | Low |

**Architectural debt:** The boolean `source_is_latlong` and enum `source_crs_family` are parallel systems. The boolean is only reliably set for null-transform lookups. All consumers except `spheroid_init_from_srid()` still use the boolean. Refactoring to use `source_crs_family == LW_CRS_GEOGRAPHIC` would unify the systems.

---

## 3. LWFLAG_GEODETIC and Unit-Sphere Assumptions

### 3.1 FLAGS_GET_GEODETIC Usage

The GEODETIC flag is defined in `liblwgeom/liblwgeom.h.in` and serialization headers (`gserialized1.h`, `gserialized2.h`). Critical usages:

| File | Function | Effect | Risk for Non-Geographic |
|---|---|---|---|
| `liblwgeom/lwgeom.c:823` | `lwgeom_calculate_gbox()` | Routes to geodetic GBOX calculation | **HIGH** - assumes lat/lon |
| `liblwgeom/gbox.c:269` | `gbox_union()` | Includes Z in merge if geodetic | HIGH |
| `liblwgeom/gbox.c:287` | `gbox_overlaps()` | Errors on flag mismatch | CRITICAL (safety check) |
| `liblwgeom/gbox.c:454` | `gbox_serialized_size()` | Fixed 24 bytes for geodetic | MEDIUM |
| `liblwgeom/gbox.c:823` | `gbox_hash()` | Uses `cart2geog()` on geodetic boxes | **CRITICAL** - unit-sphere |
| `liblwgeom/gserialized1.c:272` | `gserialized1_peek_gbox_p()` | Reads Z only if geodetic | HIGH |
| `liblwgeom/gserialized2.c:368` | `gserialized2_peek_gbox_p()` | Reads Z only if geodetic | HIGH |
| `liblwgeom/lwgeodetic.c:2091` | `lwgeom_calculate_gbox_geodetic()` | Full geodetic GBOX | Requires lat/lon input |
| `postgis/gserialized_estimate.c:227` | `gbox_ndims()` | Returns 3 if geodetic | HIGH |

**Key assumption:** `LWFLAG_GEODETIC` implies coordinates are geographic lat/lon. This flag is set by the geography type at ingestion time and controls bounding box computation, serialization size, and spatial index behavior. ECEF or other non-geographic coordinates must NOT have this flag set.

### 3.2 Unit-Sphere Functions

All three functions (`geog2cart`, `cart2geog`, `ll2cart` in `liblwgeom/lwgeodetic.c:404-431`) operate on a unit sphere (radius=1.0). They:
- Convert between geographic (lat/lon in radians) and Cartesian (x,y,z) on the unit sphere
- Have no radius or scale parameter
- Are used throughout the geodetic tree index, containment tests, and GBOX calculations

**All call sites are unit-sphere-only.** Ellipsoidal calculations happen at a higher level via `lwgeom_distance_spheroid()`, `lwgeom_area_spheroid()`, etc. in `lwgeodetic_measures.c` and `lwspheroid.c`.

Major call sites (all in `liblwgeom/`):

| File | Function | Count | Classification |
|---|---|---|---|
| `lwgeodetic.c` | Various (gbox_check_poles, edge_*, ptarray_contains_*) | ~15 | Unit-sphere-only |
| `lwgeodetic_tree.c` | circ_tree_* functions | ~8 | Unit-sphere-only |
| `lwgeodetic_measures.c` | ptarray_distance_spheroid | 2 | Unit-sphere (initial), then spheroid |
| `gbox.c` | gbox_hash | 1 | Unit-sphere-only |

### 3.3 Coordinate Wrapping/Normalization

`liblwgeom/lwgeodetic.c` contains:

| Function | Lines | Range |
|---|---|---|
| `longitude_radians_normalize()` | 50-73 | [-PI, PI] |
| `latitude_radians_normalize()` | 78-100 | [-PI/2, PI/2] |
| `longitude_degrees_normalize()` | 106-127 | [-180, 180] |
| `latitude_degrees_normalize()` | 133-155 | [-90, 90] |
| `geographic_point_init()` | 180-184 | Calls both radians normalizers |
| `crosses_dateline()` | 666-677 | Sign-based dateline detection |

Additionally, `liblwgeom/lwgeom_wrapx.c` provides `lwgeom_wrapx()` for splitting geometries at arbitrary X boundaries (anti-meridian wrapping). This is CRS-agnostic.

**Impact:** All normalization assumes geographic coordinate ranges. ECEF coordinates (meters, range ~[-6.4M, +6.4M]) would be nonsensically wrapped to [-180, 180].

### 3.4 Geography Distance Functions

`postgis/geography_measurement.c` contains these functions, all assuming spheroidal input:

| Function | Lines | Spheroid | Notes |
|---|---|---|---|
| `geography_distance_knn()` | 77-133 | Sphere only (hardcoded) | KNN index |
| `geography_distance_uncached()` | 140-198 | Optional (default: true) | |
| `geography_distance()` | 206-260 | Optional (default: true) | Main entry |
| `geography_distance_tree()` | 330-376 | Optional (default: true) | Tree-accelerated |
| `geography_dwithin()` | 267-320 | Optional (default: true) | |
| `geography_area()` | 491-556 | Optional (default: true) | Pole/equator fallback |
| `geography_perimeter()` | 562-595 | Optional (default: true) | |
| `geography_length()` | 598-660 | Optional (default: true) | |

**Safety:** These functions are protected by `srid_check_latlong()` at geography type creation, preventing non-geographic SRIDs from entering the geography path. The functions themselves do not validate CRS family.

---

## 4. Bug Found

### `postgis_crs_family(4326)` returns "unknown"

**File:** `postgis/lwgeom_transform.c:788`
**Cause:** `if (srid == SRID_DEFAULT || srid == SRID_UNKNOWN)` short-circuits SRID 4326 (which equals `SRID_DEFAULT`) to return "unknown" without performing the PROJ lookup.
**Fix:** Removed `SRID_DEFAULT` from the condition. Only `SRID_UNKNOWN` (0) should return "unknown" directly.
**Verified:** All SRIDs now classify correctly in Docker testing.

---

## 5. Priority Recommendations

### Immediate (this change)
1. **Fix `postgis_crs_family(4326)` bug** - Done
2. **Add enum stability test** - Done

### High Priority (multi-crs-type-system change)
1. Refactor `lwproj_is_latlong()` to use `source_crs_family == LW_CRS_GEOGRAPHIC`
2. Update `srid_check_latlong()` to use CRS family internally
3. Add geocentric guard to `ST_Area` and `ST_Distance` geometry functions

### Medium Priority (ecef-coordinate-support change)
1. Guard ECEF data against `LWFLAG_GEODETIC` being set
2. Add CRS-family-aware distance function that transforms ECEF to geographic for geodesic computation
3. Consider BOUND_CRS unwrapping in `lwcrs_family_from_pj()`

### Low Priority
1. Add `LW_CRS_TOPOCENTRIC` detection path (no PROJ equivalent)
2. Simplify `spheroid_init_from_srid()` condition to use CRS family exclusively
