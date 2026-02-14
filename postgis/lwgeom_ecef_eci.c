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

/**
 * @file lwgeom_ecef_eci.c
 *
 * PostgreSQL SQL-callable wrappers for ECEF/ECI coordinate transforms
 * and ECEF coordinate accessors.
 *
 * Frame conversion:
 *   ST_ECEFToECI(geometry, timestamptz, text) -> geometry
 *   ST_ECIToECEF(geometry, timestamptz, text) -> geometry
 *
 * ECEF coordinate accessors:
 *   ST_ECEF_X(geometry) -> float8
 *   ST_ECEF_Y(geometry) -> float8
 *   ST_ECEF_Z(geometry) -> float8
 */

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/timestamp.h"

#include "../postgis_config.h"
#include "liblwgeom.h"
#include "lwgeom_pg.h"
#include "../liblwgeom/lwgeom_accel.h"

/* SRID for ECEF (WGS 84 geocentric) */
#define SRID_ECEF 4978

/* Seconds per Julian year (365.25 days) */
#define SECS_PER_JULIAN_YEAR (365.25 * 86400.0)

/* J2000.0 epoch as Unix timestamp: 2000-01-01T12:00:00Z */
#define J2000_UNIX_EPOCH 946728000.0

/* PostgreSQL epoch (2000-01-01 00:00:00 UTC) as Unix seconds offset from Unix epoch */
#define PG_EPOCH_UNIX_OFFSET 946684800.0

/**
 * Convert a PostgreSQL TimestampTz to a decimal year suitable for
 * lwgeom_transform_ecef_to_eci() / lwgeom_transform_eci_to_ecef().
 *
 * TimestampTz is microseconds since the PostgreSQL epoch (2000-01-01 00:00:00 UTC).
 * We convert to Unix seconds, then to decimal year:
 *   decimal_year = 2000.0 + (unix_seconds - J2000_UNIX_EPOCH) / SECS_PER_JULIAN_YEAR
 *
 * Note: J2000.0 is 2000-01-01T12:00:00Z (noon), which is 946728000 Unix seconds.
 */
static double
timestamptz_to_decimal_year(TimestampTz ts)
{
	double unix_seconds;

	/* TimestampTz is microseconds since PG epoch (2000-01-01 00:00:00 UTC) */
	unix_seconds = PG_EPOCH_UNIX_OFFSET + ((double)ts / 1000000.0);

	return 2000.0 + (unix_seconds - J2000_UNIX_EPOCH) / SECS_PER_JULIAN_YEAR;
}

/**
 * Parse an ECI frame name string and return the corresponding SRID.
 * Recognized frames (case-insensitive): ICRF, J2000, TEME.
 * Returns 0 on unrecognized frame.
 */
static int32_t
parse_eci_frame(const char *frame)
{
	if (pg_strcasecmp(frame, "ICRF") == 0)
		return SRID_ECI_ICRF;
	if (pg_strcasecmp(frame, "J2000") == 0)
		return SRID_ECI_J2000;
	if (pg_strcasecmp(frame, "TEME") == 0)
		return SRID_ECI_TEME;
	return 0;
}

/* ----------------------------------------------------------------
 * ST_ECEFToECI(geometry, timestamptz, text) -> geometry
 *
 * Transform a geometry from ECEF (SRID 4978) to an ECI frame.
 * The third argument specifies the target frame: 'ICRF', 'J2000', or 'TEME'.
 * ---------------------------------------------------------------- */
PG_FUNCTION_INFO_V1(postgis_ecef_to_eci);
Datum postgis_ecef_to_eci(PG_FUNCTION_ARGS)
{
	GSERIALIZED *geom_in;
	LWGEOM *lwgeom;
	GSERIALIZED *result;
	TimestampTz ts;
	text *frame_text;
	char *frame_str;
	int32_t srid_from;
	int32_t srid_to;
	double epoch;

	geom_in = PG_GETARG_GSERIALIZED_P_COPY(0);
	ts = PG_GETARG_TIMESTAMPTZ(1);
	frame_text = PG_GETARG_TEXT_P(2);

	/* Validate input SRID */
	srid_from = gserialized_get_srid(geom_in);
	if (srid_from != SRID_ECEF)
		elog(ERROR, "ST_ECEFToECI: input geometry must have SRID %d (ECEF/WGS 84 geocentric), got %d",
			 SRID_ECEF, srid_from);

	/* Parse target frame */
	frame_str = text_to_cstring(frame_text);
	srid_to = parse_eci_frame(frame_str);
	if (srid_to == 0)
		elog(ERROR, "ST_ECEFToECI: unrecognized ECI frame '%s' (expected ICRF, J2000, or TEME)",
			 frame_str);

	/* Convert timestamp to decimal year */
	epoch = timestamptz_to_decimal_year(ts);

	/* Deserialize, transform, set output SRID */
	lwgeom = lwgeom_from_gserialized(geom_in);
	if (lwgeom_transform_ecef_to_eci(lwgeom, epoch) != LW_SUCCESS)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(geom_in, 0);
		elog(ERROR, "ST_ECEFToECI: ECEF-to-ECI transformation failed");
	}

	lwgeom->srid = srid_to;
	if (lwgeom->bbox)
		lwgeom_refresh_bbox(lwgeom);

	result = geometry_serialize(lwgeom);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(geom_in, 0);

	PG_RETURN_POINTER(result);
}

/* ----------------------------------------------------------------
 * ST_ECIToECEF(geometry, timestamptz, text) -> geometry
 *
 * Transform a geometry from an ECI frame back to ECEF (SRID 4978).
 * The third argument specifies the source ECI frame for documentation
 * purposes and SRID validation: 'ICRF', 'J2000', or 'TEME'.
 * ---------------------------------------------------------------- */
PG_FUNCTION_INFO_V1(postgis_eci_to_ecef);
Datum postgis_eci_to_ecef(PG_FUNCTION_ARGS)
{
	GSERIALIZED *geom_in;
	LWGEOM *lwgeom;
	GSERIALIZED *result;
	TimestampTz ts;
	text *frame_text;
	char *frame_str;
	int32_t srid_from;
	int32_t srid_expected;
	double epoch;

	geom_in = PG_GETARG_GSERIALIZED_P_COPY(0);
	ts = PG_GETARG_TIMESTAMPTZ(1);
	frame_text = PG_GETARG_TEXT_P(2);

	/* Validate input SRID is an ECI SRID */
	srid_from = gserialized_get_srid(geom_in);
	if (!SRID_IS_ECI(srid_from))
		elog(ERROR, "ST_ECIToECEF: input geometry must have an ECI SRID (%d-%d), got %d",
			 SRID_ECI_BASE, SRID_ECI_MAX, srid_from);

	/* Parse frame and cross-check against SRID */
	frame_str = text_to_cstring(frame_text);
	srid_expected = parse_eci_frame(frame_str);
	if (srid_expected == 0)
		elog(ERROR, "ST_ECIToECEF: unrecognized ECI frame '%s' (expected ICRF, J2000, or TEME)",
			 frame_str);
	if (srid_from != srid_expected)
		elog(ERROR, "ST_ECIToECEF: geometry SRID %d does not match frame '%s' (SRID %d)",
			 srid_from, frame_str, srid_expected);

	/* Convert timestamp to decimal year */
	epoch = timestamptz_to_decimal_year(ts);

	/* Deserialize, transform, set output SRID to ECEF */
	lwgeom = lwgeom_from_gserialized(geom_in);
	if (lwgeom_transform_eci_to_ecef(lwgeom, epoch) != LW_SUCCESS)
	{
		lwgeom_free(lwgeom);
		PG_FREE_IF_COPY(geom_in, 0);
		elog(ERROR, "ST_ECIToECEF: ECI-to-ECEF transformation failed");
	}

	lwgeom->srid = SRID_ECEF;
	if (lwgeom->bbox)
		lwgeom_refresh_bbox(lwgeom);

	result = geometry_serialize(lwgeom);
	lwgeom_free(lwgeom);
	PG_FREE_IF_COPY(geom_in, 0);

	PG_RETURN_POINTER(result);
}

/* ----------------------------------------------------------------
 * ECEF Coordinate Accessors
 *
 * ST_ECEF_X(geometry) -> float8
 * ST_ECEF_Y(geometry) -> float8
 * ST_ECEF_Z(geometry) -> float8
 *
 * These follow the same pattern as ST_X/ST_Y/ST_Z in lwgeom_ogc.c
 * but additionally validate that the input SRID is 4978 (ECEF).
 * ---------------------------------------------------------------- */

PG_FUNCTION_INFO_V1(postgis_ecef_x);
Datum postgis_ecef_x(PG_FUNCTION_ARGS)
{
	GSERIALIZED *geom = PG_GETARG_GSERIALIZED_P(0);
	POINT4D pt;

	if (gserialized_get_type(geom) != POINTTYPE)
		lwpgerror("Argument to ST_ECEF_X() must have type POINT");

	if (gserialized_get_srid(geom) != SRID_ECEF)
		lwpgerror("Argument to ST_ECEF_X() must have SRID %d (ECEF/WGS 84 geocentric)", SRID_ECEF);

	if (gserialized_peek_first_point(geom, &pt) == LW_FAILURE)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_FLOAT8(pt.x);
}

PG_FUNCTION_INFO_V1(postgis_ecef_y);
Datum postgis_ecef_y(PG_FUNCTION_ARGS)
{
	GSERIALIZED *geom = PG_GETARG_GSERIALIZED_P(0);
	POINT4D pt;

	if (gserialized_get_type(geom) != POINTTYPE)
		lwpgerror("Argument to ST_ECEF_Y() must have type POINT");

	if (gserialized_get_srid(geom) != SRID_ECEF)
		lwpgerror("Argument to ST_ECEF_Y() must have SRID %d (ECEF/WGS 84 geocentric)", SRID_ECEF);

	if (gserialized_peek_first_point(geom, &pt) == LW_FAILURE)
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_FLOAT8(pt.y);
}

PG_FUNCTION_INFO_V1(postgis_ecef_z);
Datum postgis_ecef_z(PG_FUNCTION_ARGS)
{
	GSERIALIZED *geom = PG_GETARG_GSERIALIZED_P(0);
	POINT4D pt;

	if (gserialized_get_type(geom) != POINTTYPE)
		lwpgerror("Argument to ST_ECEF_Z() must have type POINT");

	if (gserialized_get_srid(geom) != SRID_ECEF)
		lwpgerror("Argument to ST_ECEF_Z() must have SRID %d (ECEF/WGS 84 geocentric)", SRID_ECEF);

	if (!gserialized_has_z(geom) || (gserialized_peek_first_point(geom, &pt) == LW_FAILURE))
	{
		PG_RETURN_NULL();
	}
	PG_RETURN_FLOAT8(pt.z);
}

/**
 * SQL: postgis_accel_features()
 *
 * Returns a text string describing detected hardware acceleration features:
 * SIMD instruction set, GPU backend, and Valkey batching status.
 */
PG_FUNCTION_INFO_V1(postgis_accel_features);
Datum
postgis_accel_features(PG_FUNCTION_ARGS)
{
	char *features = lwaccel_features_string();
	text *result;

	if (!features)
		PG_RETURN_NULL();

	result = cstring_to_text(features);
	lwfree(features);
	PG_RETURN_TEXT_P(result);
}
