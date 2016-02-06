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
    return new EvpnRouteKey(peer_, vrf_name_, dmac_, ip_addr_,
                              ethernet_tag_);
}

AgentRoute *
EvpnRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const
{
    EvpnRouteEntry *entry = new EvpnRouteEntry(vrf, dmac_, ip_addr_,
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
                                               uint32_t ethernet_tag) {
    EvpnRouteKey key(NULL, vrf_name(), mac, ip_addr, ethernet_tag);
    EvpnRouteEntry *route =
        static_cast<EvpnRouteEntry *>(FindActiveEntry(&key));
    return route;
}

EvpnRouteEntry *EvpnAgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac,
                                                   const IpAddress &ip_addr,
                                                   uint32_t ethernet_tag) {
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf->GetEvpnRouteTable());
    return table->FindRoute(mac, ip_addr, ethernet_tag);
}

/////////////////////////////////////////////////////////////////////////////
// EvpnAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
void EvpnAgentRouteTable::AddOvsPeerMulticastRouteInternal(const Peer *peer,
                                                           uint32_t vxlan_id,
                                                           const std::string &vn_name,
                                                           Ip4Address tsn,
                                                           Ip4Address tor_ip,
                                                           bool enqueue) {
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
                                   vxlan_id));
    req.data.reset(new MulticastRoute(vn_name, 0, vxlan_id,
                                      TunnelType::VxlanType(),
                                      nh_req, Composite::L2COMP));
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
                                                   Ip4Address tor_ip) {
    AddOvsPeerMulticastRouteInternal(peer, vxlan_id, vn_name, tsn, tor_ip, false);
}

void EvpnAgentRouteTable::AddOvsPeerMulticastRouteReq(const Peer *peer,
                                                      uint32_t vxlan_id,
                                                      const std::string &vn_name,
                                                      Ip4Address tsn,
                                                      Ip4Address tor_ip) {
    AddOvsPeerMulticastRouteInternal(peer, vxlan_id, vn_name, tsn, tor_ip, true);
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
                                   ethernet_tag));
    req.data.reset(new L2ReceiveRoute(vn_name, ethernet_tag, label, pref));
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
                                   ethernet_tag));
    req.data.reset(new L2ReceiveRoute(vn_name, ethernet_tag, label, pref));
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
                                          const PathPreference &path_pref,
                                          uint32_t ethernet_tag) {
    assert(peer);

    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf->GetUuid(), "");
    VnListType vn_list;
    vn_list.insert(vn_name);
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                          intf->vxlan_id(), false,
                                          vn_list,
                                          InterfaceNHFlags::BRIDGE,
                                          sg_id_list, CommunityList(),
                                          path_pref,
                                          IpAddress(),
                                          EcmpLoadBalance());
    data->set_tunnel_bmap(TunnelType::AllType());

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip, ethernet_tag));
    req.data.reset(data);
    EvpnTableProcess(agent, vrf_name, req);

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
                                              uint32_t ethernet_tag,
                                              AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   ethernet_tag));
    req.data.reset(data);

    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

void EvpnAgentRouteTable::AddRemoteVmRoute(const Peer *peer,
                                           const string &vrf_name,
                                           const MacAddress &mac,
                                           const IpAddress &ip_addr,
                                           uint32_t ethernet_tag,
                                           AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                   ethernet_tag));
    req.data.reset(data);

    EvpnTableProcess(Agent::GetInstance(), vrf_name, req);
}

void EvpnAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      const IpAddress &ip_addr,
                                      uint32_t ethernet_tag,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(data);
    EvpnTableEnqueue(Agent::GetInstance(), &req);
}

void EvpnAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const MacAddress &mac,
                                   const IpAddress &ip_addr,
                                   uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(NULL);
    EvpnTableProcess(Agent::GetInstance(), vrf_name, req);
}

//Notify L2 route corresponding to MAC in evpn route.
void EvpnAgentRouteTable::UpdateDependants(AgentRoute *entry) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(entry);
    if (evpn_rt->publish_to_bridge_route_table()) {
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vrf_entry()->GetBridgeRouteTable());
        table->AddBridgeRoute(entry);
    }
}

//Delete path from L2 route corresponding to MAC+IP in evpn route.
void EvpnAgentRouteTable::PreRouteDelete(AgentRoute *entry) {
    EvpnRouteEntry *evpn_rt = dynamic_cast<EvpnRouteEntry *>(entry);
    if (evpn_rt->publish_to_bridge_route_table()) {
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vrf_entry()->GetBridgeRouteTable());
        table->DeleteBridgeRoute(entry);
    }
}

/////////////////////////////////////////////////////////////////////////////
// EvpnRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
EvpnRouteEntry::EvpnRouteEntry(VrfEntry *vrf,
                               const MacAddress &mac,
                               const IpAddress &ip_addr,
                               uint32_t ethernet_tag,
                               bool is_multicast) :
    AgentRoute(vrf, is_multicast), mac_(mac), ip_addr_(ip_addr),
    ethernet_tag_(ethernet_tag),
    publish_to_inet_route_table_(true),
    publish_to_bridge_route_table_(true) {
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

    return 0;
}

DBEntryBase::KeyPtr EvpnRouteEntry::GetDBRequestKey() const {
    EvpnRouteKey *key =
        new EvpnRouteKey(Agent::GetInstance()->local_vm_peer(),
                           vrf()->GetName(), mac_, ip_addr_,
                           ethernet_tag_);
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
    if (ip_addr_.is_v4())
        return 32;
    if (ip_addr_.is_v6())
        return 128;
    assert(0);
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
            if (stale && !path->is_stale())
                continue;
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
