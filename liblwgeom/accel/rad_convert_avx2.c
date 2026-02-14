/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * AVX2 accelerated radian/degree conversion for ptarray_transform().
 * Compiled with -mavx2 -mfma flags.
 *
 **********************************************************************/

#include "../lwgeom_accel.h"
#include <immintrin.h>

/**
 * AVX2 batch multiply x,y by a scale factor (e.g., M_PI/180 or 180/M_PI).
 *
 * Operates directly on POINTARRAY serialized_pointlist, processing
 * 4 points at a time. Only x and y are scaled; z and m are untouched.
 */
void
ptarray_rad_convert_avx2(POINTARRAY *pa, double scale)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;

	__m256d scale_v = _mm256_set1_pd(scale);

	/* Process 4 points at a time */
	uint32_t simd_end = npoints & ~3u;
	for (i = 0; i < simd_end; i += 4)
	{
		double *p0 = pts + i * stride;

		/* Gather x values */
		__m256d x = _mm256_set_pd(
			p0[3 * stride], p0[2 * stride], p0[stride], p0[0]);
		/* Gather y values */
		__m256d y = _mm256_set_pd(
			p0[3 * stride + 1], p0[2 * stride + 1], p0[stride + 1], p0[1]);

		/* Scale */
		x = _mm256_mul_pd(x, scale_v);
		y = _mm256_mul_pd(y, scale_v);

		/* Scatter */
		double xr[4], yr[4];
		_mm256_storeu_pd(xr, x);
		_mm256_storeu_pd(yr, y);

		p0[0]              = xr[0]; p0[1]              = yr[0];
		p0[stride]         = xr[1]; p0[stride + 1]     = yr[1];
		p0[2 * stride]     = xr[2]; p0[2 * stride + 1] = yr[2];
		p0[3 * stride]     = xr[3]; p0[3 * stride + 1] = yr[3];
	}

	/* Scalar tail */
	for (; i < npoints; i++)
	{
		double *p = pts + i * stride;
		p[0] *= scale;
		p[1] *= scale;
	}
}
