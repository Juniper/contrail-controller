/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PROUTER_UVE_TABLE_BASE_H_
#define _ROOT_PROUTER_UVE_TABLE_BASE_H_

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <prouter_types.h>
#include <oper/interface_common.h>
#include <oper/physical_device.h>
#include <cmn/index_vector.h>
#include <string>
#include <vector>
#include <set>
#include <map>

// The container class for objects representing Prouter UVEs
// Defines routines for storing and managing (add, delete, change and send)
// Prouter UVEs
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
        LogicalInterfaceSet logical_interface_set_;
        explicit  ProuterUveEntry(const PhysicalDevice *p) : prouter_(p),
            physical_interface_set_() {
        }
        void AddPhysicalInterface(const Interface *itf);
        void DeletePhysicalInterface(const Interface *itf);
        void AddLogicalInterface(const LogicalInterface *itf);
        bool DeleteLogicalInterface(const LogicalInterface *itf);
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

    explicit ProuterUveTableBase(Agent *agent);
    virtual ~ProuterUveTableBase();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchProuterMsg(const ProuterData &uve);

    protected:
    UveProuterMap uve_prouter_map_;
    UvePhyInterfaceMap uve_phy_interface_map_;

    private:
    ProuterUveEntryPtr Allocate(const PhysicalDevice *pr);
    ProuterUveEntry *PDEntryToProuterUveEntry(const PhysicalDevice *p) const;
    PhyInterfaceUveEntry *InterfaceToPhyInterfaceUveEntry(const Interface *p)
        const;
    void FillLogicalInterfaceList(const LogicalInterfaceSet &in,
                           std::vector<UveLogicalInterfaceData> *out) const;
    void FrameProuterMsg(const PhysicalDevice *p, ProuterData *uve) const;
    void SendProuterDeleteMsg(ProuterUveEntry *e);
    void SendProuterMsg(const PhysicalDevice *p);
    void SendProuterMsgFromPhyInterface(const Interface *pi);
    void PhysicalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void PhysicalInterfaceHandler(const Interface *i, const PhysicalDevice *p);
    void DisassociatePhysicalInterface(const Interface *i,
                                       const PhysicalDevice *p);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    ProuterUveEntry* AddHandler(const PhysicalDevice *p);
    void DeleteHandler(const PhysicalDevice *p);
    void AddLogicalInterface(const Interface *p, const LogicalInterface *i);
    void DeleteLogicalInterface(const Interface *p, const LogicalInterface *i);
    void AddProuterLogicalInterface(const PhysicalDevice *p,
                                    const LogicalInterface *intf);
    void DeleteProuterLogicalInterface(const PhysicalDevice *p,
                                       const LogicalInterface *intf);
    const PhysicalDevice *InterfaceToProuter(const Interface *intf);
    void SendProuterVrouterAssociation();

    Agent *agent_;
    DBTableBase::ListenerId physical_device_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(ProuterUveTableBase);
};

#endif  // _ROOT_PROUTER_UVE_TABLE_BASE_H_
