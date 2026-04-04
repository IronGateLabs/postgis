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
#include "liblwgeom.h" /* for GSERIALIZED */
#include "utils/array.h" /* for ArrayType */

/*
** Public prototypes for GEOS utility functions.
*/

GSERIALIZED *GEOS2POSTGIS(GEOSGeom geom, uint8_t want3d);
GEOSGeometry *POSTGIS2GEOS(const GSERIALIZED *g);
GEOSGeometry** ARRAY2GEOS(ArrayType* array, uint32_t nelems, int* is3d, int* srid);
LWGEOM** ARRAY2LWGEOM(ArrayType* array, uint32_t nelems, int* is3d, int* srid);

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
#define POSTGIS2GEOS_BOTH(g1, geom1, g2, geom2) \
	do { \
		(g1) = POSTGIS2GEOS(geom1); \
		if (!(g1)) \
			HANDLE_GEOS_ERROR( \
				"First argument geometry could not be converted to GEOS"); \
		(g2) = POSTGIS2GEOS(geom2); \
		if (!(g2)) \
		{ \
			GEOSGeom_destroy((g1)); \
			HANDLE_GEOS_ERROR( \
				"Second argument geometry could not be converted to GEOS"); \
		} \
	} while (0)

#endif /* LWGEOM_GEOS_H_ */
