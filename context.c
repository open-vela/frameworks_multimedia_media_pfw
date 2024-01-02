/****************************************************************************
 * pfw/context.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pfw_context_s {
    char* buf;
    char* ptr;
    char* rest;
    char indent;
    int depth;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pfw_context_skip_indent(pfw_context_t* ctx)
{
    int space = 0, tab = 0;

    for (; *(ctx->ptr) != '\0'; ctx->ptr++) {
        if (*(ctx->ptr) == '\t')
            tab++;
        else if (*(ctx->ptr) == ' ')
            space++;
        else
            break;
    }

    if (space > 0) {
        if (space % 4 != 0 || tab > 0 || ctx->indent == '\t')
            goto err;

        ctx->indent = ' ';
        ctx->depth = space / 4;
    } else if (tab > 0) {
        if (space > 0 || ctx->indent == ' ')
            goto err;

        ctx->indent = '\t';
        ctx->depth = tab;
    } else {
        ctx->depth = 0;
    }

    return;

err:
    PFW_DEBUG("indent error: %d tab and %d space\n", tab, space);
    ctx->depth = -EINVAL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

char* pfw_context_take_word(pfw_context_t* ctx)
{
    if (!ctx || !ctx->ptr || *(ctx->ptr) == '\0' || ctx->depth < 0)
        return NULL;

    return strtok_r(NULL, " \t", &ctx->ptr);
}

char* pfw_context_take_line(pfw_context_t* ctx)
{
    char* line;

    if (!ctx || ctx->depth < 0)
        return NULL;

    line = ctx->ptr;

    /* Take indents and skip empty line. */

    while (1) {
        ctx->ptr = strtok_r(NULL, "\n", &ctx->rest);
        if (!ctx->ptr) {
            ctx->depth = EOF;
            break;
        }

        pfw_context_skip_indent(ctx);
        if (ctx->depth >= 0 && *(ctx->ptr) != '\0')
            break;
    }

    return line;
}

int pfw_context_get_depth(pfw_context_t* ctx)
{
    if (!ctx)
        return -EINVAL;

    return ctx->depth;
}

pfw_context_t* pfw_context_create(const char* filename)
{
    pfw_context_t* ctx;
    size_t length;
    FILE* file;

    // read file
    file = fopen(filename, "re");
    if (!file)
        return NULL;

    fseek(file, 0, SEEK_END);
    length = ftell(file);
    if (length < 0)
        goto err1;

    rewind(file);
    ctx = malloc(sizeof(pfw_context_t));
    if (!ctx)
        goto err1;

    memset(ctx, 0, sizeof(pfw_context_t));
    ctx->buf = malloc(length + 1);
    if (!ctx->buf)
        goto err2;

    if (fread(ctx->buf, length, 1, file) < 0)
        goto err3;

    ctx->buf[length] = '\0';
    ctx->ptr = strtok_r(ctx->buf, "\n", &ctx->rest);
    pfw_context_skip_indent(ctx);
    if (ctx->depth >= 0) {
        fclose(file);
        return ctx;
    }

err3:
    free(ctx->buf);
err2:
    free(ctx);
err1:
    fclose(file);
    return NULL;
}

void pfw_context_destroy(pfw_context_t* ctx)
{
    if (ctx) {
        free(ctx->buf);
        free(ctx);
    }
}
