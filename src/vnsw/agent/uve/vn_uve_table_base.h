/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_table_base_h
#define vnsw_agent_vn_uve_table_base_h

#include <string>
#include <set>
#include <map>
#include <vector>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_network_types.h>
#include <oper/vn.h>
#include <uve/vn_uve_entry_base.h>

//The container class for objects representing VirtualNetwork UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualNetwork UVEs
class VnUveTableBase {
public:
    struct VnUveInterfaceState :public DBState {
        VnUveInterfaceState(const std::string &vm, const std::string &vn,
                            bool ipv4_active, bool l2_active)
            : vm_name_(vm), vn_name_(vn), ipv4_active_(ipv4_active),
              l2_active_(l2_active) {
            }
        std::string vm_name_;
        std::string vn_name_;
        bool ipv4_active_;
        bool l2_active_;
    };

    typedef boost::shared_ptr<VnUveEntryBase> VnUveEntryPtr;
    typedef std::map<std::string, VnUveEntryPtr> UveVnMap;
    typedef std::pair<std::string, VnUveEntryPtr> UveVnPair;
    VnUveTableBase(Agent *agent);
    virtual ~VnUveTableBase();

    void RegisterDBClients();
    void Shutdown(void);

protected:
    void SendVnStatsMsg(const VnEntry *vn, bool only_vrf_stats);
    virtual void Delete(const VnEntry *vn);
    VnUveEntryBase* UveEntryFromVn(const VnEntry *vn);
    //The following API is made protected for UT.
    virtual void DispatchVnMsg(const UveVirtualNetworkAgent &uve);

    UveVnMap uve_vn_map_;
    Agent *agent_;
private:
    VnUveEntryBase* Add(const VnEntry *vn);
    void Add(const std::string &vn);
    virtual VnUveEntryPtr Allocate(const VnEntry *vn);
    virtual VnUveEntryPtr Allocate();
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void SendDeleteVnMsg(const std::string &vn);
    void SendVnMsg(const VnEntry *vn);
    void InterfaceDeleteHandler(const std::string &vm, const std::string &vn,
                                const Interface* intf);
    void InterfaceAddHandler(const VnEntry* vn, const Interface* intf,
                             const std::string &vm_name,
                             VnUveInterfaceState *state);

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VnUveTableBase);
};

#endif // vnsw_agent_vn_uve_table_base_h
