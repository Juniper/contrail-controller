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
                                   Resource::Type resource_key_type) :
    ResourceKey(rm, resource_key_type) {
}

IndexResourceKey::~IndexResourceKey() {
}

IndexResourceData::IndexResourceData(ResourceManager *rm, uint32_t index) :
    ResourceData(rm), index_(index) {
}

IndexResourceData::~IndexResourceData() {

}

uint32_t IndexResourceData::index() const {
    return index_;
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
    if (key.get()) {
        resource_manager()->Release(key);
    }
}

ResourceTable::DataPtr IndexResourceTable::AllocateData(ResourceKeyPtr key) {
    uint32_t index = AllocateIndex(key);
    ResourceTable::DataPtr data(static_cast<ResourceData *>
            (new IndexResourceData(key->rm_, index)));
    return data;
}


void IndexResourceTable::RestoreIndex(uint32_t index, ResourceKeyPtr key) {
    index_vector_.InsertAtIndex(index, key);
}

uint32_t IndexResourceTable::AllocateIndex(ResourceKeyPtr key) {
    //Get the index, populate resource data
    //TODO pass if rollover or use the first free index.
    uint32_t index = index_vector_.AllocIndex(key);
    return index;
}

