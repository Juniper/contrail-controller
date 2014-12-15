/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_prouter_uve_entry_base_h
#define vnsw_agent_prouter_uve_entry_base_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <prouter_types.h>
#include <oper/interface_common.h>
#include <oper/physical_device.h>

typedef std::set<const LogicalInterface *> InterfaceSet;
struct PhysicalInterfaceEntry {
    const Interface *physical_interface_;
    InterfaceSet logical_interface_set_;
    PhysicalInterfaceEntry(const Interface *itf) :
        physical_interface_(itf) {
        }
};
typedef boost::shared_ptr<PhysicalInterfaceEntry> PhysicalInterfaceEntryPtr;
typedef std::map<const Interface*, PhysicalInterfaceEntryPtr> PhyInterfaceMap;
typedef std::pair<const Interface*, PhysicalInterfaceEntryPtr> PhyInterfacePair;

//The class that defines data-structures to store PhysicalDevice information
//required for sending Prouter UVE.
class ProuterUveEntryBase {
public:
    ProuterUveEntryBase(const PhysicalDevice *p);
    ~ProuterUveEntryBase();
    void AddLogicalInterface(const LogicalInterface *intf);
    void DeleteLogicalInterface(const LogicalInterface *intf, const Interface *pintf);
    void AddPhysicalInterface(const Interface *intf);
    void DeletePhysicalInterface(const Interface *intf);
    void FrameProuterMsg(ProuterData *uve);
    void Clear();

    const PhysicalDevice *prouter() const { return prouter_; }
private:
    const PhysicalDevice *prouter_;
    PhyInterfaceMap physical_interface_map_;
    DISALLOW_COPY_AND_ASSIGN(ProuterUveEntryBase);
};

#endif // vnsw_agent_prouter_uve_entry_base_h
