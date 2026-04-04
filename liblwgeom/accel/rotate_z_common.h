/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Shared scalar helpers for Z-axis rotation tail processing.
 * Included by both NEON and AVX2 implementations to avoid duplication.
 *
 **********************************************************************/

#ifndef ROTATE_Z_COMMON_H
#define ROTATE_Z_COMMON_H

#include "../lwgeom_accel.h"
#include "../lwgeom_log.h"
#include <math.h>

/**
 * Scalar tail loop for uniform-epoch Z-rotation.
 *
 * Processes points from index @start up to @npoints using precomputed
 * cos/sin values. Used after the SIMD-width main loop.
 */
static inline void
rotate_z_scalar_tail(double *pts, uint32_t start, uint32_t npoints,
		     size_t stride, double cos_t, double sin_t)
{
	uint32_t i;
	for (i = start; i < npoints; i++)
	{
		double *p = pts + i * stride;
		double x = p[0];
		double y = p[1];
		p[0] = x * cos_t + y * sin_t;
		p[1] = -x * sin_t + y * cos_t;
	}
}

/**
 * Scalar tail loop for per-point M-epoch Z-rotation.
 *
 * Processes points from index @start up to @npoints, computing ERA
 * per point from the M coordinate. Returns LW_FAILURE on invalid epoch.
 */
static inline int
rotate_z_m_epoch_scalar_tail(double *pts, uint32_t start, uint32_t npoints,
			     size_t stride, size_t m_offset, int direction)
{
	uint32_t i;
	for (i = start; i < npoints; i++)
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

#endif /* ROTATE_Z_COMMON_H */
