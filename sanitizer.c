/****************************************************************************
 * pfw/sanitizer.c
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
#include <string.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PFW_SANITIZE_OBJ_NAME(vector, type, name)                      \
    do {                                                               \
        type *obj1, *obj2;                                             \
        int a, b;                                                      \
        for (a = 0; (obj1 = pfw_vector_get(vector, a)); a++) {         \
            for (b = a + 1; (obj2 = pfw_vector_get(vector, b)); b++) { \
                if (!strcmp(obj1->name, obj2->name)) {                 \
                    PFW_DEBUG("Duplicate name '%s'\n", obj1->name);    \
                    return false;                                      \
                }                                                      \
            }                                                          \
        }                                                              \
    } while (0);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static bool pfw_sanitize_string(pfw_vector_t* names)
{
    const char* name1;
    const char* name2;
    int i, j;
    for (i = 0; (name1 = pfw_vector_get(names, i)); i++) {
        for (j = i + 1; (name2 = pfw_vector_get(names, j)); j++) {
            if (!strcmp(name1, name2)) {
                PFW_DEBUG("Duplicate string '%s'\n", name1);
                return false;
            }
        }
    }

    return true;
}

static void pfw_sanitize_ammends(pfw_vector_t* ammends, pfw_system_t* system)
{
    pfw_criterion_t* criterion;
    pfw_ammend_t* ammend;
    int i;

    for (i = 0; (ammend = pfw_vector_get(ammends, i)); i++) {
        criterion = pfw_criteria_find(system->criteria, ammend->u.raw);
        if (criterion) {
            ammend->type = PFW_AMMEND_CRITERION;
            ammend->u.criterion = criterion;
        } else {
            ammend->type = PFW_AMMEND_RAW;
        }
    }
}

static bool pfw_sanitize_rules(pfw_rule_t* rules, pfw_system_t* system)
{
    pfw_criterion_t* criterion;
    pfw_rule_t* rule;
    int i;

    if (rules->predicate == PFW_PREDICATE_ALL
        || rules->predicate == PFW_PREDICATE_ANY) {
        for (i = 0; (rule = pfw_vector_get(rules->branches, i)); i++) {
            if (!pfw_sanitize_rules(rule, system)) {
                PFW_DEBUG("Rule branch at %d is invalid\n", i);
                return false;
            }
        }

        return true;
    }

    /* Santinize criterion. */

    criterion = pfw_criteria_find(system->criteria, rules->criterion.def);
    if (!criterion) {
        PFW_DEBUG("Criterion '%s' not found\n", rules->criterion.def);
        return false;
    }

    rules->criterion.p = criterion;

    /* Santinize state. */

    if (rules->criterion.p->type != PFW_CRITERION_NUMERICAL
        && pfw_criterion_atoi(rules->criterion.p, rules->state.def, &rules->state.v)
            < 0) {
        PFW_DEBUG("Rule has invalid state '%s' for criterion '%s'\n",
            rules->state.def, (char*)pfw_vector_get(rules->criterion.p->names, 0));
        return false;
    }

    /* Santinize predicates. */

    switch (rules->criterion.p->type) {
    case PFW_CRITERION_EXCLUSIVE:
        if (rules->predicate == PFW_PREDICATE_IS
            || rules->predicate == PFW_PREDICATE_ISNOT)
            return true;

    case PFW_CRITERION_INCLUSIVE:
        if (rules->predicate == PFW_PREDICATE_INCLUDES
            || rules->predicate == PFW_PREDICATE_EXCLUDES)
            return true;

    case PFW_CRITERION_NUMERICAL:
        if (rules->predicate == PFW_PREDICATE_IN
            || rules->predicate == PFW_PREDICATE_NOTIN)
            return true;
    }

    PFW_DEBUG("Rule uses invalid predicate '%d' for type '%d'\n",
        rules->predicate, rules->criterion.p->type);

    return false;
}

static bool pfw_sanitize_act(pfw_act_t* act, pfw_system_t* system)
{
    pfw_plugin_t* plugin = NULL;
    int i;

    for (i = 0; (plugin = pfw_vector_get(system->plugins, i)); i++) {
        if (!strcmp(act->plugin.def, plugin->name)) {
            break;
        }
    }

    /* Auto generate plugin if user doesn't define. */

    if (!plugin) {
        PFW_DEBUG("Plugin '%s' not support\n", act->plugin.def);
        return false;
    }

    act->plugin.p = plugin;

    pfw_sanitize_ammends(act->param, system);
    return true;
}

static bool pfw_sanitize_config(pfw_config_t* config, pfw_domain_t* domain,
    pfw_system_t* system)
{
    pfw_act_t* act;
    int i;

    pfw_sanitize_ammends(config->name, system);

    if (!pfw_sanitize_rules(config->rules, system)) {
        PFW_DEBUG("Bad rules in config \n");
        return false;
    }

    for (i = 0; (act = pfw_vector_get(config->acts, i)); i++) {
        if (!pfw_sanitize_act(act, system)) {
            PFW_DEBUG("Bad act in config\n");
            return false;
        }
    }

    return true;
}

static bool pfw_sanitize_domain(pfw_domain_t* domain, pfw_system_t* system)
{
    pfw_config_t* config;
    int i;

    for (i = 0; (config = pfw_vector_get(domain->configs, i)); i++) {
        if (!pfw_sanitize_config(config, domain, system)) {
            PFW_DEBUG("Bad %dth config in domain '%s'\n", i, domain->name);
            return false;
        }
    }

    return true;
}

static bool pfw_sanitize_criterion(pfw_criterion_t* criterion, pfw_system_t* system)
{
    if (criterion->init.def
        && pfw_criterion_atoi(criterion, criterion->init.def, &criterion->init.v) < 0) {
        PFW_DEBUG("Criterion has invalid initial state '%s'\n", criterion->init.def);
        return false;
    }

    criterion->state = criterion->init.v;
    if (system->on_load)
        system->on_load(system->cookie, pfw_vector_get(criterion->names, 0), &criterion->state);

    if (criterion->type != PFW_CRITERION_NUMERICAL
        && !pfw_sanitize_string(criterion->ranges)) {
        return false;
    }

    return pfw_sanitize_string(criterion->names);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool pfw_sanitize_criteria(pfw_system_t* system)
{
    pfw_vector_t* names = NULL;
    pfw_criterion_t* criterion;
    bool result = false;
    int i, j, ret;
    char* name;

    for (i = 0; (criterion = pfw_vector_get(system->criteria, i)); i++) {
        if (!pfw_sanitize_criterion(criterion, system))
            goto out;

        for (j = 0; (name = pfw_vector_get(criterion->names, j)); j++) {
            ret = pfw_vector_append(&names, name);
            if (ret < 0)
                goto out;
        }
    }

    result = pfw_sanitize_string(names);
    if (!result) {
        PFW_DEBUG("duplicate criterion name\n");
    }

out:
    pfw_vector_free(names);
    return result;
}

bool pfw_sanitize_settings(pfw_system_t* system)
{
    pfw_domain_t* domain;
    int i;

    PFW_SANITIZE_OBJ_NAME(system->domains, pfw_domain_t, name);

    for (i = 0; (domain = pfw_vector_get(system->domains, i)); i++) {
        if (!pfw_sanitize_domain(domain, system))
            return false;
    }

    return true;
}
