/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_type.h>

ResourceKey::ResourceKey(const ResourceManager *rm) :
    rm_(rm), delete_marked_(false) {
}

ResourceKey::~ResourceKey() {
}

ResourceData::ResourceData(const ResourceManager *rm) :
    rm_(rm), key_() {
}

ResourceData::~ResourceData() {
}

ResourceType::ResourceType(const ResourceManager *rm) : rm_(rm) {
}

ResourceType::~ResourceType() {
}

ResourceType::InsertKey(KeyPtr key, DataPtr data) {
    key_data_map_.insert(std::pair<KeyPtr, DataPtr>(key, data));
    data.get()->key_ = key;
}

ResourceType::DeleteKey(KeyPtr key) {
    KeyDataMapIter it = key_data_map_.find(key);
    key.get()->delete_marked_ = true;
    free_data_list_.push_back(data);
}

DataPtr ResourceType::FindKey(KeyPtr key) {
    KeyDataMapIter it = key_data_map_.find(key);
    if (it == key_data_map_.end()) {
        return NULL;
    }
    return (*it);
}
