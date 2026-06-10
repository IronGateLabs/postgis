/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * oneAPI/SYCL backend for GPU-accelerated coordinate transforms.
 * Compiled with icpx -fsycl.
 *
 **********************************************************************/

#include <sycl/sycl.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include "../lwgeom_gpu.h"
}

static sycl::queue *sycl_queue = nullptr;   // NOSONAR - mutable runtime state
static int oneapi_initialized = 0;          // NOSONAR - mutable runtime flag
static char oneapi_device[256] = "unknown"; // NOSONAR - C-compatible fixed buffer for extern "C" API

extern "C" int
lwgpu_oneapi_init(void)
{
	try
	{
		sycl_queue = new sycl::queue(sycl::gpu_selector_v);
		auto dev = sycl_queue->get_device();
		std::string name = dev.get_info<sycl::info::device::name>();
		snprintf(oneapi_device, sizeof(oneapi_device), "%s", name.c_str());
		oneapi_initialized = 1;
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" int
lwgpu_oneapi_rotate_z(double *data, size_t stride, uint32_t n, double theta)
{
	if (!sycl_queue)
		return 0;

	size_t stride_doubles = stride / sizeof(double);
	size_t total_bytes = n * stride;
	double cos_t = std::cos(theta);
	double sin_t = std::sin(theta);

	try
	{
		double *d_data = sycl::malloc_device<double>(n * stride_doubles, *sycl_queue);
		if (!d_data)
			return 0;

		sycl_queue->memcpy(d_data, data, total_bytes).wait();

		sycl_queue
		    ->parallel_for(sycl::range<1>(n),
				   [=](sycl::id<1> idx) {
					   double *p = d_data + idx[0] * stride_doubles;
					   double x = p[0], y = p[1];
					   p[0] = x * cos_t + y * sin_t;
					   p[1] = -x * sin_t + y * cos_t;
				   })
		    .wait();

		sycl_queue->memcpy(data, d_data, total_bytes).wait();
		sycl::free(d_data, *sycl_queue);
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" int
lwgpu_oneapi_rotate_z_m_epoch(double *data, size_t stride, uint32_t n, size_t m_off, int dir)
{
	if (!sycl_queue)
		return 0;

	size_t stride_doubles = stride / sizeof(double);
	size_t total_bytes = n * stride;

	try
	{
		double *d_data = sycl::malloc_device<double>(n * stride_doubles, *sycl_queue);
		if (!d_data)
			return 0;

		sycl_queue->memcpy(d_data, data, total_bytes).wait();

		sycl_queue
		    ->parallel_for(sycl::range<1>(n),
				   [=](sycl::id<1> idx) {
					   double *p = d_data + idx[0] * stride_doubles;
					   double epoch = p[m_off];
					   double jd = 2451545.0 + (epoch - 2000.0) * 365.25;
					   double Du = jd - 2451545.0;
					   double era = 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);
					   era = sycl::fmod(era, 2.0 * M_PI);
					   if (era < 0.0)
						   era += 2.0 * M_PI;

					   double theta = dir * era;
					   double cos_t = sycl::cos(theta);
					   double sin_t = sycl::sin(theta);

					   double x = p[0], y = p[1];
					   p[0] = x * cos_t + y * sin_t;
					   p[1] = -x * sin_t + y * cos_t;
				   })
		    .wait();

		sycl_queue->memcpy(data, d_data, total_bytes).wait();
		sycl::free(d_data, *sycl_queue);
		return 1;
	}
	catch (...)
	{
		return 0;
	}
}

extern "C" void
lwgpu_oneapi_shutdown(void)
{
	if (oneapi_initialized && sycl_queue)
	{
		delete sycl_queue; // NOSONAR - matches raw new in lwgpu_oneapi_init()
		sycl_queue = nullptr;
		oneapi_initialized = 0;
	}
}

extern "C" const char *
lwgpu_oneapi_device_name(void)
{
	return oneapi_device;
}
