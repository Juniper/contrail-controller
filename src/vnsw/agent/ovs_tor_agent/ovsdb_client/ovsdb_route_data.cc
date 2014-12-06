/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>

#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/agent_sandesh.h>

#include <ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_route_data.h>

using namespace std;
using namespace boost::asio;

OvsdbRouteData::OvsdbRouteData(const Peer *peer, uint32_t vxlan_id,
                               const Ip4Address &tor_ip,
                               const Ip4Address &router_id,
                               const std::string &tor_vrf,
                               const std::string &dest_vn_name) :
    AgentRouteData(false), peer_(peer), vxlan_id_(vxlan_id), tor_ip_(tor_ip),
    tor_vrf_(tor_vrf), router_id_(router_id), dest_vn_name_(dest_vn_name) {
}

OvsdbRouteData::OvsdbRouteData(const Peer *peer) :
    AgentRouteData(false), peer_(peer), vxlan_id_() {
}

OvsdbRouteData::~OvsdbRouteData() {
}

std::string OvsdbRouteData::ToString() const {
    return "OVS Route Data";
}

bool OvsdbRouteData::AddChangePath(Agent *agent, AgentPath *path,
                                   const AgentRoute *data) {
    bool ret = false;
    NextHop *nh = NULL;

    if (path->vxlan_id() != vxlan_id_) {
        path->set_vxlan_id(vxlan_id_);
        ret = true;
    }

    if (path->dest_vn_name() != dest_vn_name_) {
        path->set_dest_vn_name(dest_vn_name_);
        ret = true;
    }

    SecurityGroupList sg_list;
    if (path->sg_list() != sg_list) {
        path->set_sg_list(sg_list);
        ret = true;
    }

    // Create Tunnel-NH first
    TunnelType::Type type = TunnelType::ComputeType(TunnelType::VxlanType());
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(tor_vrf_, router_id_, tor_ip_, false,
                                      type));
    nh_req.data.reset(new TunnelNHData());
    agent->nexthop_table()->Process(nh_req);

    // Get the NH
    TunnelNHKey key(tor_vrf_, router_id_, tor_ip_, false, type);
    nh = static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));

    path->set_tunnel_bmap(TunnelType::VxlanType());
    path->set_tunnel_type(TunnelType::ComputeType(path->tunnel_bmap()));

    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}
