/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_node_proxy.h"

#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace std;

IFMapNodeProxy::IFMapNodeProxy()
    : node_(NULL), id_(DBTable::kInvalidId) {
}

IFMapNodeProxy::IFMapNodeProxy(IFMapNodeProxy *rhs)
    : node_(NULL), id_(DBTable::kInvalidId) {
    Swap(rhs);
}

IFMapNodeProxy::IFMapNodeProxy(IFMapNode *node, DBTable::ListenerId lid)
    : node_(node), id_(lid) {
    node_->SetState(node_->table(), id_, this);
}

IFMapNodeProxy::~IFMapNodeProxy() {
    if (node_ != NULL) {
        node_->ClearState(node_->table(), id_);
    }
}

void IFMapNodeProxy::Swap(IFMapNodeProxy *rhs) {
    swap(node_, rhs->node_);
    swap(id_, rhs->id_);
    if (node_ != NULL) {
        node_->SetState(node_->table(), id_, this);
    }
    if (rhs->node_ != NULL) {
        rhs->node_->SetState(rhs->node_->table(), rhs->id_, rhs);
    }
}

void IFMapNodeProxy::Clear() {
    if (node_ != NULL) {
        node_->ClearState(node_->table(), id_);
        node_ = NULL;
    }
}
