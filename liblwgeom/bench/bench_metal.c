/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Benchmark harness: CPU scalar vs NEON SIMD vs Metal GPU
 * for ECI/ECEF coordinate transform operations.
 *
 * Measures wall-clock time for Z-rotation, M-epoch Z-rotation,
 * and radian conversion at various point counts. Reports median,
 * min/max, and throughput for each backend.
 *
 * Usage:
 *   bench_metal [--csv]
 *
 **********************************************************************/

#include "../liblwgeom.h"
#include "../lwgeom_accel.h"
#include "../lwgeom_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <mach/mach_time.h>

#define BENCH_TOTAL_ITERS    10
#define BENCH_WARMUP_ITERS   2
#define BENCH_MEASURE_ITERS  (BENCH_TOTAL_ITERS - BENCH_WARMUP_ITERS)

/* Point counts to benchmark */
static const uint32_t POINT_COUNTS[] = {
	100, 1000, 5000, 10000, 50000, 100000, 500000
};
static const int N_POINT_COUNTS = sizeof(POINT_COUNTS) / sizeof(POINT_COUNTS[0]);

/* Mach timebase for converting ticks to nanoseconds */
static mach_timebase_info_data_t tb_info;

/**
 * Convert mach_absolute_time ticks to microseconds.
 */
static double
ticks_to_us(uint64_t ticks)
{
	return (double)ticks * tb_info.numer / tb_info.denom / 1000.0;
}

/**
 * Create a POINTARRAY with n random 4D points (XYZM).
 * X,Y in typical ECEF range (~6M meters), Z small, M = epoch.
 */
static POINTARRAY *
make_test_points(uint32_t n)
{
	POINTARRAY *pa;
	uint32_t i;

	pa = ptarray_construct(1, 1, n); /* has_z=1, has_m=1 */

	for (i = 0; i < n; i++)
	{
		POINT4D p;
		double angle = (double)i / n * 2.0 * M_PI;
		p.x = 6378137.0 * cos(angle);
		p.y = 6378137.0 * sin(angle);
		p.z = 1000.0 * (i % 100);
		p.m = 2025.0 + (double)i / n; /* Epoch range 2025-2026 */
		ptarray_set_point4d(pa, i, &p);
	}

	return pa;
}

/**
 * Deep-copy a POINTARRAY.
 */
static POINTARRAY *
pa_copy(const POINTARRAY *src)
{
	POINTARRAY *dst = ptarray_construct(
		FLAGS_GET_Z(src->flags), FLAGS_GET_M(src->flags), src->npoints);
	memcpy(dst->serialized_pointlist, src->serialized_pointlist,
	       src->npoints * ptarray_point_size(src));
	return dst;
}

/**
 * Compare function for qsort on doubles.
 */
static int
cmp_double(const void *a, const void *b)
{
	double da = *(const double *)a;
	double db = *(const double *)b;
	if (da < db) return -1;
	if (da > db) return 1;
	return 0;
}

/**
 * Benchmark result for a single backend+operation+size combination.
 */
typedef struct {
	double median_us;
	double min_us;
	double max_us;
	double throughput_mpts; /* million points/sec */
} BenchResult;

/**
 * Compute statistics from an array of measured times.
 */
static void
compute_stats(double *times, int n, uint32_t npoints, BenchResult *out)
{
	/* S5955: i declared here for C89-style consistency with rest of file */
	int i;
	double min_val;
	double max_val;

	qsort(times, n, sizeof(double), cmp_double);

	/* Median */
	if (n % 2 == 0)
		out->median_us = (times[n / 2 - 1] + times[n / 2]) / 2.0;
	else
		out->median_us = times[n / 2];

	/* Min/Max */
	min_val = times[0];
	max_val = times[0];
	for (i = 1; i < n; i++)
	{
		if (times[i] < min_val) min_val = times[i];
		if (times[i] > max_val) max_val = times[i];
	}
	out->min_us = min_val;
	out->max_us = max_val;

	/* Throughput: million points per second */
	if (out->median_us > 0)
		out->throughput_mpts = (double)npoints / out->median_us; /* pts/us = Mpts/s */
	else
		out->throughput_mpts = 0;
}

/* ================================================================
 * Backend operation wrappers
 * ================================================================ */

/**
 * Scalar Z-rotation wrapper.
 */
static void
op_rotate_z_scalar(POINTARRAY *pa, double theta)
{
	ptarray_rotate_z_scalar(pa, theta);
}

/**
 * NEON Z-rotation wrapper. Falls back to scalar if NEON not compiled.
 */
static void
op_rotate_z_neon(POINTARRAY *pa, double theta)
{
#ifdef HAVE_NEON
	ptarray_rotate_z_neon(pa, theta);
#else
	ptarray_rotate_z_scalar(pa, theta);
#endif
}

/**
 * Metal GPU Z-rotation wrapper.
 * Returns 0 if Metal is not available or dispatch fails.
 */
static int
op_rotate_z_metal(POINTARRAY *pa, double theta)
{
#ifdef HAVE_METAL
	return lwgpu_metal_rotate_z(
		(double *)pa->serialized_pointlist,
		ptarray_point_size(pa),
		pa->npoints,
		theta);
#else
	(void)pa;
	(void)theta;
	return 0;
#endif
}

/**
 * Scalar M-epoch Z-rotation wrapper.
 */
static void
op_rotate_z_m_epoch_scalar(POINTARRAY *pa, int direction)
{
	ptarray_rotate_z_m_epoch_scalar(pa, direction);
}

/**
 * NEON M-epoch Z-rotation wrapper.
 */
static void
op_rotate_z_m_epoch_neon(POINTARRAY *pa, int direction)
{
#ifdef HAVE_NEON
	ptarray_rotate_z_m_epoch_neon(pa, direction);
#else
	ptarray_rotate_z_m_epoch_scalar(pa, direction);
#endif
}

/**
 * Metal GPU M-epoch Z-rotation wrapper.
 */
static int
op_rotate_z_m_epoch_metal(POINTARRAY *pa, int direction)
{
#ifdef HAVE_METAL
	int has_z = FLAGS_GET_Z(pa->flags);
	size_t m_offset = has_z ? 3 : 2;
	return lwgpu_metal_rotate_z_m_epoch(
		(double *)pa->serialized_pointlist,
		ptarray_point_size(pa),
		pa->npoints,
		m_offset,
		direction);
#else
	(void)pa;
	(void)direction;
	return 0;
#endif
}

/**
 * Scalar radian conversion wrapper.
 */
static void
op_rad_convert_scalar(POINTARRAY *pa, double scale)
{
	ptarray_rad_convert_scalar(pa, scale);
}

/**
 * NEON radian conversion wrapper.
 */
static void
op_rad_convert_neon(POINTARRAY *pa, double scale)
{
#ifdef HAVE_NEON
	ptarray_rad_convert_neon(pa, scale);
#else
	ptarray_rad_convert_scalar(pa, scale);
#endif
}

/* ================================================================
 * Benchmark runners
 * ================================================================ */

/**
 * Benchmark uniform Z-rotation for a single backend at a given size.
 */
static int
bench_rotate_z(const char *label, uint32_t npoints, BenchResult *result)
{
	POINTARRAY *base;
	double theta = 1.23456;
	double times[BENCH_MEASURE_ITERS];
	int iter;
	int mi;
	int is_metal = (strcmp(label, "metal") == 0);

	base = make_test_points(npoints);

	/* Warmup + measure */
	mi = 0;
	for (iter = 0; iter < BENCH_TOTAL_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(base);
		uint64_t t0 = mach_absolute_time();

		if (strcmp(label, "scalar") == 0)
			op_rotate_z_scalar(tmp, theta);
		else if (strcmp(label, "neon") == 0)
			op_rotate_z_neon(tmp, theta);
		else if (is_metal)
		{
			if (!op_rotate_z_metal(tmp, theta))
			{
				ptarray_free(tmp);
				ptarray_free(base);
				return 0; /* Metal not available */
			}
		}

		uint64_t t1 = mach_absolute_time();

		if (iter >= BENCH_WARMUP_ITERS)
		{
			times[mi] = ticks_to_us(t1 - t0);
			mi++;
		}
		ptarray_free(tmp);
	}

	ptarray_free(base);
	compute_stats(times, BENCH_MEASURE_ITERS, npoints, result);
	return 1;
}

/**
 * Benchmark per-point M-epoch Z-rotation for a single backend.
 */
static int
bench_rotate_z_m_epoch(const char *label, uint32_t npoints, BenchResult *result)
{
	POINTARRAY *base;
	int direction = -1; /* ECI -> ECEF */
	double times[BENCH_MEASURE_ITERS];
	int iter;
	int mi;
	int is_metal = (strcmp(label, "metal") == 0);

	base = make_test_points(npoints);

	mi = 0;
	for (iter = 0; iter < BENCH_TOTAL_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(base);
		uint64_t t0 = mach_absolute_time();

		if (strcmp(label, "scalar") == 0)
			op_rotate_z_m_epoch_scalar(tmp, direction);
		else if (strcmp(label, "neon") == 0)
			op_rotate_z_m_epoch_neon(tmp, direction);
		else if (is_metal)
		{
			if (!op_rotate_z_m_epoch_metal(tmp, direction))
			{
				ptarray_free(tmp);
				ptarray_free(base);
				return 0;
			}
		}

		uint64_t t1 = mach_absolute_time();

		if (iter >= BENCH_WARMUP_ITERS)
		{
			times[mi] = ticks_to_us(t1 - t0);
			mi++;
		}
		ptarray_free(tmp);
	}

	ptarray_free(base);
	compute_stats(times, BENCH_MEASURE_ITERS, npoints, result);
	return 1;
}

/**
 * Benchmark radian conversion for a single backend.
 * (No Metal kernel for rad_convert in the GPU dispatch API,
 *  so we only benchmark scalar and NEON here.)
 */
static int
bench_rad_convert(const char *label, uint32_t npoints, BenchResult *result)
{
	POINTARRAY *base;
	double scale = M_PI / 180.0;
	double times[BENCH_MEASURE_ITERS];
	int iter;
	int mi;

	/* Metal GPU does not expose a rad_convert batch API */
	if (strcmp(label, "metal") == 0)
		return 0;

	base = make_test_points(npoints);

	mi = 0;
	for (iter = 0; iter < BENCH_TOTAL_ITERS; iter++)
	{
		POINTARRAY *tmp = pa_copy(base);
		uint64_t t0 = mach_absolute_time();

		if (strcmp(label, "scalar") == 0)
			op_rad_convert_scalar(tmp, scale);
		else if (strcmp(label, "neon") == 0)
			op_rad_convert_neon(tmp, scale);

		uint64_t t1 = mach_absolute_time();

		if (iter >= BENCH_WARMUP_ITERS)
		{
			times[mi] = ticks_to_us(t1 - t0);
			mi++;
		}
		ptarray_free(tmp);
	}

	ptarray_free(base);
	compute_stats(times, BENCH_MEASURE_ITERS, npoints, result);
	return 1;
}

/* ================================================================
 * Output formatting
 * ================================================================ */

static void
print_table_header(void)
{
	printf("%-22s  %10s  %12s  %10s  %10s  %12s\n",
	       "Operation/Backend", "Points", "Median (us)", "Min (us)",
	       "Max (us)", "Mpts/sec");
	printf("%-22s  %10s  %12s  %10s  %10s  %12s\n",
	       "----------------------", "----------", "------------",
	       "----------", "----------", "------------");
}

static void
print_table_row(const char *op, const char *backend, uint32_t npoints,
		const BenchResult *r)
{
	char label[64];
	snprintf(label, sizeof(label), "%s/%s", op, backend);
	printf("%-22s  %10u  %12.1f  %10.1f  %10.1f  %12.3f\n",
	       label, npoints, r->median_us, r->min_us, r->max_us,
	       r->throughput_mpts);
}

static void
print_csv_header(void)
{
	printf("operation,backend,points,median_us,min_us,max_us,mpts_per_sec\n");
}

static void
print_csv_row(const char *op, const char *backend, uint32_t npoints,
	      const BenchResult *r)
{
	printf("%s,%s,%u,%.1f,%.1f,%.1f,%.3f\n",
	       op, backend, npoints, r->median_us, r->min_us, r->max_us,
	       r->throughput_mpts);
}

/* ================================================================
 * Validation: compare Metal and NEON against scalar reference
 * ================================================================ */

static double
pa_max_diff(const POINTARRAY *a, const POINTARRAY *b)
{
	uint32_t i;
	double max_d = 0.0;

	for (i = 0; i < a->npoints && i < b->npoints; i++)
	{
		POINT4D pa_pt;
		POINT4D pb_pt;
		double dx;
		double dy;
		double dz;

		getPoint4d_p(a, i, &pa_pt);
		getPoint4d_p(b, i, &pb_pt);

		dx = fabs(pa_pt.x - pb_pt.x);
		dy = fabs(pa_pt.y - pb_pt.y);
		dz = fabs(pa_pt.z - pb_pt.z);
		if (dx > max_d) max_d = dx;
		if (dy > max_d) max_d = dy;
		if (dz > max_d) max_d = dz;
	}
	return max_d;
}

/**
 * Validate Metal Z-rotation against scalar reference.
 * Returns updated pass flag.
 */
static int
validate_metal_rotate_z(const POINTARRAY *base, const POINTARRAY *scalar_pa,
			uint32_t n, double theta, int pass)
{
	POINTARRAY *metal_pa = pa_copy(base);
	int rc = op_rotate_z_metal(metal_pa, theta);
	double diff;

	if (!rc)
	{
		printf("  Metal  %7u pts: dispatch failed\n", n);
		ptarray_free(metal_pa);
		return 0;
	}
	diff = pa_max_diff(scalar_pa, metal_pa);
	printf("  Metal  %7u pts: max_diff = %.2e %s\n",
	       n, diff, diff < 1e-10 ? "PASS" : "FAIL");
	if (diff >= 1e-10) pass = 0;
	ptarray_free(metal_pa);
	return pass;
}

static int
run_validation(void)
{
	int pi;
	int pass = 1;
	double theta = 1.23456;
	int metal_ok = 0;

	printf("\n=== Validation: Metal and NEON vs Scalar ===\n\n");

#ifdef HAVE_METAL
	metal_ok = lwgpu_available();
#endif

	printf("--- Uniform Z-rotation ---\n");
	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		uint32_t n = POINT_COUNTS[pi];
		POINTARRAY *base = make_test_points(n);
		POINTARRAY *scalar_pa = pa_copy(base);
		POINTARRAY *neon_pa = pa_copy(base);
		double diff;

		ptarray_rotate_z_scalar(scalar_pa, theta);

		/* NEON check */
#ifdef HAVE_NEON
		ptarray_rotate_z_neon(neon_pa, theta);
		diff = pa_max_diff(scalar_pa, neon_pa);
		printf("  NEON   %7u pts: max_diff = %.2e %s\n",
		       n, diff, diff < 1e-9 ? "PASS" : "FAIL");
		if (diff >= 1e-9) pass = 0;
#else
		printf("  NEON   %7u pts: not compiled\n", n);
#endif

		/* Metal check */
		if (metal_ok)
			pass = validate_metal_rotate_z(base, scalar_pa, n, theta, pass);

		ptarray_free(base);
		ptarray_free(scalar_pa);
		ptarray_free(neon_pa);
	}

	printf("\n--- Radian conversion ---\n");
	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		uint32_t n = POINT_COUNTS[pi];
		POINTARRAY *base = make_test_points(n);
		POINTARRAY *scalar_pa = pa_copy(base);
		POINTARRAY *neon_pa = pa_copy(base);
		double diff;

		ptarray_rad_convert_scalar(scalar_pa, M_PI / 180.0);

#ifdef HAVE_NEON
		ptarray_rad_convert_neon(neon_pa, M_PI / 180.0);
		diff = pa_max_diff(scalar_pa, neon_pa);
		printf("  NEON   %7u pts: max_diff = %.2e %s\n",
		       n, diff, diff < 1e-9 ? "PASS" : "FAIL");
		if (diff >= 1e-9) pass = 0;
#else
		printf("  NEON   %7u pts: not compiled\n", n);
#endif

		ptarray_free(base);
		ptarray_free(scalar_pa);
		ptarray_free(neon_pa);
	}

	printf("\nValidation: %s\n", pass ? "ALL PASSED" : "SOME FAILED");
	return pass ? 0 : 1;
}

/* ================================================================
 * Main
 * ================================================================ */

static void
print_usage(void)
{
	printf("Usage: bench_metal [OPTIONS]\n\n");
	printf("Benchmark CPU scalar vs NEON SIMD vs Metal GPU for ECI/ECEF transforms.\n\n");
	printf("Options:\n");
	printf("  --csv        Output results as CSV instead of table\n");
	printf("  --validate   Compare backends for numerical correctness\n");
	printf("  --help       Show this help\n");
}

/**
 * Print a single benchmark result row in the chosen format.
 */
static void
emit_result(int csv_mode, const char *op_csv, const char *op_table,
	    const char *backend, uint32_t n, const BenchResult *r)
{
	if (csv_mode)
		print_csv_row(op_csv, backend, n, r);
	else
		print_table_row(op_table, backend, n, r);
}

/**
 * Run uniform Z-rotation benchmarks across all point counts.
 */
static void
run_bench_rotate_z(int csv_mode, int metal_ok)
{
	int pi;
	BenchResult result;

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		uint32_t n = POINT_COUNTS[pi];

		bench_rotate_z("scalar", n, &result);
		emit_result(csv_mode, "rotate_z", "rotate_z", "scalar", n, &result);

		bench_rotate_z("neon", n, &result);
		emit_result(csv_mode, "rotate_z", "rotate_z", "neon", n, &result);

		if (metal_ok && bench_rotate_z("metal", n, &result))
			emit_result(csv_mode, "rotate_z", "rotate_z", "metal", n, &result);
	}
}

/**
 * Run per-point M-epoch Z-rotation benchmarks across all point counts.
 */
static void
run_bench_rotate_z_m_epoch(int csv_mode, int metal_ok)
{
	int pi;
	BenchResult result;

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		uint32_t n = POINT_COUNTS[pi];

		bench_rotate_z_m_epoch("scalar", n, &result);
		emit_result(csv_mode, "rotate_z_m_epoch", "rotate_z_m", "scalar", n, &result);

		bench_rotate_z_m_epoch("neon", n, &result);
		emit_result(csv_mode, "rotate_z_m_epoch", "rotate_z_m", "neon", n, &result);

		if (metal_ok && bench_rotate_z_m_epoch("metal", n, &result))
			emit_result(csv_mode, "rotate_z_m_epoch", "rotate_z_m", "metal", n, &result);
	}
}

/**
 * Run radian conversion benchmarks across all point counts.
 */
static void
run_bench_rad_convert(int csv_mode)
{
	int pi;
	BenchResult result;

	for (pi = 0; pi < N_POINT_COUNTS; pi++)
	{
		uint32_t n = POINT_COUNTS[pi];

		bench_rad_convert("scalar", n, &result);
		emit_result(csv_mode, "rad_convert", "rad_convert", "scalar", n, &result);

		bench_rad_convert("neon", n, &result);
		emit_result(csv_mode, "rad_convert", "rad_convert", "neon", n, &result);
	}
}

int
main(int argc, char *argv[])
{
	int csv_mode = 0;
	int validate_mode = 0;
	int help_mode = 0;
	int metal_ok = 0;
	int pi;

	static struct option long_opts[] = {
		{"csv",      no_argument, 0, 'c'},
		{"validate", no_argument, 0, 'v'},
		{"help",     no_argument, 0, 'h'},
		{0, 0, 0, 0}
	};

	int opt;
	while ((opt = getopt_long(argc, argv, "cvh", long_opts, NULL)) != -1)
	{
		switch (opt)
		{
		case 'c': csv_mode = 1; break;
		case 'v': validate_mode = 1; break;
		case 'h': help_mode = 1; break;
		default: print_usage(); return 1;
		}
	}

	if (help_mode)
	{
		print_usage();
		return 0;
	}

	/* Initialize mach timebase */
	mach_timebase_info(&tb_info);

	/* Initialize liblwgeom */
	lwgeom_set_handlers(0, 0, 0, 0, 0);

	/* Initialize acceleration (SIMD dispatch) */
	lwaccel_init();

	/* Initialize GPU if available */
#ifdef HAVE_METAL
	metal_ok = lwgpu_init(LW_GPU_METAL);
#endif

	if (!csv_mode)
	{
		char *features = lwaccel_features_string();
		printf("=== PostGIS CPU vs SIMD vs GPU Benchmark ===\n\n");
		printf("Acceleration: %s\n", features);
		printf("GPU backend:  %s",
		       metal_ok ? "Metal" : "none (Metal not available)");
		if (metal_ok)
		{
#ifdef HAVE_METAL
			printf(" (%s)", lwgpu_metal_device_name());
#endif
		}
		printf("\n");
		printf("Timing:       mach_absolute_time (%.2f ns/tick)\n",
		       (double)tb_info.numer / tb_info.denom);
		printf("Iterations:   %d total, %d warmup, %d measured\n",
		       BENCH_TOTAL_ITERS, BENCH_WARMUP_ITERS, BENCH_MEASURE_ITERS);
		printf("Point counts: ");
		for (pi = 0; pi < N_POINT_COUNTS; pi++)
			printf("%u%s", POINT_COUNTS[pi],
			       pi < N_POINT_COUNTS - 1 ? ", " : "\n");
		lwfree(features);
		printf("\n");
	}

	/* Validation mode */
	if (validate_mode)
		return run_validation();

	/* Print header */
	if (csv_mode)
		print_csv_header();
	else
		print_table_header();

	/* Uniform Z-rotation */
	run_bench_rotate_z(csv_mode, metal_ok);

	if (!csv_mode)
		printf("\n");

	/* Per-point M-epoch Z-rotation */
	run_bench_rotate_z_m_epoch(csv_mode, metal_ok);

	if (!csv_mode)
		printf("\n");

	/* Radian conversion (scalar + NEON only, no GPU kernel) */
	run_bench_rad_convert(csv_mode);

	if (!csv_mode)
		printf("\nDone.\n");

	/* Cleanup GPU */
#ifdef HAVE_METAL
	if (metal_ok)
		lwgpu_metal_shutdown();
#endif

	return 0;
}
