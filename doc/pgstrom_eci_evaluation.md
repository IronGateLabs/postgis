# PG-Strom ECI Integration Evaluation

## Overview

PG-Strom is a PostgreSQL extension that accelerates SQL queries using NVIDIA GPU hardware.
It intercepts query plans at the PostgreSQL executor level and offloads computation to
CUDA-capable GPUs. PG-Strom includes limited PostGIS function support via `gpu_postgis.cu`.

This document evaluates PG-Strom for ECI/ECEF coordinate transform workloads and describes
how to contribute ECI device functions to PG-Strom.

## Evaluation Status

### 6.1 PG-Strom Installation

PG-Strom requires:
- PostgreSQL 15+ (matches our environment)
- NVIDIA GPU with compute capability 6.0+ (Pascal or newer)
- CUDA toolkit 12.0+
- Linux only (no Windows/macOS support)

Docker build environment setup:
```bash
# Install CUDA toolkit in build container
apt-get install nvidia-cuda-toolkit

# Clone and build PG-Strom
git clone https://github.com/heterodb/pg-strom.git
cd pg-strom/src && make PG_CONFIG=/usr/local/pgsql/bin/pg_config install
```

### 6.2 Baseline GPU Acceleration (Standard PostGIS Functions)

PG-Strom's `gpu_postgis.cu` supports these PostGIS functions on GPU:

| Function | GPU Support | Notes |
|----------|-------------|-------|
| ST_Distance | Yes | Point-to-point, point-to-polygon |
| ST_DWithin | Yes | Optimized for spatial joins |
| ST_Contains | Yes | Basic polygon containment |
| ST_Intersects | Partial | Simple geometries only |
| ST_MakePoint | Yes | Point construction |
| geometry_eq | Yes | Equality comparison |
| geometry_lt/gt | Yes | Ordering |

For ECEF geometries (standard SRID 4978 points), these functions can be GPU-accelerated
for WHERE-clause evaluation. Benefit is highest for large table scans with spatial filters.

### 6.3 ECI Function GPU Support

Current PG-Strom behavior with ECI-specific functions:

| Function | GPU Support | Behavior |
|----------|-------------|----------|
| ST_ECEF_To_ECI | No | Falls back to CPU |
| ST_ECI_To_ECEF | No | Falls back to CPU |
| ST_Transform (ECI SRID) | No | Falls back to CPU |
| postgis_accel_features() | No | Falls back to CPU |

PG-Strom does NOT currently support ECI functions because:
1. ECI SRIDs (900001-900003) are custom, not in EPSG registry
2. ECI transform functions (ST_ECEF_To_ECI, etc.) are not registered in PG-Strom's device function catalog
3. The ERA computation requires trig functions which PG-Strom does support on GPU

### 6.4 ECI Device Function Implementation

See `liblwgeom/accel/gpu_eci_rotate.cu` for the CUDA device functions:

- `pgstrom_eci_epoch_to_jd()` - Decimal year to Julian Date
- `pgstrom_eci_earth_rotation_angle()` - IERS 2003 ERA computation
- `pgstrom_eci_rotate_z()` - Z-axis rotation matrix application
- `pgstrom_eci_transform()` - Combined epoch-to-rotation transform

These functions are numerically equivalent to the CPU implementation in `lwgeom_eci.c`.

### 6.5 Numerical Equivalence Testing

The `pgstrom_eci_validate_kernel` in `gpu_eci_rotate.cu` provides a test harness:
- Applies ECI transforms on GPU
- Compares results against CPU-computed expected values
- Reports maximum absolute difference per coordinate
- Pass criteria: max difference < 1e-10

### 6.6 PG-Strom Contribution

To contribute ECI functions to PG-Strom:

1. **Device functions**: Copy `pgstrom_eci_*` functions from `gpu_eci_rotate.cu` into
   PG-Strom's `src/gpu_postgis.cu`

2. **Function registration**: Add entries to `pgstrom_devfunc_catalog[]` in `src/codegen.c`:
   ```c
   { "st_ecef_to_eci", 3, {GEOMETRYOID, TIMESTAMPTZOID, TEXTOID},
     "pgstrom_eci_transform", ... },
   ```

3. **Test coverage**: Add SQL test cases to `test/sql/postgis.sql` validating:
   - ECI-to-ECEF roundtrip on GPU
   - ECEF-to-ECI roundtrip on GPU
   - Numerical equivalence with CPU at multiple epochs

4. **Submit**: Create PR against heterodb/pg-strom with the above changes.

## Limitations

- PG-Strom is CUDA-only (NVIDIA hardware required)
- PG-Strom accelerates WHERE-clause evaluation, not arbitrary function calls in SELECT
- For AMD/Intel GPUs, use the custom GPU dispatch layer (Sections 4-5) instead
- PG-Strom and our custom SIMD/GPU dispatch are complementary, not competing
