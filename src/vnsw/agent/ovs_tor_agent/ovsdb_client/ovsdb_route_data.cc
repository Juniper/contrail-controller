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
                               const std::string &dest_vn_name,
                               const SecurityGroupList &sg_list,
                               bool ha_stale_export, uint32_t sequence) :
    AgentRouteData(false), peer_(peer), vxlan_id_(vxlan_id), tor_ip_(tor_ip),
    tor_vrf_(tor_vrf), router_id_(router_id), dest_vn_name_(dest_vn_name),
    sg_list_(sg_list), ha_stale_export_(ha_stale_export), sequence_(sequence) {
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

    if (!path->dest_vn_match(dest_vn_name_)) {
        VnListType vn_list;
        vn_list.insert(dest_vn_name_);
        path->set_dest_vn_list(vn_list);
        ret = true;
    }

    if (path->sg_list() != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    PathPreference::Preference pref = PathPreference::LOW;
    // if it is a ha stale export check for path preference to be HA_STALE
    if (ha_stale_export_) {
        pref = PathPreference::HA_STALE;
    }
    PathPreference path_preference(sequence_, pref, false, false);
    if (path->path_preference() != path_preference) {
        path->set_path_preference(path_preference);
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

OvsdbRouteResyncData::OvsdbRouteResyncData(const SecurityGroupList &sg_list) :
    AgentRouteData(false), sg_list_(sg_list) {
}

OvsdbRouteResyncData::~OvsdbRouteResyncData() {
}

std::string OvsdbRouteResyncData::ToString() const {
    return "OVS Route Resync";
}

bool OvsdbRouteResyncData::AddChangePath(Agent *agent, AgentPath *path,
                                         const AgentRoute *data) {
    bool ret = false;
    if (path->sg_list() != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    return ret;
}
