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
#include <oper/agent_sandesh.h>

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
    str << "-";;
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
                                               ethernet_tag_,
                                               peer()->GetType());
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

EvpnRouteEntry *EvpnAgentRouteTable::FindRoute(const Agent *agent,
                                                   const string &vrf_name,
                                                   const MacAddress &mac,
                                                   const IpAddress &ip_addr,
                                                   uint32_t ethernet_tag) {
    VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
    if (vrf == NULL)
        return NULL;

    EvpnRouteKey key(agent->local_vm_peer(), vrf_name, mac, ip_addr,
                     ethernet_tag);
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf->GetEvpnRouteTable());
    EvpnRouteEntry *route =
        static_cast<EvpnRouteEntry *>(table->FindActiveEntry(&key));
    return route;
}

/////////////////////////////////////////////////////////////////////////////
// EvpnAgentRouteTable utility methods to add/delete routes
/////////////////////////////////////////////////////////////////////////////
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

static LocalVmRoute *MakeData(const VmInterface *intf) {
    SecurityGroupList sg_list;
    PathPreference path_preference;
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf->GetUuid(), "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, intf->l2_label(),
                                          intf->vxlan_id(), false,
                                          intf->vn()->GetName(),
                                          InterfaceNHFlags::LAYER2,
                                          sg_list, path_preference,
                                          IpAddress());
    data->set_tunnel_bmap(TunnelType::AllType());
    return data;
}

void EvpnAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                          const VmInterface *intf,
                                          uint32_t vxlan_id) {
    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    assert(peer);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);

    // Add route for IPv4 address
    if (intf->ip_addr().is_unspecified() == false) {
        req.key.reset(new EvpnRouteKey(peer, intf->vrf()->GetName(),
                                       MacAddress::FromString(intf->vm_mac()),
                                       IpAddress(intf->ip_addr()),
                                       vxlan_id));
        req.data.reset(MakeData(intf));
        EvpnTableProcess(agent, intf->vrf()->GetName(), req);
    }

    // Add route for IPv6 address
    if (intf->ip6_addr().is_unspecified() == false) {
        req.key.reset(new EvpnRouteKey(peer, intf->vrf()->GetName(),
                                       MacAddress::FromString(intf->vm_mac()),
                                       IpAddress(intf->ip6_addr()),
                                       vxlan_id));
        req.data.reset(MakeData(intf));
        EvpnTableProcess(agent, intf->vrf()->GetName(), req);
    }
}

void EvpnAgentRouteTable::DelLocalVmRoute(const Peer *peer,
                                            const string &vrf_name,
                                            const VmInterface *intf,
                                            const Ip4Address &old_v4_addr,
                                            const Ip6Address &old_v6_addr,
                                            uint32_t ethernet_tag) {
    Agent *agent = static_cast<AgentDBTable *>(intf->get_table())->agent();
    assert(peer);
    DBRequest req(DBRequest::DB_ENTRY_DELETE);

    // Delete route for IPv4 address
    if (old_v4_addr.is_unspecified() == false) {
        req.key.reset(new EvpnRouteKey(peer, vrf_name,
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(old_v4_addr), ethernet_tag));
        req.data.reset(NULL);
        EvpnTableProcess(agent, vrf_name, req);
    }

    // Delete route for IPv6 address
    if (old_v6_addr.is_unspecified() == false) {
        req.key.reset(new EvpnRouteKey(peer, vrf_name,
                                         MacAddress::FromString(intf->vm_mac()),
                                         IpAddress(old_v6_addr), ethernet_tag));
        req.data.reset(NULL);
        EvpnTableProcess(agent, vrf_name, req);
    }
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

void EvpnAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const MacAddress &mac,
                                      const IpAddress &ip_addr,
                                      uint32_t ethernet_tag) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new EvpnRouteKey(peer, vrf_name, mac, ip_addr,
                                     ethernet_tag));
    req.data.reset(NULL);
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
    EvpnRouteEntry *evpn_rt = static_cast<EvpnRouteEntry *>(entry);
    if (evpn_rt->evpn_peer() == NULL) {
        evpn_rt->CreateEvpnPeer();
    }
    Layer2AgentRouteTable *table = static_cast<Layer2AgentRouteTable *>
        (vrf_entry()->GetLayer2RouteTable());
    table->AddEvpnRoute(entry);
}

//Delete path from L2 route corresponding to MAC+IP in evpn route.
void EvpnAgentRouteTable::PreRouteDelete(AgentRoute *entry) {
    Layer2AgentRouteTable *table = static_cast<Layer2AgentRouteTable *>
        (vrf_entry()->GetLayer2RouteTable());
    table->DeleteEvpnRoute(entry);
}

/////////////////////////////////////////////////////////////////////////////
// EvpnRouteEntry methods
/////////////////////////////////////////////////////////////////////////////
EvpnRouteEntry::EvpnRouteEntry(VrfEntry *vrf,
                               const MacAddress &mac,
                               const IpAddress &ip_addr,
                               uint32_t ethernet_tag,
                               Peer::Type type) :
    AgentRoute(vrf, false), mac_(mac), ip_addr_(ip_addr),
    ethernet_tag_(ethernet_tag) {
    evpn_peer_ref_.reset();
}

string EvpnRouteEntry::ToString() const {
    std::stringstream str;
    str << ethernet_tag_;
    str << "-";;
    str << mac_.ToString();
    str << "-";
    str << ip_addr_.to_string();
    return str.str();
}

void EvpnRouteEntry::CreateEvpnPeer() {
    evpn_peer_ref_.reset(new EvpnPeer());
}

int EvpnRouteEntry::CompareTo(const Route &rhs) const {
    const EvpnRouteEntry &a = static_cast<const EvpnRouteEntry &>(rhs);

    int cmp = mac_.CompareTo(a.mac_);
    if (cmp != 0)
        return cmp;

    if (ip_addr_ < a.ip_addr_)
        return -1;

    if (ip_addr_ > a.ip_addr_)
        return 1;

    if (ethernet_tag_ < a.ethernet_tag_)
        return -1;

    if (ethernet_tag_ > a.ethernet_tag_)
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

    AgentEvpnRtSandesh *sand =
        new AgentEvpnRtSandesh(vrf, context(), "", get_stale());
    sand->DoSandesh();
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
