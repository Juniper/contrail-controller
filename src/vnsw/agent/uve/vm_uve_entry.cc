/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vm_uve_entry.h>
#include <uve/interface_uve_stats_table.h>
#include <uve/agent_uve.h>

using namespace std;

VmUveEntry::VmUveEntry(Agent *agent, const string &vm_name)
    : VmUveEntryBase(agent, vm_name), port_bitmap_() {
}

VmUveEntry::~VmUveEntry() {
}

void VmUveEntry::UpdatePortBitmap(uint8_t proto, uint16_t sport,
                                  uint16_t dport) {
    tbb::mutex::scoped_lock lock(mutex_);
    if (deleted_ && !renewed_) {
        /* Skip updates on VmUveEntry if it is marked for delete */
        return;
    }
    //Update VM bitmap
    port_bitmap_.AddPort(proto, sport, dport);

    InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
        (agent_->uve()->interface_uve_table());
    //Update vm interfaces bitmap
    InterfaceSet::iterator it = interface_tree_.begin();
    while(it != interface_tree_.end()) {
        table->UpdatePortBitmap(*it, proto, sport, dport);
        ++it;
    }
}

bool VmUveEntry::SetVmPortBitmap(UveVirtualMachineAgent *uve) {
    bool changed = false;
    tbb::mutex::scoped_lock lock(mutex_);

    vector<uint32_t> tcp_sport;
    if (port_bitmap_.tcp_sport_.Sync(tcp_sport)) {
        uve->set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap_.tcp_dport_.Sync(tcp_dport)) {
        uve->set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap_.udp_sport_.Sync(udp_sport)) {
        uve->set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap_.udp_dport_.Sync(udp_dport)) {
        uve->set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool VmUveEntry::FrameVmStatsMsg(UveVirtualMachineAgent *uve) {
    bool changed = false;
    uve->set_name(vm_config_name());

    if (SetVmPortBitmap(uve)) {
        changed = true;
    }

    return changed;
}

void VmUveEntry::Reset() {
    VmUveEntryBase::Reset();
    port_bitmap_.Reset();
}
