## Why

PostGIS currently treats all coordinate systems through a two-tier model: **GEOMETRY** (flat Cartesian/projected) and **GEOGRAPHY** (WGS84 ellipsoidal lat/lon). While it delegates to the PROJ library for transformations between 10,000+ EPSG systems, coordinate systems like **ECEF (Earth-Centered Earth-Fixed)**, **ECI (Earth-Centered Inertial)**, **geocentric Cartesian**, and other non-traditional spatial reference frames are not first-class citizens. The existing geodetic code in `liblwgeom/lwgeodetic.c` performs unit-sphere Cartesian conversions (`geog2cart`/`cart2geog`) as internal computation helpers, but these are not exposed as native storage or query types. This limits PostGIS adoption in aerospace, defense, satellite tracking, and scientific domains where ECEF/ECI coordinates are the natural representation.

## What Changes

- **Audit and document** the current coordinate system architecture across `liblwgeom`, `libpgcommon`, and `postgis` layers to map exactly where CRS assumptions are hard-coded (WGS84 constants, geography-only geodetic paths, unit-sphere-only Cartesian conversions)
- **Classify coordinate system support levels**: identify which systems (ECEF, ECI, geocentric, topocentric, local tangent plane) are already reachable via PROJ pipelines vs. which would require new internal representations
- **Identify extension points** in the type system (`LWGEOM`, `GEOGRAPHY`, `GEOMETRY`), the transformation pipeline (`LWPROJ`, `PROJSRSCache`), and the spatial indexing layer (GIST, bounding boxes) where native multi-CRS support could be inserted
- **Propose a native coordinate system abstraction** that goes beyond the current SRID-integer lookup to carry coordinate system semantics (type, epoch, datum, frame) as first-class metadata on geometry objects
- **Evaluate PROJ 9.x+ capabilities** for modern CRS types (dynamic datums, time-dependent transformations) that could back ECEF/ECI support without reimplementing geodetic math

## Capabilities

### New Capabilities
- `ecef-coordinate-support`: Analysis of ECEF (Earth-Centered Earth-Fixed) as a native coordinate system -- storage, indexing, spatial operations, and round-trip transformations to/from geographic CRS
- `eci-coordinate-support`: Analysis of ECI (Earth-Centered Inertial) frame support -- epoch-dependent transformations, time parameterization, and compatibility with existing PostGIS temporal features
- `coordinate-system-taxonomy`: Classification framework for all coordinate system families (geographic, projected, geocentric, topocentric, local tangent plane, inertial) with a gap analysis against current PostGIS capabilities
- `multi-crs-type-system`: Design for extending PostGIS type metadata to carry coordinate system family, epoch, and frame information beyond the current SRID integer

### Modified Capabilities
<!-- No existing openspec/specs/ capabilities exist yet to modify -->

## Impact

- **Core library (`liblwgeom`)**: The `LWGEOM` struct, `SPHEROID` type, geodetic functions in `lwgeodetic.c`, and point types (`POINT3D`, `POINT4D`) would need extensions to carry CRS family metadata. The existing `geog2cart`/`cart2geog` unit-sphere conversions would need to be generalized to true ECEF with ellipsoid parameters.
- **Transformation layer**: `liblwgeom/lwgeom_transform.c` and the `LWPROJ` struct would need awareness of source/target CRS families beyond the binary `source_is_latlong` flag. The PROJ cache in `libpgcommon/lwgeom_transform.c` (128-item LRU) may need to handle time-dependent transformation keys for ECI.
- **PostgreSQL layer (`postgis/`)**: `ST_Transform` SQL functions already support PROJ pipeline strings, which is a viable path for ECEF/ECI. The `spatial_ref_sys` table schema may need extension for non-EPSG systems (e.g., ITRF/ICRF frames).
- **Spatial indexing**: GIST indexes and bounding box computations assume either planar or geographic coordinates. ECEF 3D Cartesian bounding boxes would require new index operator classes.
- **Dependencies**: PROJ 6.1+ (current minimum) supports geocentric CRS; PROJ 9.x adds dynamic datum support. This analysis will determine whether the minimum PROJ version needs to be raised.
- **Downstream consumers**: Any change to type metadata or `spatial_ref_sys` schema affects tools like `shp2pgsql`, `ogr2ogr`/GDAL integration, and client libraries (e.g., `psycopg2`, `node-postgres`).
