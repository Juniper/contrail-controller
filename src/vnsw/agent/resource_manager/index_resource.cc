/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_type.h>
#include <resource_manager/index_resource.h>

IndexResourceKey::IndexResourceKey(ResourceManager *rm,
                                   const boost::uuids::uuid &uuid,
                                   uint16_t type) :
    ResourceKey(rm, type), uuid_(uuid) {
}

IndexResourceKey::~IndexResourceKey() {
}

bool IndexResourceKey::IsLess(const ResourceKey &rhs) const {
    const IndexResourceKey *key = static_cast<const IndexResourceKey *>(&rhs);
    return (key->uuid_ < uuid_);
}

IndexResourceData::IndexResourceData(ResourceManager *rm,
                                     IndexResourceType *type) :
    ResourceData(rm), index_(), resource_type_(type) {
    resource_type_->AllocateIndex(this);
}

IndexResourceData::IndexResourceData(ResourceManager *rm,
                                     IndexResourceType *type,
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

IndexResourceType::IndexResourceType(ResourceManager *rm) :
    ResourceType(rm) {
}

IndexResourceType::~IndexResourceType() {
}

void IndexResourceType::ReserveIndex(uint32_t index) {
    index_vector_.InsertAtIndex(index, NULL);
}

void IndexResourceType::ReleaseIndex(uint32_t index) {
    index_vector_.FreeIndex(index);
}

void IndexResourceType::Release(uint32_t index) {
    IndexResourceData *data = dynamic_cast<IndexResourceData *>
        (index_vector_.FindIndex(index));
    resource_manager()->Release(data->key());
}

ResourceType::DataPtr IndexResourceType::AllocateData(ResourceType::KeyPtr key) {
    IndexResourceKey *index_resource_key = static_cast<IndexResourceKey *>
        (key.get());
    //Allocate resource data
    ResourceType::DataPtr data(static_cast<ResourceData *>
                               (new IndexResourceData(key->rm_,
            static_cast<IndexResourceType *>(index_resource_key->resource_type_))));
    return data;
}

void IndexResourceType::ReleaseData(ResourceType::KeyPtr key) {
}

void IndexResourceType::RestoreIndex(IndexResourceData *data) {
    index_vector_.InsertAtIndex(data->GetIndex(), data);
}

void IndexResourceType::AllocateIndex(IndexResourceData *data) {
    //Get the index, populate resource data
    //TODO pass if rollover or use the first free index.
    data->SetIndex(index_vector_.AllocIndex());
    index_vector_.InsertAtIndex(data->GetIndex(), data);
}

void IndexResourceType::ReleaseIndex(IndexResourceData *data) {
    index_vector_.FreeIndex(data->GetIndex());
}
