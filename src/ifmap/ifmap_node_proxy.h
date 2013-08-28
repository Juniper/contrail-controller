/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __ctrlplane__ifmap_node_proxy__
#define __ctrlplane__ifmap_node_proxy__

#include "base/util.h"
#include "db/db_entry.h"
#include "db/db_table.h"

class IFMapNode;

class IFMapNodeProxy : public DBState {
public:
    IFMapNodeProxy();
    IFMapNodeProxy(IFMapNodeProxy *rhs);
    IFMapNodeProxy(IFMapNode *node, DBTable::ListenerId lid);
    ~IFMapNodeProxy();
    IFMapNode *node() { return node_; }
    const IFMapNode *node() const { return node_; }
    void Swap(IFMapNodeProxy *rhs);
    void Clear();

private:
    IFMapNode *node_;
    DBTable::ListenerId id_;
    DISALLOW_COPY_AND_ASSIGN(IFMapNodeProxy);
};

#endif /* defined(__ctrlplane__ifmap_node_proxy__) */
