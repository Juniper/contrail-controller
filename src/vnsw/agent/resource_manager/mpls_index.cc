/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include "resource_manager/mpls_index.h"
#include "resource_manager/resource_manager.h"
#include "resource_manager/resource_type.h"
#include "resource_manager/resource.h"

//VM interface
InterfaceIndexResourceKey::InterfaceIndexResourceKey(const ResourceManager *rm,
                           const boost::uuids::uuid &uuid, uint32_t id) :
    MplsIndexResourceKey(rm, uuid), id_(id) {
}

InterfaceIndexResourceKey::~InterfaceIndexResourceKey() {
}

bool InterfaceIndexResourceKey::operator!=(const ResourceKey &rhs) {
    const IndexResourceKey *key = static_cast<const IndexResourceKey *>(&rhs);
    const InterfaceIndexResourceKey *vm_key = static_cast<const
        InterfaceIndexResourceKey*>(&rhs);
    return (IndexResourceKey::operator!=(rhs) ||
            (vm_key->id_ != id_));
}

void InterfaceIndexResourceKey::Copy(const ResourceKey &rhs) {
    IndexResourceKey::Copy(rhs);
    const InterfaceIndexResourceKey *vm_key = static_cast<const
        InterfaceIndexResourceKey*>(&rhs);
    if (vm_key->id_ != id_)
        vm_key->id_ = id_;
}

VrfMplsResourceKey::VrfMplsResourceKey(const ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, boost::uuids::uuid()), name_(name) {
}

VrfMplsResourceKey::~VrfMplsResourceKey() {
}

bool VrfMplsResourceKey::operator!=(const ResourceKey &rhs) {
    const VrfMplsResourceKey *vrf_key = static_cast<const
        VrfMplsResourceKey*>(&rhs);
    return (vrf_key->name_ != name_);
}

void VrfMplsResourceKey::Copy(const ResourceKey &rhs) {
    const VrfMplsResourceKey *vrf_key = static_cast<const
        VrfMplsResourceKey*>(&rhs);
    if (vrf_key->name_ != name_)
        vrf_key->name_ = name_;
}

RouteMplsResourceKey::RouteMplsResourceKey(const ResourceManager *rm,
                                             const std::string &vrf_name,
                                             const std::string &route_str) :
    MplsIndexResourceKey(rm, boost::uuids::uuid()), vrf_name_(name),
    route_str_(route_str) {
}

RouteMplsResourceKey::~RouteMplsResourceKey() {
}

bool RouteMplsResourceKey::operator!=(const ResourceKey &rhs) {
    const RouteMplsResourceKey *route_key = static_cast<const
        RouteMplsResourceKey*>(&rhs);
    return ((route_key->vrf_name_ != vrf_name_) ||
            (route_key->route_str_ != route_str_));
}

void RouteMplsResourceKey::Copy(const ResourceKey &rhs) {
    const RouteMplsResourceKey *route_key = static_cast<const
        RouteMplsResourceKey*>(&rhs);
    if (route_key->vrf_name_ != vrf_name_)
        route_key->vrf_name_ = vrf_name_;
    if (route_key->route_str_ != route_str_)
        route_key->route_str_ = route_str_;
}

TestMplsResourceKey::TestMplsResourceKey(const ResourceManager *rm,
                                         const std::string &name) :
    MplsIndexResourceKey(rm, boost::uuids::uuid()), name_(name) {
}

TestMplsResourceKey::~TestMplsResourceKey() {
}

bool TestMplsResourceKey::operator!=(const ResourceKey &rhs) {
    const TestMplsResourceKey *test_key = static_cast<const
        TestMplsResourceKey*>(&rhs);
    return (test_key->name_ != name_);
}

void TestMplsResourceKey::Copy(const ResourceKey &rhs) {
    const TestMplsResourceKey *vrf_key = static_cast<const
        TestMplsResourceKey*>(&rhs);
    if (test_key->name_ != name_)
        test_key->name_ = name_;
}
