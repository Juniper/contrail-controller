/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_type.h>
#include <resource_manager/index_resource.h>

IndexResourceKey::IndexResourceKey(const ResourceManager *rm,
                           const boost::uuids::uuid &uuid,
                           uint16_t type) :
    ResourceKey(rm), uuid_(uuid),
    resource_type_(rm->resource_type(type)) {
}

IndexResourceKey::~IndexResourceKey() {
}

bool IndexResourceKey::operator!=(const ResourceKey &rhs) {
    const IndexResourceKey *key = static_cast<const IndexResourceKey *>(&rhs);
    return (key->uuid_ != uuid_);
}

void IndexResourceKey::Copy(const ResourceKey &rhs) {
    const IndexResourceKey *key = static_cast<const IndexResourceKey *>(&rhs);
    if (key->uuid_ != uuid_)
        uuid_ = key->uuid_;
}

IndexResourceData::IndexResourceData(const ResourceManager *rm,
                                     uint16_t type) {
    IndexResourceType *resource_type =
        static_cast<IndexResourceType *>(rm->resource_type(type));
    resource_type->AllocateIndex(this);
}

IndexResourceData::~IndexResourceData() {
    resource_type->ReleaseIndex(this);
}

uint32_t IndexResourceData::GetIndex() const {
    return index_;
}

void IndexResourceData::SetIndex(uint32_t index) const {
    index_ = index;
}

IndexResourceType::IndexResourceType(const ResourceManager *ra) :
    ResourceType(ra) {
}

IndexResourceType::~IndexResourceType() {
}

ResourceType::DataPtr IndexResourceType::AllocateData(ResourceType::KeyPtr key) {
    //Allocate resource data
    data.reset(new IndexResourceData(rm_, key->resource_type_));
    return data;
}

void IndexResourceType::ReleaseData(ResourceType::KeyPtr key) {
}

void IndexResourceType::AllocateIndex(IndexResourceData *data) {
    //Get the index, populate resource data
    //TODO pass if rollover or use the first free index.
    data->SetIndex(index_vector_.AllocIndex());
    index_vector_->InsertAtIndex(data->GetIndex(), data);
}

void IndexResourceType::ReleaseIndex(IndexResourceData *data) {
    index_vector_->FreeIndex(data->GetIndex());
}
