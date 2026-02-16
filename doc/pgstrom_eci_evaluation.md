# PG-Strom ECI Integration Evaluation

## Overview

PG-Strom is a PostgreSQL extension that accelerates SQL queries using NVIDIA GPU hardware.
It intercepts query plans at the PostgreSQL executor level and offloads computation to
CUDA-capable GPUs.  PG-Strom includes PostGIS function support via `xpu_postgis.cu`,
with 24 function signatures covering spatial predicates, point construction, and
bounding box operations — all 2D only.

This document evaluates PG-Strom for ECI/ECEF coordinate transform workloads in
high-rate sensor ingestion pipelines (20-50Hz, TimescaleDB) and describes the
planned integration.

## How PG-Strom Complements Our Custom Acceleration

PostGIS has two independent GPU acceleration paths that serve different use cases:

| Layer | What It Accelerates | When It Helps |
|-------|---------------------|---------------|
| **Custom SIMD/GPU dispatch** (liblwgeom) | Points within a single geometry (POINTARRAY batching) | Large geometries with 100K+ points |
| **PG-Strom** (query executor) | SQL expressions across many rows in a table scan | Millions of rows with small geometries |

In a TimescaleDB pipeline at 50Hz × 1000 sensors, each row contains a single POINT
geometry.  The custom SIMD layer provides no benefit (1-point arrays), but PG-Strom
can evaluate WHERE clauses and SELECT expressions on GPU across entire TimescaleDB
chunks (millions of rows per chunk).

## PG-Strom Requirements

- PostgreSQL 15+
- NVIDIA GPU with compute capability 6.0+ (Pascal or newer)
- CUDA toolkit 12.0+
- Linux only (no Windows/macOS support)

## Current PG-Strom PostGIS GPU Support

PG-Strom v6.1 supports these PostGIS functions on GPU (via `src/xpu_postgis.cu`):

| Category | Functions | Notes |
|----------|-----------|-------|
| Point construction | ST_Point, ST_MakePoint (2D/3D/4D) | All variants |
| SRID management | ST_SetSRID | |
| Distance | ST_Distance | 2D only, point/polygon |
| Proximity | ST_DWithin | 2D only, optimised for joins |
| Containment | ST_Contains, ST_Crosses, ST_Relate | Recursive geometry support |
| Operators | &&, @, ~ (geometry and box2df) | Bounding box operations |
| Expansion | ST_Expand | |
| Crossing | ST_LineCrossingDirection | |

**Not currently supported:** Any 3D spatial operations, any `postgis_ecef_eci`
extension functions, any ECI/ECEF transforms.

## Planned Integration

Full design and specifications are tracked in the PG-Strom fork at
`IronGateLabs/pg-strom`, OpenSpec change `ecef-eci-device-functions`.

### Phase 1: ECI Frame Conversion

GPU device functions for `ST_ECEF_To_ECI(geometry, timestamptz, text)` and
`ST_ECI_To_ECEF(geometry, timestamptz, text)`.

**Key design insight:** PostgreSQL `timestamptz` is int64 microseconds since
2000-01-01 (PG epoch).  `POSTGRES_EPOCH_JDATE = 2451545` is exactly J2000.0.
Therefore `Du = tstz_usec / USECS_PER_DAY` — no calendar decomposition needed.
ERA is computed directly via IERS 2003 formula on GPU.

Registration in `xpu_opcodes.h`:
```c
FUNC_OPCODE(st_ecef_to_eci, geometry/timestamptz/text, DEVKIND__ANY, st_ecef_to_eci, 20, "postgis_ecef_eci")
FUNC_OPCODE(st_eci_to_ecef, geometry/timestamptz/text, DEVKIND__ANY, st_eci_to_ecef, 20, "postgis_ecef_eci")
```

Use case: `SELECT object_id, ST_ECEF_To_ECI(geom, epoch) FROM tracks WHERE epoch > now() - '10 min'`
— PG-Strom runs ECI rotation for 30K rows on GPU in one kernel launch.

### Phase 2: ECEF Coordinate Accessors

GPU device functions for `ST_ECEF_X(geometry)`, `ST_ECEF_Y(geometry)`,
`ST_ECEF_Z(geometry)`.  Trivial coordinate extraction from point rawdata.

Use case: `WHERE ST_ECEF_X(geom) BETWEEN 6000000 AND 7000000` — range filtering
across chunk scans.

### Phase 3: 3D Spatial Operations

GPU device functions for `ST_3DDistance(geometry, geometry)` and
`ST_3DDWithin(geometry, geometry, float8)`.  Point-to-point 3D Euclidean
distance only (non-point falls back to CPU).

Use case: `WHERE ST_3DDWithin(geom, target, 100000)` — the most common spatial
predicate for ECEF/ECI data.

### Deferred: EOP-Enhanced Transforms

`ST_ECEF_To_ECI_EOP` and `ST_ECI_To_ECEF_EOP` require EOP table lookups which
cannot run on GPU.  The SQL wrapper does the lookup on CPU, but PG-Strom cannot
intercept the inner plpgsql function.  Deferred to a follow-on change.

## Reference Implementation

The PostGIS codebase contains reference CUDA device functions at
`liblwgeom/accel/gpu_eci_rotate.cu`:

- `pgstrom_eci_epoch_to_jd()` — Decimal year to Julian Date
- `pgstrom_eci_earth_rotation_angle()` — IERS 2003 ERA computation
- `pgstrom_eci_rotate_z()` — Z-axis rotation matrix application
- `pgstrom_eci_transform()` — Combined epoch-to-rotation transform
- `pgstrom_eci_validate_kernel` — GPU vs CPU numerical validation (max diff < 1e-10)

These serve as reference for the PG-Strom XPU device function implementations.
The actual PG-Strom integration uses PG-Strom's `XPU_PGFUNCTION_ARGS` pattern
and `KEXP_PROCESS_ARGS*` macros, which differ from the standalone CUDA kernel style.

## Limitations

- PG-Strom is CUDA-only (NVIDIA hardware required)
- PG-Strom accelerates GpuScan/GpuJoin/GpuPreAgg nodes, not arbitrary function calls
- For AMD/Intel GPUs, use the custom GPU dispatch layer (liblwgeom) instead
- PG-Strom and our custom SIMD/GPU dispatch are complementary, not competing
- 3D spatial operations are point-to-point only on GPU; complex geometries fall back to CPU
