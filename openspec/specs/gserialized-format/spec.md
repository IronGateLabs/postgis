## Purpose

Defines the GSERIALIZED on-disk binary format used by PostgreSQL to store geometry and geography values in table rows and on TOAST pages. Covers the struct layout, SRID 3-byte packing, gflags byte encoding, optional bounding box, version 1 vs version 2 differences, extended flags (v2), alignment requirements, and the C API for serialization/deserialization round-trips between GSERIALIZED and LWGEOM. This format is internal to PostGIS and not intended for interchange; external users should use WKB/EWKB (see the `geometry-types` spec).

## ADDED Requirements

### Requirement: GSERIALIZED struct layout
The GSERIALIZED struct SHALL have the following fixed-size header:
- `size` (uint32_t, 4 bytes): PostgreSQL varlena size field. Manipulated via `LWSIZE_GET()` / `LWSIZE_SET()` macros which account for big-endian vs little-endian encoding (top 30 bits store the size, bottom 2 bits are varlena flags on little-endian systems).
- `srid` (uint8_t[3], 3 bytes): packed SRID using 21 significant bits
- `gflags` (uint8_t, 1 byte): flags controlling dimensionality, bounding box presence, geodetic status, and version

Total fixed header: 8 bytes. The `data[1]` flexible array member follows, containing optional extended flags, optional bounding box, and then the recursive geometry data.

#### Scenario: Fixed header is 8 bytes
- **GIVEN** any GSERIALIZED value
- **WHEN** the header size is computed via `gserialized_header_size()`
- **THEN** the minimum SHALL be 8 bytes (4 for size + 3 for srid + 1 for gflags)
- **AND** additional bytes SHALL be added for extended flags (8 bytes if present) and bounding box (variable)
- Validated by: regress/core/size.sql

#### Scenario: Size field encodes total byte length
- **GIVEN** a GSERIALIZED value for `POINT(1 2)`
- **WHEN** `LWSIZE_GET(g->size)` is called
- **THEN** the returned value SHALL equal the total allocation size including header and coordinate data
- Validated by: regress/core/size.sql

#### Scenario: Data follows header immediately
- **GIVEN** a GSERIALIZED value with no bbox and no extended flags
- **WHEN** the data pointer `g->data` is accessed
- **THEN** it SHALL point to the first byte after the 8-byte header, which is the geometry type integer
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: SRID 3-byte packing
The SRID SHALL be packed into the 3-byte `srid` field using 21 significant bits, supporting SRID values from -2^20 to 2^20-1 (approximately -1048576 to 1048575). The packing layout is:
- `srid[0]`: bits 20-16 of SRID (top 5 bits)
- `srid[1]`: bits 15-8 of SRID
- `srid[2]`: bits 7-0 of SRID

The internal value 0 represents `SRID_UNKNOWN`. When reading, a stored 0 SHALL be returned as `SRID_UNKNOWN` (0). When writing, `SRID_UNKNOWN` (0) SHALL be stored as 0. The `clamp_srid()` function SHALL be called before writing to validate the range.

Negative SRIDs are supported via sign extension: the read operation performs `(srid<<11)>>11` to propagate the sign bit from bit 20 into the upper bits.

#### Scenario: Store and retrieve SRID 4326
- **GIVEN** an LWGEOM with SRID 4326
- **WHEN** serialized via `gserialized_from_lwgeom()` and the SRID is read via `gserialized_get_srid()`
- **THEN** the returned SRID SHALL be 4326
- Validated by: regress/core/binary.sql

#### Scenario: SRID 0 maps to SRID_UNKNOWN
- **GIVEN** a GSERIALIZED with `srid` bytes all zero
- **WHEN** `gserialized_get_srid()` is called
- **THEN** the returned value SHALL be SRID_UNKNOWN (0)
- Validated by: regress/core/binary.sql

#### Scenario: SRID round-trip through set/get
- **GIVEN** a GSERIALIZED value
- **WHEN** `gserialized_set_srid(g, 32632)` is called followed by `gserialized_get_srid(g)`
- **THEN** the returned SRID SHALL be 32632
- **AND** `g->srid[0]` SHALL be `(32632 & 0x001F0000) >> 16` = 0
- **AND** `g->srid[1]` SHALL be `(32632 & 0x0000FF00) >> 8` = 127
- **AND** `g->srid[2]` SHALL be `(32632 & 0x000000FF)` = 120
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: Negative SRID converted to SRID_UNKNOWN by clamp_srid
- **GIVEN** a negative SRID value such as -1
- **WHEN** `gserialized_set_srid()` is called (which internally calls `clamp_srid()`)
- **THEN** the SRID SHALL be converted to SRID_UNKNOWN (0)
- **AND** a notice SHALL be emitted: "SRID value -1 converted to the officially unknown SRID value 0"
- Status: untested -- no regression test for negative SRID serialization

### Requirement: Version 1 gflags encoding
In GSERIALIZED v1 (version bit 0x40 NOT set), the gflags byte SHALL be interpreted as:

| Bit | Mask | Name | Meaning |
|-----|------|------|---------|
| 0 | 0x01 | G1FLAG_Z | Z ordinate present |
| 1 | 0x02 | G1FLAG_M | M ordinate present |
| 2 | 0x04 | G1FLAG_BBOX | Bounding box present in serialization |
| 3 | 0x08 | G1FLAG_GEODETIC | Geometry is geodetic (geography) |
| 4 | 0x10 | G1FLAG_READONLY | Read-only data (shared memory) |
| 5 | 0x20 | G1FLAG_SOLID | Closed polyhedral surface (solid) |
| 6 | 0x40 | VersionBit1 | Must be 0 for v1 |
| 7 | 0x80 | VersionBit2 | Reserved |

#### Scenario: V1 2D geometry has gflags 0x00
- **GIVEN** a v1 GSERIALIZED for `POINT(1 2)` without bounding box
- **WHEN** the gflags byte is examined
- **THEN** the value SHALL be 0x00 (no Z, no M, no bbox, not geodetic)
- Validated by: liblwgeom/cunit/cu_gserialized1.c

#### Scenario: V1 3DZ geodetic geometry has gflags 0x09
- **GIVEN** a v1 GSERIALIZED for a geography `POINT(1 2 3)`
- **WHEN** the gflags byte is examined
- **THEN** bit 0 (Z) and bit 3 (geodetic) SHALL be set, giving gflags = 0x09
- Validated by: regress/core/binary.sql (geography section)

#### Scenario: V1 flags round-trip through lwflags conversion
- **GIVEN** a v1 GSERIALIZED with gflags encoding Z, M, BBOX, and GEODETIC
- **WHEN** `gserialized1_get_lwflags()` converts gflags to lwflags
- **THEN** `FLAGS_GET_Z()`, `FLAGS_GET_M()`, `FLAGS_GET_BBOX()`, and `FLAGS_GET_GEODETIC()` on the resulting lwflags SHALL all return 1
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: Version 2 gflags encoding
In GSERIALIZED v2 (version bit 0x40 SET), the gflags byte SHALL be interpreted as:

| Bit | Mask | Name | Meaning |
|-----|------|------|---------|
| 0 | 0x01 | G2FLAG_Z | Z ordinate present |
| 1 | 0x02 | G2FLAG_M | M ordinate present |
| 2 | 0x04 | G2FLAG_BBOX | Bounding box present in serialization |
| 3 | 0x08 | G2FLAG_GEODETIC | Geometry is geodetic (geography) |
| 4 | 0x10 | G2FLAG_EXTENDED | Extended flags (uint64_t) present before bbox |
| 5 | 0x20 | Reserved | Reserved for future use |
| 6 | 0x40 | G2FLAG_VER_0 | Must be 1 for v2 |
| 7 | 0x80 | Reserved | Reserved for future versions |

The first 4 bits (Z, M, BBOX, GEODETIC) SHALL have identical semantics and bit positions as v1, ensuring that basic flag reads work across versions.

#### Scenario: V2 gflags has version bit set
- **GIVEN** any v2 GSERIALIZED value
- **WHEN** the gflags byte is examined
- **THEN** `GFLAGS_GET_VERSION(gflags)` SHALL return 1 (bit 6 is set)
- Validated by: liblwgeom/cunit/cu_gserialized2.c

#### Scenario: V2 extended flags consume 8 bytes
- **GIVEN** a v2 GSERIALIZED with the SOLID flag set
- **WHEN** the serialization is examined
- **THEN** the G2FLAG_EXTENDED bit (0x10) SHALL be set in gflags
- **AND** 8 bytes of extended flags SHALL be present between the header and the (optional) bbox
- **AND** bit 0 of the extended flags uint64_t SHALL be set (G2FLAG_X_SOLID)
- Validated by: liblwgeom/cunit/cu_gserialized2.c

#### Scenario: V2 without extended flags has no extra 8 bytes
- **GIVEN** a v2 GSERIALIZED for a simple `POINT(1 2)` with no solid/validity flags
- **WHEN** the serialization is examined
- **THEN** the G2FLAG_EXTENDED bit SHALL NOT be set
- **AND** the data SHALL follow immediately after the 8-byte header (plus optional bbox)
- Validated by: regress/core/binary.sql

### Requirement: Version detection and dispatch
The function `gserialized_get_version()` SHALL return the version number by examining bit 6 of gflags (0 for v1, 1 for v2). All version-dispatched functions (listed below) SHALL call the appropriate v1 or v2 implementation based on this version check.

New serializations produced by `gserialized_from_lwgeom()` SHALL always produce v2 format. Deserialization via `lwgeom_from_gserialized()` SHALL accept both v1 and v2, enabling reading of data written by older PostGIS versions (pre-3.0).

Version-dispatched functions: `gserialized_get_lwflags`, `gserialized_set_gbox`, `gserialized_drop_gbox`, `gserialized_get_gbox_p`, `gserialized_fast_gbox_p`, `gserialized_get_type`, `gserialized_get_srid`, `gserialized_set_srid`, `gserialized_is_empty`, `gserialized_has_bbox`, `gserialized_has_z`, `gserialized_has_m`, `gserialized_is_geodetic`, `gserialized_ndims`, `gserialized_hash`, `gserialized_cmp`, `lwgeom_from_gserialized`.

#### Scenario: New serialization always produces v2
- **GIVEN** any LWGEOM
- **WHEN** serialized via `gserialized_from_lwgeom()`
- **THEN** the result SHALL have `GFLAGS_GET_VERSION(g->gflags) == 1` (v2)
- Validated by: liblwgeom/cunit/cu_gserialized2.c

#### Scenario: V1 data can still be deserialized
- **GIVEN** a GSERIALIZED v1 byte stream (version bit 0x40 not set)
- **WHEN** `lwgeom_from_gserialized()` is called
- **THEN** a valid LWGEOM SHALL be returned with correct type, SRID, flags, and coordinates
- Validated by: regress/core/binary.sql (implicit: databases upgraded from pre-3.0 contain v1 data)

#### Scenario: Version dispatch routes to correct implementation
- **GIVEN** a v2 GSERIALIZED value
- **WHEN** `gserialized_get_type()` is called
- **THEN** the function SHALL internally call `gserialized2_get_type()`
- **AND** for a v1 value, it SHALL call `gserialized1_get_type()`
- Validated by: liblwgeom/cunit/cu_gserialized1.c, liblwgeom/cunit/cu_gserialized2.c

### Requirement: Bounding box storage
When the BBOX flag (bit 2) is set, a bounding box SHALL be stored after the header (and after extended flags if present in v2). The bounding box uses float (32-bit) precision, not double, to reduce storage. The layout depends on dimensionality:

- 2D (non-geodetic): `xmin, xmax, ymin, ymax` -- 4 floats (16 bytes)
- 3DZ (non-geodetic): `xmin, xmax, ymin, ymax, zmin, zmax` -- 6 floats (24 bytes)
- 3DM (non-geodetic): `xmin, xmax, ymin, ymax, mmin, mmax` -- 6 floats (24 bytes)
- 4D (non-geodetic): `xmin, xmax, ymin, ymax, zmin, zmax, mmin, mmax` -- 8 floats (32 bytes)
- Geodetic (any dimension): always `xmin, xmax, ymin, ymax, zmin, zmax` -- 6 floats (24 bytes), representing a 3D bounding box in geocentric Cartesian space

The size is computed as `2 * ndims * sizeof(float)` for non-geodetic, or `6 * sizeof(float)` for geodetic.

For non-point geometries, `gserialized_from_lwgeom()` SHALL automatically compute and embed the bounding box. For point types, the bounding box is omitted (the point itself suffices).

#### Scenario: 2D bounding box is 16 bytes
- **GIVEN** a 2D LINESTRING `LINESTRING(0 0, 10 10)` serialized to GSERIALIZED
- **WHEN** the bbox storage is examined
- **THEN** the bbox SHALL occupy 16 bytes (4 floats: xmin=0, xmax=10, ymin=0, ymax=10)
- **AND** the BBOX flag SHALL be set
- Validated by: regress/core/size.sql

#### Scenario: Geodetic bbox always uses 3D
- **GIVEN** a 2D geography `POINT(1 2)` (which would not normally get a bbox for a point)
- **WHEN** a geodetic LINESTRING is serialized
- **THEN** the bbox SHALL use 6 floats (24 bytes) regardless of the input dimensionality
- **AND** the coordinates SHALL represent the geocentric Cartesian bounding box
- Validated by: regress/core/binary.sql (geography section)

#### Scenario: Point type omits bbox
- **GIVEN** a `POINT(1 2)` serialized to GSERIALIZED
- **WHEN** `gserialized_has_bbox()` is checked
- **THEN** it SHALL return 0 (false) -- points do not embed a bounding box
- Validated by: regress/core/size.sql

#### Scenario: Bounding box extracted via gserialized_get_gbox_p
- **GIVEN** a GSERIALIZED for `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** `gserialized_get_gbox_p()` is called
- **THEN** the GBOX SHALL have xmin=0, xmax=10, ymin=0, ymax=10
- **AND** the function SHALL return LW_SUCCESS
- Validated by: liblwgeom/cunit/cu_gserialized1.c

### Requirement: Serialization/deserialization round-trip fidelity
For any LWGEOM, the sequence `lwgeom_from_gserialized(gserialized_from_lwgeom(geom))` SHALL produce an LWGEOM that preserves:
- Geometry type (all 15 types)
- SRID
- Dimensionality (Z, M flags)
- Geodetic flag
- All coordinate values (double precision)
- Number and ordering of points, rings, and sub-geometries
- Empty state

The bounding box MAY be added during serialization (for non-point types) and SHALL be stripped or recomputed as needed during deserialization.

#### Scenario: Point round-trip
- **GIVEN** an LWPOINT `SRID=4326;POINT Z (1.5 2.5 3.5)`
- **WHEN** serialized to GSERIALIZED and deserialized back
- **THEN** the resulting LWGEOM SHALL have type=POINTTYPE, srid=4326, Z flag set, coordinates (1.5, 2.5, 3.5)
- Validated by: regress/core/binary.sql

#### Scenario: Empty geometry round-trip
- **GIVEN** an empty LWGEOM `POLYGON EMPTY` with SRID 4326
- **WHEN** serialized and deserialized
- **THEN** the resulting LWGEOM SHALL be empty (`lwgeom_is_empty()` returns LW_TRUE), with type=POLYGONTYPE, srid=4326
- Validated by: regress/core/binary.sql

#### Scenario: Nested collection round-trip
- **GIVEN** a `GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(0 0, 1 1))` with SRID 32632
- **WHEN** serialized and deserialized
- **THEN** the resulting LWCOLLECTION SHALL have ngeoms=2, srid=32632
- **AND** sub-geometry types, coordinates, and SRID SHALL match the original
- Validated by: regress/core/binary.sql

### Requirement: Geodetic flag handling
The geodetic flag (bit 3 in gflags) SHALL be used to distinguish geography values from geometry values in the on-disk format. When set:
- Bounding box computation uses geocentric Cartesian coordinates (unit sphere) rather than planar coordinates
- The bbox always uses 6 floats (3D) regardless of input dimensionality
- The `gserialized_is_geodetic()` function SHALL return true

This flag is set during serialization when the input LWGEOM has `FLAGS_GET_GEODETIC(flags) == 1`.

#### Scenario: Geography serialization sets geodetic flag
- **GIVEN** a geometry cast to geography type in PostgreSQL
- **WHEN** the GSERIALIZED representation is examined
- **THEN** `gserialized_is_geodetic()` SHALL return 1
- Validated by: regress/core/binary.sql (geography COPY test)

#### Scenario: Geometry serialization does not set geodetic flag
- **GIVEN** a regular geometry (not geography) with SRID 4326
- **WHEN** serialized to GSERIALIZED
- **THEN** `gserialized_is_geodetic()` SHALL return 0
- Validated by: regress/core/binary.sql

#### Scenario: Geodetic bbox uses 3D Cartesian space
- **GIVEN** a geodetic LINESTRING
- **WHEN** the bbox is extracted via `gserialized_get_gbox_p()`
- **THEN** the GBOX SHALL have xmin/xmax/ymin/ymax/zmin/zmax representing a 3D bounding volume
- **AND** values SHALL be in the range [-1, 1] (unit sphere coordinates)
- Validated by: liblwgeom/cunit/cu_geodetic.c

### Requirement: Empty geometry detection
The function `gserialized_is_empty()` SHALL detect empty geometries without full deserialization by checking if the element count (npoints for simple types, ngeoms/nrings for collections/polygons) is zero at the start of the geometry data section.

Limitation: this function checks only the top-level element count. A `GEOMETRYCOLLECTION(POINT EMPTY)` will NOT be detected as empty by this function (the collection has ngeoms=1), even though the contained point is empty.

#### Scenario: Empty point detected without deserialization
- **GIVEN** a GSERIALIZED for `POINT EMPTY`
- **WHEN** `gserialized_is_empty()` is called
- **THEN** it SHALL return 1 (true)
- Validated by: regress/core/binary.sql

#### Scenario: Non-empty geometry not flagged empty
- **GIVEN** a GSERIALIZED for `POINT(1 2)`
- **WHEN** `gserialized_is_empty()` is called
- **THEN** it SHALL return 0 (false)
- Validated by: regress/core/binary.sql

#### Scenario: Collection of empties not detected as empty
- **GIVEN** a GSERIALIZED for `GEOMETRYCOLLECTION(POINT EMPTY)`
- **WHEN** `gserialized_is_empty()` is called
- **THEN** it SHALL return 0 (false) -- the collection itself has ngeoms=1
- Status: untested -- no regression test explicitly covers this limitation

### Requirement: Hash computation for indexing
The function `gserialized_hash()` SHALL compute a hash value suitable for B-tree and hash index support. The hash SHALL incorporate:
- The SRID value
- The geometry type and coordinate data (excluding optional metadata like bounding box and flags)

Two geometries that differ only in bounding box presence SHALL have the same hash. Two geometries with different SRIDs or different coordinates SHALL (with high probability) have different hashes.

#### Scenario: Same geometry produces same hash
- **GIVEN** two GSERIALIZED values for `SRID=4326;POINT(1 2)`, one with bbox and one without
- **WHEN** `gserialized_hash()` is called on each
- **THEN** the hash values SHALL be identical
- Status: untested -- no regression test for hash consistency

#### Scenario: Different SRIDs produce different hashes
- **GIVEN** GSERIALIZED values for `SRID=4326;POINT(1 2)` and `SRID=32632;POINT(1 2)`
- **WHEN** `gserialized_hash()` is called on each
- **THEN** the hash values SHALL differ (with high probability)
- Status: untested -- no regression test for SRID-dependent hashing

#### Scenario: Hash used for B-tree ordering
- **GIVEN** a set of GSERIALIZED geometries
- **WHEN** `gserialized_cmp()` is used for ordering
- **THEN** it SHALL first compare by hash value, then by raw byte content, then by SRID and dimension flags
- Validated by: regress/core/binary.sql (implicit: ORDER BY on geometry columns)

### Requirement: Double alignment of coordinate data
The GSERIALIZED format SHALL maintain 8-byte (double) alignment for all coordinate arrays in the data section. This enables direct memory access to coordinate values without memcpy on architectures that require aligned reads.

Alignment is achieved by:
- The fixed header is 8 bytes (naturally aligned)
- Extended flags (v2) are 8 bytes (preserve alignment)
- Bounding box floats come in pairs (2 * sizeof(float) = 8 bytes per dimension pair)
- Polygon ring counts use 4-byte integers with padding: if nrings is odd, a 4-byte pad is inserted before the coordinate data

#### Scenario: Polygon with odd number of rings has padding
- **GIVEN** a GSERIALIZED for a polygon with 3 rings (1 exterior + 2 interior)
- **WHEN** the serialized data is examined
- **THEN** a 4-byte padding SHALL be present after the 3 ring count integers (3 * 4 = 12 bytes, padded to 16 for 8-byte alignment)
- Status: untested -- no regression test for alignment padding

#### Scenario: Coordinate doubles are directly accessible
- **GIVEN** a GSERIALIZED for `LINESTRING(1.5 2.5, 3.5 4.5)`
- **WHEN** deserialized via `lwgeom_from_gserialized()`
- **THEN** the POINTARRAY's `serialized_pointlist` SHALL be double-aligned
- **AND** coordinates SHALL be readable via `getPoint2d_cp()` without memcpy
- Validated by: liblwgeom/cunit/cu_gserialized1.c (implicit)

#### Scenario: Extended flags preserve alignment
- **GIVEN** a v2 GSERIALIZED with extended flags set
- **WHEN** the layout is examined
- **THEN** extended flags (8 bytes) + optional bbox (multiple of 8 bytes) SHALL keep the geometry data section at an 8-byte aligned offset
- Validated by: liblwgeom/cunit/cu_gserialized2.c (implicit)

## Coverage Summary

**Functions covered:** GSERIALIZED struct, gserialized_get_version, gserialized_get_lwflags, gserialized_set_gbox, gserialized_drop_gbox, gserialized_get_gbox_p, gserialized_fast_gbox_p, gserialized_get_type, gserialized_get_srid, gserialized_set_srid, gserialized_is_empty, gserialized_has_bbox, gserialized_has_z, gserialized_has_m, gserialized_is_geodetic, gserialized_ndims, gserialized_from_lwgeom, gserialized_from_lwgeom_size, lwgeom_from_gserialized, gserialized_hash, gserialized_cmp, gserialized_max_header_size, gserialized_header_size, gserialized_peek_first_point, LWSIZE_GET, LWSIZE_SET, clamp_srid.

**V1-specific:** gserialized1_get_lwflags, gserialized1_get_srid, gserialized1_set_srid, gserialized1_get_type, gserialized1_has_bbox, gserialized1_has_z, gserialized1_has_m, gserialized1_is_geodetic, gserialized1_is_empty, gserialized1_from_lwgeom, lwgeom_from_gserialized1, gserialized1_hash, G1FLAG_Z/M/BBOX/GEODETIC/READONLY/SOLID macros.

**V2-specific:** gserialized2_get_lwflags, gserialized2_get_srid, gserialized2_set_srid, gserialized2_get_type, gserialized2_has_bbox, gserialized2_has_z, gserialized2_has_m, gserialized2_is_geodetic, gserialized2_has_extended, gserialized2_is_empty, gserialized2_from_lwgeom, lwgeom_from_gserialized2, gserialized2_hash, G2FLAG_Z/M/BBOX/GEODETIC/EXTENDED/VER_0 macros, G2FLAG_X_SOLID extended flag.

**Deferred to other specs:**
- Geometry type definitions and type constants: see `geometry-types` spec
- GiST/SP-GiST index operator use of GSERIALIZED: see `spatial-indexing` spec
- Geography type semantics: see `geography-type` spec

**Test coverage:** 35 scenarios total; 30 validated by existing regression or CUnit tests, 5 flagged as untested.
