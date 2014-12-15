/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/prouter_uve_table_base.h>
#include <uve/agent_uve_base.h>

ProuterUveTableBase::ProuterUveTableBase(Agent *agent)
    : uve_prouter_map_(), uve_phy_interface_map_(), agent_(agent),
      physical_device_listener_id_(DBTableBase::kInvalidId),
      interface_listener_id_(DBTableBase::kInvalidId) {
}

ProuterUveTableBase::~ProuterUveTableBase() {
}

void ProuterUveTableBase::ProuterUveEntry::AddPhysicalInterface
                                           (const Interface *itf) {
    InterfaceSet::iterator it = physical_interface_set_.find(itf);
    if (it == physical_interface_set_.end()) {
        physical_interface_set_.insert(itf);
    }
}

void ProuterUveTableBase::ProuterUveEntry::DeletePhysicalInterface
                                           (const Interface *itf) {
    InterfaceSet::iterator it = physical_interface_set_.find(itf);
    if (it != physical_interface_set_.end()) {
        physical_interface_set_.erase(it);
    }
}

ProuterUveTableBase::ProuterUveEntryPtr ProuterUveTableBase::Allocate
                                        (const PhysicalDevice *pr) {
    ProuterUveEntryPtr uve(new ProuterUveEntry(pr));
    return uve;
}

ProuterUveTableBase::ProuterUveEntry *ProuterUveTableBase::PDEntryToProuterUveEntry
                                              (const PhysicalDevice *p) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

ProuterUveTableBase::PhyInterfaceUveEntry *ProuterUveTableBase::
                InterfaceToPhyInterfaceUveEntry(const Interface *pintf) {
    UvePhyInterfaceMap::iterator it = uve_phy_interface_map_.find(pintf);
    if (it != uve_phy_interface_map_.end()) {
        return it->second.get();
    }
    return NULL;
}

const PhysicalDevice *ProuterUveTableBase::InterfaceToProuter
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

void ProuterUveTableBase::FrameProuterMsg(const PhysicalDevice *p,
                                          ProuterData *uve) {
    vector<UvePhysicalInterfaceData> phy_if_list;
    uve->set_name(p->name());
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
            LogicalInterfaceSet::iterator lit = ientry->logical_interface_set_.
                                                        begin();

            while (lit != ientry->logical_interface_set_.end()) {
                UveLogicalInterfaceData lif_data;
                vector<std::string> vmi_list;
                const LogicalInterface *lif = *lit;
                const VmInterface *vmi = lif->vm_interface();
                ++lit;
                lif_data.set_name(lif->display_name());
                lif_data.set_uuid(to_string(lif->GetUuid()));
                if (vmi != NULL) {
                    vmi_list.push_back(to_string(vmi->GetUuid()));
                }
                lif_data.set_vm_interface_list(vmi_list);
                lif_list.push_back(lif_data);
            }
        }
        pif_data.set_name(pintf->name());
        pif_data.set_uuid(to_string(pintf->GetUuid()));
        pif_data.set_logical_interface_list(lif_list);
        phy_if_list.push_back(pif_data);
    }
    uve->set_physical_interface_list(phy_if_list);
}

void ProuterUveTableBase::SendProuterMsg(const PhysicalDevice *p) {
    ProuterData uve;
    FrameProuterMsg(p, &uve);
    DispatchProuterMsg(uve);
}

void ProuterUveTableBase::SendProuterDeleteMsg(ProuterUveEntry *e) {
    ProuterData uve;
    uve.set_name(e->prouter_->name());
    uve.set_deleted(true);
    DispatchProuterMsg(uve);
}

void ProuterUveTableBase::DispatchProuterMsg(const ProuterData &uve) {
    UveProuterAgent::Send(uve);
}

void ProuterUveTableBase::SendProuterVrouterAssociation() {
    vector<string> plist;
    UveProuterMap::iterator it = uve_prouter_map_.begin();
    while (it != uve_prouter_map_.end()) {
        ProuterUveEntry* entry = it->second.get();
        plist.push_back(entry->prouter_->name());
        ++it;
    }
    agent_->uve()->vrouter_uve_entry()->SendVrouterProuterAssociation(plist);
}

ProuterUveTableBase::ProuterUveEntry* ProuterUveTableBase::Add
    (const PhysicalDevice *p) {
    ProuterUveEntryPtr uve = Allocate(p);
    pair<UveProuterMap::iterator, bool> ret;
    ret = uve_prouter_map_.insert(UveProuterPair(p->uuid(), uve));
    UveProuterMap::iterator it = ret.first;
    SendProuterMsg(p);
    SendProuterVrouterAssociation();
    return it->second.get();
}

void ProuterUveTableBase::Delete(const PhysicalDevice *p) {
    UveProuterMap::iterator it = uve_prouter_map_.find(p->uuid());
    if (it == uve_prouter_map_.end()) {
        return;
    }

    ProuterUveEntry* entry = it->second.get();
    SendProuterDeleteMsg(entry);
    entry->physical_interface_set_.clear();
    uve_prouter_map_.erase(it);
    SendProuterVrouterAssociation();
}

void ProuterUveTableBase::AddLogicalInterface(const Interface *pintf,
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
}

void ProuterUveTableBase::DeleteLogicalInterface(const Interface *pintf,
                                              const LogicalInterface *intf) {
    PhyInterfaceUveEntry *entry = InterfaceToPhyInterfaceUveEntry(pintf);
    if (entry != NULL) {
        LogicalInterfaceSet::iterator it = entry->logical_interface_set_.
            find(intf);
        if (it != entry->logical_interface_set_.end()) {
            entry->logical_interface_set_.erase(it);
        }
    }
}

void ProuterUveTableBase::PhysicalDeviceNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    const PhysicalDevice *pr = static_cast<const PhysicalDevice *>(e);
    PhysicalDeviceState *state = static_cast<PhysicalDeviceState *>
        (e->GetState(partition->parent(), physical_device_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            /* Uve Msg send is part of Delete API */
            Delete(pr);
            e->ClearState(partition->parent(), physical_device_listener_id_);
            delete state;
        }
    } else {
        if (!state) {
            state = new PhysicalDeviceState();
            e->SetState(partition->parent(), physical_device_listener_id_, state);
            /* Uve Msg send is part of Add API */
            Add(pr);
        } else {
            /* For Change notifications send only the msg */
            SendProuterMsg(pr);
        }
    }
}

void ProuterUveTableBase::DisassociateLogicalInterface
    (const LogicalInterface *intf, const Interface *pintf) { 
    DeleteLogicalInterface(pintf, intf);
    
    const PhysicalDevice *pde = InterfaceToProuter(pintf);
    if (pde) {
        SendProuterMsg(pde);
    }
}

void ProuterUveTableBase::DisassociatePhysicalInterface(const Interface *intf,
                                                   const PhysicalDevice *pde) {
    ProuterUveEntry *entry = PDEntryToProuterUveEntry(pde);
    InterfaceSet::iterator it = entry->physical_interface_set_.find(intf);
    if (it != entry->physical_interface_set_.end()) {
        entry->physical_interface_set_.erase(it);
    }
    SendProuterMsg(pde);
}

void ProuterUveTableBase::PhysicalInterfaceHandler(const Interface *intf,
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
    UvePhyInterfaceMap::iterator pit = uve_phy_interface_map_.find(intf);
    if (intf->IsDeleted()) {
        entry->DeletePhysicalInterface(intf);
        if (pit != uve_phy_interface_map_.end()) {
            uve_phy_interface_map_.erase(pit);
        }
    } else {
        entry->AddPhysicalInterface(intf);
    }
    SendProuterMsg(pde);
}

void ProuterUveTableBase::LogicalInterfaceHandler
    (const LogicalInterface *intf, const Interface *pintf) {
    /* Ignore notifications for LogicalInterface if it has no
       PhysicalInterface */
    if (pintf == NULL) {
        return;
    }
    if (intf->IsDeleted()) {
        DeleteLogicalInterface(pintf, intf);
    } else {
        AddLogicalInterface(pintf, intf);
    }
    const PhysicalDevice *pde = InterfaceToProuter(pintf);
    if (pde && !pde->IsDeleted()) {
        SendProuterMsg(pde);
    }
}

void ProuterUveTableBase::InterfaceNotify(DBTablePartBase *partition,
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
                LogicalInterfaceHandler(lintf, state->physical_interface_);
            }
            e->ClearState(partition->parent(), interface_listener_id_);
            delete state;
        }
    } else {
        if (intf->type() == Interface::LOGICAL) {
            //TODO: Handle change of physical_interface for logical_interface?
            if (state && state->physical_interface_ != NULL) {
                if (physical_interface == NULL) {
                    DisassociateLogicalInterface(lintf, state->physical_interface_);
                    state->physical_interface_ = NULL;
                    return;
                }
                assert(state->physical_interface_ == physical_interface);
            }
        } else {
            if (state && state->physical_device_ != NULL) {
                if (pde == NULL) {
                    DisassociatePhysicalInterface(intf, state->physical_device_);
                    state->physical_device_ = NULL;
                    return;
                }
                //TODO: Handle change of physical_device for interface?
                assert(state->physical_device_ == pde);
            }
        }
        if (!state) {
            state = new ProuterInterfaceState();
            e->SetState(partition->parent(), interface_listener_id_, state);
        }
        if (pintf) {
            PhysicalInterfaceHandler(pintf, pde);
            state->physical_device_ = pde;
        } else if (rpintf) {
            PhysicalInterfaceHandler(rpintf, pde);
            state->physical_device_ = pde;
        } else if (lintf) {
            LogicalInterfaceHandler(lintf, physical_interface);
            state->physical_interface_ = physical_interface;
        }
    }
}

void ProuterUveTableBase::RegisterDBClients() {
    PhysicalDeviceTable *pd_table = agent_->physical_device_table();
    physical_device_listener_id_ = pd_table->Register
        (boost::bind(&ProuterUveTableBase::PhysicalDeviceNotify, this, _1, _2));

    InterfaceTable *i_table = agent_->interface_table();
    interface_listener_id_ = i_table->Register
        (boost::bind(&ProuterUveTableBase::InterfaceNotify, this, _1, _2));
}

void ProuterUveTableBase::Shutdown(void) {
    agent_->physical_device_table()->Unregister(physical_device_listener_id_);
    agent_->interface_table()->Unregister(interface_listener_id_);
}
