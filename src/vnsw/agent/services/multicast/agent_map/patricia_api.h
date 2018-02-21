/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_patricia_api_h
#define vnsw_agent_patricia_api_h

#include "mcast_common.h"

typedef struct patroot_ {
    void *agent_patroot;
} patroot;

typedef struct patnode_ {
    void *agent_patnode;
} patnode;

#define PATNODE_TO_STRUCT(function, structure, pat_node)                    \
    static inline structure *(function)(patnode *node) {                    \
        if (node) {                                                         \
            return (structure *)((char *)node - offsetof(structure, pat_node));     \
        }                                                                   \
        return NULL;                                                        \
    }

extern patroot *patricia_root_init(patroot *root, boolean key_is_ptr,
                        uint16_t klen, uint8_t offset);
extern void patricia_root_delete(patroot *root);
extern boolean patricia_add(patroot *root, patnode *node);
extern boolean patricia_delete(patroot *root, patnode *node);
extern patnode *patricia_lookup(patroot *root, const void *key);
extern patnode *patricia_lookup_least(patroot *root);
extern patnode *patricia_lookup_greatest(patroot *root);
extern patnode *patricia_get_next(patroot *root, patnode *node);
extern patnode *patricia_get_previous(patroot *root, patnode *node);
extern patnode *patricia_lookup_geq(patroot *root, patnode *node);

#endif /* vnsw_agent_patricia_api_h */
