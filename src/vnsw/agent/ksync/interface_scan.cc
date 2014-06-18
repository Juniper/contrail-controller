/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <ksync/interface_scan.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>

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
             *(agent_->event_manager())->io_service(), "InterfaceKScanTimer");
    timer_->Start(timeout_, boost::bind(&InterfaceKScan::Reset, this));
}

void InterfaceKScan::KernelInterfaceData(vr_interface_req *r) {
    char name[IF_NAMESIZE + 1];
    if (r->get_vifr_os_idx() >= 0 && r->get_vifr_type() == VIF_TYPE_VIRTUAL) {
        uint32_t ipaddr = r->get_vifr_ip();
        if (ipaddr && if_indextoname(r->get_vifr_os_idx(), name)) {
            agent_->interface_table()->AddDhcpSnoopEntry(name,
                                                           Ip4Address(ipaddr));
        }
    }
}

bool InterfaceKScan::Reset() { 
    agent_->interface_table()->AuditDhcpSnoopTable();
    return false;
}

