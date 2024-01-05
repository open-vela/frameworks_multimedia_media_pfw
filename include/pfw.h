/****************************************************************************
 * pfw/include/pfw.h
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

#ifndef PFW_INCLUDE_PFW_H
#define PFW_INCLUDE_PFW_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

typedef void (*pfw_callback_t)(void* cookie, const char* params);
typedef void (*pfw_listen_t)(void* cookie, int number, char* literal);
typedef void (*pfw_load_t)(void* cookie, const char* name, int32_t* state);
typedef void (*pfw_save_t)(void* cookie, const char* name, int32_t state);
typedef void (*pfw_release_t)(void* cookie);

typedef struct pfw_plugin_def_t {
    const char* name;
    void* cookie;
    pfw_callback_t cb;
} pfw_plugin_def_t;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void* pfw_create(const char* criteria, const char* settings,
    pfw_plugin_def_t* defs, int nb, pfw_load_t on_load,
    pfw_save_t on_save, void* cookie);
void pfw_apply(void* handle);
void pfw_destroy(void* handle, pfw_release_t on_release);
char* pfw_dump(void* handle);

/* Criterion modify. */

int pfw_setint(void* handle, const char* name, int value);
int pfw_setstring(void* handle, const char* name, const char* value);
int pfw_include(void* handle, const char* name, const char* value);
int pfw_exclude(void* handle, const char* name, const char* value);
int pfw_increase(void* handle, const char* name);
int pfw_decrease(void* handle, const char* name);
int pfw_reset(void* handle, const char* name);

/* Criterion subscribe */
void* pfw_subscribe(void* handle, const char* name,
    pfw_listen_t on_change, void* cookie);
void pfw_unsubscribe(void* handle, void* subscriber);

/* Criterion query. */

int pfw_getint(void* handle, const char* name, int* value);
int pfw_getstring(void* handle, const char* name, char* value, int len);
int pfw_getrange(void* handle, const char* name, int* min_value, int* max_value);
int pfw_contain(void* handle, const char* name, const char* value,
    int* contain);

#ifdef __cplusplus
}
#endif

#endif // PFW_INCLUDE_PFW_H
