/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Benchmark harness for acceleration backends.
 *
 * Usage:
 *   bench_accel --operation eci_rotate --backend auto
 *   bench_accel --operation proj_transform --backend scalar
 *   bench_accel --operation gpu_overhead --backend cuda
 *   bench_accel --validate
 *   bench_accel --csv
 *
 **********************************************************************/

#include "../liblwgeom.h"
#include "../lwgeom_accel.h"
#include "../lwgeom_gpu.h"

#include "bench_helpers.h"

#include <stdio.h>
#include <time.h>
#include <getopt.h>

#define BENCH_WARMUP_ITERS 3
#define BENCH_MEASURE_ITERS 10

/* Point counts to benchmark */
static const int POINT_COUNTS[] = {1, 100, 1000, 10000, 100000, 1000000, 10000000};
static const int N_POINT_COUNTS = sizeof(POINT_COUNTS) / sizeof(POINT_COUNTS[0]);

typedef struct {
	char operation[64];
	char backend[32];
	int validate;
	int calibrate;
	int csv;
	int help;
} BenchOptions;

static double
now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* Convenience aliases for shared helpers */
#define make_test_points(n) bench_make_test_pa((uint32_t)(n))
#define pa_copy bench_pa_copy
#define pa_max_diff bench_pa_max_diff

/*
 * Benchmark: uniform-epoch ECI rotation
 */
static void
bench_eci_rotate(const char *backend, int npoints, int csv, double *out_throughput)
{
	POINTARRAY *pa;
	double theta = 1.23456; /* Fixed rotation angle */
	int iter;
	double t_start, t_end, total_us;

	pa = make_test_points(npoints);

	/* Warmup */
	for (iter = 0; iter < BENCH_WARMUP_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(pa);
		if (strcmp(backend, "scalar") == 0)
			ptarray_rotate_z_scalar(tmp, theta);
		else
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rotate_z(tmp, theta);
		}
		ptarray_free(tmp);
	}

	/* Measure */
	total_us = 0;
	for (iter = 0; iter < BENCH_MEASURE_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(pa);
		t_start = now_us();

		if (strcmp(backend, "scalar") == 0)
			ptarray_rotate_z_scalar(tmp, theta);
		else
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rotate_z(tmp, theta);
		}

		t_end = now_us();
		total_us += (t_end - t_start);
		ptarray_free(tmp);
	}

	double avg_us = total_us / BENCH_MEASURE_ITERS;
	double throughput = (avg_us > 0) ? (double)npoints / (avg_us / 1e6) : 0;

	if (out_throughput)
		*out_throughput = throughput;

	if (csv)
		printf("eci_rotate,%s,%d,%.0f,%.2f\n", backend, npoints, throughput, avg_us);
	else
		printf("  %-8s  %10d pts  %12.0f pts/sec  %10.2f us\n", backend, npoints, throughput, avg_us);

	ptarray_free(pa);
}

/*
 * Benchmark: rad/deg conversion
 */
static void
bench_rad_convert(const char *backend, int npoints, int csv)
{
	POINTARRAY *pa;
	double scale = M_PI / 180.0;
	int iter;
	double t_start, t_end, total_us;

	pa = make_test_points(npoints);

	/* Warmup */
	for (iter = 0; iter < BENCH_WARMUP_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(pa);
		if (strcmp(backend, "scalar") == 0)
			ptarray_rad_convert_scalar(tmp, scale);
		else
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rad_convert(tmp, scale);
		}
		ptarray_free(tmp);
	}

	/* Measure */
	total_us = 0;
	for (iter = 0; iter < BENCH_MEASURE_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(pa);
		t_start = now_us();

		if (strcmp(backend, "scalar") == 0)
			ptarray_rad_convert_scalar(tmp, scale);
		else
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rad_convert(tmp, scale);
		}

		t_end = now_us();
		total_us += (t_end - t_start);
		ptarray_free(tmp);
	}

	double avg_us = total_us / BENCH_MEASURE_ITERS;
	double throughput = (avg_us > 0) ? (double)npoints / (avg_us / 1e6) : 0;

	if (csv)
		printf("rad_convert,%s,%d,%.0f,%.2f\n", backend, npoints, throughput, avg_us);
	else
		printf("  %-8s  %10d pts  %12.0f pts/sec  %10.2f us\n", backend, npoints, throughput, avg_us);

	ptarray_free(pa);
}

/*
 * Validation mode: compare SIMD output against scalar.
 */
static int
validate_backends(void)
{
	int pi, pass = 1;
	double theta = 1.23456;

	printf("\n=== Validation: SIMD vs Scalar ===\n\n");

	for (pi = 0; pi < N_POINT_COUNTS && POINT_COUNTS[pi] <= 100000; pi++)
	{
		int n = POINT_COUNTS[pi];
		POINTARRAY *base = make_test_points(n);
		POINTARRAY *scalar_pa = pa_copy(base);
		POINTARRAY *simd_pa = pa_copy(base);
		double max_diff;

		/* Scalar transform */
		ptarray_rotate_z_scalar(scalar_pa, theta);

		/* SIMD transform */
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rotate_z(simd_pa, theta);
		}

		max_diff = pa_max_diff(scalar_pa, simd_pa);

		printf("  %7d pts: max_diff = %.2e %s\n", n, max_diff, max_diff < 1e-9 ? "PASS" : "FAIL");

		if (max_diff >= 1e-9)
			pass = 0;

		ptarray_free(base);
		ptarray_free(scalar_pa);
		ptarray_free(simd_pa);
	}

	/* Also validate rad/deg conversion */
	printf("\n  Radian conversion:\n");
	for (pi = 0; pi < N_POINT_COUNTS && POINT_COUNTS[pi] <= 100000; pi++)
	{
		int n = POINT_COUNTS[pi];
		POINTARRAY *base = make_test_points(n);
		POINTARRAY *scalar_pa = pa_copy(base);
		POINTARRAY *simd_pa = pa_copy(base);
		double max_diff;

		ptarray_rad_convert_scalar(scalar_pa, M_PI / 180.0);
		{
			const LW_ACCEL_DISPATCH *accel = lwaccel_get();
			accel->rad_convert(simd_pa, M_PI / 180.0);
		}

		max_diff = pa_max_diff(scalar_pa, simd_pa);
		printf("  %7d pts: max_diff = %.2e %s\n", n, max_diff, max_diff < 1e-9 ? "PASS" : "FAIL");

		if (max_diff >= 1e-9)
			pass = 0;

		ptarray_free(base);
		ptarray_free(scalar_pa);
		ptarray_free(simd_pa);
	}

	printf("\nValidation: %s\n", pass ? "ALL PASSED" : "SOME FAILED");
	return pass ? 0 : 1;
}

static void
print_usage(void)
{
	printf("Usage: bench_accel [OPTIONS]\n\n");
	printf("Options:\n");
	printf("  --operation <op>   Operation to benchmark:\n");
	printf("                       eci_rotate    - ECI Z-rotation\n");
	printf("                       rad_convert   - Radian/degree conversion\n");
	printf("                       gpu_overhead  - GPU dispatch overhead\n");
	printf("                       all           - All operations (default)\n");
	printf("  --backend <be>     Backend to use:\n");
	printf("                       auto    - Best available (default)\n");
	printf("                       scalar  - Scalar only\n");
	printf("  --validate         Compare SIMD vs scalar for correctness\n");
	printf("  --calibrate        Run GPU auto-calibration and show result\n");
	printf("  --csv              Output as CSV\n");
	printf("  --help             Show this help\n");
}

static void
run_rotation_bench(const BenchOptions *opts)
{
	int pi;
	if (strcmp(opts->operation, "all") != 0 && strcmp(opts->operation, "eci_rotate") != 0)
		return;

	if (!opts->csv)
		printf("--- ECI Rotation (uniform epoch) ---\n");

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		if (strcmp(opts->backend, "scalar") == 0 || strcmp(opts->backend, "all") == 0)
			bench_eci_rotate("scalar", POINT_COUNTS[pi], opts->csv, NULL);
		if (strcmp(opts->backend, "auto") == 0 || strcmp(opts->backend, "all") == 0 ||
		    strcmp(opts->backend, "simd") == 0)
			bench_eci_rotate("simd", POINT_COUNTS[pi], opts->csv, NULL);
	}

	if (!opts->csv)
		printf("\n");
}

static void
run_radconvert_bench(const BenchOptions *opts)
{
	int pi;
	if (strcmp(opts->operation, "all") != 0 && strcmp(opts->operation, "rad_convert") != 0)
		return;

	if (!opts->csv)
		printf("--- Radian/Degree Conversion ---\n");

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		if (strcmp(opts->backend, "scalar") == 0 || strcmp(opts->backend, "all") == 0)
			bench_rad_convert("scalar", POINT_COUNTS[pi], opts->csv);
		if (strcmp(opts->backend, "auto") == 0 || strcmp(opts->backend, "all") == 0 ||
		    strcmp(opts->backend, "simd") == 0)
			bench_rad_convert("simd", POINT_COUNTS[pi], opts->csv);
	}

	if (!opts->csv)
		printf("\n");
}

static void
run_gpu_overhead_bench(const BenchOptions *opts)
{
	int pi;
	if (strcmp(opts->operation, "gpu_overhead") != 0)
		return;

	if (!opts->csv)
		printf("--- GPU Dispatch Overhead ---\n");

	if (!lwgpu_available())
	{
		printf("  No GPU available, skipping.\n");
		return;
	}

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		int n = POINT_COUNTS[pi];
		POINTARRAY *pa = make_test_points(n);
		double t_start = now_us();
		lwgpu_rotate_z_batch((double *)pa->serialized_pointlist, ptarray_point_size(pa), n, 1.234);
		double t_end = now_us();
		double gpu_us = t_end - t_start;
		double throughput = (gpu_us > 0) ? (double)n / (gpu_us / 1e6) : 0;

		if (opts->csv)
			printf("gpu_overhead,gpu,%d,%.0f,%.2f\n", n, throughput, gpu_us);
		else
			printf("  gpu  %10d pts  %12.0f pts/sec  %10.2f us\n", n, throughput, gpu_us);

		ptarray_free(pa);
	}

	if (!opts->csv)
		printf("\n");
}

int
main(int argc, char *argv[])
{
	BenchOptions opts = {.operation = "all", .backend = "auto", .validate = 0, .calibrate = 0, .csv = 0, .help = 0};

	static struct option long_opts[] = {{"operation", required_argument, 0, 'o'},
					    {"backend", required_argument, 0, 'b'},
					    {"validate", no_argument, 0, 'v'},
					    {"calibrate", no_argument, 0, 'C'},
					    {"csv", no_argument, 0, 'c'},
					    {"help", no_argument, 0, 'h'},
					    {0, 0, 0, 0}};

	int opt;
	while ((opt = getopt_long(argc, argv, "o:b:vCch", long_opts, NULL)) != -1)
	{
		switch (opt)
		{
		case 'o':
			snprintf(opts.operation, sizeof(opts.operation), "%s", optarg);
			break;
		case 'b':
			snprintf(opts.backend, sizeof(opts.backend), "%s", optarg);
			break;
		case 'v':
			opts.validate = 1;
			break;
		case 'C':
			opts.calibrate = 1;
			break;
		case 'c':
			opts.csv = 1;
			break;
		case 'h':
			opts.help = 1;
			break;
		default:
			print_usage();
			return 1;
		}
	}

	if (opts.help)
	{
		print_usage();
		return 0;
	}

	/* Initialize liblwgeom */
	lwgeom_set_handlers(0, 0, 0, 0, 0);

	/* Initialize acceleration */
	lwaccel_init();

	/* Show detected features */
	char *features = lwaccel_features_string();
	if (!opts.csv)
	{
		printf("=== PostGIS Acceleration Benchmark ===\n\n");
		printf("Detected: %s\n\n", features);
	}
	lwfree(features);

	/* Validation mode */
	if (opts.validate)
		return validate_backends();

	/* Calibration mode */
	if (opts.calibrate)
	{
		if (!lwgpu_available())
		{
			printf("No GPU available, calibration requires GPU.\n");
			return 1;
		}
		printf("Running GPU auto-calibration...\n\n");
		uint32_t threshold = lwaccel_calibrate_gpu();
		printf("\nCalibrated GPU dispatch threshold: %u points\n", threshold);
		printf("Set via: SET postgis.gpu_dispatch_threshold = %u;\n", threshold);
		printf("Or 0 for auto-calibrate on first use (default).\n");

		/* Show updated features string */
		lwaccel_set_gpu_threshold(threshold);
		char *features2 = lwaccel_features_string();
		printf("\nFeatures: %s\n", features2);
		lwfree(features2);
		return 0;
	}

	/* CSV header */
	if (opts.csv)
		printf("operation,backend,point_count,throughput_pts_per_sec,latency_us\n");

	/* Run benchmarks */
	run_rotation_bench(&opts);
	run_radconvert_bench(&opts);
	run_gpu_overhead_bench(&opts);

	return 0;
}
