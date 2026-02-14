/**********************************************************************
 *
 * PostGIS - Spatial Types for PostgreSQL
 * http://postgis.net
 *
 * Valkey-based GPU batch transform worker.
 *
 * Accumulates streaming coordinate transform requests in a Valkey
 * list and dispatches them as GPU-sized batches when a threshold
 * (point count or time window) is reached.
 *
 * This is a PostgreSQL background worker that runs alongside the
 * main backends.
 *
 **********************************************************************/

#include "../postgis_config.h"

#ifdef HAVE_VALKEY

#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/latch.h"
#include "utils/guc.h"
#include "utils/timestamp.h"

#include <hiredis/hiredis.h>
#include <string.h>
#include <math.h>

#include "liblwgeom.h"
#include "../liblwgeom/lwgeom_gpu.h"

/* Forward declaration */
void postgis_valkey_register_gucs(void);

/* GUC variables */
static char *postgis_gpu_valkey_url = NULL;
static int postgis_gpu_batch_size = 10000;
static int postgis_gpu_batch_timeout_ms = 100;

/* Valkey connection */
static redisContext *valkey_ctx = NULL;

/* Batch buffer */
typedef struct {
	double *data;        /* Interleaved point data */
	uint32_t npoints;    /* Current point count */
	uint32_t capacity;   /* Allocated capacity */
	size_t stride;       /* Bytes per point */
	double theta;        /* Rotation angle (uniform epoch) */
	int has_m_epoch;     /* Per-point M epoch mode */
	int direction;       /* ECI direction for M-epoch */
} BatchBuffer;

static BatchBuffer batch = {0};

static int
valkey_connect(void)
{
	if (valkey_ctx)
		return 1;

	if (!postgis_gpu_valkey_url || postgis_gpu_valkey_url[0] == '\0')
		return 0;

	/* Parse host:port from URL */
	char host[256] = "127.0.0.1";
	int port = 6379;
	if (sscanf(postgis_gpu_valkey_url, "%255[^:]:%d", host, &port) < 1)
		return 0;

	struct timeval timeout = {0, 500000}; /* 500ms connect timeout */
	valkey_ctx = redisConnectWithTimeout(host, port, timeout);
	if (!valkey_ctx || valkey_ctx->err)
	{
		if (valkey_ctx)
		{
			elog(WARNING, "PostGIS Valkey batch: connection failed: %s",
			     valkey_ctx->errstr);
			redisFree(valkey_ctx);
			valkey_ctx = NULL;
		}
		return 0;
	}

	return 1;
}

static void
valkey_disconnect(void)
{
	if (valkey_ctx)
	{
		redisFree(valkey_ctx);
		valkey_ctx = NULL;
	}
}

static void
batch_init(size_t stride)
{
	if (batch.data)
		pfree(batch.data);

	batch.capacity = postgis_gpu_batch_size;
	batch.stride = stride;
	batch.data = palloc(batch.capacity * stride);
	batch.npoints = 0;
	batch.has_m_epoch = 0;
	batch.direction = 0;
	batch.theta = 0;
}

static int
batch_flush(void)
{
	int result;

	if (batch.npoints == 0)
		return 1;

	if (!lwgpu_available())
	{
		/* CPU fallback */
		elog(WARNING, "PostGIS Valkey batch: no GPU available, using CPU SIMD fallback");

		if (batch.has_m_epoch)
		{
			/* Not easily done without POINTARRAY; return failure */
			elog(WARNING, "PostGIS Valkey batch: M-epoch CPU fallback not implemented for raw data");
			batch.npoints = 0;
			return 0;
		}

		/* Apply rotation using scalar math */
		double cos_t = cos(batch.theta);
		double sin_t = sin(batch.theta);
		size_t stride_d = batch.stride / sizeof(double);
		uint32_t i;

		for (i = 0; i < batch.npoints; i++)
		{
			double *p = batch.data + i * stride_d;
			double x = p[0], y = p[1];
			p[0] = x * cos_t + y * sin_t;
			p[1] = -x * sin_t + y * cos_t;
		}
		result = 1;
	}
	else
	{
		if (batch.has_m_epoch)
		{
			size_t m_offset = (batch.stride / sizeof(double)) > 3 ? 3 : 2;
			result = lwgpu_rotate_z_m_epoch_batch(batch.data, batch.stride,
							      batch.npoints, m_offset,
							      batch.direction);
		}
		else
		{
			result = lwgpu_rotate_z_batch(batch.data, batch.stride,
						      batch.npoints, batch.theta);
		}
	}

	if (!result)
		elog(WARNING, "PostGIS Valkey batch: GPU dispatch failed, results may be incomplete");

	/* Write results back to Valkey */
	if (valkey_ctx)
	{
		redisReply *reply = redisCommand(valkey_ctx,
			"RPUSH postgis:batch:results %b",
			batch.data, batch.npoints * batch.stride);
		if (reply)
			freeReplyObject(reply);
	}

	batch.npoints = 0;
	return result;
}

/*
 * Background worker main loop.
 * Monitors a Valkey list for transform requests and dispatches batches.
 */
void
postgis_valkey_batch_main(Datum main_arg)
{
	TimestampTz last_flush;

	/* Initialize */
	batch_init(32); /* Default to 4D stride */
	last_flush = GetCurrentTimestamp();

	while (!ShutdownRequestPending)
	{
		int rc;

		rc = WaitLatch(MyLatch,
			       WL_LATCH_SET | WL_TIMEOUT | WL_EXIT_ON_PM_DEATH,
			       postgis_gpu_batch_timeout_ms,
			       PG_WAIT_EXTENSION);

		ResetLatch(MyLatch);

		if (ShutdownRequestPending)
			break;

		/* Try to connect to Valkey if not connected */
		if (!valkey_ctx && !valkey_connect())
		{
			/* Sleep and retry */
			continue;
		}

		/* Check for pending requests */
		redisReply *reply = redisCommand(valkey_ctx,
			"LPOP postgis:batch:requests");

		if (reply && reply->type == REDIS_REPLY_STRING)
		{
			/* Append to batch buffer */
			size_t data_len = reply->len;
			size_t npoints_in = data_len / batch.stride;

			if (batch.npoints + npoints_in > batch.capacity)
			{
				/* Flush current batch before adding more */
				batch_flush();
			}

			memcpy(batch.data + batch.npoints * (batch.stride / sizeof(double)),
			       reply->str, data_len);
			batch.npoints += npoints_in;
		}

		if (reply)
			freeReplyObject(reply);

		/* Check flush conditions */
		if (batch.npoints >= (uint32_t)postgis_gpu_batch_size)
		{
			batch_flush();
			last_flush = GetCurrentTimestamp();
		}
		else if (batch.npoints > 0)
		{
			TimestampTz now = GetCurrentTimestamp();
			long secs;
			int usecs;
			TimestampDifference(last_flush, now, &secs, &usecs);
			long elapsed_ms = secs * 1000 + usecs / 1000;

			if (elapsed_ms >= postgis_gpu_batch_timeout_ms)
			{
				batch_flush();
				last_flush = now;
			}
		}
	}

	/* Flush remaining */
	if (batch.npoints > 0)
		batch_flush();

	valkey_disconnect();

	if (batch.data)
		pfree(batch.data);
}

/*
 * Register GUC parameters for Valkey batching.
 * Called from _PG_init.
 */
void
postgis_valkey_register_gucs(void)
{
	DefineCustomStringVariable(
		"postgis.gpu_valkey_url",
		"Valkey/Redis URL for GPU batch queueing (host:port).",
		"Set to enable Valkey-based GPU batch dispatch. Empty disables.",
		&postgis_gpu_valkey_url,
		"",
		PGC_SIGHUP,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		"postgis.gpu_batch_size",
		"GPU batch size threshold (points).",
		"Flush batch to GPU when this many points are accumulated.",
		&postgis_gpu_batch_size,
		10000,
		100,
		10000000,
		PGC_SIGHUP,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		"postgis.gpu_batch_timeout_ms",
		"GPU batch timeout (milliseconds).",
		"Flush partial batch after this many ms even if batch size not reached.",
		&postgis_gpu_batch_timeout_ms,
		100,
		10,
		10000,
		PGC_SIGHUP,
		0,
		NULL, NULL, NULL);
}

#else /* !HAVE_VALKEY */

#include "postgres.h"

void postgis_valkey_register_gucs(void);

void
postgis_valkey_register_gucs(void)
{
	/* No Valkey support compiled in */
}

#endif /* HAVE_VALKEY */
