/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * OS Functions for the convolutional coder.
 */

#include <stdlib.h>
#include <assert.h>

#include "convcode_os_funcs.h"

unsigned int mem_alloced;

static void *
o_zalloc(convcode_os_funcs *o, unsigned long size)
{
    unsigned long *v;

    assert(size);
    v = calloc(size + sizeof(*v), 1);
    if (v) {
	*v = size;
	v++;
	mem_alloced += size;
	o->bytes_allocated += size;
    }
    return v;
}

static void 
o_free(convcode_os_funcs *o, void *to_free)
{
    unsigned long *v = to_free;

    assert(v);
    v--;
    assert(*v && *v <= mem_alloced);
    mem_alloced -= *v;
    free(v);
}

convcode_os_funcs osfuncs = {
    .zalloc = o_zalloc,
    .free = o_free,
};

convcode_os_funcs *o = &osfuncs;
