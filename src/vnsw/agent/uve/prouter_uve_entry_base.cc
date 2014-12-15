/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/prouter_uve_entry_base.h>
#include <uve/agent_uve_base.h>

ProuterUveEntryBase::ProuterUveEntryBase(const PhysicalDevice *p)
    : prouter_(p) {
}

ProuterUveEntryBase::~ProuterUveEntryBase() {
}

void ProuterUveEntryBase::Clear() {
    PhyInterfaceMap::iterator it = physical_interface_map_.begin();
    while (it != physical_interface_map_.end()) {
        PhysicalInterfaceEntry *e = it->second.get();
        e->logical_interface_set_.clear();
        ++it;
    }
    physical_interface_map_.clear();
}

void ProuterUveEntryBase::FrameProuterMsg(ProuterData *uve) {
    vector<UvePhysicalInterfaceData> phy_if_list;
    uve->set_name(prouter_->name());
    uve->set_uuid(to_string(prouter_->uuid()));
    PhyInterfaceMap::iterator it = physical_interface_map_.begin();
    while (it != physical_interface_map_.end()) {
        UvePhysicalInterfaceData pif_data;
        vector<UveLogicalInterfaceData> lif_list;
        PhysicalInterfaceEntry *e = it->second.get();
        ++it;
        InterfaceSet::iterator pit = e->logical_interface_set_.begin();
        while (pit != e->logical_interface_set_.end()) {
            UveLogicalInterfaceData lif_data;
            vector<std::string> vmi_list;
            const LogicalInterface *lif = *pit;
            const VmInterface *vmi = lif->vm_interface();
            ++pit;
            lif_data.set_name(lif->display_name());
            lif_data.set_uuid(to_string(lif->GetUuid()));
            if (vmi != NULL) {
                vmi_list.push_back(to_string(vmi->GetUuid()));
            }
            lif_data.set_vm_interface_list(vmi_list);
            lif_list.push_back(lif_data);
        }
        const RemotePhysicalInterface *pif = static_cast
                <const RemotePhysicalInterface *>(e->physical_interface_);
        pif_data.set_name(pif->name());
        pif_data.set_uuid(to_string(pif->GetUuid()));
        pif_data.set_logical_interface_list(lif_list);
        phy_if_list.push_back(pif_data);
    }
    uve->set_physical_interface_list(phy_if_list);
}

void ProuterUveEntryBase::DeleteLogicalInterface(const LogicalInterface *intf,
                                                 const Interface *pintf) {
    PhyInterfaceMap::iterator it = physical_interface_map_.find(pintf);
    PhysicalInterfaceEntry *entry = NULL;
    if (it != physical_interface_map_.end()) {
        entry =  it->second.get();
        InterfaceSet::iterator lit = entry->logical_interface_set_.find(intf);
        if (lit != entry->logical_interface_set_.end()) {
            entry->logical_interface_set_.erase(lit);
        }
    }
}

void ProuterUveEntryBase::AddLogicalInterface(const LogicalInterface *intf) {
    Interface *pintf = intf->physical_interface();
    PhyInterfaceMap::iterator it = physical_interface_map_.find(pintf);
    PhysicalInterfaceEntry *entry = NULL;
    if (it != physical_interface_map_.end()) {
        entry =  it->second.get();
    } else {
        entry = new PhysicalInterfaceEntry(pintf);
        PhysicalInterfaceEntryPtr ptr(entry);
        physical_interface_map_.insert(PhyInterfacePair(pintf, ptr));
    }
    InterfaceSet::iterator lit = entry->logical_interface_set_.find(intf);
    if (lit == entry->logical_interface_set_.end()) {
        entry->logical_interface_set_.insert(intf);
    }
}

void ProuterUveEntryBase::DeletePhysicalInterface(const Interface *intf) {
    PhyInterfaceMap::iterator it = physical_interface_map_.find(intf);
    if (it != physical_interface_map_.end()) {
        physical_interface_map_.erase(it);
    }
}

void ProuterUveEntryBase::AddPhysicalInterface(const Interface *intf) {
    PhyInterfaceMap::iterator it = physical_interface_map_.find(intf);
    if (it == physical_interface_map_.end()) {
        PhysicalInterfaceEntryPtr entry(new PhysicalInterfaceEntry(intf));
        physical_interface_map_.insert(PhyInterfacePair(intf, entry));
    }
}
