/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/prouter_uve_table.h>
#include <uve/agent_uve_base.h>

ProuterUveTable::ProuterUveTable(Agent *agent, uint32_t default_intvl)
    : uve_prouter_map_(), uve_phy_interface_map_(), agent_(agent),
      physical_device_listener_id_(DBTableBase::kInvalidId),
      interface_listener_id_(DBTableBase::kInvalidId),
      timer_last_visited_(nil_uuid()),
      timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "ProuterUveTimer",
              TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0)) {
      expiry_time_ = default_intvl;
      timer_->Start(expiry_time_,
                    boost::bind(&ProuterUveTable::TimerExpiry, this));
}

ProuterUveTable::~ProuterUveTable() {
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;
    }
}

bool ProuterUveTable::TimerExpiry() {
    UveProuterMap::iterator it = uve_prouter_map_.lower_bound
        (timer_last_visited_);
    if (it == uve_prouter_map_.end()) {
        timer_last_visited_ = nil_uuid();
        return true;
    }

    uint32_t count = 0;
    while (it != uve_prouter_map_.end() &&
           count < AgentUveBase::kUveCountPerTimer) {
        ProuterUveEntry* entry = it->second.get();
        UveProuterMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted_) {
            SendProuterDeleteMsg(entry);
            if (!entry->renewed_) {
                uve_prouter_map_.erase(prev);
                SendProuterVrouterAssociation();
            } else {
                entry->deleted_ = false;
                entry->renewed_ = false;
                entry->changed_ = false;
                SendProuterMsg(entry);
            }
        } else if (entry->changed_) {
            SendProuterMsg(entry);
            entry->changed_ = false;
            /* Clear renew flag to be on safer side. Not really required */
            entry->renewed_ = false;
        }
    }

    if (it == uve_prouter_map_.end()) {
        timer_last_visited_ = nil_uuid();
        set_expiry_time(agent_->uve()->default_interval());
    } else {
        timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval());
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

void ProuterUveTable::set_expiry_time(int time) {
    if (time != expiry_time_) {
        expiry_time_ = time;
        timer_->Reschedule(expiry_time_);
    }
}

ProuterUveTable::PhyInterfaceAttrEntry::PhyInterfaceAttrEntry
    (const Interface *itf) :
    uuid_(itf->GetUuid()) {
}

ProuterUveTable::ProuterUveEntry::ProuterUveEntry(const PhysicalDevice *p) :
    name_(p->name()), uuid_(p->uuid()), physical_interface_set_(),
    changed_(true), deleted_(false), renewed_(false) {
}

ProuterUveTable::LogicalInterfaceUveEntry::LogicalInterfaceUveEntry
    (const LogicalInterface *li) : name_(li->name()) {
    Update(li);
}

void ProuterUveTable::LogicalInterfaceUveEntry::Update
    (const LogicalInterface *li) {
    const VlanLogicalInterface *vlif = dynamic_cast
            <const VlanLogicalInterface *>(li);
    if (vlif) {
        vlan_ = vlif->vlan();
    } else {
        vlan_ = kInvalidVlanId;
    }
    const VmInterface *vmi = li->vm_interface();
    if (vmi) {
        vmi_uuid_ = vmi->GetUuid();
    } else {
        vmi_uuid_ = nil_uuid();
    }
}

ProuterUveTable::ProuterUveEntry::~ProuterUveEntry() {
}

void ProuterUveTable::ProuterUveEntry::Reset() {
    physical_interface_set_.clear();
    logical_interface_set_.clear();
    deleted_ = true;
    renewed_ = false;
}

void ProuterUveTable::ProuterUveEntry::AddPhysicalInterface
                                           (const Interface *itf) {
    UvePhyInterfaceAttrMap::iterator it = physical_interface_set_.find(itf->
                                                                       name());
    if (it == physical_interface_set_.end()) {
        PhyInterfaceAttrEntryPtr uve(new PhyInterfaceAttrEntry(itf));
        physical_interface_set_.insert(UvePhyInterfaceAttrPair
                                       (itf->name(), uve));
    }
}

void ProuterUveTable::ProuterUveEntry::DeletePhysicalInterface
                                           (const Interface *itf) {
    UvePhyInterfaceAttrMap::iterator it = physical_interface_set_.find(itf->
                                                                       name());
    if (it != physical_interface_set_.end()) {
        physical_interface_set_.erase(it);
    }
}

void ProuterUveTable::ProuterUveEntry::AddLogicalInterface
                                           (const LogicalInterface *itf) {
    LogicalInterfaceMap::iterator it = logical_interface_set_.find
        (itf->GetUuid());
    if (it == logical_interface_set_.end()) {
        LogicalInterfaceUveEntryPtr uve(new LogicalInterfaceUveEntry(itf));
        logical_interface_set_.insert(LogicalInterfacePair(itf->GetUuid(),
                                                           uve));
    }
}

void ProuterUveTable::ProuterUveEntry::UpdateLogicalInterface
                                           (const LogicalInterface *itf) {
    LogicalInterfaceMap::iterator it = logical_interface_set_.find
        (itf->GetUuid());
    if (it == logical_interface_set_.end()) {
        return;
    } else {
        LogicalInterfaceUveEntry *entry = it->second.get();
        entry->Update(itf);
    }
}

bool ProuterUveTable::ProuterUveEntry::DeleteLogicalInterface
                                           (const LogicalInterface *itf) {
    bool deleted = false;
    LogicalInterfaceMap::iterator it = logical_interface_set_.find(itf->
                                                                   GetUuid());
    if (it != logical_interface_set_.end()) {
        logical_interface_set_.erase(it);
        deleted = true;
    }
    return deleted;
}

ProuterUveTable::ProuterUveEntryPtr ProuterUveTable::Allocate
                                        (const PhysicalDevice *pr) {
    ProuterUveEntryPtr uve(new ProuterUveEntry(pr));
    return uve;
}

ProuterUveTable::ProuterUveEntry *
ProuterUveTable::PDEntryToProuterUveEntry(const boost::uuids::uuid &u) const {
    UveProuterMap::const_iterator it = uve_prouter_map_.find(u);
    if (it == uve_prouter_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

ProuterUveTable::PhyInterfaceUveEntry *ProuterUveTable::
                NameToPhyInterfaceUveEntry(const string &name) const {
    UvePhyInterfaceMap::const_iterator it = uve_phy_interface_map_.find(name);
    if (it != uve_phy_interface_map_.end()) {
        return it->second.get();
    }
    return NULL;
}

const Interface *ProuterUveTable::NameToInterface(const string &name) const {
    PhysicalInterfaceKey phy_key(name);
    Interface *intf = static_cast<PhysicalInterface *>
        (agent_->interface_table()->FindActiveEntry(&phy_key));
    if (intf == NULL) {
        RemotePhysicalInterfaceKey rem_key(name);
        intf = static_cast<RemotePhysicalInterface *>
            (agent_->interface_table()->FindActiveEntry(&rem_key));
    }
    return intf;
}

const PhysicalDevice *ProuterUveTable::InterfaceToProuter
                                           (const Interface *intf) {
    PhysicalDevice *pde = NULL;
    const RemotePhysicalInterface *rpintf;
    const PhysicalInterface *pintf;
    if (intf->type() == Interface::REMOTE_PHYSICAL) {
        rpintf = static_cast<const RemotePhysicalInterface *>(intf);
        pde = rpintf->physical_device();
    } else if (intf->type() == Interface::PHYSICAL) {
        pintf = static_cast<const PhysicalInterface *>(intf);
        pde = pintf->physical_device();
    }
    return pde;
}

void ProuterUveTable::FillLogicalInterfaceList
    (const LogicalInterfaceMap &in, vector<UveLogicalInterfaceData> *out)
    const {
    LogicalInterfaceMap::const_iterator lit = in.begin();

    while (lit != in.end()) {
        UveLogicalInterfaceData lif_data;
        vector<string> vmi_list;
        LogicalInterfaceUveEntry *lentry = lit->second.get();
        boost::uuids::uuid luuid = lit->first;

        ++lit;
        lif_data.set_name(lentry->name_);
        lif_data.set_uuid(to_string(luuid));
        if (lentry->vlan_ != kInvalidVlanId) {
            lif_data.set_vlan(lentry->vlan_);
        }
        if (lentry->vmi_uuid_ != nil_uuid()) {
            vmi_list.push_back(to_string(lentry->vmi_uuid_));
        }
        lif_data.set_vm_interface_list(vmi_list);
        out->push_back(lif_data);
    }
}

void ProuterUveTable::FrameProuterMsg(ProuterUveEntry *entry,
                                      ProuterData *uve) const {
    vector<UvePhysicalInterfaceData> phy_if_list;
    vector<UveLogicalInterfaceData> logical_list;
    /* We are using hostname instead of fq-name because Prouter UVE sent by
     * other modules to send topology information uses hostname and we want
     * both these information to be seen in same UVE */
    uve->set_name(entry->name_);
    uve->set_uuid(to_string(entry->uuid_));
    UvePhyInterfaceAttrMap::iterator pit = entry->physical_interface_set_.
        begin();
    while (pit != entry->physical_interface_set_.end()) {
        UvePhysicalInterfaceData pif_data;
        vector<UveLogicalInterfaceData> lif_list;
        string pname = pit->first;
        PhyInterfaceAttrEntry *pentry = pit->second.get();
        ++pit;
        PhyInterfaceUveEntry *ientry = NameToPhyInterfaceUveEntry(pname);
        if (ientry != NULL) {
            FillLogicalInterfaceList(ientry->logical_interface_set_, &lif_list);
        }
        pif_data.set_name(pname);
        pif_data.set_uuid(to_string(pentry->uuid_));
        pif_data.set_logical_interface_list(lif_list);
        phy_if_list.push_back(pif_data);
    }
    uve->set_physical_interface_list(phy_if_list);
    FillLogicalInterfaceList(entry->logical_interface_set_, &logical_list);
    uve->set_logical_interface_list(logical_list);
}

bool ProuterUveTable::SendProuterMsg(ProuterUveEntry *entry) {
    ProuterData uve;
    FrameProuterMsg(entry, &uve);
    DispatchProuterMsg(uve);
    return true;
}

void ProuterUveTable::SendProuterDeleteMsg(ProuterUveEntry *e) {
    ProuterData uve;
    uve.set_name(e->name_);
    uve.set_deleted(true);
    DispatchProuterMsg(uve);
}

void ProuterUveTable::SendProuterMsgFromPhyInterface(const Interface *pi) {
    const PhysicalDevice *pde = InterfaceToProuter(pi);
    if (pde && !pde->IsDeleted()) {
        ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde->uuid());
        if (entry) {
            entry->changed_ = true;
        }
    }
}

void ProuterUveTable::DispatchProuterMsg(const ProuterData &uve) {
    UveProuterAgent::Send(uve);
}

void ProuterUveTable::SendProuterVrouterAssociation() {
    vector<string> plist;
    UveProuterMap::iterator it = uve_prouter_map_.begin();
    while (it != uve_prouter_map_.end()) {
        ProuterUveEntry* entry = it->second.get();
        plist.push_back(entry->name_);
        ++it;
    }
    agent_->uve()->vrouter_uve_entry()->SendVrouterProuterAssociation(plist);
}

ProuterUveTable::ProuterUveEntry* ProuterUveTable::AddHandler
    (const PhysicalDevice *p) {
    ProuterUveEntryPtr uve = Allocate(p);
    pair<UveProuterMap::iterator, bool> ret;
    ret = uve_prouter_map_.insert(UveProuterPair(p->uuid(), uve));
    UveProuterMap::iterator it = ret.first;
    ProuterUveEntry *entry = it->second.get();
    entry->changed_ = true;
    if (entry->deleted_) {
        entry->renewed_ = true;
    }
    if (!entry->renewed_) {
        SendProuterVrouterAssociation();
    }
    return entry;
}

void ProuterUveTable::DeleteHandler(const PhysicalDevice *p) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    /* Reset all the non-key fields of entry so that it has proper values in
     * case it gets reused. Also update deleted_ and renewed_ flags */
    entry->Reset();
}

void ProuterUveTable::UpdateLogicalInterface(const Interface *pintf,
                                             const LogicalInterface *intf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf->
                                                                  name());
    PhyInterfaceUveEntry *ientry = NULL;
    if (it == uve_phy_interface_map_.end()) {
        return;
    } else {
        ientry = it->second.get();
    }
    LogicalInterfaceMap::iterator lit = ientry->logical_interface_set_.
        find(intf->GetUuid());
    if (lit == ientry->logical_interface_set_.end()) {
        return;
    } else {
        LogicalInterfaceUveEntry *lentry = lit->second.get();
        lentry->Update(intf);
    }
    SendProuterMsgFromPhyInterface(pintf);
}

void ProuterUveTable::AddLogicalInterface(const Interface *pintf,
                                          const LogicalInterface *intf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf->
                                                                  name());
    PhyInterfaceUveEntry *ientry = NULL;
    if (it == uve_phy_interface_map_.end()) {
        PhyInterfaceUveEntryPtr entry(new PhyInterfaceUveEntry());
        uve_phy_interface_map_.insert(UvePhyInterfacePair(pintf->name(),
                                                          entry));
        ientry = entry.get();
    } else {
        ientry = it->second.get();
    }
    LogicalInterfaceMap::iterator lit = ientry->logical_interface_set_.
        find(intf->GetUuid());
    if (lit == ientry->logical_interface_set_.end()) {
        LogicalInterfaceUveEntryPtr uve(new LogicalInterfaceUveEntry(intf));
        ientry->logical_interface_set_.insert(LogicalInterfacePair
                                              (intf->GetUuid(), uve));
    }
    SendProuterMsgFromPhyInterface(pintf);
}

void ProuterUveTable::AddProuterLogicalInterface
    (const PhysicalDevice *p, const LogicalInterface *intf) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    entry->AddLogicalInterface(intf);
    entry->changed_ = true;
}

void ProuterUveTable::UpdateProuterLogicalInterface
    (const PhysicalDevice *p, const LogicalInterface *intf) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    entry->UpdateLogicalInterface(intf);
    entry->changed_ = true;
}

void ProuterUveTable::DeleteLogicalInterface(const string &name,
                                             const LogicalInterface *intf) {
    PhyInterfaceUveEntry *entry = NameToPhyInterfaceUveEntry(name);
    if (entry != NULL) {
        LogicalInterfaceMap::iterator it = entry->logical_interface_set_.
            find(intf->GetUuid());
        if (it != entry->logical_interface_set_.end()) {
            entry->logical_interface_set_.erase(it);
        }
    }
    const Interface *pintf = NameToInterface(name);
    if (pintf) {
        /* If Physical interface is not found (because it is marked for delete),
         * then required UVEs will be sent as part of physical interface
         * delete notification
         */
        SendProuterMsgFromPhyInterface(pintf);
    }
}

void ProuterUveTable::DeleteProuterLogicalInterface
    (const boost::uuids::uuid &u, const LogicalInterface *intf) {
    UveProuterMap::iterator it = uve_prouter_map_.find(u);
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    bool deleted  = entry->DeleteLogicalInterface(intf);
    if (deleted) {
        entry->changed_ = true;
    }
}

void ProuterUveTable::PhysicalDeviceNotify(DBTablePartBase *partition,
                                           DBEntryBase *e) {
    const PhysicalDevice *pr = static_cast<const PhysicalDevice *>(e);
    PhysicalDeviceState *state = static_cast<PhysicalDeviceState *>
        (e->GetState(partition->parent(), physical_device_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            /* Uve Msg send is part of Delete API */
            DeleteHandler(pr);
            e->ClearState(partition->parent(), physical_device_listener_id_);
            delete state;
        }
    } else {
        if (!state) {
            state = new PhysicalDeviceState();
            e->SetState(partition->parent(), physical_device_listener_id_,
                        state);
            /* Uve Msg send is part of Add API */
            AddHandler(pr);
        } else {
            /* For Change notifications send only the msg */
            ProuterUveEntry *entry = PDEntryToProuterUveEntry(pr->uuid());
            entry->changed_ = true;
        }
    }
}

void ProuterUveTable::DisassociatePhysicalInterface(const Interface *intf,
                                                 const boost::uuids::uuid &u) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(u);
    if (!entry) {
        return;
    }
    entry->DeletePhysicalInterface(intf);
    entry->changed_ = true;
}

void ProuterUveTable::PhysicalInterfaceHandler(const Interface *intf,
                                               const boost::uuids::uuid &u) {
    /* Ignore notifications for PhysicalInterface if it has no
       PhysicalDevice */
    if (u == nil_uuid()) {
        return;
    }
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(u);
    if (entry == NULL) {
        /* We hit this condition when physical-device is deleted before
         * physical interface */
        return;
    }
    if (intf->IsDeleted()) {
        UvePhyInterfaceMap::iterator pit = uve_phy_interface_map_.find(intf->
                                                                       name());
        entry->DeletePhysicalInterface(intf);
        if (pit != uve_phy_interface_map_.end()) {
            uve_phy_interface_map_.erase(pit);
        }
    } else {
        entry->AddPhysicalInterface(intf);
    }
    entry->changed_ = true;
}

void ProuterUveTable::InterfaceNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    const LogicalInterface *lintf = NULL;
    const RemotePhysicalInterface *rpintf = NULL;
    const PhysicalInterface *pintf = NULL;
    const Interface *physical_interface = NULL;
    PhysicalDevice *pde = NULL;
    if (intf->type() == Interface::REMOTE_PHYSICAL) {
        rpintf = static_cast<const RemotePhysicalInterface *>(intf);
        pde = rpintf->physical_device();
    } else if (intf->type() == Interface::LOGICAL) {
        lintf = static_cast<const LogicalInterface *>(intf);
        physical_interface = lintf->physical_interface();
        pde = lintf->physical_device();
    } else if (intf->type() == Interface::PHYSICAL) {
        pintf = static_cast<const PhysicalInterface *>(intf);
        pde = pintf->physical_device();
    } else {
        /* Ignore notifications for interface which are not
           RemotePhysical/Logical */
        return;
    }
    ProuterInterfaceState *state = static_cast<ProuterInterfaceState *>
                    (e->GetState(partition->parent(), interface_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            if (pintf) {
                PhysicalInterfaceHandler(pintf, state->physical_device_);
            } else if (rpintf) {
                PhysicalInterfaceHandler(rpintf, state->physical_device_);
            } else if (lintf) {
                if (!state->physical_interface_.empty()) {
                    DeleteLogicalInterface(state->physical_interface_, lintf);
                }
                if (state->physical_device_ != nil_uuid()) {
                    DeleteProuterLogicalInterface(state->physical_device_,
                                                  lintf);
                }
            }
            e->ClearState(partition->parent(), interface_listener_id_);
            delete state;
        }
    } else {
        if (!state) {
            state = new ProuterInterfaceState();
            e->SetState(partition->parent(), interface_listener_id_, state);
        }
        boost::uuids::uuid pde_uuid = nil_uuid();
        if (pde) {
            pde_uuid = pde->uuid();
        }
        string pname;
        if (physical_interface) {
            pname = physical_interface->name();
        }
        if (intf->type() == Interface::LOGICAL) {
            bool uve_send = true;
            if (state->physical_interface_ != pname) {
                if (!state->physical_interface_.empty()) {
                    DeleteLogicalInterface(state->physical_interface_, lintf);
                }
                if (physical_interface) {
                    AddLogicalInterface(physical_interface, lintf);
                }
                state->physical_interface_ = pname;
                uve_send = false;
            }
            if (state->physical_device_ != pde_uuid) {
                if (state->physical_device_ != nil_uuid()) {
                    DeleteProuterLogicalInterface(state->physical_device_,
                                                  lintf);
                }
                if (!physical_interface && pde) {
                    AddProuterLogicalInterface(pde, lintf);
                }
                state->physical_device_ = pde_uuid;
                uve_send = false;
            }
            if (uve_send) {
                /* There is change in logical interface. Update our
                   logical interface data and send UVE */
                if (physical_interface) {
                    UpdateLogicalInterface(physical_interface, lintf);
                } else if (pde) {
                    UpdateProuterLogicalInterface(pde, lintf);
                }
            }
        } else {
            if (state->physical_device_ != pde_uuid) {
                if (state->physical_device_ != nil_uuid()) {
                    DisassociatePhysicalInterface(intf,
                                                  state->physical_device_);
                }
                if (pde) {
                    if (pintf) {
                        PhysicalInterfaceHandler(pintf, pde->uuid());
                    } else if (rpintf) {
                        PhysicalInterfaceHandler(rpintf, pde->uuid());
                    }
                }
                state->physical_device_ = pde_uuid;
            }
        }
    }
}

void ProuterUveTable::RegisterDBClients() {
    PhysicalDeviceTable *pd_table = agent_->physical_device_table();
    physical_device_listener_id_ = pd_table->Register
        (boost::bind(&ProuterUveTable::PhysicalDeviceNotify, this, _1, _2));

    InterfaceTable *i_table = agent_->interface_table();
    interface_listener_id_ = i_table->Register
        (boost::bind(&ProuterUveTable::InterfaceNotify, this, _1, _2));
}

void ProuterUveTable::Shutdown(void) {
    if (physical_device_listener_id_ != DBTableBase::kInvalidId)
        agent_->physical_device_table()->
            Unregister(physical_device_listener_id_);
    if (interface_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(interface_listener_id_);
}
