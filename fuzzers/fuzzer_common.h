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

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#include <set>

extern "C"
{
#include "liblwgeom.h"
#include "geos_stub.h"
#include "proj_stub.h"
}

/* Heap tracking for leak-free cleanup on longjmp */
static std::set<void*> oSetPointers;
static jmp_buf jmpBuf;

extern "C"
{
    static void *
    allocator(size_t size)
    {
            void *mem = malloc(size);
            oSetPointers.insert(mem);
            return mem;
    }

    static void
    freeor(void *mem)
    {
            oSetPointers.erase(mem);
            free(mem);
    }

    static void *
    reallocator(void *mem, size_t size)
    {
            oSetPointers.erase(mem);
            void *ret = realloc(mem, size);
            oSetPointers.insert(ret);
            return ret;
    }

    static void
    noticereporter(const char *, va_list)
    {
    }

    static void
    errorreporter(const char *, va_list)
    {
        for(std::set<void*>::iterator oIter = oSetPointers.begin();
            oIter != oSetPointers.end(); ++oIter)
        {
            free(*oIter);
        }
        oSetPointers.clear();
        longjmp(jmpBuf, 1);
    }

    static void
    debuglogger(int, const char *, va_list)
    {
    }
}

extern "C" int LLVMFuzzerInitialize(int* /*argc*/, char*** /*argv*/)
{
	lwgeom_set_handlers(malloc, realloc, free, noticereporter, noticereporter);
	lwgeom_set_debuglogger(debuglogger);
	return 0;
}

#endif /* FUZZER_COMMON_H */
