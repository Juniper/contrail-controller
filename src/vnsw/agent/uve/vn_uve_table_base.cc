/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vn_uve_table_base.h>
#include <uve/agent_uve_base.h>

VnUveTableBase::VnUveTableBase(Agent *agent)
    : uve_vn_map_(), agent_(agent),
      vn_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId),
      timer_(NULL),
      timer_last_visited_("") {
    if (agent->tsn_enabled() || agent->tor_agent_enabled()) {
        timer_ = TimerManager::CreateTimer
            (*(agent->event_manager())->io_service(), "VnUveTimer",
             TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
        timer_->Start(AgentUveBase::kTimerInterval,
                      boost::bind(&VnUveTableBase::TimerExpiry, this));
    }
}

VnUveTableBase::~VnUveTableBase() {
}

void VnUveTableBase::RegisterDBClients() {
    VnTable *vn_table = agent_->vn_table();
    vn_listener_id_ = vn_table->Register
                  (boost::bind(&VnUveTableBase::VnNotify, this, _1, _2));

    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&VnUveTableBase::InterfaceNotify, this, _1, _2));
    Add(FlowHandler::UnknownVn());
    Add(FlowHandler::LinkLocalVn());
}

void VnUveTableBase::Shutdown(void) {
    if (vn_listener_id_ != DBTableBase::kInvalidId)
        agent_->vn_table()->Unregister(vn_listener_id_);
    if (intf_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(intf_listener_id_);
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;
    }
}

bool VnUveTableBase::TimerExpiry() {
    UveVnMap::iterator it = uve_vn_map_.lower_bound(timer_last_visited_);
    if (it == uve_vn_map_.end()) {
        timer_last_visited_ = "";
        return true;
    }

    uint32_t count = 0;
    while (it != uve_vn_map_.end() && count < AgentUveBase::kUveCountPerTimer) {
        VnUveEntryBase *entry = it->second.get();
        UveVnMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted()) {
            SendDeleteVnMsg(prev->first);
            Delete(prev->first);
        } else if (entry->changed()) {
            const VnEntry *vn = entry->vn();
            UveVirtualNetworkAgent uve;
            entry->set_changed(false);
            if (entry->FrameVnMsg(vn, uve)) {
                DispatchVnMsg(uve);
            }
        }
    }

    if (it == uve_vn_map_.end()) {
        timer_last_visited_ = "";
    } else {
        timer_last_visited_ = it->first;
    }
    return true;
}


void VnUveTableBase::DispatchVnMsg(const UveVirtualNetworkAgent &uve) {
    UveVirtualNetworkAgentTrace::Send(uve);
}

VnUveEntryBase* VnUveTableBase::UveEntryFromVn(const VnEntry *vn) {
    if (vn->GetName() == agent_->NullString()) {
       return NULL;
    }

    UveVnMap::iterator it = uve_vn_map_.find(vn->GetName());
    if (it == uve_vn_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

void VnUveTableBase::SendVnMsg(const VnEntry *vn) {
    VnUveEntryBase* entry = UveEntryFromVn(vn);
    if (entry == NULL) {
        return;
    }

    if (timer_) {
        entry->set_changed(true);
        return;
    }
    UveVirtualNetworkAgent uve;

    bool send = entry->FrameVnMsg(vn, uve);
    if (send) {
        DispatchVnMsg(uve);
    }
}

void VnUveTableBase::SendDeleteVnMsg(const string &vn) {
    UveVirtualNetworkAgent s_vn;
    s_vn.set_name(vn);
    s_vn.set_deleted(true);
    DispatchVnMsg(s_vn);
}

void VnUveTableBase::Delete(const std::string &name) {
    UveVnMap::iterator it = uve_vn_map_.find(name);
    if (it != uve_vn_map_.end()) {
        uve_vn_map_.erase(it);
    }
}

void VnUveTableBase::Delete(const VnEntry *vn) {
    Delete(vn->GetName());
}

VnUveEntryBase* VnUveTableBase::Add(const VnEntry *vn) {
    VnUveEntryPtr uve = Allocate(vn);
    pair<UveVnMap::iterator, bool> ret;
    ret = uve_vn_map_.insert(UveVnPair(vn->GetName(), uve));
    UveVnMap::iterator it = ret.first;
    VnUveEntryBase* entry = it->second.get();
    entry->set_vn(vn);

    return entry;
}

void VnUveTableBase::Add(const string &vn) {
    VnUveEntryPtr uve = Allocate();
    uve_vn_map_.insert(UveVnPair(vn, uve));
}

VnUveTableBase::VnUveEntryPtr VnUveTableBase::Allocate(const VnEntry *vn) {
    VnUveEntryPtr uve(new VnUveEntryBase(agent_, vn));
    return uve;
}

VnUveTableBase::VnUveEntryPtr VnUveTableBase::Allocate() {
    VnUveEntryPtr uve(new VnUveEntryBase(agent_));
    return uve;
}

void VnUveTableBase::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VnEntry *vn = static_cast<const VnEntry *>(e);

    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vn_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            if (timer_) {
                VnUveEntryBase *uve = UveEntryFromVn(vn);
                if (uve) {
                    uve->set_deleted();
                }
            } else {
                SendDeleteVnMsg(vn->GetName());
                Delete(vn);
            }

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

void VnUveTableBase::InterfaceDeleteHandler(const string &vm, const string &vn,
                                        const Interface* intf) {
    if (vn == agent_->NullString()) {
        return;
    }

    UveVnMap::iterator it = uve_vn_map_.find(vn);
    if (it == uve_vn_map_.end()) {
        return;
    }

    VnUveEntryPtr vn_uve_entry_ptr(it->second);
    VnUveEntryBase *vn_uve_entry = vn_uve_entry_ptr.get();
    UveVirtualNetworkAgent uve;

    vn_uve_entry->VmDelete(vm);
    vn_uve_entry->InterfaceDelete(intf);
    if (timer_) {
        vn_uve_entry->set_changed(true);
        return;
    }
    if (vn_uve_entry->BuildInterfaceVmList(uve)) {
        DispatchVnMsg(uve);
    }
}

void VnUveTableBase::InterfaceAddHandler(const VnEntry* vn,
                                         const Interface* intf,
                                         const string &vm_name,
                                         VnUveInterfaceState *state) {

    VnUveEntryBase *vn_uve_entry;
    vn_uve_entry = Add(vn);

    UveVirtualNetworkAgent uve;

    if (vm_name != state->vm_name_) {
        if (state->vm_name_.length()) {
            vn_uve_entry->VmDelete(state->vm_name_);
        }
    }
    if (vm_name.length()) {
        vn_uve_entry->VmAdd(vm_name);
    }
    vn_uve_entry->InterfaceAdd(intf);
    if (timer_) {
        vn_uve_entry->set_changed(true);
        return;
    }
    if (vn_uve_entry->BuildInterfaceVmList(uve)) {
        DispatchVnMsg(uve);
    }

}

void VnUveTableBase::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
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
        const VnEntry *vn = vm_port->vn();
        if (vn == NULL) {
            return;
        }

        const VmEntry *vm = vm_port->vm();
        std::string vm_name = vm? vm->GetCfgName() : agent_->NullString();

        if (!state) {
            state = new VnUveInterfaceState(vm_name, vn->GetName(), false,
                                            false);
            e->SetState(partition->parent(), intf_listener_id_, state);
        } else {
            /* Change in VN name is not supported now */
            assert(state->vn_name_.compare(vn->GetName()) == 0);
        }

        if ((state->ipv4_active_ == vm_port->ipv4_active()) &&
            (state->l2_active_ == vm_port->l2_active()) &&
            (state->vm_name_.compare(vm_name) == 0)) {
            return;
        }
        InterfaceAddHandler(vn, vm_port, vm_name, state);
        state->ipv4_active_ = vm_port->ipv4_active();
        state->l2_active_ = vm_port->l2_active();
        state->vm_name_ = vm_name;
    }
    return;
}

void VnUveTableBase::SendVnAclRuleCount() {
    UveVnMap::const_iterator it = uve_vn_map_.begin();
    while (it != uve_vn_map_.end()) {
        VnUveEntryBase *entry = it->second.get();
        ++it;
        if (entry->deleted()) {
            continue;
        }
        if (entry->vn()) {
            UveVirtualNetworkAgent uve;
            bool send = entry->FrameVnAclRuleCountMsg(entry->vn(), &uve);
            if (send) {
                DispatchVnMsg(uve);
            }
        }
    }
}
