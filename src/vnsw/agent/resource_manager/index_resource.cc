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
                                     IndexResourceTable *table,
                                     uint32_t index) :
    ResourceData(rm), index_(index), resource_table_(table) {
}

IndexResourceData::IndexResourceData(ResourceManager *rm,
                                     uint32_t index,
                                     ResourceManager::KeyPtr key) :
    ResourceData(rm), index_(index), resource_table_(
            static_cast<IndexResourceTable*>(key->resource_table_)) {
    resource_table_->RestoreIndex(index, key);
}

IndexResourceData::~IndexResourceData() {
    resource_table_->ReleaseIndex(this->GetIndex());
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
    ResourceKeyPtr dummy_key_entry;
    index_vector_.InsertAtIndex(index, dummy_key_entry);
}

void IndexResourceTable::ReleaseIndex(uint32_t index) {
    index_vector_.FreeIndex(index);
}

void IndexResourceTable::Release(uint32_t index) {
    ResourceKeyPtr key = index_vector_.FindIndex(index);
    resource_manager()->Release(key);
}

ResourceTable::DataPtr IndexResourceTable::AllocateData(ResourceKeyPtr key) {
    IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
        (key.get());
    IndexResourceTable *table = static_cast<IndexResourceTable *>(
            index_resource_key->resource_table_);
    uint32_t index = table->AllocateIndex(key);
    ResourceTable::DataPtr data(static_cast<ResourceData *>
            (new IndexResourceData(key->rm_, table, index)));
    return data;
}


void IndexResourceTable::RestoreIndex(uint32_t index, ResourceKeyPtr key) {
    index_vector_.InsertAtIndex(index, key);
}

uint32_t IndexResourceTable::AllocateIndex(ResourceKeyPtr key) {
    //Get the index, populate resource data
    //TODO pass if rollover or use the first free index.
    ResourceKeyPtr dummy_key_entry;
    uint32_t index = index_vector_.AllocIndex(dummy_key_entry);
    index_vector_.InsertAtIndex(index, key);
    return index;
}

