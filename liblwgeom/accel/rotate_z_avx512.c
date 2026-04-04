/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * AVX-512 accelerated Z-axis rotation for ECI/ECEF transforms.
 * Compiled with -mavx512f flag.
 *
 **********************************************************************/

#include "../lwgeom_accel.h"
#include <math.h>
#include <immintrin.h>

/**
 * AVX-512 uniform-epoch Z-rotation (8-wide).
 *
 * Processes 8 points at a time using 512-bit registers.
 * Same gather/scatter pattern as AVX2 but with 8 lanes.
 */
int
ptarray_rotate_z_avx512(POINTARRAY *pa, double theta)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;
	double cos_t = cos(theta);
	double sin_t = sin(theta);

	__m512d cos_v = _mm512_set1_pd(cos_t);
	__m512d sin_v = _mm512_set1_pd(sin_t);
	__m512d neg_sin_v = _mm512_set1_pd(-sin_t);

	/* Process 8 points at a time */
	uint32_t simd_end = npoints & ~7u;
	for (i = 0; i < simd_end; i += 8)
	{
		double *p0 = pts + i * stride;

		/* Gather 8 x values and 8 y values from strided layout */
		__m512d x = _mm512_set_pd(
			p0[7 * stride], p0[6 * stride], p0[5 * stride], p0[4 * stride],
			p0[3 * stride], p0[2 * stride], p0[stride], p0[0]);
		__m512d y = _mm512_set_pd(
			p0[7 * stride + 1], p0[6 * stride + 1], p0[5 * stride + 1], p0[4 * stride + 1],
			p0[3 * stride + 1], p0[2 * stride + 1], p0[stride + 1], p0[1]);

		/* Rotation: x' via cos/sin, y' via negated sin/cos */
		__m512d x_new = _mm512_fmadd_pd(x, cos_v, _mm512_mul_pd(y, sin_v));
		__m512d y_new = _mm512_fmadd_pd(x, neg_sin_v, _mm512_mul_pd(y, cos_v));

		/* Scatter back */
		double xr[8];
		double yr[8];
		_mm512_storeu_pd(xr, x_new);
		_mm512_storeu_pd(yr, y_new);

		int j;
		for (j = 0; j < 8; j++)
		{
			p0[j * stride]     = xr[j];
			p0[j * stride + 1] = yr[j];
		}
	}

	/* Scalar tail for remaining points */
	for (; i < npoints; i++)
	{
		double *p = pts + i * stride;
		double x = p[0];
		double y = p[1];
		p[0] = x * cos_t + y * sin_t;
		p[1] = -x * sin_t + y * cos_t;
	}

	return LW_SUCCESS;
}
