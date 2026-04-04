/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * AVX2+FMA accelerated Z-axis rotation for ECI/ECEF transforms.
 * Compiled with -mavx2 -mfma flags.
 *
 **********************************************************************/

#include "../lwgeom_accel.h"
#include "../lwgeom_log.h"
#include <math.h>
#include <immintrin.h>

/**
 * AVX2 uniform-epoch Z-rotation.
 *
 * All points are rotated by the same angle theta. The sin/cos are
 * computed once and broadcast to SIMD registers. The rotation matrix
 * multiply is done with FMA instructions.
 *
 * Memory layout: POINTARRAY stores points as interleaved doubles.
 * For 2D: [x0,y0, x1,y1, ...]     stride = 2 doubles
 * For 3D: [x0,y0,z0, x1,y1,z1, ...] stride = 3 doubles
 * For 4D: [x0,y0,z0,m0, ...]        stride = 4 doubles
 *
 * We process 4 points at a time by gathering x and y values,
 * applying the rotation, and scattering back.
 */
int
ptarray_rotate_z_avx2(POINTARRAY *pa, double theta)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;
	double cos_t = cos(theta);
	double sin_t = sin(theta);

	/* Broadcast sin/cos to 256-bit registers */
	__m256d cos_v = _mm256_set1_pd(cos_t);
	__m256d sin_v = _mm256_set1_pd(sin_t);
	__m256d neg_sin_v = _mm256_set1_pd(-sin_t);

	/* Process 4 points at a time */
	uint32_t simd_end = npoints & ~3u; /* Round down to multiple of 4 */
	for (i = 0; i < simd_end; i += 4)
	{
		/* Gather 4 x values and 4 y values */
		double *p0 = pts + i * stride;
		__m256d x = _mm256_set_pd(
			p0[3 * stride], p0[2 * stride], p0[stride], p0[0]);
		__m256d y = _mm256_set_pd(
			p0[3 * stride + 1], p0[2 * stride + 1], p0[stride + 1], p0[1]);

		/* x_new = x * cos + y * sin */
		__m256d x_new = _mm256_fmadd_pd(x, cos_v, _mm256_mul_pd(y, sin_v));
		/* y_new = -x * sin + y * cos = x * (-sin) + y * cos */
		__m256d y_new = _mm256_fmadd_pd(x, neg_sin_v, _mm256_mul_pd(y, cos_v));

		/* Scatter results back to interleaved layout */
		double xr[4];
		double yr[4];
		_mm256_storeu_pd(xr, x_new);
		_mm256_storeu_pd(yr, y_new);

		p0[0]              = xr[0]; p0[1]              = yr[0];
		p0[stride]         = xr[1]; p0[stride + 1]     = yr[1];
		p0[2 * stride]     = xr[2]; p0[2 * stride + 1] = yr[2];
		p0[3 * stride]     = xr[3]; p0[3 * stride + 1] = yr[3];
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

/**
 * AVX2 per-point M-epoch Z-rotation.
 *
 * Each point has its own epoch in the M coordinate. We compute ERA
 * per point (trig is not vectorizable), then vectorize the multiply-add.
 * The SIMD benefit here is modest since sin/cos dominates, but the
 * multiply-add for 4 points in parallel still helps pipeline utilization.
 */
int
ptarray_rotate_z_m_epoch_avx2(POINTARRAY *pa, int direction)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;

	/* M coordinate offset depends on dimensionality */
	int has_z = FLAGS_GET_Z(pa->flags);
	int has_m = FLAGS_GET_M(pa->flags);
	size_t m_offset;

	if (!has_m)
	{
		lwerror("ECI M-epoch transform requires M coordinate");
		return LW_FAILURE;
	}

	m_offset = has_z ? 3 : 2; /* M is at index 3 for XYZM, index 2 for XYM */

	/* Process 4 points at a time: compute per-point ERA, then batch rotate */
	uint32_t simd_end = npoints & ~3u;
	for (i = 0; i < simd_end; i += 4)
	{
		double *p0 = pts + i * stride;
		double cos_v[4];
		double sin_v[4];
		int j;

		/* Per-point ERA computation (scalar - trig not vectorizable) */
		for (j = 0; j < 4; j++)
		{
			double *p = p0 + j * stride;
			double epoch = p[m_offset];
			double jd;
			double era;
			double theta;

			if (epoch < 1000.0 || epoch > 3000.0)
			{
				lwerror("ECI transform: point %u has invalid epoch M=%.4f "
					"(expected decimal year in range 1000-3000)", i + j, epoch);
				return LW_FAILURE;
			}

			jd = lweci_epoch_to_jd(epoch);
			era = lweci_earth_rotation_angle(jd);
			theta = direction * era;
			cos_v[j] = cos(theta);
			sin_v[j] = sin(theta);
		}

		/* Gather x, y */
		__m256d x = _mm256_set_pd(
			p0[3 * stride], p0[2 * stride], p0[stride], p0[0]);
		__m256d y = _mm256_set_pd(
			p0[3 * stride + 1], p0[2 * stride + 1], p0[stride + 1], p0[1]);

		/* Load per-point sin/cos */
		__m256d cv = _mm256_loadu_pd(cos_v);
		__m256d sv = _mm256_loadu_pd(sin_v);
		__m256d neg_sv = _mm256_sub_pd(_mm256_setzero_pd(), sv);

		/* Rotate */
		__m256d x_new = _mm256_fmadd_pd(x, cv, _mm256_mul_pd(y, sv));
		__m256d y_new = _mm256_fmadd_pd(x, neg_sv, _mm256_mul_pd(y, cv));

		/* Scatter */
		double xr[4];
		double yr[4];
		_mm256_storeu_pd(xr, x_new);
		_mm256_storeu_pd(yr, y_new);

		p0[0]              = xr[0]; p0[1]              = yr[0];
		p0[stride]         = xr[1]; p0[stride + 1]     = yr[1];
		p0[2 * stride]     = xr[2]; p0[2 * stride + 1] = yr[2];
		p0[3 * stride]     = xr[3]; p0[3 * stride + 1] = yr[3];
	}

	/* Scalar tail */
	for (; i < npoints; i++)
	{
		double *p = pts + i * stride;
		double epoch = p[m_offset];
		double jd;
		double era;
		double theta;
		double cos_t;
		double sin_t;
		double x;
		double y;

		if (epoch < 1000.0 || epoch > 3000.0)
		{
			lwerror("ECI transform: point %u has invalid epoch M=%.4f "
				"(expected decimal year in range 1000-3000)", i, epoch);
			return LW_FAILURE;
		}

		jd = lweci_epoch_to_jd(epoch);
		era = lweci_earth_rotation_angle(jd);
		theta = direction * era;
		cos_t = cos(theta);
		sin_t = sin(theta);

		x = p[0]; y = p[1];
		p[0] = x * cos_t + y * sin_t;
		p[1] = -x * sin_t + y * cos_t;
	}

	return LW_SUCCESS;
}
