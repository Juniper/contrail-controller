/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

/* Pick up the local environment.  We probably ought to be more picky here. */

#ifndef BVX_ENVIRONMENT_H
#define BVX_ENVIRONMENT_H

#include "gmpx_basic_types.h"

/* Map all patricia stuff directly. */

#define bvx_patnode patnode
#define bvx_patroot patroot
#define BVX_PATNODE_TO_STRUCT PATNODE_TO_STRUCT
#define bvx_patricia_lookup_least patricia_lookup_least
#define bvx_patricia_lookup_greatest patricia_lookup_greatest
#define bvx_patroot_init(keylen, offset) \
    patricia_root_init(NULL, FALSE, (keylen), (offset))
#define BVX_PATRICIA_OFFSET STRUCT_OFFSET
#define bvx_patricia_add patricia_add
#define bvx_patroot_destroy patricia_root_delete
#define bvx_patricia_get_next patricia_get_next
#define bvx_patricia_lookup patricia_lookup
#define bvx_patricia_delete patricia_delete

/* Same with the memory block stuff. */

#define bvx_block_tag block_t
#define bvx_malloc_block task_block_alloc
#define bvx_free_block task_block_free
#define bvx_malloc_block_create task_block_init

/* And with assertion. */

#define bvx_assert assert

#define BVX_UNUSED UNUSED

/*
 * Define the bit vector entry size.
 */
#define BV_BITSIZE_LOG2 5        /* 32 bits per entry for now */

/*
 * Define the size of a bit vector word.
 */
typedef uint32_t bv_word_t;        /* Bit vector word type */

#endif
