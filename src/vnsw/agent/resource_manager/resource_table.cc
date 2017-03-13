/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/index_resource.h>

ResourceKey::ResourceKey(ResourceManager *rm, Resource::Type type) :
    rm_(rm), dirty_(false),
    resource_table_(static_cast<ResourceTable *>(rm->resource_table(type))) {
}

ResourceKey::~ResourceKey() {
}

bool ResourceKey::operator<(const ResourceKey &rhs) const {
    return IsLess(rhs);
}

ResourceBackupEndKey::ResourceBackupEndKey(ResourceManager *rm) :
    ResourceKey(rm, Resource::INVALID) {
}

ResourceBackupEndKey::~ResourceBackupEndKey() {
}

ResourceData::ResourceData(ResourceManager *rm) :
    rm_(rm) {
}

ResourceData::~ResourceData() {
}


ResourceTable::ResourceTable(ResourceManager *rm) : rm_(rm) {
}

ResourceTable::~ResourceTable() {
    assert(key_data_map_.size() == 0);
}

void ResourceTable::InsertKey(KeyPtr key, DataPtr data) {
    key_data_map_.insert(std::pair<KeyPtr, DataPtr>(key, data));
}

void ResourceTable::DeleteKey(KeyPtr key) {
    key_data_map_.erase(key);
}

ResourceTable::DataPtr ResourceTable::FindKeyPtr(KeyPtr key) {
    KeyDataMapIter it = key_data_map_.find(key);
    if (it == key_data_map_.end()) {
        return DataPtr();
    }
    return (*it).second;
}

ResourceData* ResourceTable::FindKey(KeyPtr key) {
    return (FindKeyPtr(key).get());
}

// Walk all the etries remove keys are not usable.
void ResourceTable::FlushStale() {
    for (KeyDataMapIter it = key_data_map_.begin();
        it != key_data_map_.end();) {
        KeyPtr key = it->first;
        if (key->dirty()) {
            it++;
            rm_->Release(key);
        } else {
            it++;
        }
    }
}

// Allocate the resource and mark the key usable
ResourceTable::DataPtr ResourceTable::Allocate(KeyPtr key) {
    ResourceManager::DataPtr data = FindKeyPtr(key);
    if (data.get() == NULL) {
        data = AllocateData(key);
        InsertKey(key, data);
    }
    key->reset_dirty();
    return data;
}

