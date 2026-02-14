/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * GPU abstraction layer for batch coordinate transforms.
 * Supports CUDA, ROCm/HIP, and oneAPI/SYCL backends through
 * a common C API.
 *
 **********************************************************************/

#ifndef LWGEOM_GPU_H
#define LWGEOM_GPU_H

#include "../postgis_config.h"
#include <stdint.h>
#include <stddef.h>

/**
 * GPU backend identifier.
 */
typedef enum {
	LW_GPU_NONE   = 0,
	LW_GPU_CUDA   = 1,
	LW_GPU_ROCM   = 2,
	LW_GPU_ONEAPI = 3
} LW_GPU_BACKEND;

/**
 * Initialize the GPU backend. Detects available hardware and
 * selects the best available backend (or a specific one if
 * preferred != LW_GPU_NONE).
 *
 * Returns 1 on success (GPU available), 0 if no GPU detected.
 * Safe to call multiple times; no-op after first successful init.
 */
int lwgpu_init(LW_GPU_BACKEND preferred);

/**
 * Check if GPU dispatch is available and initialized.
 */
int lwgpu_available(void);

/**
 * Get the active GPU backend type.
 */
LW_GPU_BACKEND lwgpu_backend(void);

/**
 * Get a human-readable string for the active GPU backend.
 */
const char *lwgpu_backend_name(void);

/**
 * GPU batch Z-rotation: uniform angle for all points.
 *
 * @param xy_pairs  Pointer to interleaved x,y data (stride in bytes)
 * @param stride    Byte stride between consecutive points
 * @param npoints   Number of points to transform
 * @param theta     Rotation angle in radians
 * @return 1 on success, 0 on failure (caller should fallback to CPU)
 */
int lwgpu_rotate_z_batch(double *xy_pairs, size_t stride,
			 uint32_t npoints, double theta);

/**
 * GPU batch Z-rotation: per-point epoch from M coordinate.
 *
 * @param xyzm      Pointer to interleaved x,y,z,m data
 * @param stride    Byte stride between consecutive points
 * @param npoints   Number of points to transform
 * @param m_offset  Offset of M coordinate in doubles from point start
 * @param direction -1 for ECI->ECEF, +1 for ECEF->ECI
 * @return 1 on success, 0 on failure (caller should fallback to CPU)
 */
int lwgpu_rotate_z_m_epoch_batch(double *xyzm, size_t stride,
				 uint32_t npoints, size_t m_offset,
				 int direction);

/**
 * Shut down GPU backend and release resources.
 */
void lwgpu_shutdown(void);

/*
 * Backend-specific initialization functions.
 * Each backend implements these; only the compiled one is linked.
 */
#ifdef HAVE_CUDA
int lwgpu_cuda_init(void);
int lwgpu_cuda_rotate_z(double *data, size_t stride, uint32_t n, double theta);
int lwgpu_cuda_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
				size_t m_off, int dir);
void lwgpu_cuda_shutdown(void);
const char *lwgpu_cuda_device_name(void);
#endif

#ifdef HAVE_ROCM
int lwgpu_rocm_init(void);
int lwgpu_rocm_rotate_z(double *data, size_t stride, uint32_t n, double theta);
int lwgpu_rocm_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
				size_t m_off, int dir);
void lwgpu_rocm_shutdown(void);
const char *lwgpu_rocm_device_name(void);
#endif

#ifdef HAVE_ONEAPI
int lwgpu_oneapi_init(void);
int lwgpu_oneapi_rotate_z(double *data, size_t stride, uint32_t n, double theta);
int lwgpu_oneapi_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
				  size_t m_off, int dir);
void lwgpu_oneapi_shutdown(void);
const char *lwgpu_oneapi_device_name(void);
#endif

#endif /* LWGEOM_GPU_H */
