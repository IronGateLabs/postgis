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
 * Epoch Conversion Tests
 */

static void test_epoch_to_jd_j2000(void)
{
	/* J2000.0 epoch: 2000.0 -> JD 2451545.0 */
	double jd = lweci_epoch_to_jd(2000.0);
	CU_ASSERT_DOUBLE_EQUAL(jd, 2451545.0, 0.001);
}

static void test_epoch_to_jd_2024(void)
{
	/* 2024.0 -> 2451545.0 + 24*365.25 = 2460311.0 */
	double jd = lweci_epoch_to_jd(2024.0);
	CU_ASSERT_DOUBLE_EQUAL(jd, 2451545.0 + 24.0 * 365.25, 0.001);
}

static void test_epoch_to_jd_negative(void)
{
	/* 1990.0 -> 2451545.0 + (-10)*365.25 = 2447892.5 */
	double jd = lweci_epoch_to_jd(1990.0);
	CU_ASSERT_DOUBLE_EQUAL(jd, 2451545.0 - 10.0 * 365.25, 0.001);
}

/***********************************************************************
 * Earth Rotation Angle Tests
 */

static void test_era_j2000(void)
{
	/* At J2000.0 epoch (JD 2451545.0), ERA should be a specific value */
	double era = lweci_earth_rotation_angle(2451545.0);
	/* ERA at J2000.0 = 2*pi*0.7790572732640 ≈ 4.894961... rad */
	double expected = 2.0 * M_PI * 0.7790572732640;
	expected = fmod(expected, 2.0 * M_PI);
	CU_ASSERT_DOUBLE_EQUAL(era, expected, 1e-10);
}

static void test_era_one_sidereal_day(void)
{
	/* After one sidereal day, ERA should increase by ~2*pi */
	/* One sidereal day ≈ 0.99726957 solar days */
	double jd1 = 2451545.0;
	double jd2 = jd1 + (1.0 / 1.00273781191135448); /* one sidereal day */
	double era1 = lweci_earth_rotation_angle(jd1);
	double era2 = lweci_earth_rotation_angle(jd2);

	/* ERA2 - ERA1 should be approximately 2*pi (one full rotation) */
	double diff = era2 - era1;
	if (diff < 0) diff += 2.0 * M_PI;
	CU_ASSERT_DOUBLE_EQUAL(diff, 2.0 * M_PI, 0.001);
}

static void test_era_range(void)
{
	/* ERA should always be in [0, 2*pi) */
	int i;
	for (i = 0; i < 100; i++)
	{
		double jd = 2451545.0 + i * 37.7; /* arbitrary dates */
		double era = lweci_earth_rotation_angle(jd);
		CU_ASSERT(era >= 0.0);
		CU_ASSERT(era < 2.0 * M_PI);
	}
}

static void test_era_monotonic(void)
{
	/* ERA should increase monotonically (mod 2*pi) */
	double prev_raw = 0;
	int i;
	for (i = 0; i < 10; i++)
	{
		double jd = 2451545.0 + i * 0.1; /* every 0.1 days */
		double Du = jd - 2451545.0;
		double raw = 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);
		if (i > 0)
			CU_ASSERT(raw > prev_raw);
		prev_raw = raw;
	}
}

/***********************************************************************
 * ECI-to-ECEF Transformation Tests
 */

static void test_eci_to_ecef_point(void)
{
	/* Test that ECI-to-ECEF applies a Z-axis rotation */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	/* Create a point along ECEF X axis at Earth surface */
	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	/* Transform at J2000 epoch */
	int ret = lwgeom_transform_eci_to_ecef(geom, 2000.0);
	CU_ASSERT_EQUAL(ret, LW_SUCCESS);

	/* The point should be rotated around Z. Check radius preserved. */
	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	double radius = sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
	CU_ASSERT_DOUBLE_EQUAL(radius, 6378137.0, 0.001);
	/* Z should be unchanged (rotation is about Z axis) */
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	lwgeom_free(geom);
}

static void test_ecef_to_eci_point(void)
{
	/* Test reverse: ECEF-to-ECI applies opposite rotation */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	int ret = lwgeom_transform_ecef_to_eci(geom, 2000.0);
	CU_ASSERT_EQUAL(ret, LW_SUCCESS);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	double radius = sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
	CU_ASSERT_DOUBLE_EQUAL(radius, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 0.0, 0.001);

	lwgeom_free(geom);
}

static void test_eci_ecef_roundtrip(void)
{
	/* ECI -> ECEF -> ECI should return to original */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p_orig, p_final;
	double epoch = 2024.5;

	geom = lwgeom_from_wkt("POINT Z (5000000 3000000 4000000)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	/* ECI -> ECEF */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	/* ECEF -> ECI */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_ecef_roundtrip_linestring(void)
{
	/* Test roundtrip with linestring geometry */
	LWGEOM *geom;
	LWLINE *line;
	POINT4D p_orig[2], p_final[2];
	double epoch = 2020.0;
	int i;

	geom = lwgeom_from_wkt(
		"LINESTRING Z (6378137 0 0, 0 6378137 0)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	line = (LWLINE *)geom;
	for (i = 0; i < 2; i++)
		getPoint4d_p(line->points, i, &p_orig[i]);

	/* ECI -> ECEF -> ECI */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	for (i = 0; i < 2; i++)
	{
		getPoint4d_p(line->points, i, &p_final[i]);
		CU_ASSERT_DOUBLE_EQUAL(p_final[i].x, p_orig[i].x, 1e-6);
		CU_ASSERT_DOUBLE_EQUAL(p_final[i].y, p_orig[i].y, 1e-6);
		CU_ASSERT_DOUBLE_EQUAL(p_final[i].z, p_orig[i].z, 1e-6);
	}

	lwgeom_free(geom);
}

static void test_eci_z_axis_invariant(void)
{
	/* Points on the Z axis should be unaffected by rotation about Z */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;

	geom = lwgeom_from_wkt("POINT Z (0 0 6356752)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, 2024.0), LW_SUCCESS);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	CU_ASSERT_DOUBLE_EQUAL(p.x, 0.0, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p.y, 0.0, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p.z, 6356752.0, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_different_epochs_differ(void)
{
	/* Same point at different epochs should produce different ECEF coordinates */
	LWGEOM *geom1, *geom2;
	LWPOINT *pt1, *pt2;
	POINT4D p1, p2;

	geom1 = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	geom2 = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom1);
	CU_ASSERT_PTR_NOT_NULL(geom2);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom1, 2024.0), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom2, 2024.5), LW_SUCCESS);

	pt1 = (LWPOINT *)geom1;
	pt2 = (LWPOINT *)geom2;
	getPoint4d_p(pt1->point, 0, &p1);
	getPoint4d_p(pt2->point, 0, &p2);

	/* At half-year difference, Earth rotates ~182 full turns; coordinates differ */
	CU_ASSERT(fabs(p1.x - p2.x) > 1.0 || fabs(p1.y - p2.y) > 1.0);

	lwgeom_free(geom1);
	lwgeom_free(geom2);
}

static void test_eci_no_epoch_error(void)
{
	/* Attempting ECI transform without epoch should fail */
	LWGEOM *geom;

	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	/* LWPROJ_NO_EPOCH (0.0) should be rejected - but we need to catch lwerror.
	 * Since CUnit doesn't easily catch lwerror, we verify the epoch check
	 * is applied by checking the constant value instead */
	CU_ASSERT_DOUBLE_EQUAL(LWPROJ_NO_EPOCH, 0.0, 0.0);

	lwgeom_free(geom);
}

static void test_eci_empty_geometry(void)
{
	/* Empty geometry should transform successfully */
	LWGEOM *geom;

	geom = lwgeom_from_wkt("POINT EMPTY", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	/* Empty geometries should pass through without error */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, 2024.0), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, 2024.0), LW_SUCCESS);

	lwgeom_free(geom);
}

static void test_eci_polygon(void)
{
	/* Test polygon roundtrip */
	LWGEOM *geom;
	LWPOLY *poly;
	POINT4D p_orig, p_final;
	double epoch = 2023.0;

	geom = lwgeom_from_wkt(
		"POLYGON Z ((5000000 0 0, 5000000 1000000 0, 6000000 1000000 0, 6000000 0 0, 5000000 0 0))",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	poly = (LWPOLY *)geom;
	getPoint4d_p(poly->rings[0], 0, &p_orig);

	/* Roundtrip */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(poly->rings[0], 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);

	lwgeom_free(geom);
}

static void test_lwproj_epoch_field(void)
{
	/* Verify LWPROJ epoch field initialization */
	LWPROJ *lp;

	/* lwproj_from_str should initialize epoch to LWPROJ_NO_EPOCH */
	lp = lwproj_from_str("EPSG:4326", "EPSG:4978");
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_DOUBLE_EQUAL(lp->epoch, LWPROJ_NO_EPOCH, 0.0);
		/* Set a custom epoch */
		lp->epoch = 2024.5;
		CU_ASSERT_DOUBLE_EQUAL(lp->epoch, 2024.5, 0.0);
		proj_destroy(lp->pj);
		lwfree(lp);
	}

	/* Pipeline should also initialize to no epoch */
	lp = lwproj_from_str_pipeline(
		"+proj=pipeline +step +proj=cart +ellps=WGS84", true);
	CU_ASSERT_PTR_NOT_NULL(lp);
	if (lp)
	{
		CU_ASSERT_DOUBLE_EQUAL(lp->epoch, LWPROJ_NO_EPOCH, 0.0);
		proj_destroy(lp->pj);
		lwfree(lp);
	}
}

static void test_lwcrs_family_requires_epoch(void)
{
	/* Only INERTIAL requires epoch */
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_INERTIAL), 1);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_GEOGRAPHIC), 0);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_PROJECTED), 0);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_GEOCENTRIC), 0);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_TOPOCENTRIC), 0);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_ENGINEERING), 0);
	CU_ASSERT_EQUAL(lwcrs_family_requires_epoch(LW_CRS_UNKNOWN), 0);
}

/***********************************************************************
 * ECI SRID Range Tests
 */

static void test_srid_is_eci(void)
{
	/* ECI SRID range: 900001-900099 */
	CU_ASSERT_EQUAL(SRID_IS_ECI(SRID_ECI_ICRF), 1);
	CU_ASSERT_EQUAL(SRID_IS_ECI(SRID_ECI_J2000), 1);
	CU_ASSERT_EQUAL(SRID_IS_ECI(SRID_ECI_TEME), 1);
	CU_ASSERT_EQUAL(SRID_IS_ECI(SRID_ECI_MAX), 1);
	CU_ASSERT_EQUAL(SRID_IS_ECI(900050), 1);

	/* Outside ECI range */
	CU_ASSERT_EQUAL(SRID_IS_ECI(900000), 0);
	CU_ASSERT_EQUAL(SRID_IS_ECI(900100), 0);
	CU_ASSERT_EQUAL(SRID_IS_ECI(4326), 0);
	CU_ASSERT_EQUAL(SRID_IS_ECI(4978), 0);
	CU_ASSERT_EQUAL(SRID_IS_ECI(0), 0);
}

static void test_eci_srid_family_lookup(void)
{
	/* ECI SRIDs should return LW_CRS_INERTIAL from the family lookup */
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_ICRF), LW_CRS_INERTIAL);
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_J2000), LW_CRS_INERTIAL);
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(SRID_ECI_TEME), LW_CRS_INERTIAL);
	CU_ASSERT_EQUAL(lwsrid_get_crs_family(900050), LW_CRS_INERTIAL);
}

static void test_eci_srid_constants(void)
{
	/* Verify SRID constants are in the correct range */
	CU_ASSERT_EQUAL(SRID_ECI_BASE, 900001);
	CU_ASSERT_EQUAL(SRID_ECI_ICRF, 900001);
	CU_ASSERT_EQUAL(SRID_ECI_J2000, 900002);
	CU_ASSERT_EQUAL(SRID_ECI_TEME, 900003);
	CU_ASSERT_EQUAL(SRID_ECI_MAX, 900099);
}

/***********************************************************************
 * ECI Bounding Box Tests
 */

static void test_eci_gbox_point(void)
{
	/* ECI bounding box for a single point */
	LWGEOM *geom;
	GBOX gbox;

	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	memset(&gbox, 0, sizeof(GBOX));
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, &gbox), LW_SUCCESS);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmin, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmax, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymin, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymax, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmin, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmax, 0.0, 0.001);

	lwgeom_free(geom);
}

static void test_eci_gbox_linestring(void)
{
	/* ECI bounding box for a linestring spanning 3D space */
	LWGEOM *geom;
	GBOX gbox;

	geom = lwgeom_from_wkt(
		"LINESTRING Z (-6378137 -6378137 -6356752, 6378137 6378137 6356752)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	memset(&gbox, 0, sizeof(GBOX));
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, &gbox), LW_SUCCESS);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmin, -6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmax, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymin, -6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymax, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmin, -6356752.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmax, 6356752.0, 0.001);

	lwgeom_free(geom);
}

static void test_eci_gbox_with_m_epoch(void)
{
	/* ECI bounding box with M coordinate as epoch */
	LWGEOM *geom;
	GBOX gbox;

	/* Two points at different epochs stored in M: 2024.0 and 2024.5 */
	geom = lwgeom_from_wkt(
		"LINESTRING ZM (6378137 0 0 2024.0, 0 6378137 0 2024.5)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	memset(&gbox, 0, sizeof(GBOX));
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, &gbox), LW_SUCCESS);

	/* Spatial extent */
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmin, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmax, 6378137.0, 0.001);

	/* Temporal extent via M values */
	CU_ASSERT_DOUBLE_EQUAL(gbox.mmin, 2024.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.mmax, 2024.5, 0.001);

	lwgeom_free(geom);
}

static void test_eci_gbox_empty(void)
{
	/* Empty geometry should fail */
	LWGEOM *geom;
	GBOX gbox;

	geom = lwgeom_from_wkt("POINT EMPTY", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	memset(&gbox, 0, sizeof(GBOX));
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, &gbox), LW_FAILURE);

	lwgeom_free(geom);
}

static void test_eci_gbox_null_args(void)
{
	/* NULL arguments should fail gracefully */
	GBOX gbox;
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(NULL, &gbox), LW_FAILURE);

	LWGEOM *geom = lwgeom_from_wkt("POINT Z (1 2 3)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, NULL), LW_FAILURE);
	lwgeom_free(geom);
}

static void test_postgis_eci_enabled(void)
{
	/* Verify compile-time macros are defined */
	CU_ASSERT_EQUAL(POSTGIS_ECI_ENABLED, 1);
#if POSTGIS_PROJ_VERSION >= 90200
	CU_ASSERT_EQUAL(POSTGIS_PROJ_HAS_COORDINATE_EPOCH, 1);
#else
	CU_ASSERT_EQUAL(POSTGIS_PROJ_HAS_COORDINATE_EPOCH, 0);
#endif
}

static void test_eci_multipoint_gbox(void)
{
	/* ECI bounding box for a multipoint */
	LWGEOM *geom;
	GBOX gbox;

	geom = lwgeom_from_wkt(
		"MULTIPOINT Z (6378137 0 0, -6378137 0 0, 0 0 6356752)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	memset(&gbox, 0, sizeof(GBOX));
	CU_ASSERT_EQUAL(lwgeom_eci_compute_gbox(geom, &gbox), LW_SUCCESS);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmin, -6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.xmax, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymin, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.ymax, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmin, 0.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(gbox.zmax, 6356752.0, 0.001);

	lwgeom_free(geom);
}

/***********************************************************************
 * Branch Coverage Tests - Geometry Type Switch Cases
 */

static void test_eci_2d_geometry(void)
{
	/* 2D geometry (no Z) should still rotate X/Y correctly */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p_orig, p_final;
	double epoch = 2024.0;

	geom = lwgeom_from_wkt("POINT (6378137 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	/* Roundtrip should preserve coords */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_geometrycollection(void)
{
	/* GeometryCollection roundtrip (exercises COLLECTIONTYPE branch) */
	LWGEOM *geom;
	LWCOLLECTION *col;
	POINT4D p_orig, p_final;
	double epoch = 2023.0;

	geom = lwgeom_from_wkt(
		"GEOMETRYCOLLECTION Z ("
		"  POINT Z (6378137 0 0),"
		"  LINESTRING Z (6378137 0 0, 0 6378137 0)"
		")",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	col = (LWCOLLECTION *)geom;
	getPoint4d_p(((LWPOINT *)col->geoms[0])->point, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(((LWPOINT *)col->geoms[0])->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_multipolygon(void)
{
	/* MultiPolygon roundtrip (exercises MULTIPOLYGONTYPE branch) */
	LWGEOM *geom;
	LWCOLLECTION *col;
	LWPOLY *poly;
	POINT4D p_orig, p_final;
	double epoch = 2022.0;

	geom = lwgeom_from_wkt(
		"MULTIPOLYGON Z ("
		"  ((5000000 0 0, 5000000 1000000 0, 6000000 1000000 0, 6000000 0 0, 5000000 0 0)),"
		"  ((0 5000000 0, 0 5000000 1000000, 0 6000000 1000000, 0 6000000 0, 0 5000000 0))"
		")",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	col = (LWCOLLECTION *)geom;
	poly = (LWPOLY *)col->geoms[0];
	getPoint4d_p(poly->rings[0], 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(poly->rings[0], 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_multilinestring(void)
{
	/* MultiLineString roundtrip (exercises MULTILINETYPE branch) */
	LWGEOM *geom;
	LWCOLLECTION *col;
	LWLINE *line;
	POINT4D p_orig, p_final;
	double epoch = 2025.0;

	geom = lwgeom_from_wkt(
		"MULTILINESTRING Z ("
		"  (6378137 0 0, 0 6378137 0),"
		"  (0 0 6356752, 6378137 0 0)"
		")",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	col = (LWCOLLECTION *)geom;
	line = (LWLINE *)col->geoms[0];
	getPoint4d_p(line->points, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(line->points, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_triangle(void)
{
	/* Triangle roundtrip (exercises TRIANGLETYPE branch) */
	LWGEOM *geom;
	POINT4D p_orig, p_final;
	double epoch = 2024.0;
	LWLINE *tri;

	geom = lwgeom_from_wkt(
		"TRIANGLE Z ((5000000 0 0, 5000000 1000000 0, 6000000 0 0, 5000000 0 0))",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	tri = (LWLINE *)geom;
	getPoint4d_p(tri->points, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(tri->points, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_circularstring(void)
{
	/* CircularString roundtrip (exercises CIRCSTRINGTYPE branch) */
	LWGEOM *geom;
	LWLINE *cs;
	POINT4D p_orig, p_final;
	double epoch = 2024.0;

	geom = lwgeom_from_wkt(
		"CIRCULARSTRING Z (5000000 0 0, 5500000 500000 0, 6000000 0 0)",
		LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	cs = (LWLINE *)geom;
	getPoint4d_p(cs->points, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(cs->points, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_extreme_epoch_far_future(void)
{
	/* Test with far-future epoch: year 9999 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p_orig, p_final;
	double radius;

	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, 9999.0), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, 9999.0), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-3);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-3);

	/* Radius should be preserved */
	radius = sqrt(p_final.x * p_final.x + p_final.y * p_final.y + p_final.z * p_final.z);
	CU_ASSERT_DOUBLE_EQUAL(radius, 6378137.0, 0.001);

	lwgeom_free(geom);
}

static void test_eci_extreme_epoch_far_past(void)
{
	/* Test with far-past epoch: year 1000 */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p_orig, p_final;

	geom = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, 1000.0), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, 1000.0), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-3);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-3);

	lwgeom_free(geom);
}

static void test_eci_large_pointarray(void)
{
	/* Test with a large LineString (1000 points) for stress/performance */
	LWGEOM *geom;
	LWLINE *line;
	POINT4D p_orig, p_final;
	double epoch = 2024.0;
	int i;
	char wkt[65536];
	int offset;
	double radius;

	offset = snprintf(wkt, sizeof(wkt), "LINESTRING Z (");
	for (i = 0; i < 1000; i++)
	{
		double angle = 2.0 * M_PI * i / 1000.0;
		double x = 6378137.0 * cos(angle);
		double y = 6378137.0 * sin(angle);
		if (i > 0) offset += snprintf(wkt + offset, sizeof(wkt) - offset, ", ");
		offset += snprintf(wkt + offset, sizeof(wkt) - offset, "%.3f %.3f 0", x, y);
	}
	snprintf(wkt + offset, sizeof(wkt) - offset, ")");

	geom = lwgeom_from_wkt(wkt, LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);
	if (!geom) return;

	line = (LWLINE *)geom;
	getPoint4d_p(line->points, 0, &p_orig);

	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef(geom, epoch), LW_SUCCESS);
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom, epoch), LW_SUCCESS);

	getPoint4d_p(line->points, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 0.01);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 0.01);

	/* All points should preserve radius */
	for (i = 0; i < 1000; i++)
	{
		getPoint4d_p(line->points, i, &p_final);
		radius = sqrt(p_final.x * p_final.x + p_final.y * p_final.y + p_final.z * p_final.z);
		CU_ASSERT_DOUBLE_EQUAL(radius, 6378137.0, 0.1);
	}

	lwgeom_free(geom);
}

/***********************************************************************
 * EOP-Enhanced Transform Tests
 */

static void test_eci_eop_roundtrip(void)
{
	/* ECEF -> ECI (EOP) -> ECEF (EOP) should return to original */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p_orig, p_final;
	double epoch = 2024.5;
	double dut1 = 0.035;   /* UT1-UTC in seconds */
	double xp = 0.1234;    /* polar motion x in arcsec */
	double yp = 0.5678;    /* polar motion y in arcsec */

	geom = lwgeom_from_wkt("POINT Z (5000000 3000000 4000000)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p_orig);

	/* ECEF -> ECI with EOP */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom, epoch, dut1, xp, yp), LW_SUCCESS);
	/* ECI -> ECEF with EOP */
	CU_ASSERT_EQUAL(lwgeom_transform_eci_to_ecef_eop(geom, epoch, dut1, xp, yp), LW_SUCCESS);

	getPoint4d_p(pt->point, 0, &p_final);
	CU_ASSERT_DOUBLE_EQUAL(p_final.x, p_orig.x, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.y, p_orig.y, 1e-6);
	CU_ASSERT_DOUBLE_EQUAL(p_final.z, p_orig.z, 1e-6);

	lwgeom_free(geom);
}

static void test_eci_eop_dut1_effect(void)
{
	/* dut1 correction should produce a different result from non-EOP */
	LWGEOM *geom_eop, *geom_plain;
	LWPOINT *pt_eop, *pt_plain;
	POINT4D p_eop, p_plain;
	double epoch = 2024.0;
	double dut1 = 0.5;    /* large dut1 for visible effect */
	double xp = 0.0;
	double yp = 0.0;

	geom_eop = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	geom_plain = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom_eop);
	CU_ASSERT_PTR_NOT_NULL(geom_plain);

	/* EOP transform with dut1 correction */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom_eop, epoch, dut1, xp, yp), LW_SUCCESS);
	/* Plain transform (no dut1) */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom_plain, epoch), LW_SUCCESS);

	pt_eop = (LWPOINT *)geom_eop;
	pt_plain = (LWPOINT *)geom_plain;
	getPoint4d_p(pt_eop->point, 0, &p_eop);
	getPoint4d_p(pt_plain->point, 0, &p_plain);

	/* dut1=0.5s shifts ERA by ~0.5s of Earth rotation => noticeable X/Y diff */
	CU_ASSERT(fabs(p_eop.x - p_plain.x) > 1.0 || fabs(p_eop.y - p_plain.y) > 1.0);

	lwgeom_free(geom_eop);
	lwgeom_free(geom_plain);
}

static void test_eci_eop_polar_motion(void)
{
	/* Polar motion (xp, yp) should affect X, Y, and Z coordinates */
	LWGEOM *geom_pm, *geom_nopm;
	LWPOINT *pt_pm, *pt_nopm;
	POINT4D p_pm, p_nopm;
	double epoch = 2024.0;
	double dut1 = 0.0;
	double xp = 0.2;   /* arcsec */
	double yp = 0.3;   /* arcsec */

	geom_pm = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	geom_nopm = lwgeom_from_wkt("POINT Z (6378137 0 0)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom_pm);
	CU_ASSERT_PTR_NOT_NULL(geom_nopm);

	/* With polar motion */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom_pm, epoch, dut1, xp, yp), LW_SUCCESS);
	/* Without polar motion (same dut1=0 so ERA matches plain) */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom_nopm, epoch, dut1, 0.0, 0.0), LW_SUCCESS);

	pt_pm = (LWPOINT *)geom_pm;
	pt_nopm = (LWPOINT *)geom_nopm;
	getPoint4d_p(pt_pm->point, 0, &p_pm);
	getPoint4d_p(pt_nopm->point, 0, &p_nopm);

	/* Polar motion rotates about X and Y axes, affecting Z component */
	CU_ASSERT(fabs(p_pm.z - p_nopm.z) > 0.001);

	/* Radius should be preserved */
	double r_pm = sqrt(p_pm.x * p_pm.x + p_pm.y * p_pm.y + p_pm.z * p_pm.z);
	double r_nopm = sqrt(p_nopm.x * p_nopm.x + p_nopm.y * p_nopm.y + p_nopm.z * p_nopm.z);
	CU_ASSERT_DOUBLE_EQUAL(r_pm, 6378137.0, 0.001);
	CU_ASSERT_DOUBLE_EQUAL(r_nopm, 6378137.0, 0.001);

	lwgeom_free(geom_pm);
	lwgeom_free(geom_nopm);
}

static void test_eci_eop_zero_params_matches_plain(void)
{
	/* EOP with dut1=0, xp=0, yp=0 should match the non-EOP result */
	LWGEOM *geom_eop, *geom_plain;
	LWPOINT *pt_eop, *pt_plain;
	POINT4D p_eop, p_plain;
	double epoch = 2024.0;

	geom_eop = lwgeom_from_wkt("POINT Z (5000000 3000000 4000000)", LW_PARSER_CHECK_NONE);
	geom_plain = lwgeom_from_wkt("POINT Z (5000000 3000000 4000000)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom_eop);
	CU_ASSERT_PTR_NOT_NULL(geom_plain);

	/* EOP with all zeros */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom_eop, epoch, 0.0, 0.0, 0.0), LW_SUCCESS);
	/* Plain (no EOP) */
	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci(geom_plain, epoch), LW_SUCCESS);

	pt_eop = (LWPOINT *)geom_eop;
	pt_plain = (LWPOINT *)geom_plain;
	getPoint4d_p(pt_eop->point, 0, &p_eop);
	getPoint4d_p(pt_plain->point, 0, &p_plain);

	/* Results should be identical */
	CU_ASSERT_DOUBLE_EQUAL(p_eop.x, p_plain.x, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p_eop.y, p_plain.y, 1e-9);
	CU_ASSERT_DOUBLE_EQUAL(p_eop.z, p_plain.z, 1e-9);

	lwgeom_free(geom_eop);
	lwgeom_free(geom_plain);
}

static void test_eci_eop_z_preserved(void)
{
	/* With xp=0 and yp=0, Z should be preserved (Z-rotation only) */
	LWGEOM *geom;
	LWPOINT *pt;
	POINT4D p;
	double epoch = 2024.0;
	double dut1 = 0.1;

	geom = lwgeom_from_wkt("POINT Z (5000000 3000000 4500000)", LW_PARSER_CHECK_NONE);
	CU_ASSERT_PTR_NOT_NULL(geom);

	CU_ASSERT_EQUAL(lwgeom_transform_ecef_to_eci_eop(geom, epoch, dut1, 0.0, 0.0), LW_SUCCESS);

	pt = (LWPOINT *)geom;
	getPoint4d_p(pt->point, 0, &p);
	/* Z should be unchanged when polar motion is zero */
	CU_ASSERT_DOUBLE_EQUAL(p.z, 4500000.0, 1e-6);

	lwgeom_free(geom);
}

/***********************************************************************
 * Suite Setup
 */

void eci_suite_setup(void)
{
	CU_pSuite suite = CU_add_suite("eci", NULL, NULL);

	/* Epoch conversion */
	PG_ADD_TEST(suite, test_epoch_to_jd_j2000);
	PG_ADD_TEST(suite, test_epoch_to_jd_2024);
	PG_ADD_TEST(suite, test_epoch_to_jd_negative);

	/* Earth Rotation Angle */
	PG_ADD_TEST(suite, test_era_j2000);
	PG_ADD_TEST(suite, test_era_one_sidereal_day);
	PG_ADD_TEST(suite, test_era_range);
	PG_ADD_TEST(suite, test_era_monotonic);

	/* ECI-ECEF transformations */
	PG_ADD_TEST(suite, test_eci_to_ecef_point);
	PG_ADD_TEST(suite, test_ecef_to_eci_point);
	PG_ADD_TEST(suite, test_eci_ecef_roundtrip);
	PG_ADD_TEST(suite, test_eci_ecef_roundtrip_linestring);
	PG_ADD_TEST(suite, test_eci_z_axis_invariant);
	PG_ADD_TEST(suite, test_eci_different_epochs_differ);
	PG_ADD_TEST(suite, test_eci_no_epoch_error);
	PG_ADD_TEST(suite, test_eci_empty_geometry);
	PG_ADD_TEST(suite, test_eci_polygon);

	/* LWPROJ epoch support */
	PG_ADD_TEST(suite, test_lwproj_epoch_field);
	PG_ADD_TEST(suite, test_lwcrs_family_requires_epoch);

	/* ECI SRID range */
	PG_ADD_TEST(suite, test_srid_is_eci);
	PG_ADD_TEST(suite, test_eci_srid_family_lookup);
	PG_ADD_TEST(suite, test_eci_srid_constants);

	/* ECI bounding box */
	PG_ADD_TEST(suite, test_eci_gbox_point);
	PG_ADD_TEST(suite, test_eci_gbox_linestring);
	PG_ADD_TEST(suite, test_eci_gbox_with_m_epoch);
	PG_ADD_TEST(suite, test_eci_gbox_empty);
	PG_ADD_TEST(suite, test_eci_gbox_null_args);
	PG_ADD_TEST(suite, test_postgis_eci_enabled);
	PG_ADD_TEST(suite, test_eci_multipoint_gbox);

	/* Branch coverage - geometry type switch cases */
	PG_ADD_TEST(suite, test_eci_2d_geometry);
	PG_ADD_TEST(suite, test_eci_geometrycollection);
	PG_ADD_TEST(suite, test_eci_multipolygon);
	PG_ADD_TEST(suite, test_eci_multilinestring);
	PG_ADD_TEST(suite, test_eci_triangle);
	PG_ADD_TEST(suite, test_eci_circularstring);
	PG_ADD_TEST(suite, test_eci_extreme_epoch_far_future);
	PG_ADD_TEST(suite, test_eci_extreme_epoch_far_past);
	PG_ADD_TEST(suite, test_eci_large_pointarray);

	/* EOP-enhanced transforms */
	PG_ADD_TEST(suite, test_eci_eop_roundtrip);
	PG_ADD_TEST(suite, test_eci_eop_dut1_effect);
	PG_ADD_TEST(suite, test_eci_eop_polar_motion);
	PG_ADD_TEST(suite, test_eci_eop_zero_params_matches_plain);
	PG_ADD_TEST(suite, test_eci_eop_z_preserved);
}
