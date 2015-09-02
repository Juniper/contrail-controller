/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_object.h"

IFMapObject::IFMapObject()
    : refcount_(0), sequence_number_(0) {
}

IFMapObject::~IFMapObject() {
}

void IFMapObject::Release(IFMapObject *object) {
    if (!object->node_.is_linked()) {
        delete object;
    }
}

IFMapIdentifier::IFMapIdentifier() {
}

IFMapIdentifier::IFMapIdentifier(int property_count) :
    property_set_(property_count), old_property_set_(property_count) {
}

void IFMapIdentifier::TransferPropertyToOldProperty() {
    old_property_set_ = property_set_;
    property_set_.reset();
}

bool IFMapIdentifier::ResolveStalePropertiesAndResetOld() {
    bool ret = (property_set_ != old_property_set_);
    old_property_set_.reset();
    return ret;
}

bool IFMapIdentifier::ResolveStaleness() {
    return ResolveStalePropertiesAndResetOld();
}

IFMapLinkAttr::IFMapLinkAttr() {
}

// TODO: might need to resolve staleness by checking contents of data
bool IFMapLinkAttr::ResolveStaleness() {
    return false;
}

