/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/agent_uve.h>
#include <uve/vn_uve_table.h>
#include <uve/vn_uve_entry.h>
#include <uve/agent_stats_collector.h>

VnUveTable::VnUveTable(Agent *agent)
    : VnUveTableBase(agent) {
}

VnUveTable::~VnUveTable() {
}

VnUveTableBase::VnUveEntryPtr VnUveTable::Allocate(const VnEntry *vn) {
    VnUveEntryPtr uve(new VnUveEntry(agent_, vn));
    return uve;
}

VnUveTableBase::VnUveEntryPtr VnUveTable::Allocate() {
    VnUveEntryPtr uve(new VnUveEntry(agent_));
    return uve;
}

bool VnUveTable::SendUnresolvedVnMsg(const string &vn_name,
                                     UveVirtualNetworkAgent &uve) {
    UveVnMap::iterator it = uve_vn_map_.find(vn_name);
    if (it == uve_vn_map_.end()) {
        return false;
    }
    VnUveEntryPtr entry_ptr(it->second);
    VnUveEntry *entry = static_cast<VnUveEntry *>(entry_ptr.get());

    bool changed = false;
    uve.set_name(vn_name);
    changed = entry->PopulateInterVnStats(uve);


    AgentUve *u = static_cast<AgentUve *>(agent_->uve());
    AgentStatsCollector *collector = u->agent_stats_collector();
    /* Send Nameless VrfStats as part of Unknown VN */
    if (vn_name.compare(FlowHandler::UnknownVn()) == 0) {
        changed = entry->FillVrfStats(collector->GetNamelessVrfId(), uve);
    }

    return changed;
}

void VnUveTable::SendVnStats(bool only_vrf_stats) {
    UveVnMap::const_iterator it = uve_vn_map_.begin();
    while (it != uve_vn_map_.end()) {
        const VnUveEntry *entry = static_cast<VnUveEntry *>(it->second.get());
        if (entry->vn()) {
            SendVnStatsMsg(entry->vn(), only_vrf_stats);
        }
        ++it;
    }
    UveVirtualNetworkAgent uve1, uve2;
    if (SendUnresolvedVnMsg(FlowHandler::UnknownVn(), uve1)) {
        DispatchVnMsg(uve1);
    }
    if (SendUnresolvedVnMsg(FlowHandler::LinkLocalVn(), uve1)) {
        DispatchVnMsg(uve2);
    }
}

void VnUveTable::SendVnStatsMsg(const VnEntry *vn, bool only_vrf_stats) {
    VnUveEntry* entry = static_cast<VnUveEntry*>(UveEntryFromVn(vn));
    if (entry == NULL) {
        return;
    }
    UveVirtualNetworkAgent uve;

    bool send = entry->FrameVnStatsMsg(vn, uve, only_vrf_stats);
    if (send) {
        DispatchVnMsg(uve);
    }
}

void VnUveTable::UpdateBitmap(const string &vn, uint8_t proto, uint16_t sport,
                              uint16_t dport) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntry * entry = static_cast<VnUveEntry *>(it->second.get());
    entry->UpdatePortBitmap(proto, sport, dport);
}

void VnUveTable::VnStatsUpdateInternal(const string &src, const string &dst,
                                       uint64_t bytes, uint64_t pkts,
                                       bool outgoing) {
    UveVnMap::iterator it = uve_vn_map_.find(src);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntry * entry = static_cast<VnUveEntry *>(it->second.get());
    entry->UpdateInterVnStats(dst, bytes, pkts, outgoing);
}

void VnUveTable::UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes,
                                    uint64_t pkts) {

    string src_vn = fe->data().source_vn, dst_vn = fe->data().dest_vn;

    if (!fe->data().source_vn.length())
        src_vn = FlowHandler::UnknownVn();
    if (!fe->data().dest_vn.length())
        dst_vn = FlowHandler::UnknownVn();

    /* When packet is going from src_vn to dst_vn it should be interpreted
     * as ingress to vrouter and hence in-stats for src_vn w.r.t. dst_vn
     * should be incremented. Similarly when the packet is egressing vrouter
     * it should be considered as out-stats for dst_vn w.r.t. src_vn.
     * Here the direction "in" and "out" should be interpreted w.r.t vrouter
     */
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        VnStatsUpdateInternal(src_vn, dst_vn, bytes, pkts, false);
        VnStatsUpdateInternal(dst_vn, src_vn, bytes, pkts, true);
    } else {
        if (fe->is_flags_set(FlowEntry::IngressDir)) {
            VnStatsUpdateInternal(src_vn, dst_vn, bytes, pkts, false);
        } else {
            VnStatsUpdateInternal(dst_vn, src_vn, bytes, pkts, true);
        }
    }
}

void VnUveTable::RemoveInterVnStats(const string &vn) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntry * entry = static_cast<VnUveEntry *>(it->second.get());
    entry->ClearInterVnStats();
}

void VnUveTable::Delete(const VnEntry *vn) {
    RemoveInterVnStats(vn->GetName());
    VnUveTableBase::Delete(vn);
}

