/****************************************************************************
 * pfw/system.c
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
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pfw_free_plugins(pfw_system_t* system)
{
    pfw_listener_t *listener, *tmp;
    int i;

    for (i = 0; (i < system->nb_plugins); i++) {
        pfw_plugin_t* plugin = &system->plugins[i];

        LIST_FOREACH_SAFE(listener, &system->plugins->listeners, entry, tmp)
        {
            free(listener);
        }

        free(plugin->name);
    }

    free(system->plugins);
}

/**
 * @brief Regist callback at plugin as listener.
 */
static void* pfw_listener_register(pfw_plugin_t* plugin,
    void* cookie, pfw_callback_t cb)
{
    pfw_listener_t* listener;

    listener = malloc(sizeof(pfw_listener_t));
    if (!listener)
        return NULL;

    listener->cb = cb;
    listener->cookie = cookie;
    LIST_INSERT_HEAD(&plugin->listeners, listener, entry);

    return listener;
}

static bool pfw_plugins_register(pfw_system_t* system,
    pfw_plugin_def_t* defs, int nb)
{
    int i, j;

    system->nb_plugins = nb;
    system->plugins = calloc(nb, sizeof(pfw_plugin_t));
    if (!system->plugins)
        return NULL;

    for (i = 0; i < nb; i++) {
        pfw_plugin_t* plugin = &system->plugins[i];
        pfw_plugin_def_t* def = &defs[i];

        if (!def->name)
            return false;

        for (j = 0; j < i; j++) {
            if (!strcmp(def->name, system->plugins[j].name)) {
                PFW_DEBUG("Duplicate plugin name %s\n", def->name);
                return false;
            }
        }

        plugin->name = strdup(def->name);
        if (!plugin->name)
            return false;

        if (def->cb && !pfw_listener_register(plugin, def->cookie, def->cb))
            return false;
    }

    return true;
}

/**
 * @brief Apply paramters to plugin listeners.
 */
static void pfw_apply_act(pfw_act_t* act)
{
    pfw_listener_t* listener;

    LIST_FOREACH(listener, &act->plugin.p->listeners, entry)
    {
        listener->cb(listener->cookie, act->params);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Apply criteria changes to domains.
 */
void pfw_apply(void* handle)
{
    pfw_system_t* system = handle;
    pfw_domain_t* domain;
    pfw_config_t* config;
    pfw_act_t* act;
    int i, j, k;

    if (!system)
        return;

    for (i = 0; (domain = pfw_vector_get(system->domains, i)); i++) {

        for (j = 0; (config = pfw_vector_get(domain->configs, j)); j++) {

            if (pfw_rule_match(config->rules)) {
                if (domain->current != config) {
                    for (k = 0; (act = pfw_vector_get(config->acts, k)); k++)
                        pfw_apply_act(act);

                    domain->current = config;
                }

                break;
            }
        }
    }
}

void* pfw_subscribe(void* handle, const char* name,
    void* cookie, pfw_callback_t cb)
{
    pfw_system_t* system = handle;
    pfw_plugin_t* plugin;
    int i;

    if (!system || !name)
        return NULL;

    for (i = 0; (i < system->nb_plugins); i++) {
        plugin = &system->plugins[i];
        if (!strcmp(plugin->name, name))
            break;
    }

    if (i == system->nb_plugins)
        return NULL;

    return pfw_listener_register(plugin, cookie, cb);
}

void pfw_unsubscribe(void* handle)
{
    pfw_listener_t* listener = handle;

    if (!listener)
        return;

    LIST_REMOVE(listener, entry);
    free(listener);
}

void* pfw_create(const char* criteria, const char* settings,
    pfw_plugin_def_t* defs, int nb,
    pfw_load_t load, pfw_save_t save)
{
    pfw_system_t* system;

    system = calloc(1, sizeof(pfw_system_t));
    if (!system)
        return NULL;

    if (!pfw_plugins_register(system, defs, nb))
        goto err;

    system->load = load;
    system->save = save;

    /* Parse criteria. */

    system->criteria_ctx = pfw_context_create(criteria);
    if (!system->criteria_ctx)
        goto err;

    if (pfw_parse_criteria(system->criteria_ctx, &system->criteria) < 0)
        goto err;

    if (!pfw_sanitize_criteria(system))
        goto err;

    /* Parse settings. */

    system->settings_ctx = pfw_context_create(settings);
    if (!system->settings_ctx)
        goto err;

    if (pfw_parse_settings(system->settings_ctx, &system->domains) < 0)
        goto err;

    if (!pfw_sanitize_settings(system))
        goto err;

    return system;

err:
    pfw_destroy(system);
    return NULL;
}

void pfw_destroy(void* handle)
{
    pfw_system_t* system = handle;

    if (system) {
        pfw_context_destroy(system->criteria_ctx);
        pfw_context_destroy(system->settings_ctx);
        pfw_free_criteria(system->criteria);
        pfw_free_settings(system->domains);
        pfw_free_plugins(system);
        free(system);
    }
}

void pfw_dump(void* handle)
{
    const char* delim = "+-------------------------------------------------------------";
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    pfw_domain_t* domain;
    char tmp[64];
    int i;

    if (!system)
        return;

    PFW_INFO("%s\n", delim);
    PFW_INFO("| %-32s | %-8s | %s\n", "CRITERIA", "STATE", "VALUE");
    PFW_INFO("%s\n", delim);
    for (i = 0; (criterion = pfw_vector_get(system->criteria, i)); i++) {
        if (criterion->type != PFW_CRITERION_NUMERICAL)
            pfw_criterion_itoa(criterion, criterion->state, tmp, sizeof(tmp));
        else
            tmp[0] = '\0';
        PFW_INFO("| %-32s | %-8d | %s\n", (char*)pfw_vector_get(criterion->names, 0),
            criterion->state, tmp);
    }

    PFW_INFO("%s\n", delim);
    PFW_INFO("| %-32s | %s\n", "DOMAIN", "CONFIG");
    PFW_INFO("%s\n", delim);
    for (i = 0; (domain = pfw_vector_get(system->domains, i)); i++) {
        PFW_INFO("| %-32s | %s\n", domain->name,
            domain->current ? domain->current->name : "");
    }

    PFW_INFO("%s\n", delim);
}
