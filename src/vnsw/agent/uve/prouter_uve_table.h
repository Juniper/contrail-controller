/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_PROUTER_UVE_TABLE_H_
#define _ROOT_PROUTER_UVE_TABLE_H_

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/uuid/uuid_io.hpp>
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
    typedef std::set<std::string> InterfaceSet;
    struct LogicalInterfaceUveEntry {
        const std::string name_;
        uint16_t vlan_;
        InterfaceSet vmi_list_;
        bool changed_;
        bool deleted_;
        bool renewed_;

        explicit LogicalInterfaceUveEntry(const LogicalInterface *li);
        void Update(const LogicalInterface *li);
        void FillVmInterfaceList(std::vector<std::string> &vmi_list) const;
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
        boost::uuids::uuid physical_device_;
        std::string physical_interface_;
        ProuterInterfaceState() : physical_device_(boost::uuids::nil_uuid()),
            physical_interface_() {
        }
    };

    struct VmInterfaceState : public DBState {
        boost::uuids::uuid logical_interface_;
        VmInterfaceState() : logical_interface_(boost::uuids::nil_uuid()) {
        }
    };

    typedef std::set<boost::uuids::uuid> LogicalInterfaceSet;
    struct ProuterUveEntry {
        explicit  ProuterUveEntry(const PhysicalDevice *p);
        ~ProuterUveEntry();
        void AddPhysicalInterface(const Interface *itf);
        void DeletePhysicalInterface(const Interface *itf);
        void AddLogicalInterface(const LogicalInterface *itf);
        bool DeleteLogicalInterface(const LogicalInterface *itf);
        void Reset();

        std::string name_;
        boost::uuids::uuid uuid_;
        InterfaceSet physical_interface_set_;
        LogicalInterfaceSet logical_interface_set_;
        bool changed_;
        bool deleted_;
        bool renewed_;
        bool mastership_;
    };
    typedef boost::shared_ptr<ProuterUveEntry> ProuterUveEntryPtr;
    typedef std::map<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterMap;
    typedef std::pair<boost::uuids::uuid, ProuterUveEntryPtr> UveProuterPair;

    struct PhyInterfaceUveEntry {
        explicit PhyInterfaceUveEntry(const Interface *pintf);
        void Update(const Interface *pintf);
        void FillLogicalInterfaceList(std::vector<std::string> &list) const;

        boost::uuids::uuid uuid_;
        LogicalInterfaceSet logical_interface_set_;
        bool changed_;
        bool deleted_;
        bool renewed_;
    };
    typedef boost::shared_ptr<PhyInterfaceUveEntry> PhyInterfaceUveEntryPtr;
    typedef std::map<std::string, PhyInterfaceUveEntryPtr>
        UvePhyInterfaceMap;
    typedef std::pair<std::string, PhyInterfaceUveEntryPtr>
        UvePhyInterfacePair;

    static const uint16_t kInvalidVlanId = 0xFFFF;
    explicit ProuterUveTable(Agent *agent, uint32_t default_intvl);
    virtual ~ProuterUveTable();
    void RegisterDBClients();
    void Shutdown(void);
    virtual void DispatchProuterMsg(const ProuterData &uve);
    virtual void DispatchLogicalInterfaceMsg(const UveLogicalInterfaceAgent &u);
    virtual void DispatchPhysicalInterfaceMsg
        (const UvePhysicalInterfaceAgent &uve);
    bool TimerExpiry();
    bool PITimerExpiry();
    bool LITimerExpiry();
    void UpdateMastership(const boost::uuids::uuid &u, bool value);

 protected:
    UveProuterMap uve_prouter_map_;
    UvePhyInterfaceMap uve_phy_interface_map_;
    LogicalInterfaceMap uve_logical_interface_map_;

 private:
    ProuterUveEntryPtr Allocate(const PhysicalDevice *pr);
    ProuterUveEntry *PDEntryToProuterUveEntry(const boost::uuids::uuid &u) const;
    PhyInterfaceUveEntry *NameToPhyInterfaceUveEntry(const std::string &name)
        const;
    const Interface *NameToInterface(const std::string &name) const;
    void FrameProuterMsg(ProuterUveEntry *entry, ProuterData *uve) const;
    void SendProuterDeleteMsg(ProuterUveEntry *e);
    bool SendProuterMsg(ProuterUveEntry *entry);
    void SendProuterMsgFromPhyInterface(const Interface *pi);
    void PhysicalDeviceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void PhysicalInterfaceHandler(const Interface *i, const boost::uuids::uuid &u);
    void MarkDeletedPhysical(const Interface *pintf);
    void DeletePhysicalFromProuter(const Interface *i,
                                   const boost::uuids::uuid &u);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    ProuterUveEntry* AddHandler(const PhysicalDevice *p);
    void DeleteHandler(const PhysicalDevice *p);
    void AddLogicalToPhysical(const Interface *p, const LogicalInterface *i);
    void AddUpdateLogicalInterface(const LogicalInterface *i);
    void MarkDeletedLogical(const LogicalInterface *pintf);
    void DeleteLogicalFromPhysical(const std::string &name,
                                   const LogicalInterface *i);
    void AddProuterLogicalInterface(const PhysicalDevice *p,
                                    const LogicalInterface *intf);
    void DeleteProuterLogicalInterface(const boost::uuids::uuid &u,
                                       const LogicalInterface *intf);
    const PhysicalDevice *InterfaceToProuter(const Interface *intf);
    void SendProuterVrouterAssociation();
    void set_expiry_time(int time, Timer *timer);
    void TimerCleanup(Timer *timer);
    void VmInterfaceHandler(DBTablePartBase *partition, DBEntryBase *e);
    void VMInterfaceAdd(const VmInterface *vmi);
    void VMInterfaceRemove(const boost::uuids::uuid &li,
                                      const VmInterface *vmi);
    void MarkPhysicalDeviceChanged(const PhysicalDevice *pde);
    void MarkChanged(const boost::uuids::uuid &li);
    void AddUpdatePhysicalInterface(const Interface *intf);
    void SendPhysicalInterfaceDeleteMsg(const std::string &cfg_name);
    void SendPhysicalInterfaceMsg(const std::string &name,
                                  PhyInterfaceUveEntry *entry);
    void SendLogicalInterfaceDeleteMsg(const std::string &config_name);
    void SendLogicalInterfaceMsg(const boost::uuids::uuid &u,
                                 LogicalInterfaceUveEntry *entry);

    Agent *agent_;
    DBTableBase::ListenerId physical_device_listener_id_;
    DBTableBase::ListenerId interface_listener_id_;
    // Last visited Prouter by timer
    boost::uuids::uuid pr_timer_last_visited_;
    Timer *pr_timer_;

    std::string pi_timer_last_visited_;
    Timer *pi_timer_;

    boost::uuids::uuid li_timer_last_visited_;
    Timer *li_timer_;
    DISALLOW_COPY_AND_ASSIGN(ProuterUveTable);
};

#endif  // _ROOT_PROUTER_UVE_TABLE_H_
