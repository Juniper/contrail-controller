/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_task_block_api_h
#define vnsw_agent_task_block_api_h

#include <stdlib.h>

typedef unsigned long block_t;

extern block_t task_block_init(size_t size, const char *name);
extern void *task_block_alloc(block_t block);
extern void task_block_free(block_t block, void *mem);

#endif /* vnsw_agent_task_block_api_h */
