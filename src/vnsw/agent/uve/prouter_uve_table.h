/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PROUTER_UVE_TABLE_H_
#define _ROOT_PROUTER_UVE_TABLE_H_

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
class ProuterUveTable {
 public:
    struct PhyInterfaceAttrEntry {
        boost::uuids::uuid uuid_;
        explicit PhyInterfaceAttrEntry(const Interface *itf);
    };
    typedef boost::shared_ptr<PhyInterfaceAttrEntry> PhyInterfaceAttrEntryPtr;
    typedef std::map<std::string, PhyInterfaceAttrEntryPtr>
        UvePhyInterfaceAttrMap;
    typedef std::pair<std::string, PhyInterfaceAttrEntryPtr>
        UvePhyInterfaceAttrPair;

    struct LogicalInterfaceUveEntry {
        const std::string name_;
        uint16_t vlan_;
        boost::uuids::uuid vmi_uuid_;

        explicit LogicalInterfaceUveEntry(const LogicalInterface *li);
        void Update(const LogicalInterface *li);
    };
    typedef boost::shared_ptr<LogicalInterfaceUveEntry>
        LogicalInterfaceUveEntryPtr;
    typedef std::map<boost::uuids::uuid, LogicalInterfaceUveEntryPtr>
        LogicalInterfaceMap;
    typedef std::pair<boost::uuids::uuid, LogicalInterfaceUveEntryPtr>
        LogicalInterfacePair;

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
        explicit  ProuterUveEntry(const PhysicalDevice *p);
        ~ProuterUveEntry();
        void AddPhysicalInterface(const Interface *itf);
        void DeletePhysicalInterface(const Interface *itf);
        void AddLogicalInterface(const LogicalInterface *itf);
        void UpdateLogicalInterface(const LogicalInterface *itf);
        bool DeleteLogicalInterface(const LogicalInterface *itf);

        std::string name_;
        boost::uuids::uuid uuid_;
        UvePhyInterfaceAttrMap physical_interface_set_;
        LogicalInterfaceMap logical_interface_set_;
        TaskTrigger *encoder_task_trigger_;
        bool prouter_msg_enqueued_;
        bool deleted_;
    };
    typedef boost::shared_ptr<ProuterUveEntry> ProuterUveEntryPtr;
    typedef std::map<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterMap;
    typedef std::pair<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterPair;

    struct PhyInterfaceUveEntry {
        LogicalInterfaceMap logical_interface_set_;
    };
    typedef boost::shared_ptr<PhyInterfaceUveEntry> PhyInterfaceUveEntryPtr;
    typedef std::map<std::string, PhyInterfaceUveEntryPtr>
        UvePhyInterfaceMap;
    typedef std::pair<std::string, PhyInterfaceUveEntryPtr>
        UvePhyInterfacePair;
    static const uint16_t kInvalidVlanId = 0xFFFF;
    explicit ProuterUveTable(Agent *agent);
    virtual ~ProuterUveTable();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchProuterMsg(const ProuterData &uve);

 protected:
    UveProuterMap uve_prouter_map_;
    UvePhyInterfaceMap uve_phy_interface_map_;

 private:
    ProuterUveEntryPtr Allocate(const PhysicalDevice *pr);
    ProuterUveEntry *PDEntryToProuterUveEntry(const PhysicalDevice *p) const;
    void RemoveUveEntry(const boost::uuids::uuid &u);
    PhyInterfaceUveEntry *NameToPhyInterfaceUveEntry(const std::string &name)
        const;
    void FillLogicalInterfaceList(const LogicalInterfaceMap &in,
                           std::vector<UveLogicalInterfaceData> *out) const;
    void FrameProuterMsg(ProuterUveEntry *entry, ProuterData *uve) const;
    void SendProuterDeleteMsg(ProuterUveEntry *e);
    bool SendProuterMsg(const PhysicalDevice *p, ProuterUveEntry *entry);
    void SendProuterMsgFromPhyInterface(const Interface *pi);
    void PhysicalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void PhysicalInterfaceHandler(const Interface *i, const PhysicalDevice *p);
    void DisassociatePhysicalInterface(const Interface *i,
                                       const PhysicalDevice *p);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    ProuterUveEntry* AddHandler(const PhysicalDevice *p);
    void DeleteHandler(const PhysicalDevice *p);
    void AddLogicalInterface(const Interface *p, const LogicalInterface *i);
    void UpdateLogicalInterface(const Interface *p, const LogicalInterface *i);
    void DeleteLogicalInterface(const Interface *p, const LogicalInterface *i);
    void AddProuterLogicalInterface(const PhysicalDevice *p,
                                    const LogicalInterface *intf);
    void UpdateProuterLogicalInterface(const PhysicalDevice *p,
                                       const LogicalInterface *intf);
    void DeleteProuterLogicalInterface(const PhysicalDevice *p,
                                       const LogicalInterface *intf);
    const PhysicalDevice *InterfaceToProuter(const Interface *intf);
    void SendProuterVrouterAssociation();
    void EnqueueProuterMsg(const PhysicalDevice *p);

    Agent *agent_;
    DBTableBase::ListenerId physical_device_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(ProuterUveTable);
};

#endif  // _ROOT_PROUTER_UVE_TABLE_H_
