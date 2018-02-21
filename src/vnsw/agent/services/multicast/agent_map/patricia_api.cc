/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "patricia_map.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "patricia_api.h"
#ifdef __cplusplus
}
#endif

patroot *patricia_root_init(patroot *root, boolean key_is_ptr, uint16_t klen,
                        uint8_t koffset)
{
    patroot *pat_root = NULL;

    pat_root = (patroot *)malloc(sizeof(patroot));
    if (!pat_root) {
        return NULL;
    }

    PatriciaMapTable *agent_patroot = new PatriciaMapTable(klen, koffset);
    if (!agent_patroot) {
        free(pat_root);
        return NULL;
    }

    pat_root->agent_patroot = agent_patroot;

    return pat_root;
}

void patricia_root_delete(patroot *root)
{
    if (!root) {
        return;
    }

    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;
    delete agent_patroot;

    free(root);

    return;
}

boolean patricia_add(patroot *root, patnode *node)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = new PatriciaNode(agent_patroot->klen_,
                                        agent_patroot->koffset_, node, node);

    bool ret = agent_patroot->Insert(agent_patnode);
    if (!ret) {
        delete agent_patnode;
        return FALSE;
    }

    node->agent_patnode = agent_patnode;
    return TRUE;
}

boolean patricia_delete(patroot *root, patnode *node)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = (PatriciaNode *)node->agent_patnode;

    bool ret = agent_patroot->Remove(agent_patnode);
    if (!ret) {
        return FALSE;
    }

    delete (PatriciaNode *)node->agent_patnode;

    return TRUE;
}

patnode *patricia_lookup(patroot *root, const void *key)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = new PatriciaNode(agent_patroot->klen_,
                                        0, NULL, key);
    if (!agent_patnode) {
        return NULL;
    }

    PatriciaNode *found = agent_patroot->Find(agent_patnode);

    delete agent_patnode;
    if (!found) {
        return NULL;
    }

    patnode *node = (patnode *)found->knode_;
    return node;
}

patnode *patricia_lookup_least(patroot *root)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = NULL;
    agent_patnode = agent_patroot->GetNext(agent_patnode);
    if (!agent_patnode) {
        return NULL;
    }

    patnode *node = (patnode *)agent_patnode->knode_;

    return node;
}

patnode *patricia_lookup_greatest(patroot *root)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = agent_patroot->GetLast();
    if (!agent_patnode) {
        return NULL;
    }

    patnode *node = (patnode *)agent_patnode->knode_;

    return node;
}

patnode *patricia_get_next(patroot *root, patnode *node)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = node ? (PatriciaNode *)node->agent_patnode : NULL;
    agent_patnode = agent_patroot->GetNext(agent_patnode);
    if (!agent_patnode) {
        return NULL;
    }

    return (patnode *)agent_patnode->knode_;
}

patnode *patricia_get_previous(patroot *root, patnode *node)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = (PatriciaNode *)node->agent_patnode;
    agent_patnode = agent_patroot->GetPrev(agent_patnode);
    if (!agent_patnode) {
        return NULL;
    }

    return (patnode *)agent_patnode->knode_;
}

patnode *patricia_lookup_geq(patroot *root, patnode *node)
{
    PatriciaMapTable *agent_patroot = (PatriciaMapTable *)root->agent_patroot;

    PatriciaNode *agent_patnode = new PatriciaNode(agent_patroot->klen_,
                                        agent_patroot->koffset_, node, node);
    if (!agent_patnode) {
        return NULL;
    }

    PatriciaNode *found = agent_patroot->Find(agent_patnode);

    if (found) {
        delete agent_patnode;
        return (patnode *)found->knode_;
    }

    found = agent_patroot->GetNext(agent_patnode);

    delete agent_patnode;
    if (!found) {
        return NULL;
    }

    return (patnode *)found->knode_;
}

