## Purpose

Defines the spatial indexing subsystem in PostGIS, covering GiST 2D and N-D indexes, SP-GiST quad-tree indexes, BRIN block range indexes, operator classes and their operators, selectivity estimation, and the two-phase index-filter-then-exact-test pattern. This spec depends on geometry-types for LWGEOM structures and gserialized-format for the on-disk bounding box representations (BOX2DF, GIDX).

## ADDED Requirements

### Requirement: GiST 2D operator class
The operator class `gist_geometry_ops_2d` SHALL be the DEFAULT operator class for geometry using the GiST access method. It SHALL use `box2df` (2D float bounding box) as the storage type and support the following operators:

| Strategy | Operator | Meaning |
|----------|----------|---------|
| 1 | `<<` | strictly left of |
| 2 | `&<` | does not extend to the right of |
| 3 | `&&` | overlaps (bounding box intersection) |
| 4 | `&>` | does not extend to the left of |
| 5 | `>>` | strictly right of |
| 6 | `~=` | same bounding box |
| 7 | `~` | contains |
| 8 | `@` | contained by |
| 9 | `&<\|` | does not extend above |
| 10 | `<<\|` | strictly below |
| 11 | `\|>>` | strictly above |
| 12 | `\|&>` | does not extend below |
| 13 | `<->` | distance (ORDER BY, returns float) |
| 14 | `<#>` | bounding box distance (ORDER BY, returns float) |

Support functions SHALL include: consistent (1), union (2), compress (3), decompress (4), penalty (5), picksplit (6), same (7), distance (8), and optionally sortsupport (11, PostgreSQL 15+).

#### Scenario: Bounding box overlap query uses GiST 2D index
- **GIVEN** a table with a GiST 2D index on a geometry column
- **WHEN** a query with `WHERE geom && ST_MakeEnvelope(0,0,1,1,4326)` is executed
- **THEN** the query planner SHALL use the GiST index for the `&&` operator
- Validated by: regress/core/regress_index.sql

#### Scenario: KNN distance ordering uses GiST 2D index
- **GIVEN** a table with a GiST 2D index on a geometry column
- **WHEN** a query with `ORDER BY geom <-> query_point LIMIT k` is executed
- **THEN** the index SHALL be used for distance-ordered retrieval
- Validated by: regress/core/knn_recheck.sql

#### Scenario: NULL geometries handled in GiST index
- **GIVEN** a table with NULL geometry values and a GiST 2D index
- **WHEN** queries with `&&` are executed
- **THEN** NULL entries SHALL not cause errors and SHALL not match any predicate
- Validated by: regress/core/regress_index_nulls.sql

#### Scenario: Containment operators use GiST 2D
- **GIVEN** a table with a GiST 2D index
- **WHEN** a query with `WHERE big_geom ~ small_geom` (contains) is executed
- **THEN** the GiST 2D index SHALL be used
- Validated by: regress/core/regress_index.sql

### Requirement: GiST 2D support functions
The GiST 2D implementation SHALL provide the following support functions operating on BOX2DF (2D float bounding boxes):
- **consistent**: determine if a key (BOX2DF) is consistent with a query for a given strategy
- **union**: compute the bounding box union of a set of entries
- **compress**: convert a geometry to its BOX2DF representation
- **decompress**: convert BOX2DF back (identity operation)
- **penalty**: compute the cost of inserting a new entry into a subtree (area enlargement)
- **picksplit**: split a set of entries into two groups minimizing overlap
- **same**: check if two BOX2DF keys are identical
- **distance**: compute distance between a key and query for KNN search
- **sortsupport** (PG 15+): enable sort-based index build for faster creation

The picksplit function SHALL use double-sorting algorithm to find optimal partitioning, limiting split ratio to avoid degenerate splits (no more than 2/3 of entries in one child).

#### Scenario: Compress converts geometry to BOX2DF
- **GIVEN** a geometry `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))`
- **WHEN** the GiST compress function is called
- **THEN** the result SHALL be a BOX2DF with xmin=0, ymin=0, xmax=10, ymax=10
- Status: untested -- internal GiST function not directly testable from SQL

#### Scenario: Penalty computes area enlargement
- **GIVEN** two BOX2DF entries
- **WHEN** the penalty function is called
- **THEN** it SHALL return the area increase required to accommodate the new entry
- Status: untested -- internal GiST function not directly testable from SQL

#### Scenario: Sortsupport enabled on PostgreSQL 15+
- **GIVEN** PostgreSQL version >= 15
- **WHEN** a GiST 2D index is built on a geometry column
- **THEN** the sortsupport function (strategy 11) SHALL be available for faster bulk index construction
- Status: untested -- version-dependent feature

### Requirement: GiST N-D operator class
The operator class `gist_geometry_ops_nd` SHALL provide N-dimensional (3D/4D) indexing for geometry using the GiST access method. It SHALL use `gidx` (N-dimensional float bounding box) as storage and support:

| Strategy | Operator | Meaning |
|----------|----------|---------|
| 3 | `&&&` | N-D bounding box overlaps |
| 6 | `~~=` | N-D same |
| 7 | `~~` | N-D contains |
| 8 | `@@` | N-D contained by |
| 13 | `<<->>` | N-D centroid distance (ORDER BY) |
| 20 | `\|=\|` | closest point of approach distance (temporal KNN) |

Support functions: consistent (1), union (2), compress (3), decompress (4), penalty (5), picksplit (6), same (7), distance (8).

#### Scenario: 3D overlap query uses N-D GiST index
- **GIVEN** a table with 3D geometries and a GiST N-D index
- **WHEN** a query with `WHERE geom &&& query_box3d` is executed
- **THEN** the N-D GiST index SHALL be used, comparing 3D bounding boxes
- Validated by: regress/core/regress_gist_index_nd.sql

#### Scenario: N-D contains operator
- **GIVEN** a table with 3D geometries and a GiST N-D index
- **WHEN** a query with `WHERE geom1 ~~ geom2` (N-D contains) is executed
- **THEN** the result SHALL correctly identify 3D containment
- Validated by: regress/core/regress_gist_index_nd.sql

#### Scenario: Temporal KNN with closest point of approach
- **GIVEN** a table with 4D geometries (xyzt trajectories) and a GiST N-D index
- **WHEN** a query with `ORDER BY traj |=| query_traj` is executed
- **THEN** the index SHALL return results ordered by closest point of approach distance
- Validated by: regress/core/temporal_knn.sql

### Requirement: SP-GiST 2D operator class
The operator class `spgist_geometry_ops_2d` SHALL be the DEFAULT operator class for geometry using the SP-GiST access method. It implements a quad-tree in 4-dimensional space (treating bounding boxes as 4D points: xmin, ymin, xmax, ymax), splitting into 16 quadrants per level.

The operator class SHALL support the same 2D operators as GiST 2D (strategies 1-12: `<<`, `&<`, `&&`, `&>`, `>>`, `~=`, `~`, `@`, `&<|`, `<<|`, `|>>`, `|&>`).

Support functions: config (1), choose (2), picksplit (3), inner_consistent (4), leaf_consistent (5), compress (6).

#### Scenario: SP-GiST 2D index overlap query
- **GIVEN** a table with an SP-GiST 2D index on a geometry column
- **WHEN** a query with `WHERE geom && query_envelope` is executed
- **THEN** the SP-GiST index SHALL be usable for the overlap predicate
- Validated by: regress/core/regress_spgist_index_2d.sql

#### Scenario: SP-GiST 2D handles spaghetti data
- **GIVEN** a dataset with many overlapping geometries
- **WHEN** an SP-GiST 2D index is built and queried
- **THEN** the quad-tree decomposition SHALL handle overlapping objects by treating bounding boxes as points in 4D space
- Validated by: regress/core/regress_spgist_index_2d.sql

#### Scenario: SP-GiST 2D positional operators
- **GIVEN** a table with an SP-GiST 2D index
- **WHEN** queries with `<<` (strictly left) and `>>` (strictly right) are executed
- **THEN** the index SHALL correctly filter using bounding box positional tests
- Validated by: regress/core/regress_spgist_index_2d.sql

### Requirement: SP-GiST 3D and N-D operator classes
PostGIS SHALL provide additional SP-GiST operator classes:
- `spgist_geometry_ops_3d`: supports 3D operators `&/&` (3D overlaps), `@>>` (3D contains), `<<@` (3D contained by), `~=` (3D same)
- `spgist_geometry_ops_nd`: supports N-D operators `&&&` (N-D overlaps), `~~` (N-D contains), `@@` (N-D contained by), `~~=` (N-D same)
- `spgist_geography_ops_nd`: supports geography `&&` operator for N-D bounding box overlap

#### Scenario: SP-GiST 3D overlap query
- **GIVEN** a table with 3D geometries and an SP-GiST 3D index
- **WHEN** a query with `WHERE geom &/& query_geom` (3D overlaps) is executed
- **THEN** the SP-GiST 3D index SHALL be used
- Validated by: regress/core/regress_spgist_index_3d.sql

#### Scenario: SP-GiST N-D query
- **GIVEN** a table with N-D geometries and an SP-GiST N-D index
- **WHEN** a query with `WHERE geom &&& query_geom` is executed
- **THEN** the SP-GiST N-D index SHALL be used
- Validated by: regress/core/regress_spgist_index_nd.sql

#### Scenario: SP-GiST geography N-D index
- **GIVEN** a table with geography data and an SP-GiST geography N-D index
- **WHEN** a query with `WHERE geog && query_geog` is executed
- **THEN** the SP-GiST index SHALL be used for geography bounding box overlap
- Status: untested -- no dedicated regression test for SP-GiST geography

### Requirement: BRIN operator classes
PostGIS SHALL provide BRIN (Block Range INdex) operator classes for geometry:
- `brin_geometry_inclusion_ops_2d` (DEFAULT for geometry USING brin): stores BOX2DF, supports `&&` (overlaps), `~` (contains), `@` (contained by) including cross-type operators with box2df
- `brin_geometry_inclusion_ops_3d`: stores GIDX, supports `&&&` (N-D overlaps) including cross-type with gidx
- `brin_geometry_inclusion_ops_4d`: stores GIDX, supports `&&&` (N-D overlaps) including cross-type with gidx

BRIN indexes summarize block ranges using inclusion-based logic: each range stores the bounding box union of all geometries in that block range.

#### Scenario: BRIN 2D index overlap query
- **GIVEN** a table with spatially sorted data and a BRIN 2D index
- **WHEN** a query with `WHERE geom && query_envelope` is executed
- **THEN** the BRIN index SHALL eliminate block ranges whose summary box does not overlap the query
- Validated by: regress/core/regress_brin_index.sql

#### Scenario: BRIN 3D index
- **GIVEN** a table with 3D geometries and a BRIN 3D index
- **WHEN** a query with `WHERE geom &&& query_geom` is executed
- **THEN** the BRIN index SHALL use GIDX storage for 3D range summaries
- Validated by: regress/core/regress_brin_index_3d.sql

#### Scenario: BRIN geography index
- **GIVEN** a table with geography data and a BRIN index
- **WHEN** a query with `WHERE geog && query_geog` is executed
- **THEN** the BRIN index SHALL support geography bounding box overlap
- Validated by: regress/core/regress_brin_index_geography.sql

#### Scenario: BRIN handles empty geometries
- **GIVEN** a block range containing empty geometries
- **WHEN** the BRIN add_value function encounters an empty geometry
- **THEN** it SHALL set the "contains empty" flag without raising an error
- Status: untested -- edge case for BRIN empty handling

### Requirement: Selectivity estimation
PostGIS SHALL provide selectivity estimation functions for the query planner:
- `gserialized_gist_sel`: restriction selectivity for geometry operators (e.g., `&&` with a constant)
- `gserialized_gist_joinsel`: join selectivity for geometry operators (e.g., `t1.geom && t2.geom`)
- `gserialized_gist_sel_nd`: N-D restriction selectivity
- `gserialized_gist_joinsel_nd`: N-D join selectivity

Estimation uses 2D or N-D histograms computed by ANALYZE:
- geometry `&&` geometry uses 2D histogram
- geometry `&&&` geometry uses N-D histogram
- geography `&&` geography uses N-D histogram

The `gserialized_gist_sel` function SHALL sum histogram cell values that overlap the constant search box. The `gserialized_gist_joinsel` function SHALL sum the product of overlapping cells from both relations' histograms.

#### Scenario: ANALYZE computes spatial histogram
- **GIVEN** a table with geometry data
- **WHEN** `ANALYZE` is run on the table
- **THEN** the system SHALL compute 2D and N-D histograms of geometry bounding box occurrences
- Validated by: regress/core/regress_selectivity.sql

#### Scenario: Selectivity estimation for overlap predicate
- **GIVEN** a table with analyzed geometry data
- **WHEN** the planner evaluates `WHERE geom && constant_box`
- **THEN** `gserialized_gist_sel` SHALL return an estimated selectivity based on histogram overlap
- Validated by: regress/core/regress_selectivity.sql

#### Scenario: Join selectivity estimation
- **GIVEN** two tables with analyzed geometry data
- **WHEN** the planner evaluates `WHERE t1.geom && t2.geom`
- **THEN** `gserialized_gist_joinsel` SHALL return an estimated selectivity based on histogram cross-product
- Validated by: regress/core/regress_selectivity.sql

#### Scenario: Estimated extent from statistics
- **GIVEN** an analyzed table with geometry data
- **WHEN** `ST_EstimatedExtent(schema, table, column)` is called
- **THEN** the function SHALL return a bounding box derived from the histogram statistics
- Validated by: regress/core/estimatedextent.sql

### Requirement: Two-phase index-then-exact pattern
Spatial index operators (e.g., `&&`) operate on bounding boxes and may return false positives. SQL functions like `ST_Intersects()`, `ST_Contains()`, and other spatial predicates use a two-phase pattern:
1. **Index phase**: the `&&` operator filters candidates using bounding box overlap (fast, index-accelerated)
2. **Exact phase**: the actual geometric predicate is evaluated on the candidate set (precise, potentially expensive)

This pattern is typically expressed in SQL function definitions as:
```sql
SELECT $1 && $2 AND _ST_Intersects($1, $2)
```
where the `&&` operator provides the index support and `_ST_Intersects()` is the exact computation.

#### Scenario: ST_Intersects uses two-phase pattern
- **GIVEN** a table with a spatial index
- **WHEN** `WHERE ST_Intersects(geom, query_geom)` is executed
- **THEN** the planner SHALL use the `&&` index operator for initial filtering, then apply exact intersection test on candidates
- Validated by: regress/core/regress_index.sql

#### Scenario: False positive from bounding box overlap eliminated by exact test
- **GIVEN** two L-shaped polygons whose bounding boxes overlap but geometries do not intersect
- **WHEN** `ST_Intersects(geom1, geom2)` is evaluated
- **THEN** the `&&` operator SHALL return true (bounding box overlap) but `_ST_Intersects()` SHALL return false (exact test)
- Status: untested -- no dedicated test for false positive elimination

#### Scenario: KNN recheck for exact distances
- **GIVEN** a GiST index used for KNN distance ordering
- **WHEN** `ORDER BY geom <-> query_point` returns candidates
- **THEN** the system SHALL recheck exact distances to ensure correct ordering (bounding box distances may differ from true geometry distances)
- Validated by: regress/core/knn_recheck.sql

### Requirement: Geography GiST operator class
The operator class `gist_geography_ops` SHALL be the DEFAULT operator class for geography using GiST. It SHALL use `gidx` storage (N-D bounding box on the sphere) and support:
- Operator 3: `&&` (bounding box overlaps)
- Operator 13: `<->` (KNN distance, ORDER BY)

The distance function for geography KNN SHALL use spherical distance (not spheroid) for index consistency.

#### Scenario: Geography overlap uses GiST index
- **GIVEN** a table with geography data and a GiST index
- **WHEN** a query with `WHERE geog && query_geog` is executed
- **THEN** the GiST geography index SHALL be used
- Validated by: regress/core/regress_brin_index_geography.sql (implicitly tests geography indexing)

#### Scenario: Geography KNN uses sphere distance
- **GIVEN** a table with geography data and a GiST index
- **WHEN** `ORDER BY geog <-> query_point` is used
- **THEN** the index distance function SHALL use spherical (not spheroid) distance for consistency
- Status: untested -- internal behavior of geography KNN distance function

#### Scenario: Geography KNN distance is approximate
- **GIVEN** geography points at different latitudes
- **WHEN** KNN ordering is used
- **THEN** the index distance SHALL be an approximation that may differ from `ST_Distance(geography)` with spheroid, but ordering SHALL be correct for nearby points
- Status: untested -- no dedicated test for geography KNN approximation quality
