/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Metal Shading Language compute kernels for batch coordinate
 * transforms on Apple Silicon GPUs.
 *
 * PRECISION CONTRACT (FP32_ONLY backend class)
 * ---------------------------------------------
 * Apple Silicon GPU shader cores have NO 64-bit floating-point
 * ALUs. This is a hardware constraint, not a software choice.
 * Metal Shading Language does not expose `double` as a compute
 * type in any version, on any Apple GPU generation (M1-M4, all
 * A-series). All kernel arithmetic is 32-bit float.
 *
 * The host-side bridge (gpu_metal.m) converts the caller's
 * double* buffer to float* before dispatch, and back to double*
 * after. Expected absolute error at Earth-scale ECEF coordinates
 * (magnitudes ~6.4e6 m):
 *
 *   1 ULP of float32 at 6.4e6  =  6.4e6 * 2^-23  ~=  0.76 m
 *   After rotation (cos+sin+mul+add)             ~=  1-2 m
 *
 * Earlier iterations of this file claimed "sub-millimeter at
 * Earth scale". That claim was wrong by three orders of magnitude
 * and has been corrected. See:
 *   - openspec/changes/apple-metal-gpu-backend/design.md Decision 8 & 9
 *   - openspec/changes/apple-metal-gpu-backend/specs/metal-compute-kernels/spec.md
 *     (FP32_ONLY precision class and scale-relative error bounds)
 *
 * Dispatch is GATED per-operation in lwgeom_accel.c so the
 * precision tradeoff is only taken where it is acceptable:
 *
 *   - rotate_z_uniform   : always SKIPPED (NEON is faster anyway)
 *   - rad_convert        : always SKIPPED (NEON is faster anyway)
 *   - rotate_z_m_epoch   : dispatched at 50K+ points (5x the
 *                          PCIe-GPU threshold) when the compute
 *                          cost amortizes Metal launch overhead
 *
 * Users needing sub-meter precision on Apple Silicon should
 * either rely on NEON (always used for rotate_z_uniform and
 * rad_convert) or set postgis.gpu_dispatch_threshold = 0 to
 * disable all GPU dispatch.
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
};

struct RadConvertParams
{
	uint  stride;
	uint  npoints;
	float scale;
};

/*
 * WHY there is no ERA computation in this file
 * ---------------------------------------------
 * An earlier version of this kernel computed the Earth Rotation Angle
 * inline on the GPU from each point's M-epoch (decimal year). That
 * produced ~900m to ~900km of positional error because float32 cannot
 * represent modern-year epochs precisely enough:
 *
 *   float32 ULP at 2025       ~= 1.22e-4 year
 *   Du error from 1 ULP       ~= 0.022 days
 *   ERA error (* 2*pi * 1.00) ~= 0.138 rad
 *   Position error (* 6.4e6)  ~= 880,000 m  (!)
 *
 * The precision is lost at the double->float boundary BEFORE the
 * kernel sees the value -- fmod'ing after the fact does nothing.
 *
 * Fix: the host (gpu_metal.m) now precomputes per-point theta in
 * double precision using lweci_epoch_to_jd / lweci_earth_rotation_angle,
 * applies the direction sign, reduces modulo 2*pi, and only then
 * narrows to float. The kernel receives ready-to-use float thetas
 * via a separate input buffer at [[buffer(2)]].
 *
 * After reduction, theta is in [0, 2*pi) where float32 ULP is ~7e-7.
 * That gives sub-meter absolute error at Earth scale after rotation --
 * matching the FP32_ONLY precision contract (~1-2 m worst case).
 */

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

	data[base]     = x * params.cos_t + y * params.sin_t;
	data[base + 1] = -x * params.sin_t + y * params.cos_t;
}

/**
 * Per-point M-epoch Z-rotation kernel.
 *
 * The host (gpu_metal.m) precomputes per-point rotation angles in
 * double precision from each point's M-epoch (including direction
 * sign and mod-2*pi reduction), then narrows to float. Each thread
 * reads its pre-reduced theta from the thetas buffer and applies
 * a Z-rotation to the corresponding point.
 *
 * The thetas buffer contains `params.npoints` floats, one per point.
 * Because theta is pre-reduced to [-2*pi, 2*pi), cos()/sin() operate
 * in their accurate regime (~1 ULP) rather than the catastrophic
 * large-argument regime that ruined the earlier inline-ERA version.
 */
kernel void rotate_z_m_epoch(device float                *data   [[buffer(0)]],
                             constant RotateZMEpochParams &params [[buffer(1)]],
                             device const float          *thetas [[buffer(2)]],
                             uint id [[thread_position_in_grid]])
{
	if (id >= params.npoints)
		return;

	uint base = id * params.stride;
	float theta = thetas[id];

	float cos_t = cos(theta);
	float sin_t = sin(theta);

	float x = data[base];
	float y = data[base + 1];

	data[base]     = x * cos_t + y * sin_t;
	data[base + 1] = -x * sin_t + y * cos_t;
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
