/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Shared test/benchmark POINTARRAY helpers.
 * Used by bench_metal.c, bench_accel.c, and cunit/cu_metal.c.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU General Public Licence. See the COPYING file.
 *
 **********************************************************************/

#ifndef BENCH_HELPERS_H
#define BENCH_HELPERS_H

#include "../liblwgeom.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/**
 * Create a POINTARRAY with n random 4D points (XYZM).
 * X,Y in typical ECEF range (~6M meters), Z small, M = epoch.
 */
static inline POINTARRAY *
bench_make_test_pa(uint32_t n)
{
	POINTARRAY *pa;
	uint32_t i;
	uint32_t safe_n = (n > 1) ? n : 1;

	pa = ptarray_construct(1, 1, n); /* has_z=1, has_m=1 */

	for (i = 0; i < n; i++)
	{
		POINT4D p;
		double angle = (double)i / safe_n * 2.0 * M_PI;
		p.x = 6378137.0 * cos(angle);
		p.y = 6378137.0 * sin(angle);
		p.z = 1000.0 * (i % 100);
		p.m = 2025.0 + (double)i / safe_n; /* Epoch range 2025-2026 */
		ptarray_set_point4d(pa, i, &p);
	}

	return pa;
}

/**
 * Deep-copy a POINTARRAY.
 */
static inline POINTARRAY *
bench_pa_copy(const POINTARRAY *src)
{
	POINTARRAY *dst = ptarray_construct(FLAGS_GET_Z(src->flags), FLAGS_GET_M(src->flags), src->npoints);
	memcpy(dst->serialized_pointlist, src->serialized_pointlist, src->npoints * ptarray_point_size(src));
	return dst;
}

/**
 * Compute maximum per-component difference between two POINTARRAYs.
 */
static inline double
bench_pa_max_diff(const POINTARRAY *a, const POINTARRAY *b)
{
	uint32_t i;
	double max_d = 0.0;

	for (i = 0; i < a->npoints && i < b->npoints; i++)
	{
		POINT4D pa_pt;
		POINT4D pb_pt;
		double dx;
		double dy;
		double dz;

		getPoint4d_p(a, i, &pa_pt);
		getPoint4d_p(b, i, &pb_pt);

		dx = fabs(pa_pt.x - pb_pt.x);
		dy = fabs(pa_pt.y - pb_pt.y);
		dz = fabs(pa_pt.z - pb_pt.z);
		if (dx > max_d)
			max_d = dx;
		if (dy > max_d)
			max_d = dy;
		if (dz > max_d)
			max_d = dz;
	}
	return max_d;
}

#endif /* BENCH_HELPERS_H */
