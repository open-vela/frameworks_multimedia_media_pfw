############################################################################
# pfw/test/Makefile
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

CC     := gcc
CSRCS  := $(wildcard ../*.c)
CSRCS  += test.c
CFLAGS := -Wall -Werror -O0 -g -I ../include -D CONFIG_LIB_PFW_DEBUG -fsanitize=address -fsanitize=leak

test: $(CSRCS)
	cc -o test $(CSRCS) $(CFLAGS)

clean:
	rm test
