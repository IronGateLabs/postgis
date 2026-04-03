/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * CUnit tests for Apple Metal GPU compute backend.
 * All tests are guarded by HAVE_METAL; when Metal is not compiled in,
 * the tests trivially pass so that the suite is still registered.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "CUnit/Basic.h"

#include "liblwgeom_internal.h"
#include "../lwgeom_accel.h"
#include "../lwgeom_gpu.h"
#include "cu_tester.h"

/**
 * Helper: create a POINTARRAY with n 4D (XYZM) points on a circle.
 * X,Y in ECEF-like range, Z small, M = epoch value.
 */
static POINTARRAY *
make_test_pa(uint32_t n)
{
	POINTARRAY *pa;
	uint32_t i;

	pa = ptarray_construct(1, 1, n); /* has_z=1, has_m=1 */
	for (i = 0; i < n; i++)
	{
		POINT4D p;
		double angle = (double)i / (n > 1 ? n : 1) * 2.0 * M_PI;
		p.x = 6378137.0 * cos(angle);
		p.y = 6378137.0 * sin(angle);
		p.z = 1000.0 * (i % 100);
		p.m = 2025.0 + (double)i / (n > 1 ? n : 1);
		ptarray_set_point4d(pa, i, &p);
	}
	return pa;
}

/**
 * Helper: deep-copy a POINTARRAY.
 */
static POINTARRAY *
pa_copy(const POINTARRAY *src)
{
	POINTARRAY *dst = ptarray_construct(
		FLAGS_GET_Z(src->flags), FLAGS_GET_M(src->flags), src->npoints);
	memcpy(dst->serialized_pointlist, src->serialized_pointlist,
	       src->npoints * ptarray_point_size(src));
	return dst;
}

/**
 * Helper: compute maximum per-component difference between two POINTARRAYs.
 */
static double
pa_max_diff(const POINTARRAY *a, const POINTARRAY *b)
{
	uint32_t i;
	double max_d = 0.0;

	for (i = 0; i < a->npoints && i < b->npoints; i++)
	{
		POINT4D pa_pt, pb_pt;
		double dx, dy, dz;

		getPoint4d_p(a, i, &pa_pt);
		getPoint4d_p(b, i, &pb_pt);

		dx = fabs(pa_pt.x - pb_pt.x);
		dy = fabs(pa_pt.y - pb_pt.y);
		dz = fabs(pa_pt.z - pb_pt.z);
		if (dx > max_d) max_d = dx;
		if (dy > max_d) max_d = dy;
		if (dz > max_d) max_d = dz;
	}
	return max_d;
}

/***********************************************************************
 * test_metal_init
 *
 * Verify lwgpu_metal_init() returns success on macOS with Metal,
 * or gracefully returns 0 on non-Metal systems.
 */
static void test_metal_init(void)
{
#ifdef HAVE_METAL
	int rc = lwgpu_metal_init();
	/*
	 * On a Mac with Metal GPU, init should succeed.
	 * On CI without GPU, init returns 0 -- that is also acceptable.
	 * We just verify it does not crash.
	 */
	CU_ASSERT(rc == 0 || rc == 1);

	if (rc)
	{
		/* If init succeeded, the device name should be non-empty */
		const char *name = lwgpu_metal_device_name();
		CU_ASSERT_PTR_NOT_NULL(name);
		CU_ASSERT(strlen(name) > 0);
	}
#else
	/* Metal not compiled in -- trivially pass */
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * test_metal_rotate_z_uniform
 *
 * Create a 4D POINTARRAY with known points, apply Z-rotation via
 * Metal, verify results match scalar computation within 1e-10.
 */
static void test_metal_rotate_z_uniform(void)
{
#ifdef HAVE_METAL
	POINTARRAY *base, *scalar_pa, *metal_pa;
	double theta = 1.23456;
	double diff;
	int rc;

	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}

	base = make_test_pa(1000);
	scalar_pa = pa_copy(base);
	metal_pa = pa_copy(base);

	/* Scalar reference */
	ptarray_rotate_z_scalar(scalar_pa, theta);

	/* Metal GPU */
	rc = lwgpu_metal_rotate_z(
		(double *)metal_pa->serialized_pointlist,
		ptarray_point_size(metal_pa),
		metal_pa->npoints,
		theta);
	CU_ASSERT_EQUAL(rc, 1);

	/* Compare */
	diff = pa_max_diff(scalar_pa, metal_pa);
	if (diff >= 1e-10)
		fprintf(stderr, "Metal rotate_z max_diff = %.2e (expected < 1e-10)\n", diff);
	CU_ASSERT(diff < 1e-10);

	ptarray_free(base);
	ptarray_free(scalar_pa);
	ptarray_free(metal_pa);
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * test_metal_rotate_z_m_epoch
 *
 * Create 4D POINTARRAY with M=epoch values, run per-point epoch
 * rotation, verify against scalar reference.
 */
static void test_metal_rotate_z_m_epoch(void)
{
#ifdef HAVE_METAL
	POINTARRAY *base, *scalar_pa, *metal_pa;
	int direction = -1; /* ECI -> ECEF */
	double diff;
	int rc;
	int has_z;
	size_t m_offset;

	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}

	base = make_test_pa(1000);
	scalar_pa = pa_copy(base);
	metal_pa = pa_copy(base);

	/* Scalar reference */
	ptarray_rotate_z_m_epoch_scalar(scalar_pa, direction);

	/* Metal GPU */
	has_z = FLAGS_GET_Z(metal_pa->flags);
	m_offset = has_z ? 3 : 2;
	rc = lwgpu_metal_rotate_z_m_epoch(
		(double *)metal_pa->serialized_pointlist,
		ptarray_point_size(metal_pa),
		metal_pa->npoints,
		m_offset,
		direction);
	CU_ASSERT_EQUAL(rc, 1);

	/* Compare */
	diff = pa_max_diff(scalar_pa, metal_pa);
	if (diff >= 1e-10)
		fprintf(stderr, "Metal rotate_z_m_epoch max_diff = %.2e (expected < 1e-10)\n", diff);
	CU_ASSERT(diff < 1e-10);

	ptarray_free(base);
	ptarray_free(scalar_pa);
	ptarray_free(metal_pa);
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * test_metal_fallback
 *
 * Verify that when Metal is not available or fails, the system falls
 * back to NEON/scalar without error.
 */
static void test_metal_fallback(void)
{
#ifdef HAVE_METAL
	POINTARRAY *base, *accel_pa, *scalar_pa;
	double theta = 0.5;
	double diff;
	const LW_ACCEL_DISPATCH *dispatch;

	base = make_test_pa(100);
	accel_pa = pa_copy(base);
	scalar_pa = pa_copy(base);

	/*
	 * Use the dispatch table -- this should work regardless of
	 * whether Metal is available, falling back to NEON or scalar.
	 */
	dispatch = lwaccel_get();
	CU_ASSERT_PTR_NOT_NULL(dispatch);

	dispatch->rotate_z(accel_pa, theta);
	ptarray_rotate_z_scalar(scalar_pa, theta);

	diff = pa_max_diff(accel_pa, scalar_pa);
	CU_ASSERT(diff < 1e-10);

	ptarray_free(base);
	ptarray_free(accel_pa);
	ptarray_free(scalar_pa);
#else
	/*
	 * Without HAVE_METAL, the dispatch table uses NEON or scalar.
	 * Verify that still works.
	 */
	POINTARRAY *base, *accel_pa, *scalar_pa;
	double theta = 0.5;
	double diff;
	const LW_ACCEL_DISPATCH *dispatch;

	base = make_test_pa(100);
	accel_pa = pa_copy(base);
	scalar_pa = pa_copy(base);

	dispatch = lwaccel_get();
	CU_ASSERT_PTR_NOT_NULL(dispatch);

	dispatch->rotate_z(accel_pa, theta);
	ptarray_rotate_z_scalar(scalar_pa, theta);

	diff = pa_max_diff(accel_pa, scalar_pa);
	CU_ASSERT(diff < 1e-10);

	ptarray_free(base);
	ptarray_free(accel_pa);
	ptarray_free(scalar_pa);
#endif
}

/***********************************************************************
 * test_metal_small_array
 *
 * Test with very small arrays (1, 2, 3 points) to verify edge cases.
 */
static void test_metal_small_array(void)
{
#ifdef HAVE_METAL
	uint32_t sizes[] = {1, 2, 3};
	int si;
	double theta = 0.789;

	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}

	for (si = 0; si < 3; si++)
	{
		POINTARRAY *base = make_test_pa(sizes[si]);
		POINTARRAY *scalar_pa = pa_copy(base);
		POINTARRAY *metal_pa = pa_copy(base);
		double diff;
		int rc;

		ptarray_rotate_z_scalar(scalar_pa, theta);

		rc = lwgpu_metal_rotate_z(
			(double *)metal_pa->serialized_pointlist,
			ptarray_point_size(metal_pa),
			metal_pa->npoints,
			theta);
		CU_ASSERT_EQUAL(rc, 1);

		diff = pa_max_diff(scalar_pa, metal_pa);
		if (diff >= 1e-10)
			fprintf(stderr, "Metal small_array(%u) max_diff = %.2e\n",
				sizes[si], diff);
		CU_ASSERT(diff < 1e-10);

		ptarray_free(base);
		ptarray_free(scalar_pa);
		ptarray_free(metal_pa);
	}
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * test_metal_large_array
 *
 * Test with 100K points to verify no memory issues.
 */
static void test_metal_large_array(void)
{
#ifdef HAVE_METAL
	POINTARRAY *base, *scalar_pa, *metal_pa;
	double theta = 2.718;
	double diff;
	int rc;

	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}

	base = make_test_pa(100000);
	scalar_pa = pa_copy(base);
	metal_pa = pa_copy(base);

	/* Scalar reference */
	ptarray_rotate_z_scalar(scalar_pa, theta);

	/* Metal GPU */
	rc = lwgpu_metal_rotate_z(
		(double *)metal_pa->serialized_pointlist,
		ptarray_point_size(metal_pa),
		metal_pa->npoints,
		theta);
	CU_ASSERT_EQUAL(rc, 1);

	/* Compare */
	diff = pa_max_diff(scalar_pa, metal_pa);
	if (diff >= 1e-10)
		fprintf(stderr, "Metal large_array(100000) max_diff = %.2e\n", diff);
	CU_ASSERT(diff < 1e-10);

	ptarray_free(base);
	ptarray_free(scalar_pa);
	ptarray_free(metal_pa);
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * Suite registration
 */
void metal_suite_setup(void)
{
	CU_pSuite suite = CU_add_suite("metal", NULL, NULL);

	PG_ADD_TEST(suite, test_metal_init);
	PG_ADD_TEST(suite, test_metal_rotate_z_uniform);
	PG_ADD_TEST(suite, test_metal_rotate_z_m_epoch);
	PG_ADD_TEST(suite, test_metal_fallback);
	PG_ADD_TEST(suite, test_metal_small_array);
	PG_ADD_TEST(suite, test_metal_large_array);
}
