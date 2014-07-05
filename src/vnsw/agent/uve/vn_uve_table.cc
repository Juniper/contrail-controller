/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vn_uve_table.h>
#include <uve/agent_uve.h>
#include <uve/agent_stats_collector.h>

VnUveTable::VnUveTable(Agent *agent) 
    : uve_vn_map_(), agent_(agent), 
      vn_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId) {
}

VnUveTable::~VnUveTable() {
}

void VnUveTable::RegisterDBClients() {
    VnTable *vn_table = agent_->vn_table();
    vn_listener_id_ = vn_table->Register
                  (boost::bind(&VnUveTable::VnNotify, this, _1, _2));

    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&VnUveTable::InterfaceNotify, this, _1, _2));
    Add(*FlowHandler::UnknownVn());
    Add(*FlowHandler::LinkLocalVn());
}

void VnUveTable::Shutdown(void) {
    agent_->vn_table()->Unregister(vn_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
}

void VnUveTable::DispatchVnMsg(const UveVirtualNetworkAgent &uve) {
    UveVirtualNetworkAgentTrace::Send(uve);
}

bool VnUveTable::SendUnresolvedVnMsg(const string &vn_name,
                                     UveVirtualNetworkAgent &uve) {
    UveVnMap::iterator it = uve_vn_map_.find(vn_name);
    if (it == uve_vn_map_.end()) {
        return false;
    }
    VnUveEntryPtr entry_ptr(it->second);
    VnUveEntry *entry = entry_ptr.get();

    bool changed = false;
    uve.set_name(vn_name);
    changed = entry->PopulateInterVnStats(uve);

    AgentStatsCollector *collector = agent_->uve()->agent_stats_collector();
    /* Send Nameless VrfStats as part of Unknown VN */
    if (vn_name.compare(*FlowHandler::UnknownVn()) == 0) {
        changed = entry->FillVrfStats(collector->GetNamelessVrfId(), uve);
    }

    return changed;
}

void VnUveTable::SendVnStats(bool only_vrf_stats) {
    UveVnMap::const_iterator it = uve_vn_map_.begin();
    while (it != uve_vn_map_.end()) {
        const VnUveEntry *entry = it->second.get();
        if (entry->vn()) {
            SendVnStatsMsg(entry->vn(), only_vrf_stats);
        }
        ++it;
    }
    UveVirtualNetworkAgent uve1, uve2;
    if (SendUnresolvedVnMsg(*FlowHandler::UnknownVn(), uve1)) {
        DispatchVnMsg(uve1);
    }
    if (SendUnresolvedVnMsg(*FlowHandler::LinkLocalVn(), uve1)) {
        DispatchVnMsg(uve2);
    }
}

VnUveEntry* VnUveTable::UveEntryFromVn(const VnEntry *vn) {
    if (vn->GetName() == agent_->NullString()) {
       return NULL;
    }

    UveVnMap::iterator it = uve_vn_map_.find(vn->GetName());
    if (it == uve_vn_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

void VnUveTable::SendVnMsg(const VnEntry *vn) {
    VnUveEntry* entry = UveEntryFromVn(vn);
    if (entry == NULL) {
        return;
    }
    UveVirtualNetworkAgent uve;

    bool send = entry->FrameVnMsg(vn, uve);
    if (send) {
        DispatchVnMsg(uve);
    }
}

void VnUveTable::SendVnStatsMsg(const VnEntry *vn, bool only_vrf_stats) {
    VnUveEntry* entry = UveEntryFromVn(vn);
    if (entry == NULL) {
        return;
    }
    UveVirtualNetworkAgent uve;

    bool send = entry->FrameVnStatsMsg(vn, uve, only_vrf_stats);
    if (send) {
        DispatchVnMsg(uve);
    }
}

void VnUveTable::SendDeleteVnMsg(const string &vn) {
    UveVirtualNetworkAgent s_vn;
    s_vn.set_name(vn);
    s_vn.set_deleted(true); 
    DispatchVnMsg(s_vn);
}

void VnUveTable::Delete(const VnEntry *vn) {
    RemoveInterVnStats(vn->GetName());

    UveVnMap::iterator it = uve_vn_map_.find(vn->GetName());
    if (it != uve_vn_map_.end()) {
        uve_vn_map_.erase(it);
    }
}

VnUveEntry* VnUveTable::Add(const VnEntry *vn) {
    VnUveEntryPtr uve = Allocate(vn);
    pair<UveVnMap::iterator, bool> ret;
    ret = uve_vn_map_.insert(UveVnPair(vn->GetName(), uve));
    UveVnMap::iterator it = ret.first;
    VnUveEntry* entry = it->second.get();
    entry->set_vn(vn);

    return entry;
}

void VnUveTable::Add(const string &vn) {
    VnUveEntryPtr uve = Allocate();
    uve_vn_map_.insert(UveVnPair(vn, uve));
}

VnUveTable::VnUveEntryPtr VnUveTable::Allocate(const VnEntry *vn) {
    VnUveEntryPtr uve(new VnUveEntry(agent_, vn));
    return uve;
}

VnUveTable::VnUveEntryPtr VnUveTable::Allocate() {
    VnUveEntryPtr uve(new VnUveEntry(agent_));
    return uve;
}

void VnUveTable::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VnEntry *vn = static_cast<const VnEntry *>(e);

    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vn_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            Delete(vn);
            SendDeleteVnMsg(vn->GetName());

            e->ClearState(partition->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vn_listener_id_, state);

        Add(vn);
    }
    SendVnMsg(vn);
}

void VnUveTable::InterfaceDeleteHandler(const string &vm, const string &vn, 
                                        const Interface* intf) {
    if (vn == agent_->NullString()) {
        return;
    }

    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntryPtr vn_uve_entry_ptr(it->second);
    VnUveEntry *vn_uve_entry = vn_uve_entry_ptr.get();
    UveVirtualNetworkAgent uve;

    vn_uve_entry->VmDelete(vm);
    vn_uve_entry->InterfaceDelete(intf);
    if (vn_uve_entry->BuildInterfaceVmList(uve)) {
        DispatchVnMsg(uve);
    }
}

void VnUveTable::InterfaceAddHandler(const VmEntry* vm, const VnEntry* vn, 
                                     const Interface* intf) {

    VnUveEntry *vn_uve_entry;
    vn_uve_entry = Add(vn);

    UveVirtualNetworkAgent uve;

    vn_uve_entry->VmAdd(vm->GetCfgName());
    vn_uve_entry->InterfaceAdd(intf);
    if (vn_uve_entry->BuildInterfaceVmList(uve)) {
        DispatchVnMsg(uve);
    }
}

void VnUveTable::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    VnUveInterfaceState *state = static_cast<VnUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted() || ((vm_port->ipv4_active() == false) &&
                           (vm_port->l2_active() == false))) {
        if (state) {
            if (e->IsDeleted() || ((state->ipv4_active_ == true) ||
                                    (state->l2_active_ == true))) {
                InterfaceDeleteHandler(state->vm_name_, state->vn_name_,
                                       vm_port);
                state->ipv4_active_ = false;
                state->l2_active_ = false;
            }
            if (e->IsDeleted()) {
                e->ClearState(partition->parent(), intf_listener_id_);
                delete state;
            }
        }
    } else {
        const VmEntry *vm = vm_port->vm();
        const VnEntry *vn = vm_port->vn();

        if (!state) {
            state = new VnUveInterfaceState(vm->GetCfgName(),
                                            vn->GetName(), false, false);
            e->SetState(partition->parent(), intf_listener_id_, state);
        }
        /* Change in VM config name is not supported now */
        if (state->vm_name_ != agent_->NullString() && 
                (state->vm_name_.compare(vm->GetCfgName()) != 0)) {
            assert(0);
        }
        /* Change in VN name is not supported now */
        if (state->vn_name_ != agent_->NullString() && 
                (state->vn_name_.compare(vn->GetName()) != 0)) {
            assert(0);
        }
        if (!state->ipv4_active_ && !state->l2_active_ &&
            vn->GetName() != agent_->NullString())  {
            InterfaceAddHandler(vm, vn, vm_port);
            state->ipv4_active_ = vm_port->ipv4_active();
            state->l2_active_ = vm_port->l2_active();
        }
        state->vm_name_ = vm? vm->GetCfgName() : agent_->NullString();
        state->vn_name_ = vn? vn->GetName() : agent_->NullString();
    }
    return;
}

void VnUveTable::UpdateBitmap(const string &vn, uint8_t proto, uint16_t sport,
                              uint16_t dport) {
    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return;
    }

    it->second.get()->UpdatePortBitmap(proto, sport, dport);
}

void VnUveTable::VnStatsUpdateInternal(const string &src, const string &dst, 
                                       uint64_t bytes, uint64_t pkts, 
                                       bool outgoing) {
    UveVnMap::iterator it = uve_vn_map_.find(src);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntryPtr vn_uve_entry(it->second);
    vn_uve_entry.get()->UpdateInterVnStats(dst, bytes, pkts, outgoing);
}

void VnUveTable::UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes, 
                                    uint64_t pkts) {

    string src_vn = fe->data().source_vn, dst_vn = fe->data().dest_vn;

    if (!fe->data().source_vn.length())
        src_vn = *FlowHandler::UnknownVn();
    if (!fe->data().dest_vn.length())
        dst_vn = *FlowHandler::UnknownVn();

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

    VnUveEntryPtr vn_uve_entry(it->second);
    vn_uve_entry.get()->ClearInterVnStats();
}


