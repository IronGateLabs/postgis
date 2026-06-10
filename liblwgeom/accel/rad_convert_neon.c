/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * ARM NEON accelerated radian/degree conversion for ptarray_transform().
 *
 **********************************************************************/

#include "../lwgeom_accel.h"
#include <arm_neon.h>

/**
 * NEON batch multiply x,y by a scale factor (2-wide).
 */
void
ptarray_rad_convert_neon(POINTARRAY *pa, double scale)
{
	uint32_t i;
	uint32_t npoints = pa->npoints;
	size_t stride = ptarray_point_size(pa) / sizeof(double);
	double *pts = (double *)pa->serialized_pointlist;

	float64x2_t scale_v = vdupq_n_f64(scale);

	uint32_t simd_end = npoints & ~1u;
	for (i = 0; i < simd_end; i += 2)
	{
		double *p0 = pts + i * stride;

		double xv[2] = { p0[0], p0[stride] };
		double yv[2] = { p0[1], p0[stride + 1] };

		float64x2_t x = vmulq_f64(vld1q_f64(xv), scale_v);
		float64x2_t y = vmulq_f64(vld1q_f64(yv), scale_v);

		double xr[2];
		double yr[2];
		vst1q_f64(xr, x);
		vst1q_f64(yr, y);

		p0[0]          = xr[0]; p0[1]          = yr[0];
		p0[stride]     = xr[1]; p0[stride + 1] = yr[1];
	}

	if (i < npoints)
	{
		double *p = pts + i * stride;
		p[0] *= scale;
		p[1] *= scale;
	}
}
