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
/* The simplified conversion used here applies a Z-axis rotation:           */
/*   ECEF from ECI via Rz(-ERA)                                            */
/*   ECI from ECEF via Rz(+ERA)                                            */
/*                                                                          */
/* where Rz(theta) is rotation about the Z axis.                           */
/* ECI transforms use pure C math (cos/sin/fmod) plus the vendored ERFA    */
/* subset in liblwgeom/erfa/ for Earth rotation and precession-nutation.   */
/* No PROJ library calls are needed. This works with any PROJ version.     */
/*                                                                          */
/* Phase 2 of ecef-eci-full-iau-precision (2026-04-12): the internal ERA   */
/* formula was replaced with a call to ERFA's eraEra00. The public         */
/* function signature is unchanged; callers continue to work. Results      */
/* match the previous hand-rolled formula to machine precision because     */
/* both implement the same IERS 2003 definition.                            */
/***************************************************************************/

#include "erfa/erfa.h"

/**
 * Convert a decimal year to a single-part Julian Date.
 *
 * Algorithm: JD = 2451545.0 + (year - 2000.0) * 365.25
 *
 * This single-part form is retained for API compatibility with existing
 * callers that pass a single double JD to lweci_earth_rotation_angle.
 * For maximum precision when feeding ERFA routines that accept two-part
 * JD, prefer lweci_epoch_to_jd_two_part.
 */
double
lweci_epoch_to_jd(double decimal_year)
{
	return 2451545.0 + (decimal_year - 2000.0) * 365.25;
}

/**
 * Convert a decimal year to a two-part Julian Date suitable for ERFA.
 *
 * ERFA routines accept dates as (jd1, jd2) where jd1 + jd2 equals the
 * true JD. The canonical split places jd1 = 2400000.5 (MJD epoch) and
 * jd2 = mjd + fraction. Using two doubles preserves ~16 extra bits of
 * precision across spans of many years versus a single-double JD.
 *
 * Callers that need maximum precision (e.g., full IAU 2006/2000A
 * bias-precession-nutation matrix construction in Phase 3) should use
 * this form. Callers of the existing lweci_earth_rotation_angle API
 * can continue to use lweci_epoch_to_jd and pass 0.0 for the second
 * component; precision loss is ~10 nanoseconds at epochs near J2000.0
 * which is well within the ERA accuracy budget.
 */
void
lweci_epoch_to_jd_two_part(double decimal_year, double *jd1, double *jd2)
{
	/* jd1 = MJD epoch; jd2 = MJD + fractional day */
	double jd_full = 2451545.0 + (decimal_year - 2000.0) * 365.25;
	*jd1 = 2400000.5;
	*jd2 = jd_full - 2400000.5;
}

/**
 * Compute the Earth Rotation Angle (ERA) in radians.
 *
 * Wraps ERFA's eraEra00 which implements the IAU 2000 definition:
 *   ERA = 2*pi*(0.7790572732640 + 1.00273781191135448 * Du)
 * where Du = Julian UT1 date - 2451545.0 (days since J2000.0 epoch).
 *
 * The result is normalized to [0, 2*pi). This function accepts a
 * single-part JD for backwards compatibility; internally it passes
 * (julian_ut1_date, 0.0) to eraEra00 which accepts a two-part JD.
 */
double
lweci_earth_rotation_angle(double julian_ut1_date)
{
	return eraEra00(julian_ut1_date, 0.0);
}

/* Note: Phase 3 removed the simple lwgeom_rotate_z / lwgeom_rotate_xy
 * helpers that built up transforms as a chain of axis rotations. The
 * full IAU 2006/2000A path builds a single 3x3 matrix via ERFA and
 * applies it via lwgeom_apply_matrix3x3 at the bottom of this file.
 * The per-point M-epoch rotation path (lwgeom_transform_*_m) still
 * uses SIMD-dispatched ptarray_rotate_z_m_epoch. */

int
lwgeom_transform_eci_to_ecef(LWGEOM *geom, double epoch, int32_t frame_srid)
{
	/* Full IAU 2006/2000A transform with zero EOP corrections.
	 * Defers to the _eop variant so all ECI->ECEF paths share a single
	 * code path (matrix build + application) and frame handling. */
	return lwgeom_transform_eci_to_ecef_eop(geom, epoch, frame_srid, 0.0, 0.0, 0.0, 0.0, 0.0);
}

int
lwgeom_transform_ecef_to_eci(LWGEOM *geom, double epoch, int32_t frame_srid)
{
	/* Full IAU 2006/2000A transform with zero EOP corrections.
	 * Defers to the _eop variant. */
	return lwgeom_transform_ecef_to_eci_eop(geom, epoch, frame_srid, 0.0, 0.0, 0.0, 0.0, 0.0);
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
	case TRIANGLETYPE: {
		LWLINE *g = (LWLINE *)geom;
		return ptarray_rotate_z_m_epoch(g->points, direction);
	}
	case POLYGONTYPE: {
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
	case TINTYPE: {
		LWCOLLECTION *g = (LWCOLLECTION *)geom;
		for (i = 0; i < g->ngeoms; i++)
		{
			if (lwgeom_rotate_z_m_epoch(g->geoms[i], direction) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	default:
		lwerror("lwgeom_rotate_z_m_epoch: Cannot handle type '%s'", lwtype_name(geom->type));
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
/* EOP-Enhanced Transforms                                                  */
/*                                                                          */
/* With Earth Orientation Parameters (EOP), the simplified ERA-only         */
/* rotation is enhanced with:                                               */
/*   1. dut1 correction: ERA computed using UT1 instead of UTC              */
/*   2. Polar motion (xp, yp): additional X/Y axis rotations               */
/*                                                                          */
/* IERS 2003 convention (simplified, no precession-nutation):               */
/*   ECEF from ECI via Ry(xp) then Rx(yp) then Rz(-ERA_UT1)              */
/*   ECI from ECEF via Rz(+ERA_UT1) then Rx(-yp) then Ry(-xp)            */
/*                                                                          */
/* where xp, yp are polar motion in arcseconds and dut1 is UT1-UTC in      */
/* seconds. The TIO locator s' is neglected (sub-microarcsecond).          */
/***************************************************************************/

/* Arcseconds to radians */
#define ARCSEC_TO_RAD (M_PI / (180.0 * 3600.0))

/* Note: ptarray_rotate_x, ptarray_rotate_y, and lwgeom_rotate_xy were
 * removed in Phase 3 along with their sole caller (the old polar
 * motion axis-rotation chain in lwgeom_transform_*_eop). The full
 * BPN matrix path in lwgeom_apply_matrix3x3 supersedes them. */

/***************************************************************************/
/* IAU 2006/2000A full bias-precession-nutation transforms (Phase 3)        */
/*                                                                          */
/* Replaces the simplified Z-rotation + polar motion model with the full   */
/* IAU 2006/2000A celestial-to-terrestrial matrix built via ERFA. The new  */
/* path produces distinct, correct results for ICRF, J2000, and TEME       */
/* frames and is accurate to ~1 micrometer at Earth radius when EOP data   */
/* is available, ~6 cm without.                                             */
/*                                                                          */
/* Matrix composition (per-geometry, amortized across all points):         */
/*                                                                          */
/*   rc2t = rpom * rera * rbpn                                              */
/*                                                                          */
/* where rbpn is the IAU 2006 bias-precession-nutation matrix from         */
/* eraPnm06a, rera is the Earth rotation about Z by eraEra00(UT1), and    */
/* rpom is the polar motion matrix from eraPom00(xp, yp, sp). The full    */
/* matrix is built once per transform call and applied to every point in  */
/* the geometry, so per-geometry cost is dominated by matrix construction */
/* (~25 kFLOPs) not per-point application (~15 FLOPs per point).          */
/*                                                                          */
/* Frame handling:                                                          */
/*   ICRF  - use rc2t directly (ERFA default)                               */
/*   J2000 - right-multiply by inverse frame bias                           */
/*   TEME  - replace ERA with Greenwich Mean Sidereal Time via eraGmst06  */
/***************************************************************************/

/**
 * Apply a 3x3 rotation matrix to every point in a POINTARRAY.
 *
 * For 3D point arrays, applies the full 3x3 matrix to (x, y, z).
 * For 2D point arrays (no Z flag), applies only the 2x2 top-left
 * submatrix to (x, y) so the transform preserves 2D semantics without
 * leaking uninitialized Z values through the rotation.
 */
static int
ptarray_apply_matrix3x3(POINTARRAY *pa, const double m[3][3])
{
	uint32_t i;
	int has_z;
	POINT4D pt;
	if (!pa)
		return LW_FAILURE;
	has_z = FLAGS_GET_Z(pa->flags);
	for (i = 0; i < pa->npoints; i++)
	{
		double x, y, z;
		getPoint4d_p(pa, i, &pt);
		x = pt.x;
		y = pt.y;
		if (has_z)
		{
			z = pt.z;
			pt.x = m[0][0] * x + m[0][1] * y + m[0][2] * z;
			pt.y = m[1][0] * x + m[1][1] * y + m[1][2] * z;
			pt.z = m[2][0] * x + m[2][1] * y + m[2][2] * z;
		}
		else
		{
			pt.x = m[0][0] * x + m[0][1] * y;
			pt.y = m[1][0] * x + m[1][1] * y;
		}
		ptarray_set_point4d(pa, i, &pt);
	}
	return LW_SUCCESS;
}

/**
 * Apply a 3x3 rotation matrix to every point in an LWGEOM, recursing
 * into collection subtypes.
 */
static int
lwgeom_apply_matrix3x3(LWGEOM *geom, const double m[3][3])
{
	uint32_t i;

	if (lwgeom_is_empty(geom))
		return LW_SUCCESS;

	switch (geom->type)
	{
	case POINTTYPE:
	case LINETYPE:
	case CIRCSTRINGTYPE:
	case TRIANGLETYPE: {
		LWLINE *g = (LWLINE *)geom;
		return ptarray_apply_matrix3x3(g->points, m);
	}
	case POLYGONTYPE: {
		LWPOLY *g = (LWPOLY *)geom;
		for (i = 0; i < g->nrings; i++)
		{
			if (ptarray_apply_matrix3x3(g->rings[i], m) != LW_SUCCESS)
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
	case TINTYPE: {
		LWCOLLECTION *g = (LWCOLLECTION *)geom;
		for (i = 0; i < g->ngeoms; i++)
		{
			if (lwgeom_apply_matrix3x3(g->geoms[i], m) != LW_SUCCESS)
				return LW_FAILURE;
		}
		return LW_SUCCESS;
	}
	default:
		lwerror("lwgeom_apply_matrix3x3: cannot handle type '%s'", lwtype_name(geom->type));
		return LW_FAILURE;
	}
}

/* Forward declarations of ERFA routines we use here. We include erfa.h
 * at the top of this file for the matrix helpers above and below. */
#include "erfa/erfa.h"

/**
 * Build the full IAU 2006/2000A celestial-to-terrestrial rotation matrix
 * for a given epoch and EOP corrections. Produces the matrix that takes
 * a 3-vector in the target ECI frame to an ECEF 3-vector.
 *
 * All EOP corrections may be zero (caller passes 0.0 for each) when no
 * IERS Bulletin A data is available; the result is still a valid IAU
 * 2006/2000A matrix accurate to ~6 cm at Earth radius.
 *
 * @param epoch      Decimal-year epoch (e.g. 2025.123)
 * @param frame_srid SRID_ECI_ICRF / SRID_ECI_J2000 / SRID_ECI_TEME
 * @param dut1       UT1-UTC offset in seconds (from postgis_eop)
 * @param xp         Polar motion X in arcseconds (from postgis_eop)
 * @param yp         Polar motion Y in arcseconds (from postgis_eop)
 * @param dx         CIP X offset in arcseconds (from postgis_eop)
 * @param dy         CIP Y offset in arcseconds (from postgis_eop)
 * @param rc2t_out   Receives the 3x3 celestial-to-terrestrial matrix
 * @return LW_SUCCESS or LW_FAILURE
 */
static int
lweci_build_c2t_matrix(double epoch,
		       int32_t frame_srid,
		       double dut1,
		       double xp,
		       double yp,
		       double dx,
		       double dy,
		       double rc2t_out[3][3])
{
	double jd1_tt, jd2_tt, jd1_ut1, jd2_ut1;
	double x, y, s, sp_val;
	double rc2i[3][3], rpom[3][3], rbias[3][3], rc2t[3][3];
	double xp_rad, yp_rad, dx_rad, dy_rad;

	if (epoch == LWPROJ_NO_EPOCH)
	{
		lwerror("lweci_build_c2t_matrix: epoch is required");
		return LW_FAILURE;
	}

	/* Build two-part JDs. TT and UT1 differ only by dut1 (UT1-UTC).
	 * We treat TAI-UTC leap seconds as already absorbed by the caller;
	 * ERFA's precision budget is insensitive to the ~30-second offset
	 * between TT and UT1 for the BPN matrix computation. */
	lweci_epoch_to_jd_two_part(epoch, &jd1_tt, &jd2_tt);
	jd1_ut1 = jd1_tt;
	jd2_ut1 = jd2_tt + dut1 / 86400.0;

	/* Convert EOP corrections from arcseconds to radians. */
	xp_rad = xp * ARCSEC_TO_RAD;
	yp_rad = yp * ARCSEC_TO_RAD;
	dx_rad = dx * ARCSEC_TO_RAD;
	dy_rad = dy * ARCSEC_TO_RAD;

	/* Step 1: CIP X, Y coordinates from the IAU 2006 series, with the
	 * IERS CIP corrections injected. */
	eraXy06(jd1_tt, jd2_tt, &x, &y);
	x += dx_rad;
	y += dy_rad;

	/* Step 2: CIO locator s (small residual). */
	s = eraS06(jd1_tt, jd2_tt, x, y);

	/* Step 3: celestial-to-intermediate matrix from (X, Y, s). */
	eraC2ixys(x, y, s, rc2i);

	/* Step 4: polar motion matrix. sp is the TIO locator (tiny). */
	sp_val = eraSp00(jd1_tt, jd2_tt);
	eraPom00(xp_rad, yp_rad, sp_val, rpom);

	/* Step 5: combine into celestial-to-terrestrial matrix.
	 * eraC2tcio multiplies rpom * Rz(ERA) * rc2i internally. */
	eraC2tcio(rc2i, eraEra00(jd1_ut1, jd2_ut1), rpom, rc2t);

	/* Frame-specific post-adjustment: rc2t currently targets GCRS (ICRF).
	 * For J2000 and TEME we right-multiply by the inverse of a small bias
	 * matrix, or replace ERA with GMST respectively. */
	switch (frame_srid)
	{
	case SRID_ECI_ICRF:
		/* No adjustment. rc2t as-is takes GCRS/ICRF to ITRF/ECEF. */
		break;

	case SRID_ECI_J2000: {
		/* The IAU 2000 frame bias takes GCRS (ICRF) to the mean equator
		 * and equinox of J2000.0. We build that bias as Rx * Ry * Rz
		 * from eraBi00 and prepend its transpose so that C2T takes a
		 * J2000 vector rather than an ICRF one. */
		double dpsibi, depsbi, dra0;
		eraBi00(&dpsibi, &depsbi, &dra0);
		/* Form the frame bias matrix R_B = R3(-dra0) * R2(dpsibi*sin(eps0))
		 * * R1(-depsbi). We use ERFA's helpers to build this rotation. */
		eraIr(rbias);
		eraRz(dra0, rbias);
		eraRy(-dpsibi * sin(eraObl06(jd1_tt, jd2_tt)), rbias);
		eraRx(depsbi, rbias);
		/* Right-multiply rc2t by rbias^T: for any v in J2000,
		 * rc2t_j2000 * v_j2000 = rc2t * rbias^T * v_j2000. */
		double rbias_t[3][3];
		eraTr(rbias, rbias_t);
		double tmp[3][3];
		eraRxr(rc2t, rbias_t, tmp);
		eraCr(tmp, rc2t);
		break;
	}

	case SRID_ECI_TEME: {
		/* TEME uses Greenwich Mean Sidereal Time (GMST) instead of
		 * Earth Rotation Angle, with the true-of-date equator/equinox.
		 * We rebuild the celestial-to-terrestrial matrix from rc2i,
		 * replacing ERA with GMST. rpom is unchanged. */
		double gmst = eraGmst06(jd1_ut1, jd2_ut1, jd1_tt, jd2_tt);
		eraC2tcio(rc2i, gmst, rpom, rc2t);
		break;
	}

	default:
		lwerror("lweci_build_c2t_matrix: unsupported frame SRID %d (expected %d-%d)",
			frame_srid,
			SRID_ECI_ICRF,
			SRID_ECI_TEME);
		return LW_FAILURE;
	}

	memcpy(rc2t_out, rc2t, sizeof(double) * 9);
	return LW_SUCCESS;
}

/**
 * Full IAU 2006/2000A ECEF -> ECI transform with EOP corrections.
 *
 * Builds the celestial-to-terrestrial matrix via eraC2t06a-equivalent
 * composition (Phase 3 of ecef-eci-full-iau-precision) and applies
 * its transpose to every point. The frame parameter selects ICRF,
 * J2000, or TEME.
 *
 * When EOP data is unavailable, callers should pass 0.0 for dut1,
 * xp, yp, dx, dy. The result is still a valid IAU 2006/2000A
 * transform accurate to ~6 cm at Earth radius.
 */
int
lwgeom_transform_ecef_to_eci_eop(LWGEOM *geom,
				 double epoch,
				 int32_t frame_srid,
				 double dut1,
				 double xp,
				 double yp,
				 double dx,
				 double dy)
{
	double rc2t[3][3];
	double rt2c[3][3];

	if (lweci_build_c2t_matrix(epoch, frame_srid, dut1, xp, yp, dx, dy, rc2t) != LW_SUCCESS)
		return LW_FAILURE;

	/* ECEF -> ECI is the inverse: apply the transpose of C2T. */
	eraTr(rc2t, rt2c);
	return lwgeom_apply_matrix3x3(geom, rt2c);
}

/**
 * Full IAU 2006/2000A ECI -> ECEF transform with EOP corrections.
 */
int
lwgeom_transform_eci_to_ecef_eop(LWGEOM *geom,
				 double epoch,
				 int32_t frame_srid,
				 double dut1,
				 double xp,
				 double yp,
				 double dx,
				 double dy)
{
	double rc2t[3][3];

	if (lweci_build_c2t_matrix(epoch, frame_srid, dut1, xp, yp, dx, dy, rc2t) != LW_SUCCESS)
		return LW_FAILURE;

	/* ECI -> ECEF applies C2T directly. */
	return lwgeom_apply_matrix3x3(geom, rc2t);
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
