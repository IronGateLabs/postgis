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

#ifndef LWGEOM_ACCEL_H
#define LWGEOM_ACCEL_H

#include "liblwgeom.h"
#include "../postgis_config.h"

/**
 * SIMD backend identifier for runtime dispatch.
 */
typedef enum {
	LW_ACCEL_NONE = 0,   /* Scalar fallback */
	LW_ACCEL_NEON,        /* ARM NEON (128-bit, 2-wide double) */
	LW_ACCEL_AVX2,        /* x86 AVX2 + FMA (256-bit, 4-wide double) */
	LW_ACCEL_AVX512       /* x86 AVX-512 (512-bit, 8-wide double) */
} LW_ACCEL_BACKEND;

/**
 * Acceleration dispatch table: function pointers for SIMD-accelerated
 * transform operations. Populated at first use via lwaccel_init().
 */
typedef struct {
	/** Uniform-epoch Z-rotation: all points rotated by same theta */
	int (*rotate_z)(POINTARRAY *pa, double theta);

	/** Per-point M-epoch Z-rotation: each point uses its M as epoch */
	int (*rotate_z_m_epoch)(POINTARRAY *pa, int direction);

	/** Batch radian conversion: multiply x,y by scale factor */
	void (*rad_convert)(POINTARRAY *pa, double scale);

	/** Active backend identifier */
	LW_ACCEL_BACKEND backend;
} LW_ACCEL_DISPATCH;

/**
 * Initialize the acceleration dispatch table. Called automatically
 * on first use; safe to call multiple times (no-op after first).
 */
void lwaccel_init(void);

/**
 * Get the current acceleration dispatch table.
 * Calls lwaccel_init() if not yet initialized.
 */
const LW_ACCEL_DISPATCH *lwaccel_get(void);

/**
 * Return a human-readable string describing detected acceleration
 * features. Caller must lwfree() the result.
 */
char *lwaccel_features_string(void);

/**
 * Set/get the GPU dispatch threshold. POINTARRAYs with >= threshold
 * points will be dispatched to GPU if available. Default: 10000.
 */
void lwaccel_set_gpu_threshold(uint32_t threshold);
uint32_t lwaccel_get_gpu_threshold(void);

/*
 * SIMD backend implementations (called through dispatch table).
 * These are defined in accel and registered by lwaccel_init().
 */
#ifdef HAVE_AVX2
int ptarray_rotate_z_avx2(POINTARRAY *pa, double theta);
int ptarray_rotate_z_m_epoch_avx2(POINTARRAY *pa, int direction);
void ptarray_rad_convert_avx2(POINTARRAY *pa, double scale);
#endif

#ifdef HAVE_AVX512
int ptarray_rotate_z_avx512(POINTARRAY *pa, double theta);
#endif

#ifdef HAVE_NEON
int ptarray_rotate_z_neon(POINTARRAY *pa, double theta);
int ptarray_rotate_z_m_epoch_neon(POINTARRAY *pa, int direction);
void ptarray_rad_convert_neon(POINTARRAY *pa, double scale);
#endif

/* Scalar fallback implementations (always available) */
int ptarray_rotate_z_scalar(POINTARRAY *pa, double theta);
int ptarray_rotate_z_m_epoch_scalar(POINTARRAY *pa, int direction);
void ptarray_rad_convert_scalar(POINTARRAY *pa, double scale);

#endif /* LWGEOM_ACCEL_H */
