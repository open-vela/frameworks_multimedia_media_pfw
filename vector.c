/****************************************************************************
 * pfw/vector.c
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
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PFW_VECTOR_FACTOR 2
#define PFW_VECTOR_INIT_SIZE 4
#define PFW_VECTOR_MAX_SIZE 128

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pfw_vector_s {
    size_t cnt;
    size_t size;
    void** eles;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static pfw_vector_t* pfw_vector_alloc(void)
{
    pfw_vector_t* vector;

    vector = malloc(sizeof(pfw_vector_t));
    if (!vector)
        return NULL;

    vector->eles = malloc(sizeof(void*) * PFW_VECTOR_INIT_SIZE);
    if (!vector->eles) {
        free(vector);
        return NULL;
    }

    vector->cnt = 0;
    vector->size = PFW_VECTOR_INIT_SIZE;
    return vector;
}

static int pfw_vector_grow(pfw_vector_t* vector)
{
    size_t size;
    void* tmp;

    size = 2 * vector->size;
    if (size > PFW_VECTOR_MAX_SIZE)
        return -EINVAL;

    tmp = realloc(vector->eles, sizeof(void*) * size);
    if (!tmp)
        return -ENOMEM;

    vector->eles = tmp;
    vector->size = size;
    return vector->size;
}

static int pfw_vector_shrink(pfw_vector_t* vector)
{
    void* tmp;

    if (vector->cnt >= vector->size)
        return vector->size;

    tmp = realloc(vector->eles, sizeof(void*) * vector->cnt);
    if (!tmp)
        return -ENOMEM;

    vector->eles = tmp;
    vector->size = vector->cnt;
    return vector->size;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int pfw_vector_append(pfw_vector_t** pv, void* obj)
{
    pfw_vector_t* vector;
    int ret;

    if (!pv)
        return -EINVAL;

    vector = *pv;
    if (!vector) {
        vector = *pv = pfw_vector_alloc();
        if (!vector)
            return -ENOMEM;
    }

    if (vector->cnt >= vector->size) {
        ret = pfw_vector_grow(vector);
        if (ret < 0)
            return ret;
    }

    vector->eles[vector->cnt] = obj;
    vector->cnt++;
    return 0;
}

void* pfw_vector_get(pfw_vector_t* vector, int index)
{
    if (!vector || index < 0 || index >= vector->cnt)
        return NULL;

    pfw_vector_shrink(vector);

    return vector->eles[index];
}

void pfw_vector_free(pfw_vector_t* vector)
{
    if (vector) {
        free(vector->eles);
        free(vector);
    }
}
