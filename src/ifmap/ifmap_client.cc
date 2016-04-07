/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_client.h"

#include "base/bitset.h"
#include "base/time_util.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_update.h"

IFMapClient::IFMapClient()
    : index_(kIndexInvalid), exporter_(NULL), msgs_sent_(0), msgs_blocked_(0),
      bytes_sent_(0), update_nodes_sent_(0), delete_nodes_sent_(0),
      update_links_sent_(0), delete_links_sent_(0), send_is_blocked_(false),
      created_at_(UTCTimestampUsec()) {
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

