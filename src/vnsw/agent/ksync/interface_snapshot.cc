/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <ksync/interface_snapshot.h>

InterfaceKSnap::InterfaceKSnap(Agent *agent) 
    : agent_(agent), timer_(NULL) {
}

InterfaceKSnap::~InterfaceKSnap() {
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }
}

void InterfaceKSnap::Init() {
    timer_ = TimerManager::CreateTimer(
             *(agent_->GetEventManager())->io_service(), "InterfaceKSnapTimer");
    timer_->Start(timeout_, boost::bind(&InterfaceKSnap::Reset, this));
}

void InterfaceKSnap::KernelInterfaceData(vr_interface_req *r) {
    char name[IF_NAMESIZE + 1];
    tbb::mutex::scoped_lock lock(mutex_);
    if (r->get_vifr_os_idx() >= 0 && r->get_vifr_type() == VIF_TYPE_VIRTUAL) {
        uint32_t ipaddr = r->get_vifr_ip();
        if (ipaddr && if_indextoname(r->get_vifr_os_idx(), name)) {
            std::string itf_name(name);
            data_map_.insert(InterfaceKSnapPair(itf_name, ipaddr));
        }
    }
}

bool InterfaceKSnap::FindInterfaceKSnapData(const std::string &name, 
                                            uint32_t &ip) {
    tbb::mutex::scoped_lock lock(mutex_);
    InterfaceKSnapIter it = data_map_.find(name);
    if (it != data_map_.end()) {
        ip = it->second;
        return true;
    }
    return false;
}

bool InterfaceKSnap::Reset() { 
    tbb::mutex::scoped_lock lock(mutex_);
    data_map_.clear();
    return false;
}

