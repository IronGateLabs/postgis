/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * CUDA backend for GPU-accelerated coordinate transforms.
 * Compiled with nvcc.
 *
 **********************************************************************/

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>

extern "C" {
#include "../lwgeom_gpu.h"
}

static int cuda_initialized = 0;
static char cuda_device[256] = "unknown";

/* Earth Rotation Angle computation on GPU */
__device__ double
gpu_epoch_to_jd(double decimal_year)
{
	return 2451545.0 + (decimal_year - 2000.0) * 365.25;
}

__device__ double
gpu_earth_rotation_angle(double jd)
{
	double Du = jd - 2451545.0;
	double era = 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);
	era = fmod(era, 2.0 * M_PI);
	if (era < 0.0)
		era += 2.0 * M_PI;
	return era;
}

/* Uniform-angle Z-rotation kernel */
__global__ void
rotate_z_kernel(double *data, size_t stride_doubles, uint32_t npoints,
		double cos_t, double sin_t)
{
	uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
	if (idx >= npoints) return;

	double *p = data + idx * stride_doubles;
	double x = p[0], y = p[1];
	p[0] = x * cos_t + y * sin_t;
	p[1] = -x * sin_t + y * cos_t;
}

/* Per-point M-epoch Z-rotation kernel */
__global__ void
rotate_z_m_epoch_kernel(double *data, size_t stride_doubles, uint32_t npoints,
			size_t m_offset, int direction)
{
	uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
	if (idx >= npoints) return;

	double *p = data + idx * stride_doubles;
	double epoch = p[m_offset];
	double jd = gpu_epoch_to_jd(epoch);
	double era = gpu_earth_rotation_angle(jd);
	double theta = direction * era;
	double cos_t = cos(theta);
	double sin_t = sin(theta);

	double x = p[0], y = p[1];
	p[0] = x * cos_t + y * sin_t;
	p[1] = -x * sin_t + y * cos_t;
}

extern "C" int
lwgpu_cuda_init(void)
{
	int device_count = 0;
	cudaError_t err = cudaGetDeviceCount(&device_count);
	if (err != cudaSuccess || device_count == 0)
		return 0;

	cudaDeviceProp prop;
	cudaGetDeviceProperties(&prop, 0);
	snprintf(cuda_device, sizeof(cuda_device), "%s (compute %d.%d)",
		 prop.name, prop.major, prop.minor);

	cuda_initialized = 1;
	return 1;
}

extern "C" int
lwgpu_cuda_rotate_z(double *data, size_t stride, uint32_t n, double theta)
{
	double *d_data = NULL;
	size_t stride_doubles = stride / sizeof(double);
	size_t total_bytes = n * stride;
	double cos_t = cos(theta);
	double sin_t = sin(theta);
	int result = 0;

	if (cudaMalloc(&d_data, total_bytes) != cudaSuccess)
		return 0;

	if (cudaMemcpy(d_data, data, total_bytes, cudaMemcpyHostToDevice) != cudaSuccess)
		goto cleanup;

	{
		int threads = 256;
		int blocks = (n + threads - 1) / threads;
		rotate_z_kernel<<<blocks, threads>>>(d_data, stride_doubles, n, cos_t, sin_t);
	}

	if (cudaDeviceSynchronize() != cudaSuccess)
		goto cleanup;

	if (cudaMemcpy(data, d_data, total_bytes, cudaMemcpyDeviceToHost) != cudaSuccess)
		goto cleanup;

	result = 1;

cleanup:
	cudaFree(d_data);
	return result;
}

extern "C" int
lwgpu_cuda_rotate_z_m_epoch(double *data, size_t stride, uint32_t n,
			    size_t m_off, int dir)
{
	double *d_data = NULL;
	size_t stride_doubles = stride / sizeof(double);
	size_t total_bytes = n * stride;
	int result = 0;

	if (cudaMalloc(&d_data, total_bytes) != cudaSuccess)
		return 0;

	if (cudaMemcpy(d_data, data, total_bytes, cudaMemcpyHostToDevice) != cudaSuccess)
		goto cleanup;

	{
		int threads = 256;
		int blocks = (n + threads - 1) / threads;
		rotate_z_m_epoch_kernel<<<blocks, threads>>>(d_data, stride_doubles, n, m_off, dir);
	}

	if (cudaDeviceSynchronize() != cudaSuccess)
		goto cleanup;

	if (cudaMemcpy(data, d_data, total_bytes, cudaMemcpyDeviceToHost) != cudaSuccess)
		goto cleanup;

	result = 1;

cleanup:
	cudaFree(d_data);
	return result;
}

extern "C" void
lwgpu_cuda_shutdown(void)
{
	if (cuda_initialized)
	{
		cudaDeviceReset();
		cuda_initialized = 0;
	}
}

extern "C" const char *
lwgpu_cuda_device_name(void)
{
	return cuda_device;
}
