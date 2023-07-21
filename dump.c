/****************************************************************************
 * pfw/dump.c
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
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

typedef struct pfw_buffer_t {
    char* str;
    size_t size;
} pfw_buffer_t;

static int pfw_buffer_grow(pfw_buffer_t* buf, size_t size)
{
    void* tmp;

    tmp = realloc(buf->str, buf->size + size);
    if (!tmp)
        return -ENOMEM;

    buf->size = buf->size + size;
    buf->str = tmp;
    return 0;
}

static void pfw_buffer_append(pfw_buffer_t* buf, char* str)
{
    int ret;

    if (!buf->str) {
        buf->str = strdup(str);
        if (!buf->str)
            return;

        buf->size = strlen(str) + 1;
        return;
    }

    ret = pfw_buffer_grow(buf, strlen(str));
    if (ret >= 0)
        strcat(buf->str, str);
}

static void pfw_buffer_printf(pfw_buffer_t** p, const char* fmt, ...)
{
    pfw_buffer_t* buf = *p;
    char tmp[1024];
    va_list vl;
    int ret;

    if (!buf) {
        buf = *p = malloc(sizeof(pfw_buffer_t));
        if (!buf)
            return;

        buf->str = NULL;
        buf->size = 0;
    }

    va_start(vl, fmt);
    ret = vsnprintf(tmp, sizeof(tmp), fmt, vl);
    va_end(vl);

    if (ret >= 0)
        pfw_buffer_append(buf, tmp);
}

static void pfw_buffer_free(pfw_buffer_t* buf, char** res)
{
    if (buf) {
        if (res)
            *res = buf->str;
        else
            free(buf->str);

        free(buf);
    }
}

static inline void pfw_empty_line(pfw_buffer_t** p)
{
    pfw_buffer_printf(p,
        "+-------------------------------------------------------------\n");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

char* pfw_dump(void* handle)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    pfw_buffer_t* buf = NULL;
    pfw_domain_t* domain;
    char* res = NULL;
    char tmp[64];
    int i;

    if (!system)
        return NULL;

    pthread_mutex_lock(&system->mutex);

    pfw_empty_line(&buf);
    pfw_buffer_printf(&buf, "| %-32s | %-8s | %s\n", "CRITERIA", "STATE", "VALUE");
    pfw_empty_line(&buf);
    for (i = 0; (criterion = pfw_vector_get(system->criteria, i)); i++) {
        if (criterion->type != PFW_CRITERION_NUMERICAL)
            pfw_criterion_itoa(criterion, criterion->state, tmp, sizeof(tmp));
        else
            tmp[0] = '\0';
        pfw_buffer_printf(&buf, "| %-32s | %-8" PRId32 " | %s\n",
            (char*)pfw_vector_get(criterion->names, 0), criterion->state, tmp);
    }

    pfw_empty_line(&buf);
    pfw_buffer_printf(&buf, "| %-32s | %s\n", "DOMAIN", "CONFIG");
    pfw_empty_line(&buf);
    for (i = 0; (domain = pfw_vector_get(system->domains, i)); i++) {
        pfw_buffer_printf(&buf, "| %-32s | %s\n", domain->name,
            domain->current ? domain->current->current : "");
    }

    pfw_empty_line(&buf);
    pfw_buffer_free(buf, &res);

    pthread_mutex_unlock(&system->mutex);

    return res;
}
