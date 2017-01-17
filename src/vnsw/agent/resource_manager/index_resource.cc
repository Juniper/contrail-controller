/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/index_resource.h>

IndexResourceKey::IndexResourceKey(ResourceManager *rm,
                                   uint16_t type) :
    ResourceKey(rm, type) {
}

IndexResourceKey::~IndexResourceKey() {
}

IndexResourceData::IndexResourceData(ResourceManager *rm,
                                     IndexResourceTable *type) :
    ResourceData(rm), index_(), resource_type_(type) {
    resource_type_->AllocateIndex(this);
}

IndexResourceData::IndexResourceData(ResourceManager *rm,
                                     IndexResourceTable *type,
                                     uint32_t index) :
    ResourceData(rm), index_(index), resource_type_(type) {
    resource_type_->RestoreIndex(this);
}

IndexResourceData::~IndexResourceData() {
    resource_type_->ReleaseIndex(this);
}

uint32_t IndexResourceData::GetIndex() const {
    return index_;
}

void IndexResourceData::SetIndex(uint32_t index) {
    index_ = index;
}

IndexResourceTable::IndexResourceTable(ResourceManager *rm) :
    ResourceTable(rm) {
}

IndexResourceTable::~IndexResourceTable() {
}

void IndexResourceTable::ReserveIndex(uint32_t index) {
    index_vector_.InsertAtIndex(index, NULL);
}

void IndexResourceTable::ReleaseIndex(uint32_t index) {
    index_vector_.FreeIndex(index);
}

void IndexResourceTable::Release(uint32_t index) {
    IndexResourceData *data = dynamic_cast<IndexResourceData *>
        (index_vector_.FindIndex(index));
    resource_manager()->Release(data->key());
}

ResourceTable::DataPtr IndexResourceTable::AllocateData(ResourceKeyPtr key) {
    IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
        (key.get());
    //Allocate resource data
    ResourceTable::DataPtr data(static_cast<ResourceData *>
                               (new IndexResourceData(key->rm_,
            static_cast<IndexResourceTable *>(index_resource_key->resource_type_))));
    return data;
}


void IndexResourceTable::RestoreIndex(IndexResourceData *data) {
    index_vector_.InsertAtIndex(data->GetIndex(), data);
}

void IndexResourceTable::AllocateIndex(IndexResourceData *data) {
    //Get the index, populate resource data
    //TODO pass if rollover or use the first free index.
    data->SetIndex(index_vector_.AllocIndex());
    index_vector_.InsertAtIndex(data->GetIndex(), data);
}

void IndexResourceTable::ReleaseIndex(IndexResourceData *data) {
    index_vector_.FreeIndex(data->GetIndex());
}
