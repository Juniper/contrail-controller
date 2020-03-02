/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_route_path.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

/////////////////////////////////////////////////////////////////////////////
// Utility functions
/////////////////////////////////////////////////////////////////////////////
static void BridgeTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_l2_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void BridgeTableProcess(Agent *agent, const string &vrf_name,
                               DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetBridgeRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

/////////////////////////////////////////////////////////////////////////////
// BridgeRouteKey methods
/////////////////////////////////////////////////////////////////////////////
string BridgeRouteKey::ToString() const {
    return dmac_.ToString();
}

BridgeRouteKey *BridgeRouteKey::Clone() const {
    return new BridgeRouteKey(peer(), vrf_name_, dmac_);
}

AgentRoute *
BridgeRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    BridgeRouteEntry *entry = new BridgeRouteEntry(vrf, dmac_,
                                                   peer()->GetType(),
                                                   is_multicast);
    return static_cast<AgentRoute *>(entry);
}

/////////////////////////////////////////////////////////////////////////////
// BridgeAgentRouteTable methods
/////////////////////////////////////////////////////////////////////////////
DBTableBase *BridgeAgentRouteTable::CreateTable(DB *db,
                                                const std::string &name) {
    AgentRouteTable *table = new BridgeAgentRouteTable(db, name);
    table->Init();
    return table;
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRoute(const MacAddress &mac) {
    BridgeRouteEntry entry(vrf_entry(), mac, Peer::LOCAL_PEER, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntry(&entry));
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRouteNoLock(const MacAddress &mac){
    BridgeRouteEntry entry(vrf_entry(), mac, Peer::LOCAL_PEER, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntryNoLock(&entry));
}

BridgeRouteEntry *BridgeAgentRouteTable::FindRoute(const MacAddress &mac,
                                                   Peer::Type peer) {
    BridgeRouteEntry entry(vrf_entry(), mac, peer, false);
    return static_cast<BridgeRouteEntry *>(FindActiveEntry(&entry));
}

/////////////////////////////////////////////////////////////////////////////
// BridgeAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void BridgeAgentRouteTable::AddBridgeReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  const MacAddress &mac,
                                                  const string &vn_name,
                                                  const string &interface,
                                                  bool policy) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));

    PacketInterfaceKey intf_key(boost::uuids::nil_uuid(),
                                agent->pkt_interface_name());
    req.data.reset(new HostRoute(intf_key, vn_name));

    BridgeTableEnqueue(agent, &req);
}

void BridgeAgentRouteTable::AddBridgeReceiveRoute(const Peer *peer,
                                                  const string &vrf_name,
                                                  uint32_t vxlan_id,
                                                  const MacAddress &mac,
                                                  const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, vxlan_id));
    req.data.reset(new L2ReceiveRoute(vn_name, vxlan_id, 0, PathPreference(),
                                      peer->sequence_number()));
    Process(req);
}

void BridgeAgentRouteTable::AddBridgeRoute(const AgentRoute *rt) {
    const EvpnRouteEntry *evpn_rt =
        static_cast<const EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnDerivedPathData(evpn_rt));
    BridgeTableProcess(agent(), vrf_name(), req);
}


void BridgeAgentRouteTable::AddMacVmBindingRoute(const Peer *peer,
                                                 const std::string &vrf_name,
                                                 const MacAddress &mac,
                                                 const VmInterface *vm_intf,
                                                 bool flood_dhcp) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));
    req.data.reset(new MacVmBindingPathData(vm_intf, flood_dhcp));
    BridgeTableProcess(agent(), vrf_name, req);
}

void BridgeAgentRouteTable::DeleteMacVmBindingRoute(const Peer *peer,
                                                    const std::string &vrf_name,
                                                    const MacAddress &mac,
                                                    const VmInterface *vm_intf) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, 0));
    req.data.reset(new MacVmBindingPathData(vm_intf, false));
    BridgeTableProcess(agent(), vrf_name, req);
}

void BridgeAgentRouteTable::DeleteBridgeRoute(const AgentRoute *rt) {
    const EvpnRouteEntry *evpn_rt =
        static_cast<const EvpnRouteEntry *>(rt);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(agent()->evpn_peer(),
                                     evpn_rt->vrf()->GetName(),
                                     evpn_rt->mac(), 0));
    req.data.reset(new EvpnDerivedPathData(evpn_rt));
    BridgeTableProcess(Agent::GetInstance(), evpn_rt->vrf()->GetName(), req);
}

void BridgeAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(NULL);
    BridgeTableProcess(Agent::GetInstance(), vrf_name, req);
}

AgentRouteData *BridgeAgentRouteTable::BuildNonBgpPeerData(const string &vrf_name,
                                                           const std::string &vn_name,
                                                           uint32_t label,
                                                           int vxlan_id,
                                                           uint32_t tunnel_type,
                                                           Composite::Type type,
                                                           ComponentNHKeyList
                                                           &component_nh_key_list,
                                                           bool pbb_nh,
                                                           bool learning_enabled) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false, component_nh_key_list,
                                        vrf_name));
    nh_req.data.reset(new CompositeNHData(pbb_nh, learning_enabled, false));
    return (new MulticastRoute(vn_name, label,
                               vxlan_id, tunnel_type,
                               nh_req, type, 0));
}

AgentRouteData *BridgeAgentRouteTable::BuildBgpPeerData(const Peer *peer,
                                                        const string &vrf_name,
                                                        const std::string &vn_name,
                                                        uint32_t label,
                                                        int vxlan_id,
                                                        uint32_t ethernet_tag,
                                                        uint32_t tunnel_type,
                                                        Composite::Type type,
                                                        ComponentNHKeyList
                                                        &component_nh_key_list,
                                                        bool pbb_nh,
                                                        bool learning_enabled) {
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    assert(bgp_peer != NULL);
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(type, false, component_nh_key_list,
                                        vrf_name));
    nh_req.data.reset(new CompositeNHData(pbb_nh, learning_enabled, false));
    return (new MulticastRoute(vn_name, label, ethernet_tag, tunnel_type,
                               nh_req, type, bgp_peer->sequence_number()));
}

void BridgeAgentRouteTable::AddBridgeRoute(const Peer *peer,
                                            const string &vrf_name,
                                            const MacAddress &mac,
                                            uint32_t ethernet_tag,
                                            AgentRouteData *data) {

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ethernet_tag));
    req.data.reset(data);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::DeleteBridgeRoute(const Peer *peer,
                                            const string &vrf_name,
                                            const MacAddress &mac,
                                            uint32_t ethernet_tag,
                                            COMPOSITETYPE type) {

    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name, mac, ethernet_tag));
    DBRequest nh_req;

    //For same BGP peer type comp type helps in identifying if its a delete
    //for TOR or EVPN path.
    //Only ethernet tag is required, rest are dummy.
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    if (bgp_peer) {
        req.data.reset(new MulticastRoute("", 0,
                                        ethernet_tag, TunnelType::AllType(),
                                        nh_req, type,
                                        bgp_peer->sequence_number()));
    } else {
        req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                        TunnelType::AllType(),
                                        nh_req, type, 0));
    }

    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::AddBridgeBroadcastRoute(const Peer *peer,
                                                    const string &vrf_name,
                                                    uint32_t ethernet_tag,
                                                    AgentRouteData *data) {

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    req.data.reset(data);
    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

void BridgeAgentRouteTable::DeleteBroadcastReq(const Peer *peer,
                                               const string &vrf_name,
                                               uint32_t ethernet_tag,
                                               COMPOSITETYPE type) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new BridgeRouteKey(peer, vrf_name,
                                     MacAddress::BroadcastMac(), ethernet_tag));
    DBRequest nh_req;
    //For same BGP peer type comp type helps in identifying if its a delete
    //for TOR or EVPN path.
    //Only ethernet tag is required, rest are dummy.
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    if (bgp_peer) {
        req.data.reset(new MulticastRoute("", 0,
                                     ethernet_tag, TunnelType::AllType(),
                                     nh_req, type,
                                     bgp_peer->sequence_number()));
    } else {
        req.data.reset(new MulticastRoute("", 0, ethernet_tag,
                                          TunnelType::AllType(),
                                          nh_req, type, 0));
    }

    BridgeTableEnqueue(Agent::GetInstance(), &req);
}

const VmInterface *BridgeAgentRouteTable::FindVmFromDhcpBinding
(const MacAddress &mac) {
    const BridgeRouteEntry *l2_rt = FindRoute(mac);
    if (l2_rt == NULL)
        return NULL;

    const MacVmBindingPath *dhcp_path = l2_rt->FindMacVmBindingPath();
    if (dhcp_path == NULL)
        return NULL;
    return dhcp_path->vm_interface();
}

/////////////////////////////////////////////////////////////////////////////
// BridgeRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
const std::string BridgeRouteEntry::GetAddressString() const {
    //For broadcast, xmpp message is sent with address as 255.255.255.255
    if (mac_ == MacAddress::BroadcastMac()) {
        return "255.255.255.255";
    }
    return ToString();
}

const std::string BridgeRouteEntry::GetSourceAddressString() const {
    if (mac_ == MacAddress::BroadcastMac()) {
        return "0.0.0.0";
    }
    return (MacAddress::kZeroMac).ToString();
}

string BridgeRouteEntry::ToString() const {
    return mac_.ToString();
}

int BridgeRouteEntry::CompareTo(const Route &rhs) const {
    const BridgeRouteEntry &a = static_cast<const BridgeRouteEntry &>(rhs);

    return mac_.CompareTo(a.mac_);
}

DBEntryBase::KeyPtr BridgeRouteEntry::GetDBRequestKey() const {
    BridgeRouteKey *key =
        new BridgeRouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_);
    return DBEntryBase::KeyPtr(key);
}

void BridgeRouteEntry::SetKey(const DBRequestKey *key) {
    const BridgeRouteKey *k = static_cast<const BridgeRouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
}

uint32_t BridgeRouteEntry::GetActiveLabel() const {
    uint32_t label = 0;

    if (is_multicast()) {
        if (TunnelType::ComputeType(TunnelType::AllType()) ==
            (1 << TunnelType::VXLAN)) {
            label = GetActivePath()->vxlan_id();
        } else {
            label = GetActivePath()->label();
        }
    } else {
        label = GetActivePath()->GetActiveLabel();
    }
    return label;
}

AgentPath *BridgeRouteEntry::FindPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    const Peer *peer = key->peer();
    if (is_multicast())
        return FindMulticastPathUsingKeyData(key, data);
    const EvpnPeer *evpn_peer = dynamic_cast<const EvpnPeer*>(peer);
    if (evpn_peer != NULL)
        return FindEvpnPathUsingKeyData(key, data);

    return FindPath(peer);
}

AgentPath *BridgeRouteEntry::FindMulticastPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    assert(is_multicast());

    Route::PathList::const_iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end();
         it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        //Handle multicast peer matching,
        //In case of BGP peer also match VXLAN id.
        if (path->peer()->GetType() != Peer::BGP_PEER)
            return const_cast<AgentPath *>(path);

        const MulticastRoute *multicast_data =
            dynamic_cast<const MulticastRoute *>(data);
        assert(multicast_data != NULL);
        if (multicast_data->vxlan_id() != path->vxlan_id())
            continue;

        //In multicast from same peer, TOR and EVPN comp can
        //come. These should not overlap and be installed as
        //different path.
        const CompositeNH *cnh = dynamic_cast<CompositeNH *>(path->nexthop());
        if ((cnh != NULL) &&
            (multicast_data->comp_nh_type() != cnh->composite_nh_type()))
            continue;

        return const_cast<AgentPath *>(path);
    }
    return NULL;
}

AgentPath *BridgeRouteEntry::FindEvpnPathUsingKeyData
(const AgentRouteKey *key, const AgentRouteData *data) const {
    const Peer *peer = key->peer();
    const EvpnPeer *evpn_peer = dynamic_cast<const EvpnPeer*>(peer);
    assert(evpn_peer != NULL);

    Route::PathList::const_iterator it;
    for (it = GetPathList().begin(); it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() != key->peer())
            continue;

        //Handle mac route added via evpn route.
        const EvpnDerivedPath *evpn_path =
            dynamic_cast<const EvpnDerivedPath *>(path);
        const EvpnDerivedPathData *evpn_data =
            dynamic_cast<const EvpnDerivedPathData *>(data);
        assert(evpn_path != NULL);
        assert(evpn_data != NULL);
        if (evpn_path->ethernet_tag() != evpn_data->ethernet_tag())
            continue;
        if (evpn_path->ip_addr() != evpn_data->ip_addr())
            continue;
        return const_cast<AgentPath *>(path);
    }
    return NULL;
}

const MacVmBindingPath *BridgeRouteEntry::FindMacVmBindingPath() const {
    Agent *agent = (static_cast<AgentRouteTable *> (get_table()))->agent();
    return dynamic_cast<MacVmBindingPath*>(FindPath(agent->mac_vm_binding_peer()));
}

bool BridgeRouteEntry::ReComputePathAdd(AgentPath *path) {
    if (is_multicast()) {
        //evaluate add of path
        return ReComputeMulticastPaths(path, false);
    }
    return false;
}

bool BridgeRouteEntry::ReComputePathDeletion(AgentPath *path) {
    if (is_multicast()) {
        //evaluate delete of path
        return ReComputeMulticastPaths(path, true);
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh related methods
/////////////////////////////////////////////////////////////////////////////
void BridgeRouteReq::HandleRequest() const {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand(new AgentBridgeRtSandesh(vrf, context(), "",
                                                  get_stale(), get_mac()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr BridgeAgentRouteTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr(new AgentBridgeRtSandesh(vrf_entry(), context, "",
                                                    false, args->GetString("mac")));
}

bool BridgeRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    BridgeRouteResp *resp = static_cast<BridgeRouteResp *>(sresp);
    RouteL2SandeshData data;
    data.set_mac(ToString());
    data.set_src_vrf(vrf()->GetName());

    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            if (is_multicast()) {
                pdata.set_vxlan_id(path->vxlan_id());
            }
            const EvpnDerivedPath *evpn_path = dynamic_cast<const EvpnDerivedPath *>(path);
            if (evpn_path) {
                pdata.set_info(evpn_path->parent());
            }
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteL2SandeshData> &list =
        const_cast<std::vector<RouteL2SandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

//Supporting deprecated layer2 requests
void Layer2RouteReq::HandleRequest() const {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand(new AgentLayer2RtSandesh(vrf, context(), "",
                                                  get_stale()));
    sand->DoSandesh(sand);
}
