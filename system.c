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
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pfw_free_plugins(pfw_system_t* system)
{
    pfw_plugin_t* plugin;
    int i;

    for (i = 0; (plugin = pfw_vector_get(system->plugins, i)); i++) {
        free(plugin->parameter);
        free(plugin->name);
        free(plugin);
    }

    pfw_vector_free(system->plugins);
}

/**
 * @brief Convert ammends to string.
 */
static void pfw_apply_ammends(pfw_vector_t* ammends, char* res, int len)
{
    pfw_criterion_t* criterion;
    pfw_ammend_t* ammend;
    int pos = 0, ret, i;

    for (i = 0; (ammend = pfw_vector_get(ammends, i)); i++) {
        criterion = ammend->u.criterion;

        if (ammend->type == PFW_AMMEND_RAW) {
            ret = snprintf(res + pos, len - pos, "%s", ammend->u.raw);
        } else if (ammend->u.criterion->type == PFW_CRITERION_NUMERICAL) {
            ret = snprintf(res + pos, len - pos, "%" PRId32, criterion->state);
        } else {
            ret = pfw_criterion_itoa(criterion, criterion->state,
                res + pos, len - pos);
        }

        if (ret < 0)
            return;

        pos += ret;

        if (pos >= len)
            return;
    }
}

/**
 * @brief Check wether apply needed, and update 'current' field.
 */
static bool pfw_apply_need(pfw_domain_t* domain, pfw_config_t* config)
{
    char buffer[PFW_MAXLEN_AMMENDS];
    bool apply = false;

    if (domain->current != config) {
        domain->current = config;
        apply = true;
    }

    pfw_apply_ammends(config->name, buffer, sizeof(buffer));
    if (apply || !config->current || strcmp(config->current, buffer)) {
        free(config->current);
        config->current = strdup(buffer);
        return true;
    }

    return false;
}

/**
 * @brief Apply paramter to plugin callback.
 */
static void pfw_apply_acts(pfw_vector_t* action)
{
    char buffer[PFW_MAXLEN_AMMENDS];
    pfw_act_t* act;
    int i;

    for (i = 0; (act = pfw_vector_get(action, i)); i++) {
        pfw_apply_ammends(act->param, buffer, sizeof(buffer));
        act->plugin.p->cb(act->plugin.p->cookie, buffer);
        free(act->plugin.p->parameter);
        act->plugin.p->parameter = strdup(buffer);
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
    int i, j;

    if (!system)
        return;

    pthread_mutex_lock(&system->mutex);
    for (i = 0; (domain = pfw_vector_get(system->domains, i)); i++) {
        for (j = 0; (config = pfw_vector_get(domain->configs, j)); j++) {
            if (pfw_rule_match(config->rules)) {
                if (pfw_apply_need(domain, config)) {
                    syslog(LOG_INFO, "pfw domain:%s switch to conf:%s\n", domain->name, config->current);
                    pfw_apply_acts(config->acts);
                }
                break;
            }
        }
    }
    pthread_mutex_unlock(&system->mutex);
}

void* pfw_plugin_register(pfw_system_t* system, pfw_plugin_def_t* def)
{
    pfw_plugin_t* plugin;
    int ret = -ENOMEM;

    if (!def || !def->name)
        return NULL;

    plugin = malloc(sizeof(pfw_plugin_t));
    if (!plugin)
        return NULL;

    plugin->parameter = NULL;
    plugin->cookie = def->cookie;
    plugin->cb = def->cb;
    plugin->name = strdup(def->name);
    if (!plugin->name)
        goto err1;

    ret = pfw_vector_append(&system->plugins, plugin);
    if (ret < 0)
        goto err2;

    return plugin;

err2:
    free(plugin->name);

err1:
    free(plugin);
    return NULL;
}

void* pfw_create(const char* criteria, const char* settings,
    pfw_plugin_def_t* defs, int nb, pfw_load_t on_load,
    pfw_save_t on_save, void* cookie)
{
    pfw_system_t* system;
    int i;

    system = calloc(1, sizeof(pfw_system_t));
    if (!system)
        return NULL;

    pthread_mutex_init(&system->mutex, NULL);

    system->on_load = on_load;
    system->on_save = on_save;
    system->cookie = cookie;

    for (i = 0; i < nb; i++) {
        if (!pfw_plugin_register(system, &defs[i]))
            goto err;
    }

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
    return NULL;
}

void pfw_destroy(void* handle, pfw_release_t on_release)
{
    pfw_system_t* system = handle;

    if (system) {
        if (on_release)
            on_release(system->cookie);

        pfw_context_destroy(system->criteria_ctx);
        pfw_context_destroy(system->settings_ctx);
        pfw_free_criteria(system->criteria);
        pfw_free_settings(system->domains);
        pfw_free_plugins(system);
        pthread_mutex_destroy(&system->mutex);
        free(system);
    }
}
