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

void ProuterUveTable::ProuterUveEntry::AddPhysicalInterface
                                           (const Interface *itf) {
    InterfaceSet::iterator it = physical_interface_set_.find(itf);
    if (it == physical_interface_set_.end()) {
        physical_interface_set_.insert(itf);
    }
}

void ProuterUveTable::ProuterUveEntry::DeletePhysicalInterface
                                           (const Interface *itf) {
    InterfaceSet::iterator it = physical_interface_set_.find(itf);
    if (it != physical_interface_set_.end()) {
        physical_interface_set_.erase(it);
    }
}

void ProuterUveTable::ProuterUveEntry::AddLogicalInterface
                                           (const LogicalInterface *itf) {
    LogicalInterfaceSet::iterator it = logical_interface_set_.find(itf);
    if (it == logical_interface_set_.end()) {
        logical_interface_set_.insert(itf);
    }
}

bool ProuterUveTable::ProuterUveEntry::DeleteLogicalInterface
                                           (const LogicalInterface *itf) {
    bool deleted = false;
    LogicalInterfaceSet::iterator it = logical_interface_set_.find(itf);
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
                InterfaceToPhyInterfaceUveEntry(const Interface *pintf) const {
    UvePhyInterfaceMap::const_iterator it = uve_phy_interface_map_.find(pintf);
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
    (const LogicalInterfaceSet &in, vector<UveLogicalInterfaceData> *out)
    const {
    LogicalInterfaceSet::iterator lit = in.begin();

    while (lit != in.end()) {
        UveLogicalInterfaceData lif_data;
        vector<string> vmi_list;
        const LogicalInterface *lif = *lit;
        const VmInterface *vmi = lif->vm_interface();
        ++lit;
        lif_data.set_name(lif->name());
        lif_data.set_uuid(to_string(lif->GetUuid()));
        if (vmi != NULL) {
            vmi_list.push_back(to_string(vmi->GetUuid()));
        }
        lif_data.set_vm_interface_list(vmi_list);
        out->push_back(lif_data);
    }
}

void ProuterUveTable::FrameProuterMsg(const PhysicalDevice *p,
                                          ProuterData *uve) const {
    vector<UvePhysicalInterfaceData> phy_if_list;
    vector<UveLogicalInterfaceData> logical_list;
    uve->set_name(p->fq_name());
    uve->set_uuid(to_string(p->uuid()));
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(p);
    InterfaceSet::iterator pit = entry->physical_interface_set_.begin();
    while (pit != entry->physical_interface_set_.end()) {
        UvePhysicalInterfaceData pif_data;
        vector<UveLogicalInterfaceData> lif_list;
        const Interface *pintf = *pit;
        ++pit;
        PhyInterfaceUveEntry *ientry = InterfaceToPhyInterfaceUveEntry(pintf);
        if (ientry != NULL) {
            FillLogicalInterfaceList(ientry->logical_interface_set_, &lif_list);
        }
        pif_data.set_name(pintf->name());
        pif_data.set_uuid(to_string(pintf->GetUuid()));
        pif_data.set_logical_interface_list(lif_list);
        phy_if_list.push_back(pif_data);
    }
    uve->set_physical_interface_list(phy_if_list);
    FillLogicalInterfaceList(entry->logical_interface_set_, &logical_list);
    uve->set_logical_interface_list(logical_list);
}

void ProuterUveTable::SendProuterMsg(const PhysicalDevice *p) {
    ProuterData uve;
    FrameProuterMsg(p, &uve);
    DispatchProuterMsg(uve);
}

void ProuterUveTable::SendProuterDeleteMsg(ProuterUveEntry *e) {
    ProuterData uve;
    uve.set_name(e->prouter_->name());
    uve.set_deleted(true);
    DispatchProuterMsg(uve);
}

void ProuterUveTable::SendProuterMsgFromPhyInterface(const Interface *pi) {
    const PhysicalDevice *pde = InterfaceToProuter(pi);
    if (pde && !pde->IsDeleted()) {
        SendProuterMsg(pde);
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
        plist.push_back(entry->prouter_->name());
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
    SendProuterMsg(p);
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
    uve_prouter_map_.erase(it);
    SendProuterVrouterAssociation();
}

void ProuterUveTable::AddLogicalInterface(const Interface *pintf,
                                              const LogicalInterface *intf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf);
    PhyInterfaceUveEntry *ientry = NULL;
    if (it == uve_phy_interface_map_.end()) {
        PhyInterfaceUveEntryPtr entry(new PhyInterfaceUveEntry());
        uve_phy_interface_map_.insert(UvePhyInterfacePair(pintf, entry));
        ientry = entry.get();
    } else {
        ientry = it->second.get();
    }
    LogicalInterfaceSet::iterator lit = ientry->logical_interface_set_.
        find(intf);
    if (lit == ientry->logical_interface_set_.end()) {
        ientry->logical_interface_set_.insert(intf);
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
    SendProuterMsg(p);
}

void ProuterUveTable::DeleteLogicalInterface(const Interface *pintf,
                                              const LogicalInterface *intf) {
    PhyInterfaceUveEntry *entry = InterfaceToPhyInterfaceUveEntry(pintf);
    if (entry != NULL) {
        LogicalInterfaceSet::iterator it = entry->logical_interface_set_.
            find(intf);
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
        SendProuterMsg(p);
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
            SendProuterMsg(pr);
        }
    }
}

void ProuterUveTable::DisassociatePhysicalInterface(const Interface *intf,
                                                   const PhysicalDevice *pde) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde);
    entry->DeletePhysicalInterface(intf);
    SendProuterMsg(pde);
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
        UvePhyInterfaceMap::iterator pit = uve_phy_interface_map_.find(intf);
        entry->DeletePhysicalInterface(intf);
        if (pit != uve_phy_interface_map_.end()) {
            uve_phy_interface_map_.erase(pit);
        }
    } else {
        entry->AddPhysicalInterface(intf);
    }
    SendProuterMsg(pde);
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
                /* If there is change in logical interface and its physical
                   interface is valid, just send Prouter UVE */
                if (physical_interface) {
                    SendProuterMsgFromPhyInterface(physical_interface);
                } else if (pde) {
                    SendProuterMsg(pde);
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
    agent_->physical_device_table()->Unregister(physical_device_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
}
