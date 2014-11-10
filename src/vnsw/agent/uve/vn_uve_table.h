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
#include <uve/vn_uve_table_base.h>

//The container class for objects representing VirtualNetwork UVEs
//Defines routines for storing and managing (add, delete, change and send)
//VirtualNetwork UVEs
class VnUveTable : public VnUveTableBase {
public:
    VnUveTable(Agent *agent);
    virtual ~VnUveTable();

    void UpdateBitmap(const std::string &vn, uint8_t proto, uint16_t sport,
                      uint16_t dport);
    void SendVnStats(bool only_vrf_stats);
    void UpdateInterVnStats(const FlowEntry *e, uint64_t bytes, uint64_t pkts);

protected:
    //The following API is made protected for UT.
    void SendVnStatsMsg(const VnEntry *vn, bool only_vrf_stats);
private:
    virtual VnUveEntryPtr Allocate(const VnEntry *vn);
    virtual VnUveEntryPtr Allocate();
    virtual void Delete(const VnEntry *vn);
    bool SendUnresolvedVnMsg(const std::string &vn, UveVirtualNetworkAgent &u);
    void VnStatsUpdateInternal(const std::string &src, const std::string &dst,
                               uint64_t bytes, uint64_t pkts, bool outgoing);
    void RemoveInterVnStats(const std::string &vn);

    DBTableBase::ListenerId vn_listener_id_;
    DBTableBase::ListenerId intf_listener_id_;
    DISALLOW_COPY_AND_ASSIGN(VnUveTable);
};

#endif // vnsw_agent_vn_uve_table_h
