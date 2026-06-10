## Context

PostGIS stores coordinate reference system identity as a 21-bit SRID integer on each `LWGEOM` object and a single `LWFLAG_GEODETIC` bit that toggles between two modes: planar Cartesian (`GEOMETRY`) and spherical geographic (`GEOGRAPHY`). All geodetic internal math (`geog2cart`, `cart2geog`, `ll2cart` in `liblwgeom/lwgeodetic.c`) operates on a **unit sphere** (radius = 1.0) with no ellipsoid parameters. The PROJ library integration (`liblwgeom/lwgeom_transform.c`) detects only `PJ_TYPE_GEOGRAPHIC_2D_CRS` and `PJ_TYPE_GEOGRAPHIC_3D_CRS` when classifying source CRS; it does not check for `PJ_TYPE_GEOCENTRIC_CRS`. The `spatial_ref_sys` table schema carries `auth_name`, `auth_srid`, `srtext`, and `proj4text` columns -- enough to hold ECEF definitions (EPSG:4978) but with no metadata columns to classify CRS family.

PROJ 6.1+ (PostGIS's current minimum) already supports geocentric CRS, and PROJ 9.x adds dynamic datum and time-dependent frames. The transformation pipeline mechanism (`lwproj_from_str_pipeline`) already accepts arbitrary PROJ pipeline strings including `+proj=cart` (geodetic-to-ECEF) and epoch-parameterized operations.

This design addresses how to make ECEF, ECI, and other coordinate system families visible, queryable, and correctly handled across the PostGIS stack -- from type metadata through spatial indexing.

## Goals / Non-Goals

**Goals:**

- Produce a complete audit of every location in PostGIS where CRS-family assumptions are hard-coded (the `source_is_latlong` binary flag, unit-sphere-only geodetic math, GBOX range constraints)
- Classify coordinate systems into families (geographic, projected, geocentric/ECEF, inertial/ECI, topocentric, local tangent plane) and identify which are already reachable via PROJ pipelines vs. require new internal representations
- Design a CRS-family metadata extension to `LWGEOM` flags and/or `GSERIALIZED` that carries coordinate system semantics without breaking the existing on-disk format
- Define requirements for ECEF and ECI as first-class coordinate systems: storage, bounding-box computation, spatial indexing, and transformation round-trips
- Identify the minimum PROJ version needed for each tier of support

**Non-Goals:**

- Implementing the changes in this phase -- this is an analysis and specification effort
- Replacing the PROJ library or reimplementing geodetic math that PROJ already provides
- Supporting arbitrary user-defined coordinate system families beyond the taxonomy identified
- Changing the PostgreSQL `spatial_ref_sys` table schema in ways that break backward compatibility with existing SRID catalogs
- Full ECI orbit propagation or space-domain-awareness features (only the coordinate system representation layer)

## Decisions

### Decision 1: Extend CRS classification beyond binary latlong flag

**Choice:** Introduce a CRS family enum (`LW_CRS_GEOGRAPHIC`, `LW_CRS_PROJECTED`, `LW_CRS_GEOCENTRIC`, `LW_CRS_INERTIAL`, `LW_CRS_TOPOCENTRIC`, `LW_CRS_UNKNOWN`) derived from `PJ_TYPE` at SRID registration/lookup time, cached alongside `LWPROJ`.

**Rationale:** The current `source_is_latlong` (uint8_t boolean) in `LWPROJ` loses information -- a geocentric CRS is neither latlong nor projected, yet falls through to the projected code path today. PROJ already exposes `PJ_TYPE_GEOCENTRIC_CRS`; PostGIS just doesn't read it.

**Alternatives considered:**
- (A) Store CRS family in `LWGEOM.flags` bits -- rejected because the 16-bit `lwflags_t` has only 10 unused bits and on-disk `GSERIALIZED.gflags` has only 4 unused bits; encoding a multi-value enum here risks format breakage
- (B) Add a `crs_family` column to `spatial_ref_sys` -- viable as a supplementary approach but doesn't help the C-level transformation code path

### Decision 2: ECEF support via PROJ geocentric CRS and pipeline transforms

**Choice:** ECEF support SHALL be implemented by recognizing `PJ_TYPE_GEOCENTRIC_CRS` in the existing `lwproj_from_PJ` function and correctly routing ECEF geometries through the PROJ pipeline. No custom ECEF math in `liblwgeom`.

**Rationale:** PROJ already implements geodetic-to-geocentric (`+proj=cart`) with full ellipsoid parameters. Reimplementing this in PostGIS would duplicate effort and introduce divergence. The existing `lwproj_from_str_pipeline` already handles arbitrary pipelines -- the gap is in type detection and bounding-box computation, not in the actual coordinate math.

**Alternatives considered:**
- (A) Implement ECEF conversion functions directly in `lwgeodetic.c` -- rejected because PROJ handles this correctly and PostGIS should delegate
- (B) Use only pipeline strings, never register ECEF SRIDs -- rejected because users expect `ST_Transform(geom, 4978)` to work and EPSG:4978 is a standard geocentric CRS

### Decision 3: ECEF bounding boxes use 3D Cartesian GBOX with metric ranges

**Choice:** When `crs_family == LW_CRS_GEOCENTRIC`, the `GBOX` SHALL use true metric Cartesian bounds (X, Y, Z in meters from Earth center) rather than the unit-sphere encoding used for geodetic boxes.

**Rationale:** The current geodetic GBOX uses unit-sphere coordinates (all values in [-1, 1]) which cannot represent ECEF coordinates (values up to ~6,378,137 meters). The GIST index operators need correct range semantics for overlap/containment queries.

**Alternatives considered:**
- (A) Normalize ECEF to unit sphere before indexing -- rejected because it loses distance information and conflates with the existing geodetic encoding
- (B) Store lon/lat bounding boxes even for ECEF geometries -- rejected because it requires a transformation at index time and breaks the principle of native representation

### Decision 4: ECI support requires epoch-parameterized transformations

**Choice:** ECI (ICRF/J2000) coordinate systems SHALL be supported through PROJ's time-dependent transformation capabilities with epoch stored as the M coordinate or as an explicit parameter to `ST_Transform`.

**Rationale:** ECI frames rotate relative to ECEF at ~7.292115e-5 rad/s (Earth's rotation rate). A point in ECI at epoch T1 maps to a different ECEF location than at epoch T2. PROJ 9.x supports `+t_epoch` parameters and dynamic datums. The M dimension in `POINT4D` is already available and underutilized in most workloads.

**Alternatives considered:**
- (A) Ignore ECI and treat it as a static ECEF variant -- rejected because this produces incorrect results (kilometers of error for objects in orbit)
- (B) Implement Earth rotation in PostGIS independent of PROJ -- rejected for the same reasons as Decision 2
- (C) Require a separate time column instead of M -- viable but less elegant; both options should be supported

### Decision 5: Phased implementation starting with audit, then ECEF, then ECI

**Choice:** Phase 1 is the audit and taxonomy (this change). Phase 2 implements ECEF as a first-class CRS. Phase 3 adds ECI with time-dependent transformations. Each phase is independently valuable.

**Rationale:** ECEF is simpler (static frame, no time dependency) and serves a larger user base (geodesy, surveying, defense). ECI requires solving the epoch parameterization problem first and has a smaller but high-value audience (aerospace, satellite tracking).

## Risks / Trade-offs

- **[GSERIALIZED format stability]** Adding CRS-family bits to the on-disk format is a breaking change. → Mitigation: Phase 1 avoids on-disk changes; CRS family is derived at runtime from SRID lookup. A future `GSERIALIZED v3` could encode it.

- **[PROJ version floor increase]** Full ECI support may require PROJ 9.x (dynamic datums). → Mitigation: ECEF works with PROJ 6.1+. ECI features can be compile-time gated behind a PROJ version check (`#if PROJ_VERSION_MAJOR >= 9`).

- **[Index performance for ECEF]** ECEF Cartesian bounding boxes span much larger numeric ranges than lon/lat. The GIST R-tree may have different splitting behavior. → Mitigation: Benchmark with realistic ECEF datasets; consider a custom operator class if needed.

- **[M-coordinate overloading for ECI epoch]** Using M as epoch conflicts with existing M-coordinate semantics (linear referencing). → Mitigation: Only interpret M as epoch when `crs_family == LW_CRS_INERTIAL`; document clearly. Alternatively, support explicit epoch parameter in `ST_Transform`.

- **[Backward compatibility of ST_Transform]** New CRS families may produce unexpected results with existing spatial functions (ST_Distance, ST_Area) that assume geographic or projected input. → Mitigation: Functions that don't support geocentric input SHALL raise clear errors rather than silently producing wrong results.

## Open Questions

1. Should `spatial_ref_sys` gain a `crs_family` column, or should family classification be purely computed from PROJ at runtime?
2. What is the minimum dataset of ECEF SRIDs to pre-populate? (EPSG:4978 is WGS84 geocentric; are there others commonly used?)
3. Should the `GEOGRAPHY` type be extended to support geocentric, or should a new `GEOCENTRIC` type be introduced?
4. How should `ST_AsText`/`ST_AsGeoJSON` represent ECEF coordinates -- same WKT with different CRS metadata, or a new output format?
5. What is the performance impact of SRID-to-CRS-family lookup on every transformation? Can it be cached in the PROJ cache (`PROJSRSCache`)?
