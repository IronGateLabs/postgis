# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PostGIS is a spatial database extension for PostgreSQL, adding support for geographic objects. Version 3.7.0dev. The codebase is C and SQL, built with GNU autotools.

## Build Commands

```bash
# First-time setup (generates configure script)
./autogen.sh

# Configure (auto-detects PostgreSQL, GEOS, PROJ, GDAL)
./configure --with-raster --with-topology --with-sfcgal

# Build
make -j"$(nproc)"

# Install into PostgreSQL (required before running tests)
sudo make install
```

Key configure options: `--with-pgconfig=`, `--with-projdir=`, `--with-geosconfig=`, `--without-raster`, `--without-topology`, `--with-library-minor-version`.

## Running Tests

PostgreSQL must be running and the current user must be able to create databases. Export connection params:
```bash
export PGHOST=127.0.0.1 PGPORT=5432 PGUSER=$(whoami) PGDATABASE=postgres
```

```bash
# Full regression suite
make check RUNTESTFLAGS="--verbose --extension --raster --topology --sfcgal"

# Single SQL regression test (requires make install first)
make -C regress check RUNTESTFLAGS="--extension" TESTS="$(pwd)/regress/core/affine"

# Single CUnit test (liblwgeom)
cd liblwgeom/cunit && make cu_tester && ./cu_tester geodetic

# Upgrade tests
make check RUNTESTFLAGS="--upgrade --extension --verbose"
```

## Architecture

**Build order:** liblwgeom -> libpgcommon -> postgis -> regress -> topology/sfcgal -> raster -> loader/utils/extensions -> doc

| Directory | Purpose |
|-----------|---------|
| `liblwgeom/` | Core lightweight geometry library (data structures, algorithms). Files prefixed `lw`. Functions prefixed `lw_` or `lwtype_` (e.g. `lwline_split`). |
| `libpgcommon/` | Bridge between liblwgeom and PostgreSQL |
| `postgis/` | PostgreSQL extension module. C functions called by PG use `PG_FUNCTION_INFO_V1` and are named to match their SQL alias (e.g. `ST_Distance`). Utility functions prefixed `pgis_`. |
| `raster/` | Raster support (`postgis_raster` extension) |
| `topology/` | Topology/ISO SQL-MM support (`postgis_topology` extension) |
| `sfcgal/` | SFCGAL 3D geometry support (`postgis_sfcgal` extension) |
| `loader/` | Shapefile utilities (shp2pgsql, pgsql2shp) |
| `extensions/` | PostgreSQL extension control files and SQL |
| `regress/` | Regression tests (`.sql` files with `.sql_expected` results) |
| `fuzzers/` | Google fuzz tests for liblwgeom |

**SQL API definitions** for each extension:
- `postgis`: `postgis/postgis_sql.in`, `postgis/geography_sql.in`, `postgis/postgis_bin_sql.in`, `postgis/postgis_spgist_sql.in`
- `postgis_raster`: `raster/rt_pg/rtpostgis_sql.in`
- `postgis_sfcgal`: `sfcgal/sfcgal_sql.in`
- `postgis_topology`: `topology/sql/*.sql.in`

## Code Style

- **C files:** Tab indentation (8-space width), 120-char line limit. See `.clang-format` and `.editorconfig`.
- **Exceptions:** `topology/*.{c,h}` and `liblwgeom/topo/**` use 2-space indent.
- Comments in C style (`/* */`), not C++ (`//`). Function docs use `/** */` for Doxygen.
- Macros and enums: `ALL_UPPERCASE`.
- Format only changed code: `git clang-format` (formats staged changes).
- Separate style-only commits from logic commits.

## Versioning & Upgrade Rules

- **Patch releases (3.x.Y):** No new SQL API functions, no new dependency requirements, no structure changes. Avoid removing/stubbing C API functions.
- **Minor releases (3.X.0):** Can add SQL API functions, require newer dependency versions, stub deprecated C API functions.
- **Major releases (X.0.0):** Can remove SQL API functions and C API functions outright.

Deprecated C API functions must be stubbed (not deleted) to support `pg_upgrade`. Stubs go in:
- `postgis/postgis_legacy.c`
- `sfcgal/postgis_sfcgal_legacy.c`
- `raster/rt_pg/rtpg_legacy.c`

SQL changes require `-- Availability:` and `-- Changed:` comments. Signature changes use `-- Replaces` and drop hooks in `*_before_upgrade.sql` / `*_after_upgrade.sql`.

## Dependency Version Guards

Guard code behind version macros in C files:
```c
#if POSTGIS_GEOS_VERSION >= 31300    // GEOS 3.13+
#if POSTGIS_PGSQL_VERSION >= 150     // PostgreSQL 15+
#if POSTGIS_PROJ_VERSION > 60000     // PROJ 6.0+
#if POSTGIS_GDAL_VERSION >= 30700    // GDAL 3.7+
#if POSTGIS_SFCGAL_VERSION >= 20100  // SFCGAL 2.1+
```

Guard tests in `regress/**/tests.mk.in`:
```makefile
ifeq ($(shell expr "$(POSTGIS_GEOS_VERSION)" ">=" 31300),1)
  TESTS += my_new_test
endif
```

Functions requiring unavailable libraries must still be exposed in SQL but emit an error at runtime.

## SQL Scripting Rules

- Use `$$` as DO delimiter (not `$func$`).
- `CREATE` statements inside `DO` blocks must start at column 1 (for uninstall script generation).
- Signature/arg changes: stage drops in `postgis_drop_before.sql`, cleanup in `*_after.sql`.

## Required Dependencies

PostgreSQL 12+, GEOS 3.10+, PROJ 6.1+, GDAL 2+, LibXML2 2.5+, JSON-C 0.9+. Optional: SFCGAL 1.4+, protobuf-c 1.1+.
