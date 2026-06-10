## Why

Several spatial functions silently produce nonsensical results when given geocentric (ECEF/ECI) input — `ST_Perimeter` computes a 2D perimeter on million-meter Cartesian coordinates, `ST_Azimuth` treats XY as a plane, `ST_Project` offsets in Cartesian space, and `ST_Segmentize` densifies with flat-earth distances. The first wave of guards covered `ST_Area`, `ST_Buffer`, `ST_OffsetCurve`, `ST_Centroid`, and `ST_BuildArea`, but the remaining functions need the same treatment. Additionally, casting geocentric geometry to geography silently reinterprets ECEF meters as lat/lon degrees. On the indexing side, GiST 3D correctness tests exist but only cover ~189 points — performance at realistic scales (10K–100K+) has never been measured.

## What Changes

- Add `gserialized_check_crs_family_not_geocentric()` guards to `ST_Perimeter`, `ST_Azimuth`, `ST_Project`, `ST_Segmentize`
- Add geocentric SRID check to the `geometry → geography` cast path to prevent silent misinterpretation
- Add regression tests for all new guards (positive and negative cases)
- Create a GiST 3D index benchmark suite for ECEF/ECI data at 10K, 50K, and 100K point scales
- Add `ST_3DDistance` accuracy validation and mixed-SRID safety tests to close gaps in the spatial-index-verification spec

## Capabilities

### New Capabilities

- `gist-ecef-benchmarking`: Performance benchmarking of GiST 3D indexes with ECEF (4978) and ECI (900001–900003) geometries at scale — index build time, query throughput, and comparison against geographic baseline

### Modified Capabilities

- `ecef-coordinate-support`: Extend the "Error" classification to `ST_Perimeter`, `ST_Azimuth`, `ST_Project`, `ST_Segmentize`, and the `geometry → geography` cast when input SRID is geocentric

## Impact

- **C code**: `postgis/lwgeom_functions_basic.c` (ST_Perimeter, ST_Azimuth, ST_Project, ST_Segmentize), `postgis/geography_inout.c` (geography cast)
- **Tests**: `regress/core/ecef_eci.sql` (new guard tests), new benchmark SQL script
- **Existing behavior**: Functions that previously accepted geocentric input silently will now raise `ERRCODE_FEATURE_NOT_SUPPORTED` — this is intentionally breaking for misuse cases
- **No schema changes**: Uses existing `gserialized_check_crs_family_not_geocentric()` infrastructure
