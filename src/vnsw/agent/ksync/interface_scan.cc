/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <ksync/interface_scan.h>

InterfaceKScan::InterfaceKScan(Agent *agent) 
    : agent_(agent), timer_(NULL) {
}

InterfaceKScan::~InterfaceKScan() {
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }
}

void InterfaceKScan::Init() {
    timer_ = TimerManager::CreateTimer(
             *(agent_->GetEventManager())->io_service(), "InterfaceKScanTimer");
    timer_->Start(timeout_, boost::bind(&InterfaceKScan::Reset, this));
}

void InterfaceKScan::KernelInterfaceData(vr_interface_req *r) {
    char name[IF_NAMESIZE + 1];
    tbb::mutex::scoped_lock lock(mutex_);
    if (r->get_vifr_os_idx() >= 0 && r->get_vifr_type() == VIF_TYPE_VIRTUAL) {
        uint32_t ipaddr = r->get_vifr_ip();
        if (ipaddr && if_indextoname(r->get_vifr_os_idx(), name)) {
            std::string itf_name(name);
            data_map_.insert(InterfaceKScanPair(itf_name, ipaddr));
        }
    }
}

bool InterfaceKScan::FindInterfaceKScanData(const std::string &name, 
                                            uint32_t &ip) {
    tbb::mutex::scoped_lock lock(mutex_);
    InterfaceKScanIter it = data_map_.find(name);
    if (it != data_map_.end()) {
        ip = it->second;
        return true;
    }
    return false;
}

bool InterfaceKScan::Reset() { 
    tbb::mutex::scoped_lock lock(mutex_);
    data_map_.clear();
    return false;
}

