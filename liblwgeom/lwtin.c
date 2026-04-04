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
 * Copyright (C) 2001-2006 Refractions Research Inc.
 *
 **********************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "liblwgeom_internal.h"
#include "lwgeom_log.h"
#include "lw_surface_arcs.h"


LWTIN* lwtin_add_lwtriangle(LWTIN *mobj, const LWTRIANGLE *obj)
{
	return (LWTIN*)lwcollection_add_lwgeom((LWCOLLECTION*)mobj, (LWGEOM*)obj);
}

void lwtin_free(LWTIN *tin)
{
	uint32_t i;
	if ( ! tin ) return;
	if ( tin->bbox )
		lwfree(tin->bbox);

	for ( i = 0; i < tin->ngeoms; i++ )
		if ( tin->geoms && tin->geoms[i] )
			lwtriangle_free(tin->geoms[i]);

	if ( tin->geoms )
		lwfree(tin->geoms);

	lwfree(tin);
}


void printLWTIN(LWTIN *tin)
{
	uint32_t i;
	LWTRIANGLE *triangle;

	if (tin->type != TINTYPE)
		lwerror("printLWTIN called with something else than a TIN");

	lwnotice("LWTIN {");
	lwnotice("    ndims = %i", (int)FLAGS_NDIMS(tin->flags));
	lwnotice("    SRID = %i", (int)tin->srid);
	lwnotice("    ngeoms = %i", (int)tin->ngeoms);

	for (i=0; i<tin->ngeoms; i++)
	{
		triangle = (LWTRIANGLE *) tin->geoms[i];
		printPA(triangle->points);
	}
	lwnotice("}");
}


/*
 * TODO rewrite all this stuff to be based on a truly topological model
 */

/* We supposed that the geometry is valid
   we could have wrong result if not */
int lwtin_is_closed(const LWTIN *tin)
{
	uint32_t i;
	uint32_t j;
	uint32_t narcs;
	uint32_t carc;
	surface_arcs arcs;
	POINT4D pa;
	POINT4D pb;
	LWTRIANGLE *patch;

	/* If surface is not 3D, it's can't be closed */
	if (!FLAGS_GET_Z(tin->flags)) return 0;

	/* Max theoretical arcs number if no one is shared ... */
	narcs = 3 * tin->ngeoms;

	arcs = lwalloc(sizeof(struct surface_arc) * narcs);
	for (i=0, carc=0; i < tin->ngeoms ; i++)
	{

		patch = (LWTRIANGLE *) tin->geoms[i];
		for (j=0; j < 3 ; j++)
		{
			int rc;

			getPoint4d_p(patch->points, j,   &pa);
			getPoint4d_p(patch->points, j+1, &pb);

			/* Make sure to order the 'lower' point first */
			if ( (pa.x > pb.x) ||
			        (pa.x == pb.x && pa.y > pb.y) ||
			        (pa.x == pb.x && pa.y == pb.y && pa.z > pb.z) )
			{
				pa = pb;
				getPoint4d_p(patch->points, j, &pb);
			}

			rc = surface_arc_find_and_update(arcs, carc, &pa, &pb, i);
			if (rc < 0)
			{
				lwfree(arcs);
				return 0;
			}

			if (!rc)
			{
				surface_arc_add(arcs, &carc, &pa, &pb, i);

				if (carc > narcs)
				{
					lwfree(arcs);
					return 0;
				}
			}
		}
	}

	return surface_arcs_check_closed(arcs, carc, tin->ngeoms);
}
