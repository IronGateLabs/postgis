## Context

PostGIS has a CRS family classification system (`LW_CRS_FAMILY` enum in
`liblwgeom.h.in`) and guard functions (`gserialized_check_crs_family_not_geocentric`
in `libpgcommon/lwgeom_transform.c`) that block 2D spatial operations on
geocentric coordinates.  Guards have been applied to ~12 functions but several
2D analysis/topology functions remain unguarded.

The GiST 3D index benchmark exists at `regress/core/ecef_gist_benchmark.sql`
but covers only basic scenarios at small scales.

## Goals / Non-Goals

**Goals:**
- Complete guard coverage for all 2D spatial functions that produce wrong
  results on geocentric input
- Add adaptive 3D dispatch for functions that have 3D counterparts
- Expand GiST 3D benchmark to 100K+ datasets with standardised queries

**Non-Goals:**
- Adding guards to functions that already work correctly on ECEF (e.g.,
  ST_3DDistance, ST_3DClosestPoint) — these are fine as-is
- Modifying the CRS family detection system itself
- Optimising GiST index performance (just measuring it)

## Decisions

### 1. Guard-or-dispatch strategy

**Decision:** Functions with 3D counterparts (ST_ClosestPoint, ST_ShortestLine)
get adaptive dispatch.  Functions without meaningful 3D alternatives
(ST_Simplify, ST_ConvexHull, ST_Voronoi, ST_DelaunayTriangles,
ST_LineInterpolatePoint) get reject guards.

**Rationale:** Matches the existing pattern: ST_Distance dispatches to 3D,
ST_Area rejects.  The principle is: if a correct 3D answer exists and can be
computed by an existing function, dispatch to it; otherwise reject.

### 2. Guard placement

**Decision:** Add guards as early as possible in each function, before any
geometry deserialization, matching the existing pattern in ST_Area and
ST_Perimeter.

**Rationale:** Avoids wasting CPU on deserialisation before raising the error.

### 3. Benchmark as standalone SQL script

**Decision:** Expand the existing `regress/core/ecef_gist_benchmark.sql` rather
than creating a new framework.

**Rationale:** The existing script already has dataset generation and timing
infrastructure.  Expanding it is simpler than introducing a new benchmark tool.

## Risks / Trade-offs

- **[Risk] Breaking change for existing ECEF users** → Functions that previously
  returned silently wrong results will now ERROR.  This is intentional — wrong
  results are worse than errors.  Users can transform to geographic CRS first.

- **[Trade-off] No guard for functions that are "technically wrong but close
  enough"** → Some functions (e.g., ST_Centroid on small ECEF regions) might
  give approximately correct results.  We still guard them to be consistent.
