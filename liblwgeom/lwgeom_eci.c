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

#include "../postgis_config.h"
#include "liblwgeom_internal.h"
#include "lwgeom_accel.h"
#include "lwgeom_log.h"
#include <math.h>
#include <float.h>

/***************************************************************************/
/* ECI (Earth-Centered Inertial) coordinate system support                  */
/*                                                                          */
/* ECI frames (ICRF/J2000/TEME) are non-rotating reference frames with     */
/* origin at Earth's center. Converting between ECI and ECEF requires       */
/* applying the Earth Rotation Angle (ERA) at a given epoch.               */
/*                                                                          */
/* The simplified conversion used here:                                     */
/*   ECEF = Rz(-ERA) * ECI                                                 */
/*   ECI  = Rz(+ERA) * ECEF                                               */
/*                                                                          */
/* where Rz(theta) is rotation about the Z axis.                           */
/* This is the IERS 2003 ERA-based rotation, suitable for most             */
/* applications. Full IAU 2006/2000A precession-nutation is not included.  */
/*                                                                          */
/* PROJ independence: All ECI transforms use pure C math (cos/sin/fmod).   */
/* No PROJ library calls are needed. This works with any PROJ version.     */
/***************************************************************************/

/**
 * Convert a decimal year to Julian Date.
 * Algorithm: JD = 2451545.0 + (year - 2000.0) * 365.25
 * This is approximate but sufficient for ERA computation.
 */
double
lweci_epoch_to_jd(double decimal_year)
{
	return 2451545.0 + (decimal_year - 2000.0) * 365.25;
}

/**
 * Compute the Earth Rotation Angle (ERA) in radians.
 *
 * IERS 2003 definition:
 *   ERA = 2*pi*(0.7790572732640 + 1.00273781191135448 * Du)
 * where Du = Julian UT1 date - 2451545.0 (days since J2000.0 epoch)
 *
 * The result is normalized to [0, 2*pi).
 */
double
lweci_earth_rotation_angle(double julian_ut1_date)
{
	double Du = julian_ut1_date - 2451545.0;
	double era = 2.0 * M_PI * (0.7790572732640 + 1.00273781191135448 * Du);

	/* Normalize to [0, 2*pi) */
	era = fmod(era, 2.0 * M_PI);
	if (era < 0.0)
		era += 2.0 * M_PI;

	return era;
}

/**
 * Apply a rotation about the Z axis to a POINT4D.
 *
 * Rz(theta):
 *   x' =  x*cos(theta) + y*sin(theta)
 *   y' = -x*sin(theta) + y*cos(theta)
 *   z' =  z
 */
/* Single-point rotate_z moved to lwgeom_accel.c as ptarray_rotate_z_scalar */

/**
 * Apply Z-axis rotation to all points in a POINTARRAY.
 * Dispatches to SIMD-accelerated implementation when available.
 */
static int
ptarray_rotate_z(POINTARRAY *pa, double theta)
{
	const LW_ACCEL_DISPATCH *accel = lwaccel_get();
	return accel->rotate_z(pa, theta);
}

/**
 * Apply Z-axis rotation to all points in a geometry.
 */
static int
lwgeom_rotate_z(LWGEOM *geom, double theta)
{
	uint32_t i;

	if (lwgeom_is_empty(geom))
		return LW_SUCCESS;

	switch (geom->type)
	{
	case POINTTYPE:
	case LINETYPE:
	case CIRCSTRINGTYPE:
	case TRIANGLETYPE:
	{
		LWLINE *g = (LWLINE *)geom;
		return ptarray_rotate_z(g->points, theta);
	}
	case POLYGONTYPE:
	{
		LWPOLY *g = (LWPOLY *)geom;
		for (i = 0; i < g->nrings; i++)
		{
			if (ptarray_rotate_z(g->rings[i], theta) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COLLECTIONTYPE:
	case COMPOUNDTYPE:
	case CURVEPOLYTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	{
		LWCOLLECTION *g = (LWCOLLECTION *)geom;
		for (i = 0; i < g->ngeoms; i++)
		{
			if (lwgeom_rotate_z(g->geoms[i], theta) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	default:
		lwerror("lwgeom_rotate_z: Cannot handle type '%s'",
			lwtype_name(geom->type));
		return LW_FAILURE;
	}
}

int
lwgeom_transform_eci_to_ecef(LWGEOM *geom, double epoch)
{
	double jd, era;

	if (epoch == LWPROJ_NO_EPOCH)
	{
		lwerror("lwgeom_transform_eci_to_ecef: epoch is required for ECI-to-ECEF conversion");
		return LW_FAILURE;
	}

	/* Convert decimal year to Julian Date, then compute ERA */
	jd = lweci_epoch_to_jd(epoch);
	era = lweci_earth_rotation_angle(jd);

	/* ECEF = Rz(-ERA) * ECI */
	return lwgeom_rotate_z(geom, -era);
}

int
lwgeom_transform_ecef_to_eci(LWGEOM *geom, double epoch)
{
	double jd, era;

	if (epoch == LWPROJ_NO_EPOCH)
	{
		lwerror("lwgeom_transform_ecef_to_eci: epoch is required for ECEF-to-ECI conversion");
		return LW_FAILURE;
	}

	/* Convert decimal year to Julian Date, then compute ERA */
	jd = lweci_epoch_to_jd(epoch);
	era = lweci_earth_rotation_angle(jd);

	/* ECI = Rz(+ERA) * ECEF */
	return lwgeom_rotate_z(geom, era);
}

/**
 * Per-point M-epoch rotation for a POINTARRAY.
 * Each point's M value is used as the epoch (decimal year).
 * direction: -1 for ECI->ECEF (Rz(-ERA)), +1 for ECEF->ECI (Rz(+ERA))
 * Dispatches to SIMD-accelerated implementation when available.
 */
static int
ptarray_rotate_z_m_epoch(POINTARRAY *pa, int direction)
{
	const LW_ACCEL_DISPATCH *accel = lwaccel_get();
	return accel->rotate_z_m_epoch(pa, direction);
}

/**
 * Apply per-point M-epoch rotation to all points in a geometry.
 * direction: -1 for ECI->ECEF, +1 for ECEF->ECI
 */
static int
lwgeom_rotate_z_m_epoch(LWGEOM *geom, int direction)
{
	uint32_t i;

	if (lwgeom_is_empty(geom))
		return LW_SUCCESS;

	switch (geom->type)
	{
	case POINTTYPE:
	case LINETYPE:
	case CIRCSTRINGTYPE:
	case TRIANGLETYPE:
	{
		LWLINE *g = (LWLINE *)geom;
		return ptarray_rotate_z_m_epoch(g->points, direction);
	}
	case POLYGONTYPE:
	{
		LWPOLY *g = (LWPOLY *)geom;
		for (i = 0; i < g->nrings; i++)
		{
			if (ptarray_rotate_z_m_epoch(g->rings[i], direction) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	case MULTIPOINTTYPE:
	case MULTILINETYPE:
	case MULTIPOLYGONTYPE:
	case COLLECTIONTYPE:
	case COMPOUNDTYPE:
	case CURVEPOLYTYPE:
	case MULTICURVETYPE:
	case MULTISURFACETYPE:
	case POLYHEDRALSURFACETYPE:
	case TINTYPE:
	{
		LWCOLLECTION *g = (LWCOLLECTION *)geom;
		for (i = 0; i < g->ngeoms; i++)
		{
			if (lwgeom_rotate_z_m_epoch(g->geoms[i], direction) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	default:
		lwerror("lwgeom_rotate_z_m_epoch: Cannot handle type '%s'",
			lwtype_name(geom->type));
		return LW_FAILURE;
	}
}

int
lwgeom_transform_eci_to_ecef_m(LWGEOM *geom)
{
	/* ECEF = Rz(-ERA) * ECI, per-point using M as epoch */
	return lwgeom_rotate_z_m_epoch(geom, -1);
}

int
lwgeom_transform_ecef_to_eci_m(LWGEOM *geom)
{
	/* ECI = Rz(+ERA) * ECEF, per-point using M as epoch */
	return lwgeom_rotate_z_m_epoch(geom, +1);
}

/***************************************************************************/
/* ECI Bounding Box Computation                                             */
/*                                                                          */
/* ECI coordinates are 3D Cartesian (meters from Earth center) like ECEF.  */
/* The M coordinate is used to track the temporal (epoch) extent.           */
/* GBOX x/y/z ranges hold the spatial extent; mmin/mmax hold epoch range.  */
/***************************************************************************/

/**
 * Compute bounding box for ECI geometry with temporal extent.
 *
 * For ECI geometries:
 *   - x/y/z: Cartesian coordinate ranges (meters)
 *   - m: Epoch range (decimal years) if M values are present
 *
 * This uses standard Cartesian GBOX computation (no unit-sphere normalization)
 * since ECI coordinates are metric 3D Cartesian.
 *
 * Returns LW_SUCCESS on success, LW_FAILURE on error.
 */
int
lwgeom_eci_compute_gbox(const LWGEOM *geom, GBOX *gbox)
{
	if (!geom || !gbox)
		return LW_FAILURE;

	if (lwgeom_is_empty(geom))
		return LW_FAILURE;

	/* ECI uses standard Cartesian bounding box - no geodetic normalization.
	 * Set flags: has_z always (ECI is 3D), has_m if geometry has M values. */
	gbox->flags = geom->flags;

	/* Use the standard Cartesian GBOX computation.
	 * This correctly computes min/max for x, y, z, and m coordinates. */
	return lwgeom_calculate_gbox_cartesian(geom, gbox);
}
