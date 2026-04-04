/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Metal Shading Language compute kernels for batch coordinate
 * transforms on Apple Silicon GPUs.
 *
 * NOTE: Metal does not support double precision. All computation
 * uses float (32-bit). The host side (gpu_metal.m) converts
 * double -> float before dispatch and float -> double after.
 * This trades precision (~7 decimal digits vs ~15) for GPU
 * acceleration. For coordinate transforms the precision loss
 * is sub-millimeter at Earth scale.
 *
 **********************************************************************/

#include <metal_stdlib>
using namespace metal;

/*
 * Parameter structs passed as constant buffers to each kernel.
 */

struct RotateZParams
{
	uint  stride;
	uint  npoints;
	float cos_t;
	float sin_t;
};

struct RotateZMEpochParams
{
	uint stride;
	uint npoints;
	uint m_offset;
	int  direction;
};

struct RadConvertParams
{
	uint  stride;
	uint  npoints;
	float scale;
};

/*
 * Inline helper: convert decimal-year epoch to Julian Date.
 */
static inline float
gpu_epoch_to_jd(float epoch)
{
	return 2451545.0f + (epoch - 2000.0f) * 365.25f;
}

/*
 * Inline helper: compute Earth Rotation Angle from Julian Date.
 */
static inline float
gpu_earth_rotation_angle(float jd)
{
	float Du = jd - 2451545.0f;
	return 2.0f * M_PI_F * (0.7790572732640f + 1.00273781191135448f * Du);
}

/**
 * Uniform-angle Z-rotation kernel.
 * Each thread rotates one point by the same angle (cos_t, sin_t).
 */
kernel void rotate_z_uniform(device float          *data    [[buffer(0)]],
                             constant RotateZParams &params  [[buffer(1)]],
                             uint id [[thread_position_in_grid]])
{
	if (id >= params.npoints)
		return;

	uint base = id * params.stride;
	float x = data[base];
	float y = data[base + 1];

	data[base]     = x * params.cos_t - y * params.sin_t;
	data[base + 1] = x * params.sin_t + y * params.cos_t;
}

/**
 * Per-point M-epoch Z-rotation kernel.
 * Each thread reads its epoch from the M coordinate, computes the
 * Earth Rotation Angle, and applies Z-rotation.
 */
kernel void rotate_z_m_epoch(device float                *data   [[buffer(0)]],
                             constant RotateZMEpochParams &params [[buffer(1)]],
                             uint id [[thread_position_in_grid]])
{
	if (id >= params.npoints)
		return;

	uint base = id * params.stride;
	float epoch = data[base + params.m_offset];
	float jd = gpu_epoch_to_jd(epoch);
	float era = gpu_earth_rotation_angle(jd);
	float theta = era * float(params.direction);

	float cos_t = cos(theta);
	float sin_t = sin(theta);

	float x = data[base];
	float y = data[base + 1];

	data[base]     = x * cos_t - y * sin_t;
	data[base + 1] = x * sin_t + y * cos_t;
}

/**
 * Bulk radian/degree conversion kernel.
 * Each thread multiplies x and y coordinates by the scale factor.
 */
kernel void rad_convert(device float             *data   [[buffer(0)]],
                        constant RadConvertParams &params [[buffer(1)]],
                        uint id [[thread_position_in_grid]])
{
	if (id >= params.npoints)
		return;

	uint base = id * params.stride;
	data[base]     *= params.scale;
	data[base + 1] *= params.scale;
}
