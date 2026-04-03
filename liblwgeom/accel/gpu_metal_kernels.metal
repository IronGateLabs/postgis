/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Metal Shading Language compute kernels for batch coordinate
 * transforms on Apple Silicon GPUs.
 *
 **********************************************************************/

#include <metal_stdlib>
using namespace metal;

/*
 * Parameter structs passed as constant buffers to each kernel.
 */

struct RotateZParams
{
	uint  stride_doubles;
	uint  npoints;
	double cos_t;
	double sin_t;
};

struct RotateZMEpochParams
{
	uint stride_doubles;
	uint npoints;
	uint m_offset;
	int  direction;
};

struct RadConvertParams
{
	uint  stride_doubles;
	uint  npoints;
	double scale;
};

/*
 * Inline helper: convert decimal-year epoch to Julian Date.
 * Formula must match gpu_cuda.cu for numerical consistency.
 */
static inline double
gpu_epoch_to_jd(double epoch)
{
	return 2451545.0 + (epoch - 2000.0) * 365.25;
}

/*
 * Inline helper: compute Earth Rotation Angle from Julian Date.
 * Formula must match gpu_cuda.cu for numerical consistency.
 */
static inline double
gpu_earth_rotation_angle(double jd)
{
	double Du = jd - 2451545.0;
	return 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);
}

/**
 * Uniform-angle Z-rotation kernel.
 * Each thread rotates one point by the same angle (cos_t, sin_t).
 */
kernel void rotate_z_uniform(device double       *data    [[buffer(0)]],
                             constant RotateZParams &params [[buffer(1)]],
                             uint id [[thread_position_in_grid]])
{
	/* TODO: implement uniform Z-rotation */
	if (id >= params.npoints)
		return;

	uint base = id * params.stride_doubles;
	double x = data[base];
	double y = data[base + 1];

	data[base]     = x * params.cos_t - y * params.sin_t;
	data[base + 1] = x * params.sin_t + y * params.cos_t;
}

/**
 * Per-point M-epoch Z-rotation kernel.
 * Each thread reads its epoch from the M coordinate, computes the
 * Earth Rotation Angle, and applies Z-rotation.
 */
kernel void rotate_z_m_epoch(device double              *data   [[buffer(0)]],
                             constant RotateZMEpochParams &params [[buffer(1)]],
                             uint id [[thread_position_in_grid]])
{
	/* TODO: implement per-epoch Z-rotation */
	if (id >= params.npoints)
		return;

	uint base = id * params.stride_doubles;
	double epoch = data[base + params.m_offset];
	double jd = gpu_epoch_to_jd(epoch);
	double era = gpu_earth_rotation_angle(jd);
	double theta = era * params.direction;

	double cos_t = cos(theta);
	double sin_t = sin(theta);

	double x = data[base];
	double y = data[base + 1];

	data[base]     = x * cos_t - y * sin_t;
	data[base + 1] = x * sin_t + y * cos_t;
}

/**
 * Bulk radian/degree conversion kernel.
 * Each thread multiplies x and y by the scale factor.
 */
kernel void rad_convert(device double          *data   [[buffer(0)]],
                        constant RadConvertParams &params [[buffer(1)]],
                        uint id [[thread_position_in_grid]])
{
	/* TODO: implement radian conversion */
	if (id >= params.npoints)
		return;

	uint base = id * params.stride_doubles;
	data[base]     *= params.scale;
	data[base + 1] *= params.scale;
}
