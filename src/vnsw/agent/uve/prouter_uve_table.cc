/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/prouter_uve_table.h>
#include <uve/agent_uve_base.h>

ProuterUveTable::ProuterUveTable(Agent *agent)
    : uve_prouter_map_(), uve_phy_interface_map_(), agent_(agent),
      physical_device_listener_id_(DBTableBase::kInvalidId),
      interface_listener_id_(DBTableBase::kInvalidId) {
}

ProuterUveTable::~ProuterUveTable() {
}

ProuterUveTable::PhyInterfaceAttrEntry::PhyInterfaceAttrEntry
    (const Interface *itf) :
    uuid_(itf->GetUuid()) {
}

ProuterUveTable::ProuterUveEntry::ProuterUveEntry(const PhysicalDevice *p) :
    name_(p->name()), uuid_(p->uuid()), physical_interface_set_(),
    encoder_task_trigger_(NULL), prouter_msg_enqueued_(false), deleted_(false) {
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
    if (encoder_task_trigger_) {
        encoder_task_trigger_->Reset();
        delete encoder_task_trigger_;
    }
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
ProuterUveTable::PDEntryToProuterUveEntry(const PhysicalDevice *p) const {
    UveProuterMap::const_iterator it = uve_prouter_map_.find(p->uuid());
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

void ProuterUveTable::RemoveUveEntry(const boost::uuids::uuid &u) {
    UveProuterMap::iterator it = uve_prouter_map_.find(u);
    if (it == uve_prouter_map_.end()) {
        return;
    }
    uve_prouter_map_.erase(it);
    SendProuterVrouterAssociation();
}

bool ProuterUveTable::SendProuterMsg(const PhysicalDevice *p,
                                     ProuterUveEntry *entry) {
    ProuterData uve;
    if (entry->deleted_) {
        RemoveUveEntry(p->uuid());
        return true;
    }
    FrameProuterMsg(entry, &uve);
    DispatchProuterMsg(uve);
    entry->prouter_msg_enqueued_ = false;
    return true;
}

void ProuterUveTable::SendProuterDeleteMsg(ProuterUveEntry *e) {
    ProuterData uve;
    uve.set_name(e->name_);
    uve.set_deleted(true);
    DispatchProuterMsg(uve);
}

void ProuterUveTable::EnqueueProuterMsg(const PhysicalDevice *p) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(p);
    if (entry->prouter_msg_enqueued_) {
        return;
    }
    if (!entry->encoder_task_trigger_) {
        entry->encoder_task_trigger_ =
           new TaskTrigger(boost::bind(&ProuterUveTable::SendProuterMsg, this,
                                       p, entry),
               TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    }
    /* encoder_task_trigger_ is TaskTrigger object. This will ensure that
     * TaskTrigger callback is not invoked if it is not completed yet */
    entry->encoder_task_trigger_->Set();
    entry->prouter_msg_enqueued_ = true;
}

void ProuterUveTable::SendProuterMsgFromPhyInterface(const Interface *pi) {
    const PhysicalDevice *pde = InterfaceToProuter(pi);
    if (pde && !pde->IsDeleted()) {
        EnqueueProuterMsg(pde);
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
    EnqueueProuterMsg(p);
    SendProuterVrouterAssociation();
    return it->second.get();
}

void ProuterUveTable::DeleteHandler(const PhysicalDevice *p) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    SendProuterDeleteMsg(entry);
    entry->physical_interface_set_.clear();
    entry->logical_interface_set_.clear();
    if (!entry->prouter_msg_enqueued_) {
        uve_prouter_map_.erase(it);
        SendProuterVrouterAssociation();
    } else {
        entry->deleted_ = true;
    }
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
    EnqueueProuterMsg(p);
}

void ProuterUveTable::UpdateProuterLogicalInterface
    (const PhysicalDevice *p, const LogicalInterface *intf) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    entry->UpdateLogicalInterface(intf);
    EnqueueProuterMsg(p);
}

void ProuterUveTable::DeleteLogicalInterface(const Interface *pintf,
                                             const LogicalInterface *intf) {
    PhyInterfaceUveEntry *entry = NameToPhyInterfaceUveEntry(pintf->name());
    if (entry != NULL) {
        LogicalInterfaceMap::iterator it = entry->logical_interface_set_.
            find(intf->GetUuid());
        if (it != entry->logical_interface_set_.end()) {
            entry->logical_interface_set_.erase(it);
        }
    }
    SendProuterMsgFromPhyInterface(pintf);
}

void ProuterUveTable::DeleteProuterLogicalInterface
    (const PhysicalDevice *p, const LogicalInterface *intf) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    bool deleted  = entry->DeleteLogicalInterface(intf);
    if (deleted) {
        EnqueueProuterMsg(p);
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
            EnqueueProuterMsg(pr);
        }
    }
}

void ProuterUveTable::DisassociatePhysicalInterface(const Interface *intf,
                                                   const PhysicalDevice *pde) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde);
    entry->DeletePhysicalInterface(intf);
    EnqueueProuterMsg(pde);
}

void ProuterUveTable::PhysicalInterfaceHandler(const Interface *intf,
                                               const PhysicalDevice *pde) {
    /* Ignore notifications for PhysicalInterface if it has no
       PhysicalDevice */
    if (pde == NULL) {
        return;
    }
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde);
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
    EnqueueProuterMsg(pde);
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
                if (state->physical_interface_) {
                    DeleteLogicalInterface(state->physical_interface_, lintf);
                }
                if (state->physical_device_) {
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
        if (intf->type() == Interface::LOGICAL) {
            bool uve_send = true;
            if (state->physical_interface_ != physical_interface) {
                if (state->physical_interface_) {
                    DeleteLogicalInterface(state->physical_interface_, lintf);
                }
                if (physical_interface) {
                    AddLogicalInterface(physical_interface, lintf);
                }
                state->physical_interface_ = physical_interface;
                uve_send = false;
            }
            if (state->physical_device_ != pde) {
                if (state->physical_device_) {
                    DeleteProuterLogicalInterface(state->physical_device_,
                                                  lintf);
                }
                if (!physical_interface && pde) {
                    AddProuterLogicalInterface(pde, lintf);
                }
                state->physical_device_ = pde;
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
            if (state->physical_device_ != pde) {
                if (state->physical_device_) {
                    DisassociatePhysicalInterface(intf,
                                                  state->physical_device_);
                }
                if (pde) {
                    if (pintf) {
                        PhysicalInterfaceHandler(pintf, pde);
                    } else if (rpintf) {
                        PhysicalInterfaceHandler(rpintf, pde);
                    }
                }
                state->physical_device_ = pde;
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
