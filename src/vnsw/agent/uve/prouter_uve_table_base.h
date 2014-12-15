/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_prouter_uve_table_base_h
#define vnsw_agent_prouter_uve_table_base_h

#include <string>
#include <vector>
#include <set>
#include <map>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <prouter_types.h>
#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <cmn/index_vector.h>

//The container class for objects representing Prouter UVEs
//Defines routines for storing and managing (add, delete, change and send)
//Prouter UVEs
class ProuterUveTableBase {
public:
    typedef std::set<const Interface *> InterfaceSet;
    typedef std::set<const LogicalInterface *> LogicalInterfaceSet;

    struct PhysicalDeviceState : public DBState {
    };

    struct ProuterInterfaceState : public DBState {
        const PhysicalDevice *physical_device_;
        const Interface *physical_interface_;
        ProuterInterfaceState() : physical_device_(NULL),
            physical_interface_(NULL) {
        }
    };

    struct ProuterUveEntry {
        const PhysicalDevice *prouter_;
        InterfaceSet physical_interface_set_;
        ProuterUveEntry(const PhysicalDevice *p) : prouter_(p),
            physical_interface_set_() {
        }
        void AddPhysicalInterface(const Interface *itf);
        void DeletePhysicalInterface(const Interface *itf);
    };
    typedef boost::shared_ptr<ProuterUveEntry> ProuterUveEntryPtr;
    typedef std::map<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterMap;
    typedef std::pair<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterPair;

    struct PhyInterfaceUveEntry {
        const Interface *physical_interface_;
        LogicalInterfaceSet logical_interface_set_;
    };
    typedef boost::shared_ptr<PhyInterfaceUveEntry> PhyInterfaceUveEntryPtr;
    typedef std::map<const Interface*, PhyInterfaceUveEntryPtr>
        UvePhyInterfaceMap;
    typedef std::pair<const Interface*, PhyInterfaceUveEntryPtr>
        UvePhyInterfacePair;

    ProuterUveTableBase(Agent *agent);
    virtual ~ProuterUveTableBase();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchProuterMsg(const ProuterData &uve);
protected:
    UveProuterMap uve_prouter_map_;
    UvePhyInterfaceMap uve_phy_interface_map_;

private:
    ProuterUveEntryPtr Allocate(const PhysicalDevice *pr);
    ProuterUveEntry *PDEntryToProuterUveEntry(const PhysicalDevice *p);
    PhyInterfaceUveEntry *InterfaceToPhyInterfaceUveEntry(const Interface *p);
    void FrameProuterMsg(const PhysicalDevice *p, ProuterData *uve);
    void SendProuterDeleteMsg(ProuterUveEntry *e);
    void SendProuterMsg(const PhysicalDevice *p);
    void PhysicalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void PhysicalInterfaceHandler(const Interface *intf, const PhysicalDevice *pde);
    void LogicalInterfaceHandler(const LogicalInterface *i, const Interface *p);
    void DisassociatePhysicalInterface(const Interface *i, const PhysicalDevice *p);
    void DisassociateLogicalInterface(const LogicalInterface *intf,
                                      const Interface *pintf);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    ProuterUveEntry* Add(const PhysicalDevice *p);
    void Delete(const PhysicalDevice *p);
    void AddLogicalInterface(const Interface *p, const LogicalInterface *i);
    void DeleteLogicalInterface(const Interface *p, const LogicalInterface *i);
    const PhysicalDevice *InterfaceToProuter(const Interface *intf);
    void SendProuterVrouterAssociation();

    Agent *agent_;
    DBTableBase::ListenerId physical_device_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(ProuterUveTableBase);
};

#endif // vnsw_agent_prouter_uve_table_base_h
