/****************************************************************************
 * pfw/test/test.c
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

#include "pfw.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PFW_SUBSCRIBERS_MAX 32

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void pfw_change_callback(void* cookie, int num, char* value)
{
    printf("[%s] id:%d number:%d value:%s\n",
        __func__,(int)(intptr_t)cookie, num, value);
}

static void pfw_ffmpeg_command_callback(void* cookie, const char* params)
{
    printf("[%s] id:%d params:%s\n", __func__, (int)(intptr_t)cookie, params);
}

static void pfw_set_parameter_callback(void* cookie, const char* params)
{
    printf("[%s] id:%d params:%s\n", __func__, (int)(intptr_t)cookie, params);
}

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pfw_plugin_def_t plugins[] = {
    { "FFmpegCommand", NULL, pfw_ffmpeg_command_callback },
    { "SetParameter", NULL, pfw_set_parameter_callback }
};

static int nb_plugins = sizeof(plugins) / sizeof(plugins[0]);

void* subscribers[PFW_SUBSCRIBERS_MAX];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char* argv[])
{
    char buffer[512];
    void* handle;
    int ret = 0;

    handle = pfw_create("./criteria.txt", "./settings.pfw",
        plugins, nb_plugins, NULL, NULL, NULL);
    if (!handle) {
        printf("\n");
        return 0;
    }

    pfw_apply(handle);

    while (1) {
        char *cmd, *arg1, *arg2, *arg3, *saveptr, *dump;
        int i, res, res1;
        char resp[64];

        /* Consume command line. */

        printf("pfw> ");
        fgets(buffer, sizeof(buffer), stdin);
        if (strlen(buffer) <= 0)
            continue;

        cmd = strtok_r(buffer, " \t\n", &saveptr);
        if (!cmd)
            continue;

        arg1 = strtok_r(NULL, " \t\n", &saveptr);
        arg2 = strtok_r(NULL, " \t\n", &saveptr);
        arg3 = strtok_r(NULL, " \t\n", &saveptr);

        /* Command handle. */

        if (!strcmp(cmd, "subscribe")) {
            for (i = 0; i < PFW_SUBSCRIBERS_MAX; i++) {
                if (!subscribers[i]) {
                    subscribers[i] = pfw_subscribe(handle, arg1,
                        pfw_change_callback, (void*)(intptr_t)i + 1);
                    if (!subscribers[i]) {
                        ret = -EINVAL;
                    } else {
                        printf("Subscriber ID %d\n", i + 1);
                    }
                    break;
                }
            }
        } else if (!strcmp(cmd, "unsubscribe")) {
            i = strtol(arg1, NULL, 0) - 1;
            if (i < 0 || i >= PFW_SUBSCRIBERS_MAX || !subscribers[i]) {
                ret = -EINVAL;
            } else {
                pfw_unsubscribe(system, subscribers[i]);
            } 
        } else if (!strcmp(cmd, "apply")) {
            pfw_apply(handle);
        } else if (!strcmp(cmd, "dump")) {
            dump = pfw_dump(handle);
            printf("\n%s\n", dump);
            free(dump);
        } else if (!strcmp(cmd, "setint")) {
            ret = pfw_setint(handle, arg1, strtol(arg2, NULL, 0));
        } else if (!strcmp(cmd, "setstring")) {
            ret = pfw_setstring(handle, arg1, arg2);
        } else if (!strcmp(cmd, "include")) {
            ret = pfw_include(handle, arg1, arg2);
        } else if (!strcmp(cmd, "exclude")) {
            ret = pfw_exclude(handle, arg1, arg2);
        } else if (!strcmp(cmd, "increase")) {
            ret = pfw_increase(handle, arg1);
        } else if (!strcmp(cmd, "decrease")) {
            ret = pfw_decrease(handle, arg1);
        } else if (!strcmp(cmd, "getint")) {
            ret = pfw_getint(handle, arg1, &res);
            if (ret >= 0)
                printf("get %d\n", res);
        } else if (!strcmp(cmd, "getstring")) {
            ret = pfw_getstring(handle, arg1, resp, sizeof(resp));
            if (ret >= 0)
                printf("get %s\n", resp);
        } else if (!strcmp(cmd, "getrange")) {
            ret = pfw_getrange(handle, arg1, &res, &res1);
            if (ret >= 0)
                printf("get [%d,%d]\n", res, res1);
        } else if (!strcmp(cmd, "q")) {
            break;
        } else {
            printf("Unkown Command\n");
        }

        if (arg3 && strtol(arg3, NULL, 0) > 0)
            pfw_apply(handle);

        printf("ret %d\n", ret);
        ret = 0;
    }

    pfw_destroy(handle, NULL);
    return 0;
}
