/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * ARM NEON accelerated Z-axis rotation for ECI/ECEF transforms.
 * Available on all ARMv8+ (aarch64) processors.
 *
 **********************************************************************/

#include "../lwgeom_accel.h"
#include "../lwgeom_log.h"
#include <math.h>
#include <arm_neon.h>

/**
 * NEON uniform-epoch Z-rotation (2-wide).
 *
 * Processes 2 points at a time using 128-bit NEON registers (float64x2_t).
 */
int
ptarray_rotate_z_neon(POINTARRAY *pa, double theta)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;
	double cos_t = cos(theta);
	double sin_t = sin(theta);

	float64x2_t cos_v = vdupq_n_f64(cos_t);
	float64x2_t sin_v = vdupq_n_f64(sin_t);
	float64x2_t neg_sin_v = vdupq_n_f64(-sin_t);

	/* Process 2 points at a time */
	uint32_t simd_end = npoints & ~1u;
	for (i = 0; i < simd_end; i += 2)
	{
		double *p0 = pts + i * stride;

		/* Gather 2 x values and 2 y values */
		double xv[2] = { p0[0], p0[stride] };
		double yv[2] = { p0[1], p0[stride + 1] };

		float64x2_t x = vld1q_f64(xv);
		float64x2_t y = vld1q_f64(yv);

		/* x_new = x * cos + y * sin */
		float64x2_t x_new = vfmaq_f64(vmulq_f64(y, sin_v), x, cos_v);
		/* y_new = x * (-sin) + y * cos */
		float64x2_t y_new = vfmaq_f64(vmulq_f64(y, cos_v), x, neg_sin_v);

		double xr[2], yr[2];
		vst1q_f64(xr, x_new);
		vst1q_f64(yr, y_new);

		p0[0]          = xr[0]; p0[1]          = yr[0];
		p0[stride]     = xr[1]; p0[stride + 1] = yr[1];
	}

	/* Scalar tail for odd point */
	if (i < npoints)
	{
		double *p = pts + i * stride;
		double x = p[0], y = p[1];
		p[0] = x * cos_t + y * sin_t;
		p[1] = -x * sin_t + y * cos_t;
	}

	return LW_SUCCESS;
}

/**
 * NEON per-point M-epoch Z-rotation.
 */
int
ptarray_rotate_z_m_epoch_neon(POINTARRAY *pa, int direction)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;
	int has_z = FLAGS_GET_Z(pa->flags);
	int has_m = FLAGS_GET_M(pa->flags);
	size_t m_offset;

	if (!has_m)
	{
		lwerror("ECI M-epoch transform requires M coordinate");
		return LW_FAILURE;
	}

	m_offset = has_z ? 3 : 2;

	/* Process 2 points at a time */
	uint32_t simd_end = npoints & ~1u;
	for (i = 0; i < simd_end; i += 2)
	{
		double *p0 = pts + i * stride;
		double cos_v[2], sin_v[2];
		int j;

		for (j = 0; j < 2; j++)
		{
			double *p = p0 + j * stride;
			double epoch = p[m_offset];
			double jd, era, th;

			if (epoch < 1000.0 || epoch > 3000.0)
			{
				lwerror("ECI transform: point %u has invalid epoch M=%.4f "
					"(expected decimal year in range 1000-3000)", i + j, epoch);
				return LW_FAILURE;
			}

			jd = lweci_epoch_to_jd(epoch);
			era = lweci_earth_rotation_angle(jd);
			th = direction * era;
			cos_v[j] = cos(th);
			sin_v[j] = sin(th);
		}

		double xv[2] = { p0[0], p0[stride] };
		double yv[2] = { p0[1], p0[stride + 1] };

		float64x2_t x = vld1q_f64(xv);
		float64x2_t y = vld1q_f64(yv);
		float64x2_t cv = vld1q_f64(cos_v);
		float64x2_t sv = vld1q_f64(sin_v);
		float64x2_t neg_sv = vnegq_f64(sv);

		float64x2_t x_new = vfmaq_f64(vmulq_f64(y, sv), x, cv);
		float64x2_t y_new = vfmaq_f64(vmulq_f64(y, cv), x, neg_sv);

		double xr[2], yr[2];
		vst1q_f64(xr, x_new);
		vst1q_f64(yr, y_new);

		p0[0]          = xr[0]; p0[1]          = yr[0];
		p0[stride]     = xr[1]; p0[stride + 1] = yr[1];
	}

	/* Scalar tail */
	if (i < npoints)
	{
		double *p = pts + i * stride;
		double epoch = p[m_offset];
		double jd, era, theta, cos_t, sin_t, x, y;

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
