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
 * Copyright (C) 2011  OpenGeo.org
 *
 **********************************************************************/


#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/elog.h"
#include "utils/guc.h"
#include "libpq/pqsignal.h"

#include <limits.h>

#include "../postgis_config.h"

#include "lwgeom_log.h"
#include "lwgeom_pg.h"
#include "geos_c.h"
#include "liblwgeom.h"
#include "../liblwgeom/lwgeom_accel.h"

/* Valkey batch worker GUC registration */
extern void postgis_valkey_register_gucs(void);

#ifdef HAVE_VALKEY
#include "postmaster/bgworker.h"
extern void postgis_valkey_batch_main(Datum main_arg);
#endif

#ifdef HAVE_LIBPROTOBUF
#include "lwgeom_wagyu.h"
#endif

/*
 * This is required for builds against pgsql
 */
#ifdef PG_MODULE_MAGIC_EXT
PG_MODULE_MAGIC_EXT(
	.name = "postgis",
	.version = POSTGIS_LIB_VERSION
	);
#else
PG_MODULE_MAGIC;
#endif


static void interrupt_geos_callback(void)
{
#ifdef WIN32
	if (UNBLOCKED_SIGNAL_QUEUE())
	{
		pgwin32_dispatch_queued_signals();
	}
#endif
	/*
	 * If PgSQL global flags show interrupt,
	 * flip the pending flag in GEOS
	 * to end current query.
	 */
	if (QueryCancelPending || ProcDiePending)
	{
		GEOS_interruptRequest();
	}
}

static void interrupt_liblwgeom_callback(void)
{
#ifdef WIN32
	if (UNBLOCKED_SIGNAL_QUEUE())
	{
		pgwin32_dispatch_queued_signals();
	}
#endif
	/*
	 * If PgSQL global flags show interrupt,
	 * flip the pending flag in liblwgeom
	 * to end current query.
	 */
	if (QueryCancelPending || ProcDiePending)
	{
		lwgeom_request_interrupt();
	}
}

/*
* Pass proj error message out via the PostgreSQL logging
* system instead of letting them default into the
* stderr.
*/
#if POSTGIS_PROJ_VERSION > 60000
#include "proj.h"

static void
pjLogFunction(void* data, int logLevel, const char* message)
{
	elog(DEBUG1, "libproj threw an exception (%d): %s", logLevel, message);
}
#endif

/* GUC variables for hardware acceleration.
 * 0 = auto-calibrate at first GPU use (default). */
static int postgis_gpu_dispatch_threshold = 0;

static void
postgis_gpu_threshold_assign(int newval, void *extra)
{
	lwaccel_set_gpu_threshold((uint32_t)newval);
}

/*
 * Module load callback
 */
void _PG_init(void);
void
_PG_init(void)
{
	/*
	 * Hook up interrupt checking to call back here
	 * and examine the PgSQL interrupt state variables
	 */
	GEOS_interruptRegisterCallback(interrupt_geos_callback);
	lwgeom_register_interrupt_callback(interrupt_liblwgeom_callback);

	/* Install PostgreSQL error/memory handlers */
	pg_install_lwgeom_handlers();

#if POSTGIS_PROJ_VERSION > 60000
	/* Pass proj messages through the pgsql error handler */
	proj_log_func(NULL, NULL, pjLogFunction);
#endif

	/* Register Valkey batch GUC parameters */
	postgis_valkey_register_gucs();

#ifdef HAVE_VALKEY
	/* Register Valkey GPU batch background worker */
	{
		BackgroundWorker worker;
		memset(&worker, 0, sizeof(worker));
		snprintf(worker.bgw_name, BGW_MAXLEN, "PostGIS Valkey GPU batch worker");
		snprintf(worker.bgw_type, BGW_MAXLEN, "postgis_valkey_batch");
		worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
		worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
		worker.bgw_restart_time = 60; /* restart after 60s on crash */
		snprintf(worker.bgw_library_name, BGW_MAXLEN, "postgis-" POSTGIS_MAJOR_VERSION);
		snprintf(worker.bgw_function_name, BGW_MAXLEN, "postgis_valkey_batch_main");
		worker.bgw_main_arg = (Datum) 0;
		RegisterBackgroundWorker(&worker);
	}
#endif

	/* GPU dispatch threshold GUC */
	DefineCustomIntVariable(
		"postgis.gpu_dispatch_threshold",
		"Minimum POINTARRAY size for GPU dispatch (points).",
		"0 = auto-calibrate at first GPU use (default). "
		"Set to a specific value to override.",
		&postgis_gpu_dispatch_threshold,
		0,        /* default: auto-calibrate */
		0,        /* min: 0 = auto */
		INT_MAX,  /* max */
		PGC_USERSET,
		0,
		NULL,
		postgis_gpu_threshold_assign,
		NULL);
}

/*
 * Module unload callback
 */
void _PG_fini(void);
void
_PG_fini(void)
{
	elog(NOTICE, "Goodbye from PostGIS %s", POSTGIS_VERSION);
}


