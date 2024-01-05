/****************************************************************************
 * pfw/criterion.c
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
 * Pre-processor Definitions
 ****************************************************************************/

#define PFW_CRITERION_DELIM "|"
#define PFW_CRITERION_EMPTY "<none>"
#define PFW_CRITERION_MAX_LITERAL 256

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/**
 * @brief Check wether leaf node rule matches.
 */
static inline bool pfw_rule_match_atomic(pfw_rule_t* rule)
{
    pfw_interval_t* itv = rule->state.itv;
    int32_t s1 = rule->criterion.p->state;
    int32_t s2 = rule->state.v;

    switch (rule->predicate) {
    case PFW_PREDICATE_IS:
        return s1 == s2;

    case PFW_PREDICATE_ISNOT:
        return s1 != s2;

    case PFW_PREDICATE_INCLUDES:
        return s1 & s2;

    case PFW_PREDICATE_EXCLUDES:
        return !(s1 & s2);

    case PFW_PREDICATE_IN:
        return s1 >= itv->left && s1 <= itv->right;

    case PFW_PREDICATE_NOTIN:
        return s1 < itv->left || s1 > itv->right;
    }

    return false;
}

/**
 * @brief Convert literal state to int/bit.
 * @note For ExclusiveCriterion and InclusiveCriterion only.
 */
static int pfw_criterion_atoi_atomic(pfw_criterion_t* criterion,
    const char* value, int32_t* state)
{
    const char* str;
    int i;

    for (i = 0; (str = pfw_vector_get(criterion->ranges, i)); i++) {
        if (!strcmp(str, value)) {
            if (criterion->type == PFW_CRITERION_EXCLUSIVE)
                *state = i;
            else
                *state |= (1 << i);
            return 0;
        }
    }

    return -EINVAL;
}

/**
 * @brief Convert int/bit state to literal.
 * @note For ExclusiveCriterion and InclusiveCriterion only.
 */
static int pfw_criterion_itoa_atomic(pfw_criterion_t* criterion,
    int32_t state, char* res, int len, bool first)
{
    const char* str;

    str = pfw_vector_get(criterion->ranges, state);
    if (str) {
        if (first)
            return snprintf(res, len, "%s", str);
        else
            return snprintf(res, len, "%s%s", PFW_CRITERION_DELIM, str);
    }

    return -EINVAL;
}

static bool pfw_criterion_check_integer(pfw_criterion_t* criterion,
    int32_t state)
{
    char tmp[PFW_CRITERION_MAX_LITERAL];
    pfw_interval_t* interval;
    int i;

    switch (criterion->type) {
    case PFW_CRITERION_NUMERICAL:
        for (i = 0; (interval = pfw_vector_get(criterion->ranges, i)); i++) {
            if (interval->left <= state && state <= interval->right)
                return true;
        }
        return false;

    case PFW_CRITERION_EXCLUSIVE:
    case PFW_CRITERION_INCLUSIVE:
        return pfw_criterion_itoa(criterion, state, tmp, sizeof(tmp))
            >= 0;
    }

    return false;
}

/**
 * @brief Modify criterion state and auto-save.
 */
static inline void
pfw_criterion_set(void* handle, pfw_criterion_t* criterion, int32_t state)
{
    char literal[PFW_CRITERION_MAX_LITERAL];
    pfw_system_t* system = handle;
    pfw_listener_t* listener;
    int ret;

    if (criterion->state != state) {
        criterion->state = state;
        ret = pfw_criterion_itoa(criterion, state, literal, PFW_CRITERION_MAX_LITERAL);
        LIST_FOREACH(listener, &criterion->listeners, entry)
        {
            listener->on_change(listener->cookie, criterion->state, ret < 0 ? NULL : literal);
        }
        if (system->on_save)
            system->on_save(system->cookie, pfw_vector_get(criterion->names, 0), state);
    }
}

/**
 * @brief Modify InclusiveCriterion.
 */
static int pfw_adjust_inclusive(void* handle, const char* name,
    const char* value, bool include)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int32_t state;
    int ret;

    if (!system || !value)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    if (criterion->type != PFW_CRITERION_INCLUSIVE)
        return -EPERM;

    ret = pfw_criterion_atoi(criterion, value, &state);
    if (ret < 0)
        return -EINVAL;

    if (include)
        state = criterion->state | state;
    else
        state = criterion->state & ~state;

    pfw_criterion_set(handle, criterion, state);
    return 0;
}

/**
 * @brief Modify NumericalCriterion.
 */
static int pfw_adjust_numerical(void* handle, const char* name,
    bool increase)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int32_t state;

    if (!system)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    if (criterion->type != PFW_CRITERION_NUMERICAL)
        return -EPERM;

    if (increase)
        state = criterion->state + 1;
    else
        state = criterion->state - 1;

    if (!pfw_criterion_check_integer(criterion, state))
        return -EINVAL;

    pfw_criterion_set(handle, criterion, state);
    return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/**
 * @brief Check wether branch node rule matches.
 */
bool pfw_rule_match(pfw_rule_t* rule)
{
    pfw_rule_t* sub;
    int i;

    switch (rule->predicate) {
    case PFW_PREDICATE_ALL:
        for (i = 0; (sub = pfw_vector_get(rule->branches, i)); i++) {
            if (!pfw_rule_match(sub))
                return false;
        }

        return true;

    case PFW_PREDICATE_ANY:
        for (i = 0; (sub = pfw_vector_get(rule->branches, i)); i++) {
            if (pfw_rule_match(sub))
                return true;
        }

        return i == 0;
    }

    return pfw_rule_match_atomic(rule);
}

/**
 * @brief Convert literal state to numerical state.
 */
int pfw_criterion_atoi(pfw_criterion_t* criterion,
    const char* value, int32_t* state)
{
    char tmp[PFW_CRITERION_MAX_LITERAL];
    char *token, *saveptr;
    int ret;

    switch (criterion->type) {
    case PFW_CRITERION_NUMERICAL:
        *state = strtol(value, NULL, 0);
        return 0;

    case PFW_CRITERION_EXCLUSIVE:
        return pfw_criterion_atoi_atomic(criterion, value, state);

    case PFW_CRITERION_INCLUSIVE:
        *state = 0;
        if (!strcmp(value, PFW_CRITERION_EMPTY))
            return 0;

        strncpy(tmp, value, sizeof(tmp));
        tmp[PFW_CRITERION_MAX_LITERAL - 1] = '\0';
        for (token = strtok_r(tmp, PFW_CRITERION_DELIM, &saveptr); token;
             token = strtok_r(NULL, PFW_CRITERION_DELIM, &saveptr)) {
            ret = pfw_criterion_atoi_atomic(criterion, token, state);
            if (ret < 0)
                return ret;
        }

        return 0;
    }

    return -EINVAL;
}

/**
 * @brief Convert numerical state to literal state.
 * @note NumericalCriterion has no literal state.
 */
int pfw_criterion_itoa(pfw_criterion_t* criterion,
    int32_t state, char* res, int len)
{
    int cnt = 0, i, ret;
    bool first = true;

    switch (criterion->type) {
    case PFW_CRITERION_NUMERICAL:
        return -EINVAL;

    case PFW_CRITERION_EXCLUSIVE:
        return pfw_criterion_itoa_atomic(criterion,
            state, res, len, first);

    case PFW_CRITERION_INCLUSIVE:
        if (state == 0)
            return snprintf(res, len, "%s", PFW_CRITERION_EMPTY);

        for (i = 0; i < 31; i++) {
            if (!(state & (1 << i)))
                continue;

            ret = pfw_criterion_itoa_atomic(criterion,
                i, res + cnt, len - cnt, first);
            if (ret < 0)
                break;

            first = false;
            cnt += ret;

            if (cnt >= len)
                break;
        }

        return cnt;
    }

    return -EINVAL;
}

pfw_criterion_t* pfw_criteria_find(pfw_vector_t* criteria, const char* target)
{
    pfw_criterion_t* criterion;
    const char* name;
    int i, j;

    if (!target)
        return NULL;

    for (i = 0; (criterion = pfw_vector_get(criteria, i)); i++) {
        for (j = 0; (name = pfw_vector_get(criterion->names, j)); j++) {
            if (!strcmp(target, name))
                return criterion;
        }
    }

    return NULL;
}

/* Criterion (un)subscribe.*/

void* pfw_subscribe(void* handle, const char* name, pfw_listen_t cb, void* cookie)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    pfw_listener_t* listener;

    if (!system || !name)
        return NULL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return NULL;

    listener = malloc(sizeof(pfw_listener_t));
    if (!listener)
        return NULL;

    listener->on_change = cb;
    listener->cookie = cookie;

    pthread_mutex_lock(&system->mutex);
    LIST_INSERT_HEAD(&criterion->listeners, listener, entry);
    pthread_mutex_unlock(&system->mutex);

    return listener;
}

void pfw_unsubscribe(void* handle, void* subscriber)
{
    pfw_listener_t* listener = subscriber;
    pfw_system_t* system = handle;

    if (!system || !listener)
        return;

    pthread_mutex_lock(&system->mutex);
    LIST_REMOVE(listener, entry);
    pthread_mutex_unlock(&system->mutex);
    free(listener);
}

/* Criterion modify. */

int pfw_setint(void* handle, const char* name, int value)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int ret = -EINVAL;

    if (!system || !name)
        return ret;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return ret;

    pthread_mutex_lock(&system->mutex);
    if (pfw_criterion_check_integer(criterion, value)) {
        pfw_criterion_set(handle, criterion, value);
        ret = 0;
    }
    pthread_mutex_unlock(&system->mutex);

    return ret;
}

int pfw_setstring(void* handle, const char* name, const char* value)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int32_t state;
    int ret;

    if (!system || !name || !value)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    pthread_mutex_lock(&system->mutex);
    ret = pfw_criterion_atoi(criterion, value, &state);
    if (ret >= 0)
        pfw_criterion_set(handle, criterion, state);
    pthread_mutex_unlock(&system->mutex);

    return 0;
}

int pfw_include(void* handle, const char* name, const char* value)
{
    return pfw_adjust_inclusive(handle, name, value, true);
}

int pfw_exclude(void* handle, const char* name, const char* value)
{
    return pfw_adjust_inclusive(handle, name, value, false);
}

int pfw_increase(void* handle, const char* name)
{
    return pfw_adjust_numerical(handle, name, true);
}

int pfw_decrease(void* handle, const char* name)
{
    return pfw_adjust_numerical(handle, name, false);
}

int pfw_reset(void* handle, const char* name)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;

    if (!system || !name)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    pthread_mutex_lock(&system->mutex);
    pfw_criterion_set(handle, criterion, criterion->init.v);
    pthread_mutex_unlock(&system->mutex);

    return 0;
}

/* Criterion query. */

int pfw_getint(void* handle, const char* name, int* value)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;

    if (!system || !name || !value)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    pthread_mutex_lock(&system->mutex);
    *value = criterion->state;
    pthread_mutex_unlock(&system->mutex);

    return 0;
}

int pfw_getstring(void* handle, const char* name, char* value, int len)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int ret;

    if (!system || !name || !value)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    if (criterion->type == PFW_CRITERION_NUMERICAL)
        return -EPERM;

    pthread_mutex_lock(&system->mutex);
    ret = pfw_criterion_itoa(criterion, criterion->state, value, len);
    pthread_mutex_unlock(&system->mutex);

    return ret;
}

int pfw_getrange(void* handle, const char* name, int* min_value, int* max_value)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    pfw_interval_t* interval;

    if (!system || !name)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    if (criterion->type != PFW_CRITERION_NUMERICAL)
        return -EPERM;

    interval = pfw_vector_get(criterion->ranges, 0);
    if (!interval)
        return -EINVAL;

    /* Numerical criteria with nb_ranges > 1 are ambigious. */
    if (pfw_vector_get(criterion->ranges, 1))
        return -ENOSYS;

    if (min_value)
        *min_value = interval->left;

    if (max_value)
        *max_value = interval->right;

    return 0;
}

int pfw_contain(void* handle, const char* name, const char* value,
    int* contain)
{
    pfw_system_t* system = handle;
    pfw_criterion_t* criterion;
    int32_t state;
    int ret;

    if (!system || !name || !value || !contain)
        return -EINVAL;

    criterion = pfw_criteria_find(system->criteria, name);
    if (!criterion)
        return -EINVAL;

    if (criterion->type != PFW_CRITERION_INCLUSIVE)
        return -EPERM;

    pthread_mutex_lock(&system->mutex);
    ret = pfw_criterion_atoi(criterion, value, &state);
    if (ret >= 0)
        *contain = !!(criterion->state & state);
    pthread_mutex_unlock(&system->mutex);

    return ret;
}
