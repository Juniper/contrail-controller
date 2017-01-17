/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>

ResourceKey::ResourceKey(ResourceManager *rm, uint16_t type) :
    rm_(rm), dirty_(false), delete_marked_(false),
    resource_type_(static_cast<ResourceTable *>(rm->resource_type(type))) {
}

ResourceKey::~ResourceKey() {
}

bool ResourceKey::operator!=(const ResourceKey &rhs) const {
    return IsLess(rhs);
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
    rm_(rm), key_() {
}

ResourceData::~ResourceData() {
}

ResourceKey::Ptr ResourceData::key() {
    return key_;
}

ResourceTable::ResourceTable(ResourceManager *rm) : rm_(rm) {
}

ResourceTable::~ResourceTable() {
    assert(key_data_map_.size() == 0);
}

void ResourceTable::InsertKey(KeyPtr key, DataPtr data) {
    data.get()->key_ = key;
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

void ResourceTable::FlushStale() {
    for (KeyDataMapIter it = key_data_map_.begin();
         it != key_data_map_.end(); it++) {
        KeyPtr key = it->first;
        if (key->dirty()) {
            KeyDataMapIter del_it = it;
            it++;
            key_data_map_.erase(del_it);
        }
    }
}
