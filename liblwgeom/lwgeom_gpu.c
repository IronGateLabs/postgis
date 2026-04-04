/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * GPU abstraction layer implementation.
 * Routes calls to the compiled backend (CUDA, ROCm, or oneAPI).
 *
 **********************************************************************/

#include "lwgeom_gpu.h"
#include "lwgeom_log.h"

/* Convenience macro: true when at least one GPU backend is compiled in */
#if defined(HAVE_CUDA) || defined(HAVE_ROCM) || defined(HAVE_ONEAPI)
#define HAVE_ANY_GPU 1
#endif

static LW_GPU_BACKEND active_backend = LW_GPU_NONE;
static int gpu_initialized = 0;

int
lwgpu_init(LW_GPU_BACKEND preferred)
{
	if (gpu_initialized)
		return active_backend != LW_GPU_NONE;

	gpu_initialized = 1;

	/* If a specific backend is requested, try only that one */
	if (preferred != LW_GPU_NONE)
	{
#ifdef HAVE_ANY_GPU
#ifdef HAVE_CUDA
		if (preferred == LW_GPU_CUDA)
		{
			if (lwgpu_cuda_init())
			{
				active_backend = LW_GPU_CUDA;
				LWDEBUG(1, "GPU: CUDA backend initialized");
				return 1;
			}
		}
		else
#endif
#ifdef HAVE_ROCM
		    if (preferred == LW_GPU_ROCM)
		{
			if (lwgpu_rocm_init())
			{
				active_backend = LW_GPU_ROCM;
				LWDEBUG(1, "GPU: ROCm backend initialized");
				return 1;
			}
		}
		else
#endif
#ifdef HAVE_ONEAPI
		    if (preferred == LW_GPU_ONEAPI)
		{
			if (lwgpu_oneapi_init())
			{
				active_backend = LW_GPU_ONEAPI;
				LWDEBUG(1, "GPU: oneAPI backend initialized");
				return 1;
			}
		}
		else
#endif
		{
			/* Unknown or unsupported backend */
			(void)0;
		}
#else
		(void)preferred;
#endif
		return 0;
	}

	/* Auto-detect: try backends in priority order CUDA > ROCm > oneAPI */
#ifdef HAVE_CUDA
	if (lwgpu_cuda_init())
	{
		active_backend = LW_GPU_CUDA;
		LWDEBUG(1, "GPU: CUDA backend auto-detected");
		return 1;
	}
#endif

#ifdef HAVE_ROCM
	if (lwgpu_rocm_init())
	{
		active_backend = LW_GPU_ROCM;
		LWDEBUG(1, "GPU: ROCm backend auto-detected");
		return 1;
	}
#endif

#ifdef HAVE_ONEAPI
	if (lwgpu_oneapi_init())
	{
		active_backend = LW_GPU_ONEAPI;
		LWDEBUG(1, "GPU: oneAPI backend auto-detected");
		return 1;
	}
#endif

	LWDEBUG(1, "GPU: no backend available");
	return 0;
}

int
lwgpu_available(void)
{
	return active_backend != LW_GPU_NONE;
}

LW_GPU_BACKEND
lwgpu_backend(void)
{
	return active_backend;
}

const char *
lwgpu_backend_name(void)
{
#ifdef HAVE_ANY_GPU
#ifdef HAVE_CUDA
	if (active_backend == LW_GPU_CUDA)
		return lwgpu_cuda_device_name();
#endif
#ifdef HAVE_ROCM
	if (active_backend == LW_GPU_ROCM)
		return lwgpu_rocm_device_name();
#endif
#ifdef HAVE_ONEAPI
	if (active_backend == LW_GPU_ONEAPI)
		return lwgpu_oneapi_device_name();
#endif
#endif
	return "none";
}

int
lwgpu_rotate_z_batch(double *xy_pairs, size_t stride, uint32_t npoints, double theta)
{
#ifdef HAVE_ANY_GPU
#ifdef HAVE_CUDA
	if (active_backend == LW_GPU_CUDA)
		return lwgpu_cuda_rotate_z(xy_pairs, stride, npoints, theta);
#endif
#ifdef HAVE_ROCM
	if (active_backend == LW_GPU_ROCM)
		return lwgpu_rocm_rotate_z(xy_pairs, stride, npoints, theta);
#endif
#ifdef HAVE_ONEAPI
	if (active_backend == LW_GPU_ONEAPI)
		return lwgpu_oneapi_rotate_z(xy_pairs, stride, npoints, theta);
#endif
#else
	(void)xy_pairs;
	(void)stride;
	(void)npoints;
	(void)theta;
#endif
	return 0;
}

int
lwgpu_rotate_z_m_epoch_batch(double *xyzm, size_t stride, uint32_t npoints, size_t m_offset, int direction)
{
#ifdef HAVE_ANY_GPU
#ifdef HAVE_CUDA
	if (active_backend == LW_GPU_CUDA)
		return lwgpu_cuda_rotate_z_m_epoch(xyzm, stride, npoints, m_offset, direction);
#endif
#ifdef HAVE_ROCM
	if (active_backend == LW_GPU_ROCM)
		return lwgpu_rocm_rotate_z_m_epoch(xyzm, stride, npoints, m_offset, direction);
#endif
#ifdef HAVE_ONEAPI
	if (active_backend == LW_GPU_ONEAPI)
		return lwgpu_oneapi_rotate_z_m_epoch(xyzm, stride, npoints, m_offset, direction);
#endif
#else
	(void)xyzm;
	(void)stride;
	(void)npoints;
	(void)m_offset;
	(void)direction;
#endif
	return 0;
}

void
lwgpu_shutdown(void)
{
#ifdef HAVE_ANY_GPU
#ifdef HAVE_CUDA
	if (active_backend == LW_GPU_CUDA)
		lwgpu_cuda_shutdown();
	else
#endif
#ifdef HAVE_ROCM
	    if (active_backend == LW_GPU_ROCM)
		lwgpu_rocm_shutdown();
	else
#endif
#ifdef HAVE_ONEAPI
	    if (active_backend == LW_GPU_ONEAPI)
		lwgpu_oneapi_shutdown();
	else
#endif
	{
		/* No active backend to shut down */
		(void)0;
	}
#endif
	active_backend = LW_GPU_NONE;
	gpu_initialized = 0;
}
