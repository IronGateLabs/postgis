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
 * Shared arc-tracking structures and helpers used by lwpsurface.c
 * and lwtin.c to test whether a surface is topologically closed.
 *
 **********************************************************************/

#ifndef LW_SURFACE_ARCS_H
#define LW_SURFACE_ARCS_H

#include "liblwgeom_internal.h"

/*
 * An arc (edge) connecting two 3-D vertices, together with the face
 * it belongs to and a counter that tracks how many faces share it.
 */
struct surface_arc
{
	double ax;
	double ay;
	double az;
	double bx;
	double by;
	double bz;
	uint32_t cnt;
	uint32_t face;
};
typedef struct surface_arc *surface_arcs;

/*
 * Search for a matching arc in the arc list.  If found, increment its
 * count and return 1.  If the count exceeds 2, return -1 to signal an
 * invalid surface.  If not found, return 0.
 */
static inline int
surface_arc_find_and_update(surface_arcs arcs, uint32_t carc,
                            const POINT4D *pa, const POINT4D *pb,
                            uint32_t face)
{
	uint32_t k;
	for (k = 0; k < carc; k++)
	{
		if (arcs[k].ax == pa->x && arcs[k].ay == pa->y &&
		    arcs[k].az == pa->z && arcs[k].bx == pb->x &&
		    arcs[k].by == pb->y && arcs[k].bz == pb->z &&
		    arcs[k].face != face)
		{
			arcs[k].cnt++;
			if (arcs[k].cnt > 2)
				return -1;
			return 1;
		}
	}
	return 0;
}

/*
 * Record a new arc entry at position *carc and advance the counter.
 */
static inline void
surface_arc_add(surface_arcs arcs, uint32_t *carc,
                const POINT4D *pa, const POINT4D *pb, uint32_t face)
{
	uint32_t idx = *carc;
	arcs[idx].cnt  = 1;
	arcs[idx].face = face;
	arcs[idx].ax = pa->x;
	arcs[idx].ay = pa->y;
	arcs[idx].az = pa->z;
	arcs[idx].bx = pb->x;
	arcs[idx].by = pb->y;
	arcs[idx].bz = pb->z;
	(*carc)++;
}

/*
 * Verify that every arc in the list is shared by exactly two faces.
 * Returns 1 (closed) or 0 (not closed).  Frees the arc array in
 * either case.
 */
static inline int
surface_arcs_check_closed(surface_arcs arcs, uint32_t carc,
                           uint32_t ngeoms)
{
	uint32_t k;
	for (k = 0; k < carc; k++)
	{
		if (arcs[k].cnt != 2)
		{
			lwfree(arcs);
			return 0;
		}
	}
	lwfree(arcs);

	/* Invalid surface if fewer unique arcs than faces */
	if (carc < ngeoms)
		return 0;

	return 1;
}

#endif /* LW_SURFACE_ARCS_H */
