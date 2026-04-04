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
 **********************************************************************
 *
 * Copyright 2008 Paul Ramsey <pramsey@cleverelephant.ca>
 *
 **********************************************************************/

#ifndef LWGEOM_GEOS_H_
#define LWGEOM_GEOS_H_ 1

#include "../liblwgeom/lwgeom_geos.h" /* for GEOSGeom */
#include "liblwgeom.h"                /* for GSERIALIZED */
#include "utils/array.h"              /* for ArrayType */

/*
** Public prototypes for GEOS utility functions.
*/

GSERIALIZED *GEOS2POSTGIS(GEOSGeom geom, uint8_t want3d);
GEOSGeometry *POSTGIS2GEOS(const GSERIALIZED *g);
GEOSGeometry **ARRAY2GEOS(ArrayType *array, uint32_t nelems, int *is3d, int *srid);
LWGEOM **ARRAY2LWGEOM(ArrayType *array, uint32_t nelems, int *is3d, int *srid);

Datum geos_intersects(PG_FUNCTION_ARGS);
Datum geos_intersection(PG_FUNCTION_ARGS);
Datum geos_difference(PG_FUNCTION_ARGS);
Datum geos_geomunion(PG_FUNCTION_ARGS);
Datum LWGEOM_area_polygon(PG_FUNCTION_ARGS);
Datum LWGEOM_mindistance2d(PG_FUNCTION_ARGS);
Datum ST_3DDistance(PG_FUNCTION_ARGS);

/* Return NULL on GEOS error
 *
 * Prints error message only if it was not for interruption, in which
 * case we let PostgreSQL deal with the error.
 */
#define HANDLE_GEOS_ERROR(label) \
	{ \
		if (!strstr(lwgeom_geos_errmsg, "InterruptedException")) \
			lwpgerror("%s: %s", (label), lwgeom_geos_errmsg); \
		PG_RETURN_NULL(); \
	}

/*
 * Convert two GSERIALIZED geometries to GEOSGeometry, with error handling.
 * On failure of geom1 conversion, returns NULL via HANDLE_GEOS_ERROR.
 * On failure of geom2 conversion, destroys g1 first, then returns NULL.
 * Both g1 and g2 must be declared as GEOSGeometry* before this macro.
 */
/*
 * Deserialize two geometries from PG function args and declare common
 * variables used by GEOS predicate functions.
 */
#define GEOS_PREDICATE_PREAMBLE(fcinfo) \
	SHARED_GSERIALIZED *shared_geom1 = ToastCacheGetGeometry(fcinfo, 0); \
	SHARED_GSERIALIZED *shared_geom2 = ToastCacheGetGeometry(fcinfo, 1); \
	const GSERIALIZED *geom1 = shared_gserialized_get(shared_geom1); \
	const GSERIALIZED *geom2 = shared_gserialized_get(shared_geom2); \
	int8_t result; \
	GBOX box1; \
	GBOX box2; \
	PrepGeomCache *prep_cache __attribute__((unused))

/*
 * Check that bounding boxes overlap; if not, return early.
 * Used by predicates where non-overlapping boxes imply FALSE.
 */
#define GEOS_BBOX_OVERLAP_CHECK(geom1, geom2, retval) \
	do \
	{ \
		if (gserialized_get_gbox_p(geom1, &box1) && gserialized_get_gbox_p(geom2, &box2)) \
		{ \
			if (gbox_overlaps_2d(&box1, &box2) == LW_FALSE) \
				PG_RETURN_BOOL(retval); \
		} \
	} while (0)

/*
 * Run a GEOS predicate using the prepared geometry cache when available,
 * falling back to the plain (unprepared) version otherwise.
 *
 * prep_fn:   prepared GEOS function, e.g. GEOSPreparedTouches
 * plain_fn:  plain GEOS function, e.g. GEOSTouches
 * label:     error label string, e.g. "GEOSTouches"
 */
#define GEOS_PREP_OR_PLAIN(fcinfo, shared1, shared2, geom1, geom2, prep_fn, plain_fn, label) \
	do \
	{ \
		initGEOS(lwpgnotice, lwgeom_geos_error); \
		prep_cache = GetPrepGeomCache(fcinfo, shared1, shared2); \
		if (prep_cache && prep_cache->prepared_geom) \
		{ \
			GEOSGeometry *g = prep_cache->gcache.argnum == 1 ? POSTGIS2GEOS(geom2) : POSTGIS2GEOS(geom1); \
			if (!g) \
				HANDLE_GEOS_ERROR("Geometry could not be converted to GEOS"); \
			result = prep_fn(prep_cache->prepared_geom, g); \
			GEOSGeom_destroy(g); \
		} \
		else \
		{ \
			GEOSGeometry *g1; \
			GEOSGeometry *g2; \
			POSTGIS2GEOS_BOTH(g1, geom1, g2, geom2); \
			result = plain_fn(g1, g2); \
			GEOSGeom_destroy(g1); \
			GEOSGeom_destroy(g2); \
		} \
		if (result == 2) \
			HANDLE_GEOS_ERROR(label); \
	} while (0)

#define POSTGIS2GEOS_BOTH(g1, geom1, g2, geom2) \
	do \
	{ \
		(g1) = POSTGIS2GEOS(geom1); \
		if (!(g1)) \
			HANDLE_GEOS_ERROR("First argument geometry could not be converted to GEOS"); \
		(g2) = POSTGIS2GEOS(geom2); \
		if (!(g2)) \
		{ \
			GEOSGeom_destroy((g1)); \
			HANDLE_GEOS_ERROR("Second argument geometry could not be converted to GEOS"); \
		} \
	} while (0)

#endif /* LWGEOM_GEOS_H_ */
