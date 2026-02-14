/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * PG-Strom compatible CUDA device function for ECI Z-axis rotation.
 *
 * This file implements GPU device functions for ECI/ECEF coordinate
 * transforms that can be integrated into PG-Strom's gpu_postgis.cu
 * device function registry.
 *
 * PG-Strom device function interface:
 * - Functions are declared with __device__ qualifier
 * - They operate on individual values (PG-Strom handles parallelism)
 * - Input/output follows PostgreSQL datum conventions
 * - Error handling uses PG-Strom's error flag mechanism
 *
 * To contribute to PG-Strom:
 * 1. Add these device functions to heterodb/pg-strom/src/gpu_postgis.cu
 * 2. Register function OIDs in pgstrom_devfunc_catalog[]
 * 3. Add test cases for numerical equivalence with CPU implementation
 *
 **********************************************************************/

#include <math.h>

/*
 * Earth Rotation Angle computation (IERS 2003).
 *
 * ERA = 2*pi*(0.7790572732640 + 1.00273781191135448 * Du)
 * Du = JD_UT1 - 2451545.0
 *
 * This is identical to lweci_earth_rotation_angle() in lwgeom_eci.c.
 */
__device__ double
pgstrom_eci_epoch_to_jd(double decimal_year)
{
	return 2451545.0 + (decimal_year - 2000.0) * 365.25;
}

__device__ double
pgstrom_eci_earth_rotation_angle(double julian_ut1_date)
{
	double Du = julian_ut1_date - 2451545.0;
	double era = 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);

	/* Normalize to [0, 2*pi) */
	era = fmod(era, 2.0 * M_PI);
	if (era < 0.0)
		era += 2.0 * M_PI;

	return era;
}

/*
 * Z-axis rotation device function.
 *
 * Applies Rz(theta) to a 3D point:
 *   x' =  x*cos(theta) + y*sin(theta)
 *   y' = -x*sin(theta) + y*cos(theta)
 *   z' =  z
 *
 * PG-Strom would call this per-row when processing queries like:
 *   SELECT ST_ECEF_To_ECI(geom, epoch, 'ICRF') FROM tracking_data;
 */
__device__ void
pgstrom_eci_rotate_z(double *x, double *y, double theta)
{
	double cos_t = cos(theta);
	double sin_t = sin(theta);
	double x_new = (*x) * cos_t + (*y) * sin_t;
	double y_new = -(*x) * sin_t + (*y) * cos_t;
	*x = x_new;
	*y = y_new;
}

/*
 * Combined ECI-to-ECEF transform device function.
 * Computes ERA from epoch and applies Z-rotation.
 *
 * direction: -1 for ECI->ECEF (Rz(-ERA)), +1 for ECEF->ECI (Rz(+ERA))
 */
__device__ void
pgstrom_eci_transform(double *x, double *y, double epoch, int direction)
{
	double jd = pgstrom_eci_epoch_to_jd(epoch);
	double era = pgstrom_eci_earth_rotation_angle(jd);
	pgstrom_eci_rotate_z(x, y, direction * era);
}

/*
 * Test kernel for validating numerical equivalence with CPU.
 * Applies ECI transform to an array of points and compares with
 * pre-computed expected results.
 *
 * Input:  x[], y[] - point coordinates
 *         epoch[]  - per-point epochs (decimal year)
 *         direction - transform direction
 *         n        - number of points
 * Output: x[], y[] - transformed coordinates (in-place)
 *         max_diff - maximum absolute difference from expected
 */
__global__ void
pgstrom_eci_validate_kernel(double *x, double *y, double *epoch,
			    double *expected_x, double *expected_y,
			    int direction, int n, double *max_diff)
{
	int idx = blockIdx.x * blockDim.x + threadIdx.x;
	if (idx >= n) return;

	double px = x[idx];
	double py = y[idx];
	pgstrom_eci_transform(&px, &py, epoch[idx], direction);
	x[idx] = px;
	y[idx] = py;

	/* Compute difference from expected */
	double dx = fabs(px - expected_x[idx]);
	double dy = fabs(py - expected_y[idx]);
	double d = (dx > dy) ? dx : dy;

	/* Atomic max for reporting (approximate - races are OK for testing) */
	atomicMax((unsigned long long *)max_diff,
		  __double_as_longlong(d));
}
