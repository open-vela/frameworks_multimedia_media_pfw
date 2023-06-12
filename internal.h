/****************************************************************************
 * pfw/internal.h
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

#ifndef PFW_INTERNAL_H
#define PFW_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include "pfw.h"
#include <stdbool.h>
#include <stdio.h>
#include <sys/queue.h>

/****************************************************************************
 * Pre-Processor Definations
 ****************************************************************************/

#if CONFIG_LIB_PFW_DEBUG
#define PFW_DEBUG(...) printf(__VA_ARGS__)
#else
#define PFW_DEBUG(...)
#endif

/* Rule pridecates. */

#ifndef LIST_FOREACH
#define LIST_FOREACH(var, head, field) \
    for ((var) = LIST_FIRST(head);     \
         (var) != LIST_END(head);      \
         (var) = LIST_NEXT(var, field))
#endif

#ifndef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)      \
    for ((var) = LIST_FIRST(head);                     \
         (var) && ((tvar) = LIST_NEXT(var, field), 1); \
         (var) = (tvar))
#endif

#define PFW_MAXLEN_AMMENDS 512

/* Ammend types. */

#define PFW_AMMEND_RAW 1 // Raw string that does not need ammend.
#define PFW_AMMEND_CRITERION 2 // Ammend with criterion.

/* Criterion types. */

#define PFW_CRITERION_EXCLUSIVE 1 // Enum, each value has a literal meaning.
#define PFW_CRITERION_INCLUSIVE 2 // Bitmask, each bit has a literal meaning.
#define PFW_CRITERION_NUMERICAL 3 // Pure 32-bit integer.

/* Rule pridecates. */

#define PFW_PREDICATE_ALL 1 // True if all branches are true.
#define PFW_PREDICATE_ANY 2 // True if any branch is true.
#define PFW_PREDICATE_IS 3 // True if qeual to.
#define PFW_PREDICATE_ISNOT 4 // True if not equal to.
#define PFW_PREDICATE_INCLUDES 5 // True if has this bit.
#define PFW_PREDICATE_EXCLUDES 6 // True if dose not has this bit.
#define PFW_PREDICATE_IN 7 // True if in the interval.
#define PFW_PREDICATE_NOTIN 8 // // True if not in the interval.

/****************************************************************************
 * Types
 ****************************************************************************/

typedef struct pfw_ammend_t pfw_ammend_t;
typedef struct pfw_context_t pfw_context_t;
typedef struct pfw_vector_t pfw_vector_t;
typedef struct pfw_interval_t pfw_interval_t;
typedef struct pfw_criterion_t pfw_criterion_t;
typedef struct pfw_rule_t pfw_rule_t;
typedef struct pfw_act_t pfw_act_t;
typedef struct pfw_action_t pfw_action_t;
typedef struct pfw_config_t pfw_config_t;
typedef struct pfw_domain_t pfw_domain_t;
typedef struct pfw_listener_t pfw_listener_t;
typedef LIST_HEAD(pfw_listener_list_t, pfw_listener_t)
    pfw_listener_list_t;
typedef struct pfw_plugin_t pfw_plugin_t;
typedef struct pfw_system_t pfw_system_t;

/**
 * @brief pfw_ammend_t
 *
 * A raw string, or ammend with other object.
 *
 * @see pfw_config_t pfw_act_t
 */
struct pfw_ammend_t {
    int type;
    union {
        const char* raw;
        pfw_criterion_t* criterion;
    } u;
};

/**
 * @brief pfw_interval_t is a open interval, used by NumericalCriterion.
 * @see pfw_criterion_t
 */
struct pfw_interval_t {
    int32_t left;
    int32_t right;
};

/**
 * @brief pfw_criterion_t is condition variable.
 *
 * When criterion is modified, the rules in domains might change;
 * so domain might change in next apply.
 *
 * @see pfw_rule_t pfw_apply() pfw_criterion_*()
 */
struct pfw_criterion_t {
    int type; // @see PFW_CRITERION_*
    pfw_vector_t* names;
    pfw_vector_t* ranges;
    int32_t state;
    union {
        const char* def;
        int32_t v;
    } init;
};

/**
 * @brief pfw_rule_t is tree.
 *
 * Branch node is a composite of multiple rules; leaf node
 * requires a criterion to be a specific state.
 *
 * @see pfw_criterion_t pfw_config_t
 */
struct pfw_rule_t {
    int predicate; // @see PFW_PREDICATE_*
    pfw_vector_t* branches;
    union {
        const char* def;
        pfw_criterion_t* p;
    } criterion;
    union {
        int32_t v;
        const char* def;
        pfw_interval_t* itv;
    } state;
};

/**
 * @brief pfw_act_t is applying paramter by plugin method.
 *
 * Apply the parameter to all listener callbacks in the plugin
 * when the act is applied.
 *
 * @see pfw_action_t pfw_plugin_t
 */
struct pfw_act_t {
    union {
        const char* def;
        pfw_plugin_t* p;
    } plugin;
    pfw_vector_t* param; // @see pfw_ammend_t
};

/**
 * @brief pfw_config_t is a state in state machine.
 *
 * Once the rules is ok, take acts.
 *
 * @see pfw_domain_t pfw_rule_t pfw_action_t
 */
struct pfw_config_t {
    char* current;
    pfw_vector_t* name; // @see pfw_ammend_t
    pfw_rule_t* rules;
    pfw_vector_t* acts;
};

/**
 * @brief pfw_domain_t is a state machine.
 *
 * when apply changes, state machine might move into a new state.
 *
 * @see pfw_config_t pfw_apply()
 */
struct pfw_domain_t {
    const char* name;
    pfw_config_t* current;
    pfw_vector_t* configs;
};

/**
 * @brief pfw_listener_t is a callback in plugin
 * @see pfw_plugin_t
 */
struct pfw_listener_t {
    void* cookie;
    pfw_callback_t cb;
    LIST_ENTRY(pfw_listener_t)
    entry;
};

/**
 * @brief pfw_plugin_t is a sequence of callback
 *
 * Callbacks used by act in state machine, user can add/remove callback
 * to any plugin at runtime.
 *
 * @see pfw_listener_t pfw_subscribe() pfw_unsubscribe()
 */
struct pfw_plugin_t {
    char* name;
    char* parameter;
    pfw_listener_list_t listeners;
};

/**
 * @brief pfw_system_t is a system that includes condition variables and
 * state machines.
 *
 * Create system by parsing two configuration files, regist plugin,
 * and initialize the criteria and domains.
 *
 * @see pfw_criterion_t pfw_domain_t pfw_context_t
 * pfw_create() pfw_destroy()
 */
struct pfw_system_t {
    pfw_context_t* criteria_ctx;
    pfw_context_t* settings_ctx;
    pfw_vector_t* criteria;
    pfw_vector_t* domains;
    pfw_vector_t* plugins;
    pfw_load_t load; // Load criterion state at initilization.
    pfw_save_t save; // Save criterion state when it changes.
};

/****************************************************************************
 * Functions
 ****************************************************************************/

/* Utils functions. */

pfw_context_t* pfw_context_create(const char* filename);
char* pfw_context_take_word(pfw_context_t* ctx);
char* pfw_context_take_line(pfw_context_t* ctx);
int pfw_context_get_depth(pfw_context_t* ctx);
void pfw_context_destroy(pfw_context_t* ctx);

int pfw_vector_append(pfw_vector_t** pv, void* obj);
void* pfw_vector_get(pfw_vector_t* vector, int index);
void pfw_vector_free(pfw_vector_t* vector);

/* Parse functions. */

void pfw_free_criteria(pfw_vector_t* criteria);
void pfw_free_settings(pfw_vector_t* settings);
int pfw_parse_criteria(pfw_context_t* ctx, pfw_vector_t** p);
int pfw_parse_settings(pfw_context_t* ctx, pfw_vector_t** p);

bool pfw_sanitize_criteria(pfw_system_t* system);
bool pfw_sanitize_settings(pfw_system_t* system);

void* pfw_plugin_register(pfw_system_t* system, pfw_plugin_def_t* def);

/* Criterion functions */

bool pfw_rule_match(pfw_rule_t* rule);
int pfw_criterion_atoi(pfw_criterion_t* criterion,
    const char* value, int32_t* state);
int pfw_criterion_itoa(pfw_criterion_t* criterion,
    int32_t state, char* res, int len);
pfw_criterion_t* pfw_criteria_find(pfw_vector_t* criteria,
    const char* target);

#endif // PFW_INTERNAL_H
