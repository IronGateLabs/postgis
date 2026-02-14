/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PostGIS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * PostGIS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostGIS.  If not, see <http://www.gnu.org/licenses/>.
 *
 **********************************************************************/

#include "lwgeom_accel.h"
#include "lwgeom_gpu.h"
#include "lwgeom_log.h"

#include <string.h>
#include <math.h>

/* Runtime cpuid detection for x86 */
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#ifdef __GNUC__
#include <cpuid.h>
#define HAVE_CPUID 1
#elif defined(_MSC_VER)
#include <intrin.h>
#define HAVE_CPUID 1
#endif
#endif

static LW_ACCEL_DISPATCH dispatch;
static int initialized = 0;

/* GPU dispatch threshold: arrays with >= this many points use GPU.
 * Default 10000; can be overridden by postgis.gpu_dispatch_threshold GUC. */
static uint32_t gpu_dispatch_threshold = 10000;

void
lwaccel_set_gpu_threshold(uint32_t threshold)
{
	gpu_dispatch_threshold = threshold;
}

uint32_t
lwaccel_get_gpu_threshold(void)
{
	return gpu_dispatch_threshold;
}

/*
 * Scalar fallback: delegates to the existing rotate_z in lwgeom_eci.c
 * These are thin wrappers that match the dispatch function signatures.
 */

/* Forward declarations of the internal rotate_z from lwgeom_eci.c */
static void
rotate_z_point(POINT4D *p, double theta)
{
	double cos_t = cos(theta);
	double sin_t = sin(theta);
	double x_new = p->x * cos_t + p->y * sin_t;
	double y_new = -p->x * sin_t + p->y * cos_t;
	p->x = x_new;
	p->y = y_new;
}

int
ptarray_rotate_z_scalar(POINTARRAY *pa, double theta)
{
	uint32_t i;
	POINT4D p;
	for (i = 0; i < pa->npoints; i++)
	{
		getPoint4d_p(pa, i, &p);
		rotate_z_point(&p, theta);
		ptarray_set_point4d(pa, i, &p);
	}
	return LW_SUCCESS;
}

int
ptarray_rotate_z_m_epoch_scalar(POINTARRAY *pa, int direction)
{
	uint32_t i;
	POINT4D p;
	for (i = 0; i < pa->npoints; i++)
	{
		double jd, era;
		getPoint4d_p(pa, i, &p);

		if (p.m < 1000.0 || p.m > 3000.0)
		{
			lwerror("ECI transform: point %u has invalid epoch M=%.4f "
				"(expected decimal year in range 1000-3000)", i, p.m);
			return LW_FAILURE;
		}

		jd = lweci_epoch_to_jd(p.m);
		era = lweci_earth_rotation_angle(jd);
		rotate_z_point(&p, direction * era);
		ptarray_set_point4d(pa, i, &p);
	}
	return LW_SUCCESS;
}

void
ptarray_rad_convert_scalar(POINTARRAY *pa, double scale)
{
	uint32_t i;
	POINT4D p;
	for (i = 0; i < pa->npoints; i++)
	{
		getPoint4d_p(pa, i, &p);
		p.x *= scale;
		p.y *= scale;
		ptarray_set_point4d(pa, i, &p);
	}
}

/*
 * GPU-aware dispatch wrappers.
 * If GPU is available and point count exceeds threshold, use GPU.
 * Otherwise use the CPU SIMD dispatch.
 */
static int (*cpu_rotate_z)(POINTARRAY *pa, double theta) = NULL;
static int (*cpu_rotate_z_m_epoch)(POINTARRAY *pa, int direction) = NULL;

static int
gpu_aware_rotate_z(POINTARRAY *pa, double theta)
{
	if (pa->npoints >= gpu_dispatch_threshold && lwgpu_available())
	{
		size_t stride = ptarray_point_size(pa);
		if (lwgpu_rotate_z_batch((double *)pa->serialized_pointlist,
					 stride, pa->npoints, theta))
			return LW_SUCCESS;
		/* GPU failed, fall through to CPU */
	}
	return cpu_rotate_z(pa, theta);
}

static int
gpu_aware_rotate_z_m_epoch(POINTARRAY *pa, int direction)
{
	if (pa->npoints >= gpu_dispatch_threshold && lwgpu_available())
	{
		int has_z = FLAGS_GET_Z(pa->flags);
		size_t m_offset = has_z ? 3 : 2;
		size_t stride = ptarray_point_size(pa);
		if (lwgpu_rotate_z_m_epoch_batch((double *)pa->serialized_pointlist,
						 stride, pa->npoints, m_offset,
						 direction))
			return LW_SUCCESS;
		/* GPU failed, fall through to CPU */
	}
	return cpu_rotate_z_m_epoch(pa, direction);
}

/*
 * Runtime CPU feature detection via cpuid (x86) or compile-time (ARM).
 */
static LW_ACCEL_BACKEND
detect_simd_backend(void)
{
#ifdef HAVE_CPUID
	unsigned int eax, ebx, ecx, edx;

	/* Check for AVX2 + FMA: CPUID leaf 7, subleaf 0 */
	if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
	{
#ifdef HAVE_AVX512
		/* AVX-512F is bit 16 of EBX from leaf 7 */
		if (ebx & (1 << 16))
		{
			LWDEBUG(1, "SIMD: AVX-512 detected");
			return LW_ACCEL_AVX512;
		}
#endif
#ifdef HAVE_AVX2
		/* AVX2 is bit 5 of EBX from leaf 7 */
		if (ebx & (1 << 5))
		{
			/* Also check FMA: CPUID leaf 1, ECX bit 12 */
			if (__get_cpuid(1, &eax, &ebx, &ecx, &edx))
			{
				if (ecx & (1 << 12))
				{
					LWDEBUG(1, "SIMD: AVX2+FMA detected");
					return LW_ACCEL_AVX2;
				}
			}
			/* AVX2 without FMA — still usable but less optimal */
			LWDEBUG(1, "SIMD: AVX2 detected (no FMA)");
			return LW_ACCEL_AVX2;
		}
#endif
	}
#endif /* HAVE_CPUID */

#ifdef HAVE_NEON
	/* ARM NEON is guaranteed on ARMv8+ (aarch64) */
	LWDEBUG(1, "SIMD: ARM NEON detected");
	return LW_ACCEL_NEON;
#endif

	LWDEBUG(1, "SIMD: no acceleration available, using scalar");
	return LW_ACCEL_NONE;
}

void
lwaccel_init(void)
{
	LW_ACCEL_BACKEND backend;

	if (initialized)
		return;

	/* Start with scalar fallbacks */
	dispatch.rotate_z = ptarray_rotate_z_scalar;
	dispatch.rotate_z_m_epoch = ptarray_rotate_z_m_epoch_scalar;
	dispatch.rad_convert = ptarray_rad_convert_scalar;
	dispatch.backend = LW_ACCEL_NONE;

	backend = detect_simd_backend();

	switch (backend)
	{
#ifdef HAVE_AVX512
	case LW_ACCEL_AVX512:
		dispatch.rotate_z = ptarray_rotate_z_avx512;
		/* AVX-512 m_epoch and rad_convert fall through to AVX2 if available */
#ifdef HAVE_AVX2
		dispatch.rotate_z_m_epoch = ptarray_rotate_z_m_epoch_avx2;
		dispatch.rad_convert = ptarray_rad_convert_avx2;
#endif
		dispatch.backend = LW_ACCEL_AVX512;
		break;
#endif

#ifdef HAVE_AVX2
	case LW_ACCEL_AVX2:
		dispatch.rotate_z = ptarray_rotate_z_avx2;
		dispatch.rotate_z_m_epoch = ptarray_rotate_z_m_epoch_avx2;
		dispatch.rad_convert = ptarray_rad_convert_avx2;
		dispatch.backend = LW_ACCEL_AVX2;
		break;
#endif

#ifdef HAVE_NEON
	case LW_ACCEL_NEON:
		dispatch.rotate_z = ptarray_rotate_z_neon;
		dispatch.rotate_z_m_epoch = ptarray_rotate_z_m_epoch_neon;
		dispatch.rad_convert = ptarray_rad_convert_neon;
		dispatch.backend = LW_ACCEL_NEON;
		break;
#endif

	default:
		/* Already set to scalar */
		break;
	}

	/* Wire up GPU-aware dispatch if any GPU backend is available */
	lwgpu_init(LW_GPU_NONE); /* auto-detect */
	if (lwgpu_available())
	{
		cpu_rotate_z = dispatch.rotate_z;
		cpu_rotate_z_m_epoch = dispatch.rotate_z_m_epoch;
		dispatch.rotate_z = gpu_aware_rotate_z;
		dispatch.rotate_z_m_epoch = gpu_aware_rotate_z_m_epoch;
	}

	initialized = 1;
}

const LW_ACCEL_DISPATCH *
lwaccel_get(void)
{
	if (!initialized)
		lwaccel_init();
	return &dispatch;
}

char *
lwaccel_features_string(void)
{
	char buf[512];
	const char *simd_name;

	if (!initialized)
		lwaccel_init();

	switch (dispatch.backend)
	{
	case LW_ACCEL_AVX512: simd_name = "AVX-512"; break;
	case LW_ACCEL_AVX2:   simd_name = "AVX2+FMA"; break;
	case LW_ACCEL_NEON:   simd_name = "ARM NEON"; break;
	default:              simd_name = "none"; break;
	}

	snprintf(buf, sizeof(buf),
		"SIMD: %s"
		"; GPU: %s"
		"; Valkey: disabled",
		simd_name,
		lwgpu_available() ? lwgpu_backend_name() : "none");

	{
		size_t len = strlen(buf) + 1;
		char *result = lwalloc(len);
		if (result)
			memcpy(result, buf, len);
		return result;
	}
}
