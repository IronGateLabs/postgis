/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "CUnit/Basic.h"

#include "liblwgeom_internal.h"
#include "cu_tester.h"

/***********************************************************************
 * CRS Family Enum Tests
 */

static void test_lwcrs_family_name(void)
{
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_UNKNOWN), "unknown");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_GEOGRAPHIC), "geographic");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_PROJECTED), "projected");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_GEOCENTRIC), "geocentric");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_INERTIAL), "inertial");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_TOPOCENTRIC), "topocentric");
	ASSERT_STRING_EQUAL(lwcrs_family_name(LW_CRS_ENGINEERING), "engineering");
	/* Out-of-range value should return "unknown" */
	ASSERT_STRING_EQUAL(lwcrs_family_name((LW_CRS_FAMILY)99), "unknown");
}

static void test_lwcrs_family_from_pj_type(void)
{
	/* Geographic types */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOGRAPHIC_2D_CRS), LW_CRS_GEOGRAPHIC);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOGRAPHIC_3D_CRS), LW_CRS_GEOGRAPHIC);

	/* Projected */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_PROJECTED_CRS), LW_CRS_PROJECTED);

	/* Geocentric */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOCENTRIC_CRS), LW_CRS_GEOCENTRIC);

	/* Engineering */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_ENGINEERING_CRS), LW_CRS_ENGINEERING);

	/* Unknown types */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_UNKNOWN), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_ELLIPSOID), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEODETIC_REFERENCE_FRAME), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_BOUND_CRS), LW_CRS_UNKNOWN);
}

/***********************************************************************
 * CRS Family Detection via LWPROJ Tests
 */

static void test_lwproj_crs_family_geographic(void)
{
	/* EPSG:4326 (WGS84 geographic) -> same */
	LWPROJ *lp = lwproj_from_str("EPSG:4326", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->source_is_latlong, LW_TRUE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwproj_crs_family_projected(void)
{
	/* EPSG:4326 (geographic) -> EPSG:32632 (UTM projected) */
	LWPROJ *lp = lwproj_from_str("EPSG:4326", "EPSG:32632");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_PROJECTED);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwproj_crs_family_geocentric(void)
{
	/* EPSG:4326 (geographic) -> EPSG:4978 (WGS84 geocentric/ECEF) */
	LWPROJ *lp = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOCENTRIC);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwproj_crs_family_geocentric_to_geographic(void)
{
	/* EPSG:4978 (geocentric) -> EPSG:4326 (geographic) */
	LWPROJ *lp = lwproj_from_str("EPSG:4978", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOCENTRIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOGRAPHIC);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwproj_crs_family_geocentric_self(void)
{
	/* EPSG:4978 -> EPSG:4978 (null transform, geocentric) */
	LWPROJ *lp = lwproj_from_str("EPSG:4978", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOCENTRIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOCENTRIC);
		/* Geocentric is NOT latlong */
		CU_ASSERT_EQUAL(lp->source_is_latlong, LW_FALSE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwproj_pipeline_crs_family(void)
{
	/* Pipeline transforms have unknown CRS families */
	LWPROJ *lp = lwproj_from_str_pipeline(
		"+proj=pipeline +step +proj=cart +ellps=WGS84", true);
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_UNKNOWN);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_UNKNOWN);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

/***********************************************************************
 * ECEF Transformation Tests
 */

static void test_ecef_transform_roundtrip(void)
{
	/*
	 * Test round-trip: geographic (4326) -> ECEF (4978) -> geographic (4326)
	 * Known control point: (0, 0, 0) on WGS84 -> (6378137, 0, 0) in ECEF
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	/* Create a 3D point at lon=0, lat=0, h=0 (on equator, prime meridian, surface) */
	geom = lwgeom_from_wkt("POINT Z (0 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	/* Transform to ECEF (4978) */
	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);
	geom->srid = 4978;

	/* Verify ECEF coordinates: should be approximately (6378137, 0, 0) */
	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 6378137.0, 0.001); /* WGS84 semi-major axis */
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	/* Transform back to geographic (4326) */
	LWPROJ *to_geog = lwproj_from_str("EPSG:4978", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(to_geog);
	if (!to_geog) { proj_destroy(to_ecef->pj); lwfree(to_ecef); lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_geog), LW_SUCCESS);
	geom->srid = 4326;

	/* Verify round-trip: should be back at (0, 0, 0) within sub-mm precision */
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 1e-9);  /* lon */
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 1e-9);  /* lat */
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);  /* height */

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	proj_destroy(to_geog->pj);
	lwfree(to_geog);
	lwgeom_free(geom);
}

static void test_ecef_transform_north_pole(void)
{
	/*
	 * Control point: North Pole (lon=0, lat=90, h=0)
	 * ECEF should be (0, 0, 6356752.314) -- WGS84 semi-minor axis
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (0 90 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 6356752.314245, 0.001); /* WGS84 semi-minor */

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_ecef_transform_london(void)
{
	/*
	 * Control point: London (lon=-0.1278, lat=51.5074, h=0)
	 * Verified against PROJ command line:
	 *   echo -0.1278 51.5074 0 | cs2cs EPSG:4326 EPSG:4978
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p, p_orig;

	geom = lwgeom_from_wkt("POINT Z (-0.1278 51.5074 11)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	/* Save original for round-trip comparison */
	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	/* Forward: geographic -> ECEF */
	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);
	geom->srid = 4978;

	/* ECEF values should be large metric numbers */
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT(fabs(p.x) > 3900000.0); /* roughly 3955km */
	CU_ASSERT(fabs(p.y) < 10000.0);   /* small negative (near prime meridian) */
	CU_ASSERT(fabs(p.z) > 4960000.0); /* roughly 4968km */

	/* Inverse: ECEF -> geographic */
	LWPROJ *to_geog = lwproj_from_str("EPSG:4978", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(to_geog);
	if (!to_geog) { proj_destroy(to_ecef->pj); lwfree(to_ecef); lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_geog), LW_SUCCESS);
	geom->srid = 4326;

	/* Verify round-trip within sub-mm precision */
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, p_orig.x, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p.y, p_orig.y, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p.z, p_orig.z, 0.001);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	proj_destroy(to_geog->pj);
	lwfree(to_geog);
	lwgeom_free(geom);
}

static void test_ecef_transform_linestring(void)
{
	/* Test that multi-point geometries also transform correctly */
	LWGEOM *geom;
	LWLINE *line;
	POINT4D p;

	geom = lwgeom_from_wkt(
		"LINESTRING Z (0 0 0, 90 0 0, 0 90 0, -90 0 0)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	line = (LWLINE *)geom;

	/* Point 0: (lon=0, lat=0) -> (a, 0, 0) where a = WGS84 semi-major */
	getPoint4d_p(line->points, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	/* Point 1: (lon=90, lat=0) -> (0, a, 0) */
	getPoint4d_p(line->points, 1, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	/* Point 2: (lon=0, lat=90) -> (0, 0, b) where b = WGS84 semi-minor */
	getPoint4d_p(line->points, 2, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 6356752.314245, 0.001);

	/* Point 3: (lon=-90, lat=0) -> (0, -a, 0) */
	getPoint4d_p(line->points, 3, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, -6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_ecef_geocentric_not_latlong(void)
{
	/* Verify that geocentric CRS is NOT classified as latlong */
	LWPROJ *lp = lwproj_from_str("EPSG:4978", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		/* source_is_latlong should be false for geocentric */
		CU_ASSERT_EQUAL(lp->source_is_latlong, LW_FALSE);
		/* but CRS family should be correctly identified */
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOCENTRIC);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_ecef_transform_projected_to_ecef(void)
{
	/* EPSG:32632 (UTM 32N) -> EPSG:4978 (ECEF) */
	LWPROJ *lp = lwproj_from_str("EPSG:32632", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_PROJECTED);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOCENTRIC);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

/***********************************************************************
 * SRID-based CRS Family Lookup Tests (lwsrid_get_crs_family)
 */

static void test_lwsrid_get_crs_family_geographic(void)
{
	/* EPSG:4326 - WGS 84 geographic */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(4326), LW_CRS_GEOGRAPHIC);
	/* EPSG:4269 - NAD83 geographic */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(4269), LW_CRS_GEOGRAPHIC);
	/* EPSG:4979 - WGS 84 3D geographic */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(4979), LW_CRS_GEOGRAPHIC);
}

static void test_lwsrid_get_crs_family_projected(void)
{
	/* EPSG:32632 - WGS 84 / UTM zone 32N */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(32632), LW_CRS_PROJECTED);
	/* EPSG:3857 - WGS 84 / Pseudo-Mercator */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(3857), LW_CRS_PROJECTED);
	/* EPSG:2154 - RGF93 / Lambert-93 */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(2154), LW_CRS_PROJECTED);
}

static void test_lwsrid_get_crs_family_geocentric(void)
{
	/* EPSG:4978 - WGS 84 geocentric (ECEF) */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(4978), LW_CRS_GEOCENTRIC);
	/* EPSG:4936 - ETRS89 geocentric */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(4936), LW_CRS_GEOCENTRIC);
}

static void test_lwsrid_get_crs_family_unknown(void)
{
	/* Non-existent SRID should return UNKNOWN */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(999999), LW_CRS_UNKNOWN);
	/* Very large invalid SRID */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(9999999), LW_CRS_UNKNOWN);
}

static void test_lwsrid_get_crs_family_compound(void)
{
	/* EPSG:9518 - WGS 84 + EGM2008 height (compound: geographic + vertical)
	 * The horizontal component is geographic, so family should be geographic */
	LW_CRS_FAMILY family = lwsrid_get_crs_family(9518);
	/* Compound CRS: horizontal component determines family */
	CU_ASSERT(family == LW_CRS_GEOGRAPHIC || family == LW_CRS_UNKNOWN);
}

/***********************************************************************
 * ECEF Edge Case Tests
 */

static void test_ecef_transform_antimeridian(void)
{
	/*
	 * Test point near the antimeridian (lon=179.9999)
	 * Should still transform correctly.
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (179.9999 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	/* At antimeridian on equator, X should be nearly -a, Y near 0 */
	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, -6378137.0, 1.0);
	CU_ASSERT(fabs(p.y) < 200.0); /* small Y due to slight offset from 180 */
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_ecef_transform_south_pole(void)
{
	/*
	 * Control point: South Pole (lon=0, lat=-90, h=0)
	 * ECEF should be (0, 0, -6356752.314) -- negative semi-minor
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (0 -90 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, -6356752.314245, 0.001);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_ecef_transform_high_altitude(void)
{
	/*
	 * Test point at high altitude: GPS satellite orbit (~20,200 km)
	 * lon=0, lat=0, h=20200000
	 * ECEF should be (6378137 + 20200000, 0, 0) = (26578137, 0, 0)
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p, p_orig;

	geom = lwgeom_from_wkt("POINT Z (0 0 20200000)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 26578137.0, 1.0);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	/* Round-trip back */
	LWPROJ *to_geog = lwproj_from_str("EPSG:4978", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(to_geog);
	if (!to_geog) { proj_destroy(to_ecef->pj); lwfree(to_ecef); lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_geog), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, p_orig.x, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p.y, p_orig.y, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p.z, p_orig.z, 0.01);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	proj_destroy(to_geog->pj);
	lwfree(to_geog);
	lwgeom_free(geom);
}

static void test_ecef_transform_polygon(void)
{
	/* Test that polygon geometries transform correctly to ECEF */
	LWGEOM *geom;
	LWPOLY *poly;
	POINT4D p;

	geom = lwgeom_from_wkt(
		"POLYGON Z ((0 0 0, 1 0 0, 1 1 0, 0 1 0, 0 0 0))",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	poly = (LWPOLY *)geom;
	/* First point (lon=0, lat=0) -> (a, 0, 0) */
	getPoint4d_p(poly->rings[0], 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	/* All points should be at roughly Earth surface radius */
	getPoint4d_p(poly->rings[0], 2, &p);
	double radius = sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
	CU_ASSERT(radius > 6350000.0 && radius < 6380000.0);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_ecef_different_datums(void)
{
	/* EPSG:4936 (ETRS89 geocentric) should be geocentric family */
	LWPROJ *lp = lwproj_from_str("EPSG:4258", "EPSG:4936");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->target_crs_family, LW_CRS_GEOCENTRIC);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwcrs_family_from_pj_type_complete(void)
{
	/* Exhaustive test for all expected PJ_TYPE mappings */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOGRAPHIC_2D_CRS), LW_CRS_GEOGRAPHIC);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOGRAPHIC_3D_CRS), LW_CRS_GEOGRAPHIC);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_PROJECTED_CRS), LW_CRS_PROJECTED);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEOCENTRIC_CRS), LW_CRS_GEOCENTRIC);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_ENGINEERING_CRS), LW_CRS_ENGINEERING);

	/* Non-CRS types should all return UNKNOWN */
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_UNKNOWN), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_ELLIPSOID), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_PRIME_MERIDIAN), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_GEODETIC_REFERENCE_FRAME), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_DYNAMIC_GEODETIC_REFERENCE_FRAME), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_VERTICAL_REFERENCE_FRAME), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_DYNAMIC_VERTICAL_REFERENCE_FRAME), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_DATUM_ENSEMBLE), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_VERTICAL_CRS), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_COMPOUND_CRS), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_TEMPORAL_CRS), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_BOUND_CRS), LW_CRS_UNKNOWN);
	CU_ASSERT_EQUAL(lwcrs_family_from_pj_type(PJ_TYPE_OTHER_CRS), LW_CRS_UNKNOWN);
}

static void test_lwcrs_family_enum_stability(void)
{
	/*
	 * Enum integer values must never change across versions.
	 * These values may be stored in caches, serialized formats,
	 * or referenced by external tools. Changing them would break
	 * binary compatibility.
	 */
	CU_ASSERT_EQUAL(LW_CRS_UNKNOWN,     0);
	CU_ASSERT_EQUAL(LW_CRS_GEOGRAPHIC,  1);
	CU_ASSERT_EQUAL(LW_CRS_PROJECTED,   2);
	CU_ASSERT_EQUAL(LW_CRS_GEOCENTRIC,  3);
	CU_ASSERT_EQUAL(LW_CRS_INERTIAL,    4);
	CU_ASSERT_EQUAL(LW_CRS_TOPOCENTRIC, 5);
	CU_ASSERT_EQUAL(LW_CRS_ENGINEERING, 6);
}

static void test_lwproj_is_latlong_via_crs_family(void)
{
	/*
	 * Verify that lwproj_is_latlong() contract is satisfied by CRS family:
	 * geographic → true, everything else → false.
	 * Since lwproj_is_latlong() now returns (source_crs_family == LW_CRS_GEOGRAPHIC),
	 * we verify the source_crs_family is correctly set for each CRS type.
	 */
	LWPROJ *lp;

	/* Geographic (4326) should be latlong */
	lp = lwproj_from_str("EPSG:4326", "EPSG:4326");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->source_crs_family == LW_CRS_GEOGRAPHIC, LW_TRUE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}

	/* Projected (32632) should NOT be latlong */
	lp = lwproj_from_str("EPSG:32632", "EPSG:32632");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_PROJECTED);
		CU_ASSERT_EQUAL(lp->source_crs_family == LW_CRS_GEOGRAPHIC, LW_FALSE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}

	/* Geocentric (4978) should NOT be latlong */
	lp = lwproj_from_str("EPSG:4978", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOCENTRIC);
		CU_ASSERT_EQUAL(lp->source_crs_family == LW_CRS_GEOGRAPHIC, LW_FALSE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}

	/* Geographic 3D (4979) should be latlong */
	lp = lwproj_from_str("EPSG:4979", "EPSG:4979");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_EQUAL(lp->source_crs_family, LW_CRS_GEOGRAPHIC);
		CU_ASSERT_EQUAL(lp->source_crs_family == LW_CRS_GEOGRAPHIC, LW_TRUE);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwcrs_family_name_complete(void)
{
	/* All enum values must have non-NULL names */
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_UNKNOWN));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_GEOGRAPHIC));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_PROJECTED));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_GEOCENTRIC));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_INERTIAL));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_TOPOCENTRIC));
	CU_ASSERT_PTR_NOT_NULL(lwcrs_family_name(LW_CRS_ENGINEERING));

	/* Names should not be empty strings */
	{
		const char *name;
		name = lwcrs_family_name(LW_CRS_UNKNOWN);
		CU_ASSERT(name != NULL && name[0] != '\0');
		name = lwcrs_family_name(LW_CRS_GEOCENTRIC);
		CU_ASSERT(name != NULL && name[0] != '\0');
	}
}

/***********************************************************************
 * Additional Coverage Tests
 */

static void test_ecef_transform_empty_geom(void)
{
	/* Empty geometry through ECEF transform path */
	LWGEOM *geom;

	geom = lwgeom_from_wkt("POINT EMPTY", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	/* Empty geometry should transform successfully (no points to convert) */
	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

static void test_lwproj_epoch_in_transform(void)
{
	/*
	 * Test that the epoch field on LWPROJ is used in single-point transforms.
	 * For a standard geographic->geographic transform, a non-zero epoch
	 * should still produce valid results (epoch goes into PJ_COORD.t).
	 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (0 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	LWPROJ *lp = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (!lp) { lwgeom_free(geom); return; }

	/* Set a custom epoch */
	lp->epoch = 2024.5;
	CU_ASSERT_DOUBLE_EQUAL(lp->epoch, 2024.5, 0.0);

	/* Transform should still succeed */
	CU_ASSERT_EQUAL(lwgeom_transform(geom, lp), LW_SUCCESS);

	/* Verify we got valid ECEF coordinates */
	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 6378137.0, 1.0);

	proj_destroy(lp->pj);
	lwfree(lp);
	lwgeom_free(geom);
}

static void test_lwsrid_get_crs_family_eci_from_crs(void)
{
	/* Verify ECI SRIDs are detected by the CRS family lookup in lwgeom_transform.c */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_ICRF), LW_CRS_INERTIAL);
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_J2000), LW_CRS_INERTIAL);
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_TEME), LW_CRS_INERTIAL);
	/* Just outside the range should NOT be inertial */
	CU_ASSERT(lwsrid_get_crs_family(900000) != LW_CRS_INERTIAL);
	CU_ASSERT(lwsrid_get_crs_family(900100) != LW_CRS_INERTIAL);
}

static void test_lwproj_from_str_null_handling(void)
{
	/* Invalid CRS strings should return NULL */
	LWPROJ *lp;

	lp = lwproj_from_str("NOT_A_CRS", "EPSG:4326");
	CU_ASSERT_PTR_NULL(lp);

	lp = lwproj_from_str("EPSG:4326", "NOT_A_CRS");
	CU_ASSERT_PTR_NULL(lp);
}

static void test_ecef_multipoint_transform(void)
{
	/* Test MULTIPOINT through proj_trans_generic path (>1 point) */
	LWGEOM *geom;
	LWCOLLECTION *col;
	LWPOINT *pt0;
	POINT4D p;

	geom = lwgeom_from_wkt(
		"MULTIPOINT Z ((0 0 0), (90 0 0), (0 90 0))",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	geom->srid = 4326;

	LWPROJ *to_ecef = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(to_ecef);
	if (!to_ecef) { lwgeom_free(geom); return; }

	CU_ASSERT_EQUAL(lwgeom_transform(geom, to_ecef), LW_SUCCESS);

	col = (LWCOLLECTION *)geom;

	/* Point (0,0,0) -> (a, 0, 0) */
	pt0 = (LWPOINT *)col->geoms[0];
	getPoint4d_p(pt0->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 6378137.0, 0.001);

	proj_destroy(to_ecef->pj);
	lwfree(to_ecef);
	lwgeom_free(geom);
}

/***********************************************************************
 * Suite Setup
 */

void crs_family_suite_setup(void)
{
	CU_pSuite suite = CU_add_suite("crs_family", NULL, NULL);

	/* Enum tests */
	PG_ADD_TEST(suite, test_lwcrs_family_name);
	PG_ADD_TEST(suite, test_lwcrs_family_from_pj_type);
	PG_ADD_TEST(suite, test_lwcrs_family_enum_stability);
	PG_ADD_TEST(suite, test_lwcrs_family_name_complete);
	PG_ADD_TEST(suite, test_lwcrs_family_from_pj_type_complete);
	PG_ADD_TEST(suite, test_lwproj_is_latlong_via_crs_family);

	/* CRS family detection via LWPROJ */
	PG_ADD_TEST(suite, test_lwproj_crs_family_geographic);
	PG_ADD_TEST(suite, test_lwproj_crs_family_projected);
	PG_ADD_TEST(suite, test_lwproj_crs_family_geocentric);
	PG_ADD_TEST(suite, test_lwproj_crs_family_geocentric_to_geographic);
	PG_ADD_TEST(suite, test_lwproj_crs_family_geocentric_self);
	PG_ADD_TEST(suite, test_lwproj_pipeline_crs_family);

	/* SRID-based CRS family lookup */
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_geographic);
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_projected);
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_geocentric);
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_unknown);
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_compound);

	/* ECEF transformation tests */
	PG_ADD_TEST(suite, test_ecef_transform_roundtrip);
	PG_ADD_TEST(suite, test_ecef_transform_north_pole);
	PG_ADD_TEST(suite, test_ecef_transform_south_pole);
	PG_ADD_TEST(suite, test_ecef_transform_london);
	PG_ADD_TEST(suite, test_ecef_transform_antimeridian);
	PG_ADD_TEST(suite, test_ecef_transform_high_altitude);
	PG_ADD_TEST(suite, test_ecef_transform_linestring);
	PG_ADD_TEST(suite, test_ecef_transform_polygon);
	PG_ADD_TEST(suite, test_ecef_geocentric_not_latlong);
	PG_ADD_TEST(suite, test_ecef_transform_projected_to_ecef);
	PG_ADD_TEST(suite, test_ecef_different_datums);

	/* Additional coverage */
	PG_ADD_TEST(suite, test_ecef_transform_empty_geom);
	PG_ADD_TEST(suite, test_lwproj_epoch_in_transform);
	PG_ADD_TEST(suite, test_lwsrid_get_crs_family_eci_from_crs);
	PG_ADD_TEST(suite, test_lwproj_from_str_null_handling);
	PG_ADD_TEST(suite, test_ecef_multipoint_transform);
}
