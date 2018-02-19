/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
#include "task_block_api.h"
#ifdef __cplusplus
}
#endif

block_t task_block_init(size_t size, const char *name)
{
    return (block_t)size;
}

void *task_block_alloc(block_t block)
{
    void *memory = malloc((size_t)block);
    if (!memory) {
        return NULL;
    }

    memset(memory, 0x00, block);

    return memory;
}

void task_block_free(block_t block, void *mem)
{
    free(mem);
}

