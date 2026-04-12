/******************************************************************************
 *
 * Project:  PostGIS
 * Purpose:  Common boilerplate for fuzz test harnesses
 * Author:   PostGIS Development Team
 *
 ******************************************************************************
 * Copyright (c) 2017, Even Rouault <even.rouault at spatialys.com>
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

#ifndef FUZZER_COMMON_H
#define FUZZER_COMMON_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>

#include <set>

extern "C" {
#include "liblwgeom.h"
#include "geos_stub.h"
#include "proj_stub.h"
}

/* Heap tracking for leak-free cleanup on longjmp */
static std::set<void *> oSetPointers; // NOSONAR - mutable tracking set
static jmp_buf jmpBuf;                // NOSONAR - required for longjmp error recovery

extern "C" {
static void * // NOSONAR - void* required by liblwgeom allocator API
allocator(size_t size)
{
	void *mem = std::malloc(size); // NOSONAR - liblwgeom allocator must use malloc
	oSetPointers.insert(mem);
	return mem;
}

static void
freeor(void *mem) // NOSONAR - void* required by liblwgeom allocator API
{
	oSetPointers.erase(mem);
	std::free(mem); // NOSONAR - liblwgeom allocator must use free
}

static void * // NOSONAR - void* required by liblwgeom allocator API
reallocator(void *mem, size_t size)
{
	oSetPointers.erase(mem);
	void *ret = std::realloc(mem, size); // NOSONAR - liblwgeom allocator must use realloc
	oSetPointers.insert(ret);
	return ret;
}

static void
noticereporter(const char *, va_list)
{
	/* intentionally empty: suppress liblwgeom notice output during fuzzing */
}

__attribute__((noreturn)) static void
errorreporter(const char *, va_list)
{
	for (auto const &ptr : oSetPointers)
	{
		std::free(ptr); // NOSONAR - matches malloc from allocator()
	}
	oSetPointers.clear();
	longjmp(jmpBuf, 1);
}

static void
debuglogger(int, const char *, va_list)
{
	/* intentionally empty: suppress liblwgeom debug output during fuzzing */
}
}

extern "C" int
LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	lwgeom_set_handlers(std::malloc,
			    std::realloc,
			    std::free, // NOSONAR - liblwgeom handler API
			    noticereporter,
			    noticereporter);
	lwgeom_set_debuglogger(debuglogger);
	return 0;
}

#endif /* FUZZER_COMMON_H */
