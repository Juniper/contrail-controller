/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_client.h"

#include "ifmap/ifmap_exporter.h"
#include "base/bitset.h"
#include "ifmap/ifmap_update.h"

IFMapClient::IFMapClient()
    : index_(kIndexInvalid), exporter_(NULL), msgs_sent_(0), msgs_blocked_(0),
      bytes_sent_(0), nodes_sent_(0), links_sent_(0), send_is_blocked_(false) {
}

IFMapClient::~IFMapClient() {
}

void IFMapClient::Initialize(IFMapExporter *exporter, int index) {
    index_ = index;
    exporter_ = exporter;
}

std::vector<std::string> IFMapClient::vm_list() const {
    std::vector<std::string> vm_list;

    vm_list.reserve(vm_map_.size());
    for (VmMap::const_iterator iter = vm_map_.begin(); iter != vm_map_.end();
         ++iter) {
        vm_list.push_back(iter->first);
    }
    return vm_list;
}

void IFMapClient::ConfigTrackerAdd(IFMapState *state) {
    config_tracker_.insert(state);
}

void IFMapClient::ConfigTrackerDelete(IFMapState *state) {
    CtSz_t num = config_tracker_.erase(state);
    assert(num == 1);
}

bool IFMapClient::ConfigTrackerHasState(IFMapState *state) {
    ConfigTracker::iterator iter = config_tracker_.find(state);
    return (iter == config_tracker_.end() ? false : true);
}

bool IFMapClient::ConfigTrackerEmpty() {
    return config_tracker_.empty();
}

size_t IFMapClient::ConfigTrackerSize() {
    return config_tracker_.size();
}

void IFMapClient::ConfigDbCleanup() {
    BitSet rm_bs;
    rm_bs.set(index_);

    for (ConfigTracker::iterator iter = config_tracker_.begin();
            iter != config_tracker_.end(); ++iter) {
        IFMapState *state = *iter;
        state->InterestReset(rm_bs);
        state->AdvertisedReset(rm_bs);
    }
    config_tracker_.clear();
}

void IFMapClient::ResetLinkDeleteClients() {
    BitSet rm_bs;
    rm_bs.set(index_);

    exporter_->ResetLinkDeleteClients(rm_bs);
}

