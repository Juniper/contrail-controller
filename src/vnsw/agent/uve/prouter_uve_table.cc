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
      pr_timer_last_visited_(nil_uuid()),
      pr_timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "ProuterUveTimer",
              TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0)),
      pi_timer_last_visited_(""),
      pi_timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "PIUveTimer",
              TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0)),
      li_timer_last_visited_(nil_uuid()),
      li_timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "LIUveTimer",
              TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0)) {

      pr_timer_->Start(default_intvl,
                    boost::bind(&ProuterUveTable::TimerExpiry, this));
      pi_timer_->Start(default_intvl,
                    boost::bind(&ProuterUveTable::PITimerExpiry, this));
      li_timer_->Start(default_intvl,
                    boost::bind(&ProuterUveTable::LITimerExpiry, this));
}

ProuterUveTable::~ProuterUveTable() {
    TimerCleanup(pr_timer_);
    TimerCleanup(pi_timer_);
    TimerCleanup(li_timer_);
}

void ProuterUveTable::TimerCleanup(Timer *timer) {
    if (timer) {
        timer->Cancel();
        TimerManager::DeleteTimer(timer);
    }
}

bool ProuterUveTable::TimerExpiry() {
    UveProuterMap::iterator it = uve_prouter_map_.lower_bound
        (pr_timer_last_visited_);
    if (it == uve_prouter_map_.end()) {
        pr_timer_last_visited_ = nil_uuid();
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
        pr_timer_last_visited_ = nil_uuid();
        set_expiry_time(agent_->uve()->default_interval(), pr_timer_);
    } else {
        pr_timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval(), pr_timer_);
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

bool ProuterUveTable::PITimerExpiry() {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.lower_bound
        (pi_timer_last_visited_);
    if (it == uve_phy_interface_map_.end()) {
        pi_timer_last_visited_ = "";
        return true;
    }

    uint32_t count = 0;
    while (it != uve_phy_interface_map_.end() &&
           count < AgentUveBase::kUveCountPerTimer) {
        PhyInterfaceUveEntry* entry = it->second.get();
        string cfg_name = it->first;
        UvePhyInterfaceMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted_) {
            SendPhysicalInterfaceDeleteMsg(cfg_name);
            if (!entry->renewed_) {
                uve_phy_interface_map_.erase(prev);
            } else {
                entry->deleted_ = false;
                entry->renewed_ = false;
                entry->changed_ = false;
                SendPhysicalInterfaceMsg(cfg_name, entry);
            }
        } else if (entry->changed_) {
            SendPhysicalInterfaceMsg(cfg_name, entry);
            entry->changed_ = false;
            /* Clear renew flag to be on safer side. Not really required */
            entry->renewed_ = false;
        }
    }

    if (it == uve_phy_interface_map_.end()) {
        pi_timer_last_visited_ = "";
        set_expiry_time(agent_->uve()->default_interval(), pi_timer_);
    } else {
        pi_timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval(), pi_timer_);
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

bool ProuterUveTable::LITimerExpiry() {
    LogicalInterfaceMap::iterator it = uve_logical_interface_map_.lower_bound
        (li_timer_last_visited_);
    if (it == uve_logical_interface_map_.end()) {
        li_timer_last_visited_ = nil_uuid();
        return true;
    }

    uint32_t count = 0;
    while (it != uve_logical_interface_map_.end() &&
           count < AgentUveBase::kUveCountPerTimer) {
        LogicalInterfaceUveEntry* entry = it->second.get();
        boost::uuids::uuid u = it->first;
        LogicalInterfaceMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted_) {
            SendLogicalInterfaceDeleteMsg(entry->name_);
            if (!entry->renewed_) {
                uve_logical_interface_map_.erase(prev);
            } else {
                entry->deleted_ = false;
                entry->renewed_ = false;
                entry->changed_ = false;
                SendLogicalInterfaceMsg(u, entry);
            }
        } else if (entry->changed_) {
            SendLogicalInterfaceMsg(u, entry);
            entry->changed_ = false;
            /* Clear renew flag to be on safer side. Not really required */
            entry->renewed_ = false;
        }
    }

    if (it == uve_logical_interface_map_.end()) {
        li_timer_last_visited_ = nil_uuid();
        set_expiry_time(agent_->uve()->default_interval(), li_timer_);
    } else {
        li_timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval(), li_timer_);
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

void ProuterUveTable::set_expiry_time(int time, Timer *timer) {
    if (time != timer->time()) {
        timer->Reschedule(time);
    }
}

ProuterUveTable::PhyInterfaceUveEntry::PhyInterfaceUveEntry
    (const Interface *pintf) :
    uuid_(pintf->GetUuid()), logical_interface_set_(), changed_(true),
    deleted_(false), renewed_(false) {
    Update(pintf);
}

void ProuterUveTable::PhyInterfaceUveEntry::Update(const Interface *pintf) {
}

ProuterUveTable::ProuterUveEntry::ProuterUveEntry(const PhysicalDevice *p) :
    name_(p->name()), uuid_(p->uuid()), physical_interface_set_(),
    changed_(true), deleted_(false), renewed_(false), mastership_(p->master()) {
}

ProuterUveTable::LogicalInterfaceUveEntry::LogicalInterfaceUveEntry
    (const LogicalInterface *li) : name_(li->name()), vlan_(kInvalidVlanId),
    vmi_list_(), changed_(true), deleted_(false), renewed_(false) {
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
    changed_ = true;
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
    InterfaceSet::iterator it = physical_interface_set_.find(itf->name());
    if (it == physical_interface_set_.end()) {
        physical_interface_set_.insert(itf->name());
        changed_ = true;
    }
}

void ProuterUveTable::ProuterUveEntry::DeletePhysicalInterface
                                           (const Interface *itf) {
    InterfaceSet::iterator it = physical_interface_set_.find(itf->name());
    if (it != physical_interface_set_.end()) {
        physical_interface_set_.erase(it);
        changed_ = true;
    }
}

void ProuterUveTable::ProuterUveEntry::AddLogicalInterface
                                           (const LogicalInterface *itf) {
    LogicalInterfaceSet::iterator it = logical_interface_set_.find
        (itf->GetUuid());
    if (it == logical_interface_set_.end()) {
        logical_interface_set_.insert(itf->GetUuid());
    }
}

bool ProuterUveTable::ProuterUveEntry::DeleteLogicalInterface
                                           (const LogicalInterface *itf) {
    bool deleted = false;
    LogicalInterfaceSet::iterator it = logical_interface_set_.find(itf->
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

void ProuterUveTable::LogicalInterfaceUveEntry::FillVmInterfaceList
    (vector<string> &vmi_list) const {
    ProuterUveTable::InterfaceSet::iterator vit = vmi_list_.begin();
    while (vit != vmi_list_.end()) {
        vmi_list.push_back((*vit));
        ++vit;
    }
    return;
}

void ProuterUveTable::PhyInterfaceUveEntry::FillLogicalInterfaceList
    (vector<string> &list) const {
    ProuterUveTable::LogicalInterfaceSet::iterator it =
        logical_interface_set_.begin();
    while (it != logical_interface_set_.end()) {
        list.push_back((to_string(*it)));
        ++it;
    }
    return;
}

void ProuterUveTable::FrameProuterMsg(ProuterUveEntry *entry,
                                      ProuterData *uve) const {
    vector<string> phy_if_list;
    vector<string> logical_list;
    vector<string> connected_agent_list;
    /* We are using hostname instead of fq-name because Prouter UVE sent by
     * other modules to send topology information uses hostname and we want
     * both these information to be seen in same UVE */
    uve->set_name(entry->name_);
    uve->set_uuid(to_string(entry->uuid_));
    uve->set_agent_name(agent_->agent_name());
    InterfaceSet::iterator pit = entry->physical_interface_set_.begin();
    while (pit != entry->physical_interface_set_.end()) {
        phy_if_list.push_back(*pit);
        ++pit;
    }
    uve->set_physical_interface_list(phy_if_list);


    LogicalInterfaceSet::const_iterator lit = entry->logical_interface_set_.
        begin();

    while (lit != entry->logical_interface_set_.end()) {
        logical_list.push_back(to_string(*lit));
        ++lit;
    }
    uve->set_logical_interface_list(logical_list);
    if (entry->mastership_) {
        connected_agent_list.push_back(agent_->agent_name());
        uve->set_connected_agent_list(connected_agent_list);
    } else {
        /* Send Empty list */
        uve->set_connected_agent_list(connected_agent_list);
    }
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

void ProuterUveTable::MarkPhysicalDeviceChanged(const PhysicalDevice *pde) {
    if (pde && !pde->IsDeleted()) {
        ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde->uuid());
        if (entry) {
            entry->changed_ = true;
        }
    }
}

void ProuterUveTable::SendProuterMsgFromPhyInterface(const Interface *pi) {
    const PhysicalDevice *pde = InterfaceToProuter(pi);
    MarkPhysicalDeviceChanged(pde);
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
    entry->mastership_ = p->master();
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

void ProuterUveTable::MarkDeletedLogical(const LogicalInterface *itf) {
    LogicalInterfaceMap::iterator it = uve_logical_interface_map_.find(itf->
                                                                    GetUuid());
    if (it != uve_logical_interface_map_.end()) {
        LogicalInterfaceUveEntry *entry = it->second.get();
        entry->deleted_ = true;
    }
}

void ProuterUveTable::AddUpdateLogicalInterface(const LogicalInterface *itf) {
    LogicalInterfaceMap::iterator it = uve_logical_interface_map_.find(itf->
                                                                    GetUuid());
    if (it == uve_logical_interface_map_.end()) {
        LogicalInterfaceUveEntryPtr uve(new LogicalInterfaceUveEntry(itf));
        uve_logical_interface_map_.insert(LogicalInterfacePair(itf->GetUuid(),
                                                               uve));
        return;
    } else {
        LogicalInterfaceUveEntry *entry = it->second.get();
        entry->Update(itf);
    }
}

void ProuterUveTable::MarkDeletedPhysical(const Interface *pintf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf->
                                                                  name());
    PhyInterfaceUveEntry *entry = NULL;
    if (it != uve_phy_interface_map_.end()) {
        entry = it->second.get();
        entry->deleted_ = true;
    }
}

void ProuterUveTable::AddLogicalToPhysical(const Interface *pintf,
                                           const LogicalInterface *intf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf->
                                                                  name());
    PhyInterfaceUveEntry *ientry = NULL;
    if (it == uve_phy_interface_map_.end()) {
        PhyInterfaceUveEntryPtr entry(new PhyInterfaceUveEntry(pintf));
        uve_phy_interface_map_.insert(UvePhyInterfacePair(pintf->name(),
                                                          entry));
        ientry = entry.get();
    } else {
        ientry = it->second.get();
    }
    LogicalInterfaceSet::iterator lit = ientry->logical_interface_set_.
        find(intf->GetUuid());
    if (lit == ientry->logical_interface_set_.end()) {
        ientry->logical_interface_set_.insert(intf->GetUuid());
        ientry->changed_ = true;
    }
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

void ProuterUveTable::DeleteLogicalFromPhysical(const string &name,
                                             const LogicalInterface *intf) {
    PhyInterfaceUveEntry *entry = NameToPhyInterfaceUveEntry(name);
    if (entry != NULL) {
        LogicalInterfaceSet::iterator it = entry->logical_interface_set_.
            find(intf->GetUuid());
        if (it != entry->logical_interface_set_.end()) {
            entry->logical_interface_set_.erase(it);
            entry->changed_ = true;
        }
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
            entry->mastership_ = pr->master();
            entry->changed_ = true;
        }
    }
}

void ProuterUveTable::DeletePhysicalFromProuter(const Interface *intf,
                                                const boost::uuids::uuid &u) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(u);
    if (!entry) {
        return;
    }
    entry->DeletePhysicalInterface(intf);
    entry->changed_ = true;
}

void ProuterUveTable::AddUpdatePhysicalInterface(const Interface *intf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(intf->name());
    if (it == uve_phy_interface_map_.end()) {
        PhyInterfaceUveEntryPtr uve(new PhyInterfaceUveEntry(intf));
        uve_phy_interface_map_.insert(UvePhyInterfacePair(intf->name(), uve));
        return;
    }
    PhyInterfaceUveEntry *entry = it->second.get();
    entry->Update(intf);
}

void ProuterUveTable::PhysicalInterfaceHandler(const Interface *intf,
                                               const boost::uuids::uuid &u) {
    if (intf->IsDeleted()) {
        MarkDeletedPhysical(intf);
    }
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
        entry->DeletePhysicalInterface(intf);
    } else {
        entry->AddPhysicalInterface(intf);
    }
}

void ProuterUveTable::VMInterfaceAdd(const VmInterface *vmi) {
    LogicalInterfaceUveEntry *entry;
    const boost::uuids::uuid li = vmi->logical_interface();
    LogicalInterfaceMap::iterator it = uve_logical_interface_map_.find(li);
    if (it != uve_logical_interface_map_.end()) {
        entry = it->second.get();
    } else {
        LogicalInterface *intf;
        VlanLogicalInterfaceKey key(li, "");
        intf = static_cast<LogicalInterface *>
            (agent_->interface_table()->FindActiveEntry(&key));
        if (!intf) {
            return;
        }
        LogicalInterfaceUveEntryPtr uve(new LogicalInterfaceUveEntry(intf));
        uve_logical_interface_map_.insert(LogicalInterfacePair(li, uve));
        entry = uve.get();
    }
    InterfaceSet::iterator vit = entry->vmi_list_.find(vmi->cfg_name());
    if (vit == entry->vmi_list_.end()) {
        entry->vmi_list_.insert(vmi->cfg_name());
        entry->changed_ = true;
    }
}

void ProuterUveTable::VMInterfaceRemove(const boost::uuids::uuid &li,
                                                   const VmInterface *vmi) {
    LogicalInterfaceMap::iterator it = uve_logical_interface_map_.find(li);
    if (it != uve_logical_interface_map_.end()) {
        LogicalInterfaceUveEntry *entry = it->second.get();
        ProuterUveTable::InterfaceSet::iterator vit = entry->vmi_list_.find
            (vmi->cfg_name());
        if (vit != entry->vmi_list_.end()) {
            entry->vmi_list_.erase(vit);
            entry->changed_ = true;
        }
    }
}

void ProuterUveTable::VmInterfaceHandler(DBTablePartBase *partition,
    DBEntryBase *e) {
    VmInterfaceState *state = static_cast<VmInterfaceState *>
                    (e->GetState(partition->parent(), interface_listener_id_));
    const VmInterface *intf = static_cast<const VmInterface *>(e);
    if (e->IsDeleted()) {
        if (state) {
            VMInterfaceRemove(state->logical_interface_, intf);
            e->ClearState(partition->parent(), interface_listener_id_);
            delete state;
        }
        return;
    }
    if (!state) {
        state = new VmInterfaceState();
        e->SetState(partition->parent(), interface_listener_id_, state);
    }
    if (state->logical_interface_ != intf->logical_interface()) {
        if (state->logical_interface_ != nil_uuid()) {
            VMInterfaceRemove(state->logical_interface_, intf);
        }
        state->logical_interface_ = intf->logical_interface();
    }
    if (intf->logical_interface() != nil_uuid()) {
        VMInterfaceAdd(intf);
    }
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
        /* Ignore PhysicalInterface notifications if it is not of subtype
         * CONFIG */
        if (pintf->subtype() != PhysicalInterface::CONFIG) {
            return;
        }
        pde = pintf->physical_device();
    } else if (intf->type() == Interface::VM_INTERFACE) {
        VmInterfaceHandler(partition, e);
        return;
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
                    DeleteLogicalFromPhysical(state->physical_interface_,
                                              lintf);
                }
                if (state->physical_device_ != nil_uuid()) {
                    DeleteProuterLogicalInterface(state->physical_device_,
                                                  lintf);
                }
                MarkDeletedLogical(lintf);
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
            if (state->physical_interface_ != pname) {
                if (!state->physical_interface_.empty()) {
                    DeleteLogicalFromPhysical(state->physical_interface_,
                                              lintf);
                }
                if (physical_interface) {
                    AddLogicalToPhysical(physical_interface, lintf);
                }
                state->physical_interface_ = pname;
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
            }
            AddUpdateLogicalInterface(lintf);
        } else {
            if (state->physical_device_ != pde_uuid) {
                if (state->physical_device_ != nil_uuid()) {
                    DeletePhysicalFromProuter(intf, state->physical_device_);
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
            AddUpdatePhysicalInterface(intf);
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

void ProuterUveTable::SendLogicalInterfaceDeleteMsg(const string &config_name) {
    UveLogicalInterfaceAgent uve;
    uve.set_name(config_name);
    uve.set_deleted(true);
    DispatchLogicalInterfaceMsg(uve);
}

void ProuterUveTable::SendLogicalInterfaceMsg(const boost::uuids::uuid &u,
                                              LogicalInterfaceUveEntry *entry) {
    UveLogicalInterfaceAgent uve;
    vector<string> list;

    uve.set_name(to_string(u));
    uve.set_config_name(entry->name_);
    uve.set_vlan(entry->vlan_);

    entry->FillVmInterfaceList(list);
    uve.set_vm_interface_list(list);

    DispatchLogicalInterfaceMsg(uve);
}

void ProuterUveTable::DispatchLogicalInterfaceMsg
    (const UveLogicalInterfaceAgent &uve) {
    UveLogicalInterfaceAgentTrace::Send(uve);
}

void ProuterUveTable::SendPhysicalInterfaceDeleteMsg(const string &cfg_name) {
    UvePhysicalInterfaceAgent uve;
    uve.set_name(cfg_name);
    uve.set_deleted(true);
    DispatchPhysicalInterfaceMsg(uve);
}

void ProuterUveTable::SendPhysicalInterfaceMsg(const string &cfg_name,
                                               PhyInterfaceUveEntry *entry) {
    UvePhysicalInterfaceAgent uve;
    vector<string> list;

    uve.set_name(cfg_name);
    uve.set_uuid(to_string(entry->uuid_));

    entry->FillLogicalInterfaceList(list);
    uve.set_logical_interface_list(list);

    DispatchPhysicalInterfaceMsg(uve);
}

void ProuterUveTable::DispatchPhysicalInterfaceMsg
    (const UvePhysicalInterfaceAgent &uve) {
    UvePhysicalInterfaceAgentTrace::Send(uve);
}

void ProuterUveTable::UpdateMastership(const boost::uuids::uuid &u, bool value)
{
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(u);
    if (entry == NULL) {
        return;
    }
    if (entry->mastership_ != value) {
        entry->mastership_ = value;
        entry->changed_ = true;
    }
}
