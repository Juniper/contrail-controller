/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_table_h
#define vnsw_agent_vn_uve_table_h

#include <string>
#include <set>
#include <map>
#include <vector>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh.h>
#include <virtual_network_types.h>
#include <oper/vn.h>
#include "pkt/flow_proto.h"
#include "pkt/flow_table.h"
#include <tbb/mutex.h>
#include <uve/l4_port_bitmap.h>
#include <uve/vn_uve_entry.h>

//The container class for objects representing VirtualNetwork UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualNetwork UVEs
class VnUveTable {
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

    typedef boost::shared_ptr<VnUveEntry> VnUveEntryPtr;
    typedef std::map<std::string, VnUveEntryPtr> UveVnMap;
    typedef std::pair<std::string, VnUveEntryPtr> UveVnPair;
    VnUveTable(Agent *agent);
    virtual ~VnUveTable();

    void UpdateBitmap(const std::string &vn, uint8_t proto, uint16_t sport, 
                      uint16_t dport);
    void SendVnStats(bool only_vrf_stats);
    void UpdateInterVnStats(const FlowEntry *e, uint64_t bytes, uint64_t pkts);
    void RegisterDBClients();
    void Shutdown(void);

protected:
    UveVnMap uve_vn_map_;
    Agent *agent_;
private:
    VnUveEntry* Add(const VnEntry *vn);
    void Add(const std::string &vn);
    void Delete(const VnEntry *vn);
    virtual VnUveEntryPtr Allocate(const VnEntry *vn);
    virtual VnUveEntryPtr Allocate();
    virtual void DispatchVnMsg(const UveVirtualNetworkAgent &uve);
    void VnNotify(DBTablePartBase *partition, DBEntryBase *e);
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e);
    void SendDeleteVnMsg(const std::string &vn);
    void SendVnMsg(const VnEntry *vn);
    void SendVnStatsMsg(const VnEntry *vn, bool only_vrf_stats);
    void InterfaceDeleteHandler(const std::string &vm, const std::string &vn, 
                                const Interface* intf);
    void InterfaceAddHandler(const VmEntry *vm, const VnEntry *vn, 
                             const Interface* intf);
    bool SendUnresolvedVnMsg(const std::string &vn, UveVirtualNetworkAgent &u);
    void VnStatsUpdateInternal(const std::string &src, const std::string &dst,
                               uint64_t bytes, uint64_t pkts, bool outgoing);
    void RemoveInterVnStats(const std::string &vn);
    VnUveEntry* UveEntryFromVn(const VnEntry *vn);

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VnUveTable);
};

#endif // vnsw_agent_vn_uve_table_h
