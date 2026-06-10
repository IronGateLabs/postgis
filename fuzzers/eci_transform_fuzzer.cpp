/******************************************************************************
 *
 * Project:  PostGIS
 * Purpose:  Fuzzer for ECI/ECEF transforms
 * Author:   PostGIS Development Team
 *
 ******************************************************************************
 * Copyright (c) 2024, PostGIS Development Team
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include <math.h>
#include <vector>
#include "fuzzer_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len);

int
LLVMFuzzerTestOneInput(const uint8_t *buf, size_t len)
{
	/* Need at least 8 bytes for epoch + 1 byte for WKT */
	if (len < 9)
		return 0;

	/* Extract epoch from first 8 bytes */
	double epoch;
	memcpy(&epoch, buf, 8);

	/* Skip NaN, Inf, and zero epochs (rejected by the API) */
	if (!isfinite(epoch) || epoch == 0.0)
		return 0;

	/* Clamp to reasonable range to avoid extreme computation */
	if (epoch < -10000.0 || epoch > 10000.0)
		return 0;

	/* Extract WKT from remaining bytes */
	auto wkt_len = len - 8;
	std::vector<char> wkt_buf(wkt_len + 1);
	memcpy(wkt_buf.data(), buf + 8, wkt_len);
	wkt_buf[wkt_len] = '\0';

	if (!setjmp(jmpBuf))
	{
		LWGEOM *lwgeom = lwgeom_from_wkt(wkt_buf.data(), LW_PARSER_CHECK_NONE);
		if (lwgeom)
		{
			/* Try ECI -> ECEF -> ECI roundtrip */
			lwgeom_transform_eci_to_ecef(lwgeom, epoch);
			lwgeom_transform_ecef_to_eci(lwgeom, epoch);
			lwgeom_free(lwgeom);
		}
	}
	return 0;
}
