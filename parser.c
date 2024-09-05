/****************************************************************************
 * pfw/parser.c
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static pfw_interval_t* pfw_parse_interval(const char* word)
{
    pfw_interval_t* itv;

    itv = malloc(sizeof(pfw_interval_t));
    if (!itv)
        return NULL;

    if (2 == sscanf(word, "[%" PRId32 ",%" PRId32 "]", &itv->left, &itv->right))
        ;
    else if (1 == sscanf(word, "[%" PRId32 ",]", &itv->left))
        itv->right = INT32_MAX;
    else if (1 == sscanf(word, "[,%" PRId32 "]", &itv->right))
        itv->left = INT32_MIN;
    else
        itv->left = itv->right = strtol(word, NULL, 0);

    return itv;
}

static void pfw_free_rule(pfw_rule_t* rule)
{
    pfw_rule_t* sub;
    int i;

    if (!rule)
        return;

    for (i = 0; (sub = pfw_vector_get(rule->branches, i)); i++)
        pfw_free_rule(sub);

    if (rule->predicate == PFW_PREDICATE_IN
        || rule->predicate == PFW_PREDICATE_NOTIN)
        free(rule->state.itv);

    pfw_vector_free(rule->branches);
    free(rule);
}

static void pfw_free_ammends(pfw_vector_t* ammends)
{
    pfw_ammend_t* ammend;
    int i;

    for (i = 0; (ammend = pfw_vector_get(ammends, i)); i++)
        free(ammend);

    pfw_vector_free(ammends);
}

static void pfw_free_act(pfw_act_t* act)
{
    if (!act)
        return;

    pfw_free_ammends(act->param);
    free(act);
}

static void pfw_free_config(pfw_config_t* config)
{
    pfw_act_t* act;
    int i;

    pfw_free_rule(config->rules);
    for (i = 0; (act = pfw_vector_get(config->acts, i)); i++)
        pfw_free_act(act);

    pfw_free_ammends(config->name);
    pfw_vector_free(config->acts);
    free(config->current);
    free(config);
}

static void pfw_free_domain(pfw_domain_t* domain)
{
    pfw_config_t* config;
    int i;

    for (i = 0; (config = pfw_vector_get(domain->configs, i)); i++)
        pfw_free_config(config);

    pfw_vector_free(domain->configs);
    free(domain);
}

static void pfw_free_criterion(pfw_criterion_t* criterion)
{
    pfw_listener_t *listener, *tmp;
    pfw_interval_t* intervel;
    int i;

    if (!criterion)
        return;

    if (criterion->type == PFW_CRITERION_NUMERICAL) {
        for (i = 0; (intervel = pfw_vector_get(criterion->ranges, i)); i++)
            free(intervel);
    }

    LIST_FOREACH_SAFE(listener, &criterion->listeners, entry, tmp)
    {
        free(listener);
    }

    pfw_vector_free(criterion->ranges);
    pfw_vector_free(criterion->names);
    free(criterion);
}

static int pfw_parse_rule(pfw_context_t* ctx, pfw_rule_t** pr, int depth)
{
    pfw_rule_t *rule, *sub;
    char* word;
    bool branch;
    int ret, nb;

    ret = pfw_context_get_depth(ctx);
    if (ret != depth)
        return ret < 0 ? ret : EOF;

    rule = *pr = calloc(1, sizeof(pfw_rule_t));
    if (!rule)
        return -ENOMEM;

    word = pfw_context_take_word(ctx);
    if (!word) {
        PFW_DEBUG("Rule starts with NULL\n");
        ret = -EINVAL;
        goto err;
    } else if (!strcmp(word, "ALL")) {
        rule->predicate = PFW_PREDICATE_ALL;
        branch = true;
    } else if (!strcmp(word, "ANY")) {
        rule->predicate = PFW_PREDICATE_ANY;
        branch = true;
    } else {
        rule->criterion.def = word;
        branch = false;
    }

    if (branch) {
        /* Rule brances. */

        pfw_context_take_line(ctx);
        for (nb = 0;; nb++) {
            ret = pfw_parse_rule(ctx, &sub, depth + 1);
            if (ret == EOF)
                break;
            else if (ret < 0) {
                PFW_DEBUG("Rule branch %d invalid\n", nb);
                goto err;
            }

            ret = pfw_vector_append(&rule->branches, sub);
            if (ret < 0) {
                goto err;
            }
        }
    } else {
        /* Rule leaves. */

        word = pfw_context_take_word(ctx);
        if (!word) {
            PFW_DEBUG("Rule has no predicate\n");
            ret = -EINVAL;
            goto err;
        } else if (!strcmp(word, "Is")) {
            rule->predicate = PFW_PREDICATE_IS;
        } else if (!strcmp(word, "IsNot")) {
            rule->predicate = PFW_PREDICATE_ISNOT;
        } else if (!strcmp(word, "Excludes")) {
            rule->predicate = PFW_PREDICATE_EXCLUDES;
        } else if (!strcmp(word, "Includes")) {
            rule->predicate = PFW_PREDICATE_INCLUDES;
        } else if (!strcmp(word, "In")) {
            rule->predicate = PFW_PREDICATE_IN;
        } else if (!strcmp(word, "NotIn")) {
            rule->predicate = PFW_PREDICATE_NOTIN;
        } else {
            PFW_DEBUG("Rule use invalid predicate '%s'\n", word);
            ret = -EINVAL;
            goto err;
        }

        /* Parse state. */

        word = pfw_context_take_word(ctx);
        if (!word) {
            PFW_DEBUG("Rule has no state\n");
            ret = -EINVAL;
            goto err;
        }

        if (rule->predicate == PFW_PREDICATE_IN
            || rule->predicate == PFW_PREDICATE_NOTIN) {
            rule->state.itv = pfw_parse_interval(word);
            if (!rule->state.itv) {
                ret = -ENOMEM;
                goto err;
            }
        } else {
            rule->state.def = word;
        }

        pfw_context_take_line(ctx);
    }

    return 0;

err:
    pfw_free_rule(rule);
    return ret;
}

static int pfw_parse_ammends(pfw_context_t* ctx, pfw_vector_t** pv)
{
    pfw_ammend_t* ammend;
    char *word, *saveptr;
    int ret;

    word = pfw_context_take_line(ctx);
    if (!word)
        return -EINVAL;

    word = strtok_r(word, "%", &saveptr);
    while (word) {
        ammend = calloc(1, sizeof(pfw_ammend_t));
        if (!ammend)
            return -ENOMEM;

        ammend->u.raw = word;
        ret = pfw_vector_append(pv, ammend);
        if (ret < 0) {
            free(ammend);
            return ret;
        }

        word = strtok_r(NULL, "%", &saveptr);
    }

    return 0;
}

static int pfw_parse_act(pfw_context_t* ctx, pfw_act_t** pa)
{
    pfw_act_t* act;
    char* word;
    int ret;

    ret = pfw_context_get_depth(ctx);
    if (ret != 2)
        return ret < 0 ? ret : EOF;

    act = *pa = calloc(1, sizeof(pfw_act_t));
    if (!act)
        return -ENOMEM;

    /* plugin name. */

    word = pfw_context_take_word(ctx);
    if (!word) {
        PFW_DEBUG("Act has no plugin name\n");
        ret = -EINVAL;
        goto err;
    }

    act->plugin.def = word;

    word = pfw_context_take_word(ctx);
    if (!word || strcmp(word, "=")) {
        PFW_DEBUG("Act should use '=' instead of '%s'\n", word);
        ret = -EINVAL;
        goto err;
    }

    /* callback parameters. */

    ret = pfw_parse_ammends(ctx, &act->param);
    if (ret < 0)
        goto err;

    return 0;

err:
    pfw_free_act(act);
    return ret;
}

static int pfw_parse_config(pfw_context_t* ctx, pfw_config_t** pc)
{
    pfw_config_t* config;
    pfw_rule_t* rule = NULL;
    pfw_act_t* act;
    char* word;
    int ret, nb;

    ret = pfw_context_get_depth(ctx);
    if (ret != 1)
        return ret < 0 ? ret : EOF;

    config = *pc = calloc(1, sizeof(pfw_config_t));
    if (!config)
        return -ENOMEM;

    /* config name. */

    word = pfw_context_take_word(ctx);
    if (!word || strcmp(word, "conf:")) {
        PFW_DEBUG("Conf start with '%s'\n", word);
        return -EINVAL;
    }

    ret = pfw_parse_ammends(ctx, &config->name);
    if (ret < 0)
        return ret;

    /* config rules. */

    ret = pfw_parse_rule(ctx, &rule, 2);
    if (ret < 0 && ret != EOF) {
        PFW_DEBUG("Conf uses invalid rules\n");
        goto err;
    }

    config->rules = rule;

    /* config acts. */

    for (nb = 0;; nb++) {
        ret = pfw_parse_act(ctx, &act);
        if (ret == EOF)
            break;
        else if (ret < 0) {
            PFW_DEBUG("Conf uses invalid act\n");
            goto err;
        }

        ret = pfw_vector_append(&config->acts, act);
        if (ret < 0) {
            pfw_free_act(act);
            goto err;
        }
    }

    return 0;

err:
    pfw_free_config(config);
    return ret;
}

static int pfw_parse_domain(pfw_context_t* ctx, pfw_domain_t** pd)
{
    pfw_domain_t* domain;
    pfw_config_t* config;
    char* word;
    int ret, nb;

    ret = pfw_context_get_depth(ctx);
    if (ret < 0)
        return ret;

    domain = *pd = calloc(1, sizeof(pfw_domain_t));
    if (!domain)
        return -ENOMEM;

    /* domain name. */

    word = pfw_context_take_word(ctx);
    if (!word || strcmp(word, "domain:")) {
        PFW_DEBUG("Domain start with '%s'\n", word);
        ret = -EINVAL;
        goto err;
    }

    word = pfw_context_take_word(ctx);
    if (!word) {
        PFW_DEBUG("Domain has no name\n");
        ret = -EINVAL;
        goto err;
    }

    domain->name = word;
    pfw_context_take_line(ctx);

    /* configs. */

    for (nb = 0;; nb++) {
        ret = pfw_parse_config(ctx, &config);
        if (ret == EOF)
            break;
        else if (ret < 0) {
            PFW_DEBUG("Domain '%s' 's %dth config is invalid\n", domain->name, nb);
            goto err;
        }

        ret = pfw_vector_append(&domain->configs, config);
        if (ret < 0) {
            pfw_free_config(config);
            goto err;
        }
    }

    return 0;

err:
    pfw_free_domain(domain);
    return ret;
}

static int pfw_parse_criterion(pfw_context_t* ctx, pfw_criterion_t** pc)
{
    pfw_criterion_t* criterion;
    char* word;
    int ret, nb;

    ret = pfw_context_get_depth(ctx);
    if (ret < 0)
        return ret;

    criterion = *pc = calloc(1, sizeof(pfw_criterion_t));
    if (!criterion)
        return -ENOMEM;

    /* criterion type. */

    word = pfw_context_take_word(ctx);
    if (!word) {
        PFW_DEBUG("Criterion starts with NULL\n");
        ret = -EINVAL;
        goto err;
    }
    if (!strcmp(word, "NumericalCriterion")) {
        criterion->type = PFW_CRITERION_NUMERICAL;
    } else if (!strcmp(word, "ExclusiveCriterion")) {
        criterion->type = PFW_CRITERION_EXCLUSIVE;
    } else if (!strcmp(word, "InclusiveCriterion")) {
        criterion->type = PFW_CRITERION_INCLUSIVE;
    } else {
        PFW_DEBUG("Criterion has invalid type '%s'\n", word);
        ret = -EINVAL;
        goto err;
    }

    /* criterion names. */

    for (nb = 0;; nb++) {
        word = pfw_context_take_word(ctx);
        if (!word) {
            PFW_DEBUG("Criterion has no ranges after %d names\n", nb);
            ret = -EINVAL;
            goto err;
        } else if (!strcmp(word, ":"))
            break;

        ret = pfw_vector_append(&criterion->names, word);
        if (ret < 0)
            goto err;
    }

    /* criterion ranges. */

    for (nb = 0;; nb++) {
        word = pfw_context_take_word(ctx);
        if (!word) {
            if (nb == 0) {
                PFW_DEBUG("Criterion has no ranges after ':'\n");
                ret = -EINVAL;
                goto err;
            }
            break;
        } else if (!strcmp(word, "=")) {
            word = pfw_context_take_word(ctx);
            if (!word) {
                PFW_DEBUG("Criterion has no value after '='\n");
                ret = -EINVAL;
                goto err;
            }
            criterion->init.def = (void*)word;
            break;
        }

        if (criterion->type == PFW_CRITERION_NUMERICAL) {
            pfw_interval_t* itv;

            itv = pfw_parse_interval(word);
            if (!itv) {
                ret = -ENOMEM;
                goto err;
            }

            ret = pfw_vector_append(&criterion->ranges, itv);
            if (ret < 0) {
                free(itv);
                goto err;
            }
        } else {
            if (criterion->type == PFW_CRITERION_INCLUSIVE && nb > 31) {
                PFW_DEBUG("InclusiveCriterion's ranges has %d over 31\n", nb);
                goto err;
            }

            ret = pfw_vector_append(&criterion->ranges, word);
        }
    }

    pfw_context_take_line(ctx);
    return ret;

err:
    pfw_free_criterion(criterion);
    return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pfw_free_settings(pfw_vector_t* settings)
{
    pfw_domain_t* domain;
    int i;

    for (i = 0; (domain = pfw_vector_get(settings, i)); i++)
        pfw_free_domain(domain);

    pfw_vector_free(settings);
}

int pfw_parse_settings(pfw_context_t* ctx, pfw_vector_t** p)
{
    pfw_domain_t* domain;
    int ret, nb;

    for (nb = 0;; nb++) {
        ret = pfw_parse_domain(ctx, &domain);
        if (ret == EOF)
            break;
        else if (ret < 0) {
            PFW_DEBUG("Invalid %dth domain\n", nb);
            return ret;
        }

        ret = pfw_vector_append(p, domain);
        if (ret < 0) {
            pfw_free_domain(domain);
            return ret;
        }
    }

    return nb;
}

void pfw_free_criteria(pfw_vector_t* criteria)
{
    pfw_criterion_t* criterion;
    int i;

    for (i = 0; (criterion = pfw_vector_get(criteria, i)); i++)
        pfw_free_criterion(criterion);

    pfw_vector_free(criteria);
}

int pfw_parse_criteria(pfw_context_t* ctx, pfw_vector_t** p)
{
    pfw_criterion_t* criterion = NULL;
    int ret, nb;

    for (nb = 0;; nb++) {
        ret = pfw_parse_criterion(ctx, &criterion);
        if (ret == EOF)
            break;
        else if (ret < 0) {
            PFW_DEBUG("Invalid %dth criterion\n", nb);
            return ret;
        }

        LIST_INIT(&criterion->listeners);

        ret = pfw_vector_append(p, criterion);
        if (ret < 0) {
            pfw_free_criterion(criterion);
            return ret;
        }
    }

    return nb;
}
