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
    pfw_listener_t *listener, *tmp;
    pfw_plugin_t* plugin;
    int i;

    for (i = 0; (plugin = pfw_vector_get(system->plugins, i)); i++) {
        LIST_FOREACH_SAFE(listener, &plugin->listeners, entry, tmp)
        {
            free(listener);
        }

        free(plugin->parameter);
        free(plugin->name);
        free(plugin);
    }

    pfw_vector_free(system->plugins);
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
 * @brief Apply paramter to plugin listeners.
 */
static void pfw_apply_acts(pfw_vector_t* action)
{
    char buffer[PFW_MAXLEN_AMMENDS];
    pfw_listener_t* listener;
    pfw_act_t* act;
    int i;

    for (i = 0; (act = pfw_vector_get(action, i)); i++) {
        pfw_apply_ammends(act->param, buffer, sizeof(buffer));
        LIST_FOREACH(listener, &act->plugin.p->listeners, entry)
        {
            listener->cb(listener->cookie, buffer);
        }

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

int pfw_getparameter(void* handle, const char* name, char* para, int len)
{
    pfw_system_t* system = handle;
    pfw_plugin_t* plugin;
    int i, ret = -EINVAL;

    if (!system || !name || !para)
        return ret;

    pthread_mutex_lock(&system->mutex);
    for (i = 0; (plugin = pfw_vector_get(system->plugins, i)); i++) {
        if (!strcmp(plugin->name, name)) {
            ret = snprintf(para, len, "%s", plugin->parameter);
            break;
        }
    }
    pthread_mutex_unlock(&system->mutex);

    return ret;
}

void* pfw_subscribe(void* handle, const char* name,
    void* cookie, pfw_callback_t cb)
{
    pfw_system_t* system = handle;
    void* listener = NULL;
    pfw_plugin_t* plugin;
    int i;

    if (!system || !name)
        return NULL;

    pthread_mutex_lock(&system->mutex);
    for (i = 0; (plugin = pfw_vector_get(system->plugins, i)); i++) {
        if (!strcmp(plugin->name, name)) {
            listener = pfw_listener_register(plugin, cookie, cb);
            break;
        }
    }
    pthread_mutex_unlock(&system->mutex);

    return listener;
}

void pfw_unsubscribe(void* handle, void* subscriber)
{
    pfw_system_t* system = handle;
    pfw_listener_t* listener = subscriber;

    if (!listener)
        return;

    pthread_mutex_lock(&system->mutex);
    LIST_REMOVE(listener, entry);
    pthread_mutex_unlock(&system->mutex);
    free(listener);
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

    LIST_INIT(&plugin->listeners);
    plugin->parameter = NULL;
    plugin->name = strdup(def->name);
    if (!plugin->name)
        goto err1;

    if (def->cb && !pfw_listener_register(plugin, def->cookie, def->cb))
        goto err2;

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
    pfw_plugin_def_t* defs, int nb, pfw_load_t load,
    pfw_save_t save, void *cookie)
{
    pfw_system_t* system;
    int i;

    system = calloc(1, sizeof(pfw_system_t));
    if (!system)
        return NULL;

    pthread_mutex_init(&system->mutex, NULL);

    system->load = load;
    system->save = save;
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
    pfw_destroy(system, system->release_cb);
    return NULL;
}

void pfw_destroy(void* handle, void *release_cb)
{
    pfw_system_t* system = handle;
    system->release_cb = release_cb;

    if (system) {
        if (system->release_cb)
            system->release_cb(system->cookie);
        pfw_context_destroy(system->criteria_ctx);
        pfw_context_destroy(system->settings_ctx);
        pfw_free_criteria(system->criteria);
        pfw_free_settings(system->domains);
        pfw_free_plugins(system);
        pthread_mutex_destroy(&system->mutex);
        free(system);
    }
}
