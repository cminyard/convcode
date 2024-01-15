/*
 * Copyright 2023 Corey Minyard
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * OS Functions for the convolutional coder.
 *
 * Replace this with your own OS function handling for your own use.
 */

#ifndef CONVCODE_OS_FUNCS_H
#define CONVCODE_OS_FUNCS_H

typedef struct convcode_os_funcs convcode_os_funcs;
struct convcode_os_funcs {
    void *(*zalloc)(convcode_os_funcs *f, unsigned long size);
    void (*free)(convcode_os_funcs *f, void *data);
};

extern convcode_os_funcs osfuncs, *o;

#endif /* CONVCODE_H */
