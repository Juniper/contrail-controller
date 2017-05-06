/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vn_uve_table_base.h>
#include <uve/agent_uve_base.h>

VnUveTableBase::VnUveTableBase(Agent *agent, uint32_t default_intvl)
    : uve_vn_map_(), agent_(agent), uve_vn_map_mutex_(),
      vn_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId),
      timer_last_visited_(""),
      timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "VnUveTimer",
              TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude), 0)) {
      expiry_time_ = default_intvl;
      timer_->Start(expiry_time_,
                    boost::bind(&VnUveTableBase::TimerExpiry, this));
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
            if (!entry->renewed()) {
                Delete(prev->first);
            } else {
                entry->set_deleted(false);
                entry->set_renewed(false);
                entry->set_changed(false);
                SendVnMsg(entry, entry->vn());
                // Send VN ACE stats
                SendVnAceStats(entry, entry->vn());
            }
        } else {
            if (entry->changed()) {
                SendVnMsg(entry, entry->vn());
                entry->set_changed(false);
                /* Clear renew flag to be on safer side. Not really required */
                entry->set_renewed(false);
            }

            // Send VN ACE stats
            SendVnAceStats(entry, entry->vn());
        }
    }

    if (it == uve_vn_map_.end()) {
        timer_last_visited_ = "";
        set_expiry_time(agent_->uve()->default_interval());
    } else {
        timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval());
    }
    return true;
}

void VnUveTableBase::set_expiry_time(int time) {
    if (time != expiry_time_) {
        expiry_time_ = time;
        timer_->Reschedule(expiry_time_);
    }
}

void VnUveTableBase::SendVnMsg(VnUveEntryBase *entry, const VnEntry *vn) {
    UveVirtualNetworkAgent uve;
    if (vn == NULL) {
        return;
    }
    if (entry->FrameVnMsg(vn, uve)) {
        DispatchVnMsg(uve);
    }
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

void VnUveTableBase::MarkChanged(const VnEntry *vn) {
    VnUveEntryBase* entry = UveEntryFromVn(vn);
    if (entry == NULL) {
        return;
    }

    entry->set_changed(true);
    return;
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
        tbb::mutex::scoped_lock lock(uve_vn_map_mutex_);
        uve_vn_map_.erase(it);
    }
}

VnUveEntryBase* VnUveTableBase::Add(const VnEntry *vn) {
    VnUveEntryPtr uve = Allocate(vn);
    pair<UveVnMap::iterator, bool> ret;
    ret = uve_vn_map_.insert(UveVnPair(vn->GetName(), uve));
    UveVnMap::iterator it = ret.first;
    VnUveEntryBase* entry = it->second.get();
    entry->set_vn(vn);
    if (entry->deleted()) {
        entry->set_renewed(true);
    }

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
            VnUveEntryBase *uve = UveEntryFromVn(vn);
            if (uve) {
                /* The Reset API sets 'deleted' flag and resets 'renewed' and
                 * 'add_by_vn_notify' flags */
                uve->Reset();
            }

            e->ClearState(partition->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vn_listener_id_, state);

        VnUveEntryBase* entry = Add(vn);
        entry->set_add_by_vn_notify(true);
    }
    MarkChanged(vn);
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
    vn_uve_entry->set_changed(true);
    return;
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
    vn_uve_entry->set_changed(true);
    return;
}

void VnUveTableBase::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    VnUveInterfaceState *state = static_cast<VnUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted() || (vm_port->vn() == NULL)) {
        if (state) {
            InterfaceDeleteHandler(state->vm_name_, state->vn_name_,
                                   vm_port);
            e->ClearState(partition->parent(), intf_listener_id_);
            delete state;
        }
    } else {
        const VnEntry *vn = vm_port->vn();
        const VmEntry *vm = vm_port->vm();
        std::string vm_name = vm? vm->GetCfgName() : agent_->NullString();

        if (!state) {
            state = new VnUveInterfaceState(vm_name, vn->GetName());
            e->SetState(partition->parent(), intf_listener_id_, state);
            InterfaceAddHandler(vn, vm_port, vm_name, state);
        } else {
            if (state->vn_name_.compare(vn->GetName()) != 0) {
                InterfaceDeleteHandler(state->vm_name_, state->vn_name_,
                                       vm_port);
                state->vm_name_ = vm_name;
                state->vn_name_ = vn->GetName();
                InterfaceAddHandler(vn, vm_port, vm_name, state);
                return;
            }
            if (state->vm_name_.compare(vm_name) != 0) {
                InterfaceAddHandler(vn, vm_port, vm_name, state);
                state->vm_name_ = vm_name;
            }
        }
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
