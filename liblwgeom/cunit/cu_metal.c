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
#include "../bench/bench_helpers.h"
#include "cu_tester.h"

/* Convenience aliases for shared helpers from bench_helpers.h */
#define make_test_pa bench_make_test_pa
#define pa_copy bench_pa_copy
#define pa_max_diff bench_pa_max_diff

/**
 * Shared helper: create a PA of given size, rotate via scalar and Metal,
 * assert results match within tolerance.
 */
#ifdef HAVE_METAL
static void
verify_metal_rotate_z(uint32_t npoints, double theta, double tolerance, const char *label)
{
	POINTARRAY *base;
	POINTARRAY *scalar_pa;
	POINTARRAY *metal_pa;
	double diff;
	int rc;

	base = make_test_pa(npoints);
	scalar_pa = pa_copy(base);
	metal_pa = pa_copy(base);

	/* Scalar reference */
	ptarray_rotate_z_scalar(scalar_pa, theta);

	/* Metal GPU */
	rc = lwgpu_metal_rotate_z(
	    (double *)metal_pa->serialized_pointlist, ptarray_point_size(metal_pa), metal_pa->npoints, theta);
	CU_ASSERT_EQUAL(rc, 1);

	/* Compare */
	diff = pa_max_diff(scalar_pa, metal_pa);
	if (diff >= tolerance)
		fprintf(stderr, "Metal %s(%u) max_diff = %.2e (expected < %.0e)\n", label, npoints, diff, tolerance);
	CU_ASSERT(diff < tolerance);

	ptarray_free(base);
	ptarray_free(scalar_pa);
	ptarray_free(metal_pa);
}
#endif

/***********************************************************************
 * test_metal_init
 *
 * Verify lwgpu_metal_init() returns success on macOS with Metal,
 * or gracefully returns 0 on non-Metal systems.
 */
static void
test_metal_init(void)
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
		if (name)
		{
			/*
			 * strlen is safe: lwgpu_metal_device_name() returns a
			 * pointer to metal_device_name[], a fixed-size static
			 * buffer that is always null-terminated.
			 */
			CU_ASSERT(strlen(name) > 0);
		}
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
static void
test_metal_rotate_z_uniform(void)
{
#ifdef HAVE_METAL
	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}
	verify_metal_rotate_z(1000, 1.23456, 1e-10, "rotate_z");
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
static void
test_metal_rotate_z_m_epoch(void)
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
	rc = lwgpu_metal_rotate_z_m_epoch((double *)metal_pa->serialized_pointlist,
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
static void
test_metal_fallback(void)
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

	if (!dispatch || !dispatch->rotate_z)
	{
		ptarray_rotate_z_scalar(accel_pa, theta);
		ptarray_rotate_z_scalar(scalar_pa, theta);
	}
	else
	{
		dispatch->rotate_z(accel_pa, theta);
		ptarray_rotate_z_scalar(scalar_pa, theta);
	}

	diff = pa_max_diff(accel_pa, scalar_pa);
	/*
	 * SIMD backends (NEON, AVX2) may use FMA instructions that produce
	 * results differing from scalar by a few ULPs. At Earth-scale
	 * coordinates (~6.4e6) one ULP is ~5e-10, so allow 1e-6 tolerance.
	 */
	if (diff >= 1e-6)
		fprintf(stderr, "Metal fallback max_diff = %.2e (expected < 1e-6)\n", diff);
	CU_ASSERT(diff < 1e-6);

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
	if (!dispatch || !dispatch->rotate_z)
	{
		/* Dispatch not available -- just verify scalar works */
		ptarray_rotate_z_scalar(accel_pa, theta);
		ptarray_rotate_z_scalar(scalar_pa, theta);
	}
	else
	{
		dispatch->rotate_z(accel_pa, theta);
		ptarray_rotate_z_scalar(scalar_pa, theta);
	}

	diff = pa_max_diff(accel_pa, scalar_pa);
	if (diff >= 1e-6)
		fprintf(stderr, "Metal fallback max_diff = %.2e (expected < 1e-6)\n", diff);
	CU_ASSERT(diff < 1e-6);

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
static void
test_metal_small_array(void)
{
#ifdef HAVE_METAL
	uint32_t sizes[] = {1, 2, 3};
	int si;

	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}

	for (si = 0; si < 3; si++)
		verify_metal_rotate_z(sizes[si], 0.789, 1e-10, "small_array");
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * test_metal_large_array
 *
 * Test with 100K points to verify no memory issues.
 */
static void
test_metal_large_array(void)
{
#ifdef HAVE_METAL
	if (!lwgpu_available())
	{
		CU_PASS("Metal GPU not available at runtime");
		return;
	}
	verify_metal_rotate_z(100000, 2.718, 1e-10, "large_array");
#else
	CU_PASS("Metal not available at compile time");
#endif
}

/***********************************************************************
 * Suite registration
 */
void
metal_suite_setup(void)
{
	CU_pSuite suite = CU_add_suite("metal", NULL, NULL);

	PG_ADD_TEST(suite, test_metal_init);
	PG_ADD_TEST(suite, test_metal_rotate_z_uniform);
	PG_ADD_TEST(suite, test_metal_rotate_z_m_epoch);
	PG_ADD_TEST(suite, test_metal_fallback);
	PG_ADD_TEST(suite, test_metal_small_array);
	PG_ADD_TEST(suite, test_metal_large_array);
}
