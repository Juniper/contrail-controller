/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <oper/ecmp_load_balance.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <controller/controller_route_path.h>
#include <oper/agent_sandesh.h>
#include <pkt/pkt_init.h>
#include <pkt/pkt_handler.h>

using namespace std;
using namespace boost::asio;

/////////////////////////////////////////////////////////////////////////////
// Utility functions
/////////////////////////////////////////////////////////////////////////////
static void EvpnTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_evpn_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void EvpnTableProcess(Agent *agent, const string &vrf_name,
                               DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetEvpnRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

/////////////////////////////////////////////////////////////////////////////
// EvpnRouteKey methods
/////////////////////////////////////////////////////////////////////////////
string EvpnRouteKey::ToString() const {
    std::stringstream str;
    str << ethernet_tag_;
    str << "-";
    str << dmac_.ToString();
    str << "-";
    str << ip_addr_.to_string();
    return str.str();
}

EvpnRouteKey *EvpnRouteKey::Clone() const {
    return new EvpnRouteKey(peer(), vrf_name_, dmac_, ip_addr_,
                            plen_, ethernet_tag_);
}

AgentRoute *
EvpnRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    EvpnRouteEntry *entry = new EvpnRouteEntry(vrf, dmac_, ip_addr_, plen_,
                                               ethernet_tag_, is_multicast);
    return static_cast<AgentRoute *>(entry);
}

/////////////////////////////////////////////////////////////////////////////
// EvpnAgentRouteTable methods
/////////////////////////////////////////////////////////////////////////////
DBTableBase *EvpnAgentRouteTable::CreateTable(DB *db,
                                              const std::string &name) {
    AgentRouteTable *table = new EvpnAgentRouteTable(db, name);
    table->Init();
    return table;
}

EvpnRouteEntry *EvpnAgentRouteTable::FindRoute(const MacAddress &mac,
                                               const IpAddress &ip_addr,
                                               uint32_t plen,
                                               uint32_t ethernet_tag) {
    EvpnRouteKey key(NULL, vrf_name(), mac, ip_addr, plen, ethernet_tag);
    EvpnRouteEntry *route =
        static_cast<EvpnRouteEntry *>(FindActiveEntry(&key));
    return route;
}

EvpnRouteEntry *EvpnAgentRouteTable::FindRouteNoLock(const MacAddress &mac,
                                                     const IpAddress &ip_addr,
                                                     uint32_t plen,
                                                     uint32_t ethernet_tag) {
    EvpnRouteKey key(NULL, vrf_name(), mac, ip_addr, plen, ethernet_tag);
    EvpnRouteEntry *route =
        static_cast<EvpnRouteEntry *>(FindActiveEntryNoLock(&key));
    return route;
}

EvpnRouteEntry *EvpnAgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac,
                                                   const IpAddress &ip_addr,
                                                   uint32_t plen,
                                                   uint32_t ethernet_tag) {
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf->GetEvpnRouteTable());
    return table->FindRoute(mac, ip_addr, plen, ethernet_tag);
}

/////////////////////////////////////////////////////////////////////////////
// EvpnAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void EvpnAgentRouteTable::AddOvsPeerMulticastRouteInternal(const Peer *peer,
                                                           uint32_t vxlan_id,
                                                           const std::string &vn_name,
                                                           Ip4Address tsn,
                                                           Ip4Address tor_ip,
                                                           bool enqueue,
                                                           bool ha_stale) {
    const VrfEntry *vrf = vrf_entry();
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(vrf->GetName(), tsn, tor_ip,
                                     false, TunnelType::VXLAN));
    nh_req.data.reset(new TunnelNHData());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer,
                               vrf->GetName(),
                               MacAddress::BroadcastMac(),
                               tor_ip,
                               EvpnAgentRouteTable::ComputeHostIpPlen(tor_ip),
                               vxlan_id));
    req.data.reset(new MulticastRoute(vn_name, 0, vxlan_id,
                                      TunnelType::VxlanType(),
                                      nh_req, Composite::L2COMP,
                                      peer->sequence_number(),
                                      ha_stale));
    if (enqueue) {
        EvpnTableEnqueue(agent(), &req);
    } else {
        EvpnTableProcess(agent(), vrf_name(), req);
    }
}

void EvpnAgentRouteTable::AddOvsPeerMulticastRoute(const Peer *peer,
                                                   uint32_t vxlan_id,
                                                   const std::string &vn_name,
                                                   Ip4Address tsn,
                                                   Ip4Address tor_ip,
                                                   bool ha_stale) {
    AddOvsPeerMulticastRouteInternal(peer, vxlan_id, vn_name, tsn, tor_ip,
                                     false, ha_stale);
}

void EvpnAgentRouteTable::AddOvsPeerMulticastRouteReq(const Peer *peer,
                                                      uint32_t vxlan_id,
                                                      const std::string &vn_name,
                                                      Ip4Address tsn,
                                                      Ip4Address tor_ip) {
    AddOvsPeerMulticastRouteInternal(peer, vxlan_id, vn_name, tsn, tor_ip, true,
                                     false);
}

void EvpnAgentRouteTable::AddControllerReceiveRouteReq(const Peer *peer,
                                             const string &vrf_name,
                                             uint32_t label,
                                             const MacAddress &mac,
                                             const IpAddress &ip_addr,
                                             uint32_t ethernet_tag,
                                             const string &vn_name,
                                             const PathPreference &path_pref,
                                             uint64_t sequence_number) {
    const BgpPeer *bgp_peer = dynamic_cast<const BgpPeer *>(peer);
    assert(bgp_peer != NULL);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                               ethernet_tag));
    req.data.reset(new L2ReceiveRoute(vn_name, ethernet_tag,
                                                label, path_pref,
                                                sequence_number));
    agent()->fabric_evpn_table()->Enqueue(&req);
}

void EvpnAgentRouteTable::AddReceiveRouteReq(const Peer *peer,
                                             const string &vrf_name,
                                             uint32_t label,
                                             const MacAddress &mac,
                                             const IpAddress &ip_addr,
                                             uint32_t ethernet_tag,
                                             const string &vn_name,
                                             const PathPreference &pref) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                               ethernet_tag));
    req.data.reset(new L2ReceiveRoute(vn_name, ethernet_tag, label, pref,
                                      peer->sequence_number()));
    agent()->fabric_evpn_table()->Enqueue(&req);
}

void EvpnAgentRouteTable::AddReceiveRoute(const Peer *peer,
                                          const string &vrf_name,
                                          uint32_t label,
                                          const MacAddress &mac,
                                          const IpAddress &ip_addr,
                                          uint32_t ethernet_tag,
                                          const string &vn_name,
                                          const PathPreference &pref) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                               ethernet_tag));
    req.data.reset(new L2ReceiveRoute(vn_name, ethernet_tag, label, pref,
                                      peer->sequence_number()));
    Process(req);
}

void EvpnAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                             const string &vrf_name,
                                             const MacAddress &mac,
                                             const IpAddress &ip_addr,
                                             uint32_t ethernet_tag,
                                             LocalVmRoute *data) {
    assert(peer);
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                               ethernet_tag));
    data->set_tunnel_bmap(TunnelType::AllType());
    req.data.reset(data);
    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

void EvpnAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                          const string &vrf_name,
                                          const MacAddress &mac,
                                          const VmInterface *intf,
                                          const IpAddress &ip,
                                          uint32_t label,
                                          const string &vn_name,
                                          const SecurityGroupList &sg_id_list,
                                          const TagList &tag_id_list,
                                          const PathPreference &path_pref,
                                          uint32_t ethernet_tag,
                                          bool etree_leaf,
                                          const std::string &name) {
    assert(peer);

    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf->GetUuid(), name);
    VnListType vn_list;
    vn_list.insert(vn_name);
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                          intf->vxlan_id(), false,
                                          vn_list,
                                          InterfaceNHFlags::BRIDGE,
                                          sg_id_list, tag_id_list,
                                          CommunityList(),
                                          path_pref,
                                          IpAddress(),
                                          EcmpLoadBalance(), false, false,
                                          peer->sequence_number(),
                                          etree_leaf, false);
    data->set_tunnel_bmap(TunnelType::AllType());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip),
                               ethernet_tag));
    req.data.reset(data);
    EvpnTableProcess(agent, vrf_name, req);
}

void EvpnAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                          const string &vrf_name,
                                          const MacAddress &mac,
                                          const VmInterface *intf,
                                          const IpAddress &ip,
                                          uint32_t label,
                                          const string &vn_name,
                                          const SecurityGroupList &sg_id_list,
                                          const TagList &tag_id_list,
                                          const PathPreference &path_pref,
                                          uint32_t ethernet_tag,
                                          bool etree_leaf) {
    assert(peer);

    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf->GetUuid(), "");
    VnListType vn_list;
    vn_list.insert(vn_name);
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                          intf->vxlan_id(), false,
                                          vn_list,
                                          InterfaceNHFlags::BRIDGE,
                                          sg_id_list, tag_id_list,
                                          CommunityList(),
                                          path_pref,
                                          IpAddress(),
                                          EcmpLoadBalance(), false, false,
                                          peer->sequence_number(),
                                          etree_leaf, false);
    data->set_tunnel_bmap(TunnelType::AllType());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip,
                               EvpnAgentRouteTable::ComputeHostIpPlen(ip),
                               ethernet_tag));
    req.data.reset(data);
    EvpnTableEnqueue(agent, &req);
}

void EvpnAgentRouteTable::DeleteOvsPeerMulticastRouteInternal(const Peer *peer,
                                                              uint32_t vxlan_id,
                                                              const Ip4Address &tor_ip,
                                                              bool enqueue) {
    const VrfEntry *vrf = vrf_entry();
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer,
                                   vrf->GetName(),
                                   MacAddress::BroadcastMac(),
                                   tor_ip,
                                   EvpnAgentRouteTable::ComputeHostIpPlen(tor_ip),
                                   vxlan_id));
    req.data.reset(NULL);
    if (enqueue) {
        EvpnTableEnqueue(agent(), &req);
    } else {
        EvpnTableProcess(agent(), vrf->GetName(), req);
    }
}

void EvpnAgentRouteTable::DeleteOvsPeerMulticastRouteReq(const Peer *peer,
                                                         uint32_t vxlan_id,
                                                         const Ip4Address &tor_ip) {
    DeleteOvsPeerMulticastRouteInternal(peer, vxlan_id, tor_ip, true);
}

void EvpnAgentRouteTable::DeleteOvsPeerMulticastRoute(const Peer *peer,
                                                      uint32_t vxlan_id,
                                                      const Ip4Address &tor_ip) {
    DeleteOvsPeerMulticastRouteInternal(peer, vxlan_id, tor_ip, false);
}

void EvpnAgentRouteTable::DelLocalVmRoute(const Peer *peer,
                                          const string &vrf_name,
                                          const MacAddress &mac,
                                          const VmInterface *intf,
                                          const IpAddress &ip,
                                          uint32_t ethernet_tag) {
    assert(peer);
    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, IpAddress(ip),
                                   EvpnAgentRouteTable::ComputeHostIpPlen(ip),
                                   ethernet_tag));
    req.data.reset(NULL);
    EvpnTableProcess(agent, vrf_name, req);
}

void EvpnAgentRouteTable::ResyncVmRoute(const Peer *peer,
                                        const string &vrf_name,
                                        const MacAddress &mac,
                                        const IpAddress &ip_addr,
                                        uint32_t ethernet_tag,
                                        AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    EvpnRouteKey *key = new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                                ethernet_tag);
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    req.data.reset(data);

    EvpnTableProcess(Agent::GetInstance(), vrf_name, req);
}

void EvpnAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                              const string &vrf_name,
                                              const MacAddress &mac,
                                              const IpAddress &ip_addr,
                                              uint32_t plen,
                                              uint32_t ethernet_tag,
                                              AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   plen, ethernet_tag));
    req.data.reset(data);

    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

void EvpnAgentRouteTable::AddRemoteVmRoute(const Peer *peer,
                                           const string &vrf_name,
                                           const MacAddress &mac,
                                           const IpAddress &ip_addr,
                                           uint32_t plen,
                                           uint32_t ethernet_tag,
                                           AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   plen, ethernet_tag));
    req.data.reset(data);

    EvpnTableProcess(Agent::GetInstance(), vrf_name, req);
}

void EvpnAgentRouteTable::AddType5Route(const Peer *peer,
                                        const string &vrf_name,
                                        const IpAddress &ip_addr,
                                        uint32_t ethernet_tag,
                                        EvpnRoutingData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, MacAddress(), ip_addr,
                                   EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                                   ethernet_tag));
    req.data.reset(data);

    EvpnTableEnqueue(agent(), &req);
}

void EvpnAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      const IpAddress &ip_addr,
                                      uint32_t plen,
                                      uint32_t ethernet_tag,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   plen, ethernet_tag));
    req.data.reset(data);
    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

void EvpnAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   const IpAddress &ip_addr,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                                   ethernet_tag));
    req.data.reset(NULL);
    EvpnTableProcess(Agent::GetInstance(), vrf_name, req);
}

void EvpnAgentRouteTable::AddClonedLocalPathReq(const Peer *peer,
                                                const string &vrf_name,
                                                const MacAddress &mac,
                                                const IpAddress &ip_addr,
                                                uint32_t ethernet_tag,
                                                ClonedLocalPath *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   EvpnAgentRouteTable::ComputeHostIpPlen(ip_addr),
                                   ethernet_tag));
    req.data.reset(data);
    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

uint32_t EvpnAgentRouteTable::ComputeHostIpPlen(const IpAddress &addr) {
    if (addr.is_v4())
        return 32;
    if (addr.is_v6())
        return 128;

    assert(0);
    return 0;
}

/////////////////////////////////////////////////////////////////////////////
// EvpnRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
EvpnRouteEntry::EvpnRouteEntry(VrfEntry *vrf,
                               const MacAddress &mac,
                               const IpAddress &ip_addr,
                               uint32_t plen,
                               uint32_t ethernet_tag,
                               bool is_multicast) :
    AgentRoute(vrf, is_multicast), mac_(mac), ip_addr_(ip_addr),
    plen_(plen),
    ethernet_tag_(ethernet_tag),
    publish_to_inet_route_table_(true),
    publish_to_bridge_route_table_(true) {
        if (IsType5()) {
            publish_to_inet_route_table_ = false;
            publish_to_bridge_route_table_ = false;
        }
}

string EvpnRouteEntry::ToString() const {
    std::stringstream str;
    if (is_multicast()) {
        str << mac_.ToString();
    } else {
        str << ethernet_tag_;
        str << "-";
        str << mac_.ToString();
        str << "-";
        str << ip_addr_.to_string();
        str << "/";
        str << plen_;
    }
    return str.str();
}

int EvpnRouteEntry::CompareTo(const Route &rhs) const {
    const EvpnRouteEntry &a = static_cast<const EvpnRouteEntry &>(rhs);

    if (ethernet_tag_ < a.ethernet_tag_)
        return -1;

    if (ethernet_tag_ > a.ethernet_tag_)
        return 1;

    int cmp = mac_.CompareTo(a.mac_);
    if (cmp != 0)
        return cmp;

    if (ip_addr_ < a.ip_addr_)
        return -1;

    if (ip_addr_ > a.ip_addr_)
        return 1;

    if (plen_ < a.plen_)
        return -1;

    if (plen_ > a.plen_)
        return 1;

    return 0;
}

DBEntryBase::KeyPtr EvpnRouteEntry::GetDBRequestKey() const {
    EvpnRouteKey *key =
        new EvpnRouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_, ip_addr_,
                           plen_, ethernet_tag_);
    return DBEntryBase::KeyPtr(key);
}

void EvpnRouteEntry::SetKey(const DBRequestKey *key) {
    const EvpnRouteKey *k = static_cast<const EvpnRouteKey *>(key);
    SetVrf(Agent::GetInstance()->vrf_table()->FindVrfFromName(k->vrf_name()));
    mac_ = k->GetMac();
    ip_addr_ = k->ip_addr();
    ethernet_tag_ = k->ethernet_tag();
}

const uint32_t EvpnRouteEntry::GetVmIpPlen() const {
    return plen_;
}

uint32_t EvpnRouteEntry::GetActiveLabel() const {
    return GetActivePath()->GetActiveLabel();
}

const AgentPath *EvpnRouteEntry::FindOvsPath() const {
    for(Route::PathList::const_iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer()->GetType() == Peer::OVS_PEER) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
}

//Notify L2 route corresponding to MAC in evpn route.
//On addition of evpn routes, update bridge route using mac of evpn and inet
//route using ip of evpn.
//NH of bridge route is same as of EVPN as bridge rt is programmed in kernel.
//NH of Inet route will be of subnet route to which it belongs. This makes sure
//that n absence of any directly installed Inet route for this IP packets are
//forwarded as per the subnet route decision.
void EvpnRouteEntry::UpdateDerivedRoutes(AgentRouteTable *table,
                                         const AgentPath *path,
                                         bool active_path_changed) {
    //As active path is picked from route, any modification in non-active
    //path need not rebake agent route.
    //Path is NULL when resync is issued on route, hence no need to check flag
    if ((path != NULL) && !active_path_changed)
        return;


    if (publish_to_bridge_route_table()) {
        BridgeAgentRouteTable *bridge_table = NULL;
        bridge_table = static_cast<BridgeAgentRouteTable *>
            (table->vrf_entry()->GetBridgeRouteTable());
        if (bridge_table)
            bridge_table->AddBridgeRoute(this);
    }
    if (publish_to_inet_route_table()) {
        InetUnicastAgentRouteTable *inet_table =
            table->vrf_entry()->GetInetUnicastRouteTable(ip_addr());
        if (inet_table)
            inet_table->AddEvpnRoute(this);
    }
}

//Delete path from L2 route corresponding to MAC+IP in evpn route.
void EvpnRouteEntry::DeleteDerivedRoutes(AgentRouteTable *table) {
    //Delete from bridge table
    BridgeAgentRouteTable *bridge_table = static_cast<BridgeAgentRouteTable *>
        (table->vrf_entry()->GetBridgeRouteTable());
    if (bridge_table)
        bridge_table->DeleteBridgeRoute(this);

    //Delete from Inet table
    InetUnicastAgentRouteTable *inet_table =
        table->vrf_entry()->GetInetUnicastRouteTable(ip_addr());
    if (inet_table)
        inet_table->DeleteEvpnRoute(this);
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh related methods
/////////////////////////////////////////////////////////////////////////////
void EvpnRouteReq::HandleRequest() const {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentSandeshPtr sand(new AgentEvpnRtSandesh(vrf, context(), "",
                                                get_stale()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr EvpnAgentRouteTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr(new AgentEvpnRtSandesh(vrf_entry(), context, "",
                                                  false));
}

bool EvpnRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    EvpnRouteResp *resp = static_cast<EvpnRouteResp *>(sresp);
    RouteEvpnSandeshData data;
    data.set_mac(ToString());
    data.set_ip_addr(ip_addr_.to_string());

    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->SetSandeshData(pdata);
            data.path_list.push_back(pdata);
        }
    }
    std::vector<RouteEvpnSandeshData> &list =
        const_cast<std::vector<RouteEvpnSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

bool EvpnRouteEntry::ReComputePathDeletion(AgentPath *path) {
    bool ret = false;
    Agent *agent = static_cast<AgentRouteTable *>(get_table())->agent();
    EvpnRoutingPath *evpn_path = dynamic_cast<EvpnRoutingPath *>(path);
    if (evpn_path) {
        evpn_path->DeleteEvpnType5Route(agent, this);
    }
    return ret;
}
