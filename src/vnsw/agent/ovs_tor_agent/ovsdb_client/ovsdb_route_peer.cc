/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <ovs_tor_agent/ovsdb_client/ovsdb_route_peer.h>
#include <ovs_tor_agent/ovsdb_client/ovsdb_route_data.h>

#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>

OvsPeer::OvsPeer(const IpAddress &peer_ip, uint64_t gen_id,
                 OvsPeerManager *peer_manager) :
    Peer(Peer::OVS_PEER, "OVS-" + peer_ip.to_string(), true), peer_ip_(peer_ip),
    gen_id_(gen_id), peer_manager_(peer_manager) {
}

OvsPeer::~OvsPeer() {
}

bool OvsPeer::Compare(const Peer *rhs) const {
    const OvsPeer *rhs_peer = static_cast<const OvsPeer *>(rhs);
    if (gen_id_ != rhs_peer->gen_id_)
        return gen_id_ < rhs_peer->gen_id_;

    return peer_ip_ < rhs_peer->peer_ip_;
}

bool OvsPeer::AddOvsRoute(const boost::uuids::uuid &vn_uuid,
                          const MacAddress &mac, Ip4Address &tor_ip) {

    Agent *agent = peer_manager_->agent();

    // We dont learn the MAC to IP binding in case of TOR. Populate 0.0.0.0
    // (unknown ip) for the MAC in EVPN route
    Ip4Address prefix_ip = Ip4Address::from_string("0.0.0.0");
    int prefix_len = 32;

    VnEntry *vn = agent->vn_table()->Find(vn_uuid);
    if (vn == NULL)
        return false;

    VrfEntry *vrf = vn->GetVrf();
    if (vrf == NULL)
        return false;

    OvsdbRouteData *data = new OvsdbRouteData(this, vn->vxlan_id()->vxlan_id(),
                                              tor_ip, agent->router_id(),
                                              agent->fabric_vrf_name(),
                                              vn->GetName());
    Layer2AgentRouteTable *table = static_cast<Layer2AgentRouteTable *>
        (vrf->GetLayer2RouteTable());
    table->AddRemoteVmRouteReq(this, vrf->GetName(), mac, prefix_ip,
                               vn->vxlan_id()->vxlan_id(), prefix_len, data);
    return true;
}

bool OvsPeer::DeleteOvsRoute(const VnEntry *vn, const MacAddress &mac) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf == NULL)
        return false;

    Layer2AgentRouteTable *table = static_cast<Layer2AgentRouteTable *>
        (vrf->GetLayer2RouteTable());
    table->DeleteReq(this, vrf->GetName(), mac, vn->vxlan_id()->vxlan_id(),
                     new OvsdbRouteData(this));
    return true;
}

const Ip4Address *OvsPeer::NexthopIp(Agent *agent,
                                     const AgentPath *path) const {
    const TunnelNH *nh = dynamic_cast<const TunnelNH *>(path->nexthop(agent));
    if (nh == NULL)
        return agent->router_ip_ptr();
    return nh->GetDip();
}

OvsPeerManager::OvsPeerManager(Agent *agent) : gen_id_(0), agent_(agent) {
}

OvsPeerManager::~OvsPeerManager() {
    assert(table_.size() == 0);
}

OvsPeer *OvsPeerManager::Allocate(const IpAddress &peer_ip) {
    OvsPeer *peer = new OvsPeer(peer_ip, gen_id_++, this);
    table_.insert(peer);
    return peer;
}

void OvsPeerManager::Free(OvsPeer *peer) {
    table_.erase(peer);
    delete peer;
}

Agent *OvsPeerManager::agent() const {
    return agent_;
}

uint32_t OvsPeerManager::Size() const {
    return table_.size();
}
