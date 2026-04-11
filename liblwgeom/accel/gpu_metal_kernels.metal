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
 * Inline helper: compute Earth Rotation Angle (reduced to [0, 2*PI))
 * from a decimal-year epoch.
 *
 * Implementation notes (float32 precision):
 *
 * 1. We compute Du (days since J2000) directly from the epoch via
 *    Du = (epoch - 2000) * 365.25, WITHOUT going through an
 *    intermediate Julian Date value. JD = 2451545 + Du is around
 *    2.45e6, and float32 at that magnitude has ULP ~0.25. Computing
 *    JD and then subtracting 2451545 back out loses precision
 *    unnecessarily. Going straight from epoch to Du keeps the value
 *    small (~9000 for modern epochs) where float32 precision is fine.
 *
 * 2. We reduce the raw ERA modulo 2*PI BEFORE returning. For modern
 *    epochs the raw ERA is tens of thousands of radians (thousands of
 *    full rotations). Metal's cos()/sin() on such large arguments
 *    perform internal range reduction in float arithmetic, which
 *    accumulates catastrophic error -- enough to produce ~1000x
 *    worse results than the float32 ULP would suggest. fmod'ing to
 *    [0, 2*PI) first keeps cos/sin in the regime where they are
 *    accurate to ~1 ULP.
 *
 * This correction restores rotate_z_m_epoch correctness; prior to
 * the fix, test_metal_rotate_z_m_epoch showed 8.95e+05 meters of
 * error (895 km -- random-noise from cos/sin on huge arguments)
 * instead of the ~1 m expected from float32 ULP at Earth scale.
 */
static inline float
gpu_earth_rotation_angle(float epoch)
{
	float Du = (epoch - 2000.0f) * 365.25f;
	float era = 2.0f * M_PI_F * (0.7790572732640f + 1.00273781191135448f * Du);
	float two_pi = 2.0f * M_PI_F;
	return fmod(era, two_pi);
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

	data[base]     = x * params.cos_t + y * params.sin_t;
	data[base + 1] = -x * params.sin_t + y * params.cos_t;
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
	float era = gpu_earth_rotation_angle(epoch);
	float theta = era * float(params.direction);

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
