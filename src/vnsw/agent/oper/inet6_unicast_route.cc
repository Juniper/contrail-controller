/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include "base/task_annotations.h"
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

AgentRoute *
Inet6UnicastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const {
    Inet6UnicastRouteEntry * entry = 
        new Inet6UnicastRouteEntry(vrf, dip_, plen_, is_multicast); 
    return static_cast<AgentRoute *>(entry);
}

AgentRouteKey *Inet6UnicastRouteKey::Clone() const {
    return (new Inet6UnicastRouteKey(peer_, vrf_name_, dip_, plen_));
}

/////////////////////////////////////////////////////////////////////////////
// Inet6UnicastAgentRouteTable functions
/////////////////////////////////////////////////////////////////////////////
DBTableBase *
Inet6UnicastAgentRouteTable::CreateTable(DB *db, const std::string &name) {
    AgentRouteTable *table = new Inet6UnicastAgentRouteTable(db, name);
    table->Init();
    return table;
}

Inet6UnicastRouteEntry *
Inet6UnicastAgentRouteTable::FindLPM(const Ip6Address &ip) {
    Inet6UnicastRouteEntry key(NULL, ip, 128, false);
    return tree_.LPMFind(&key);
}

Inet6UnicastRouteEntry *
Inet6UnicastAgentRouteTable::FindLPM(const Inet6UnicastRouteEntry &rt_key) {
    return tree_.LPMFind(&rt_key);
}

static void UnicastTableEnqueue(Agent *agent, const string &vrf_name, 
                                DBRequest *req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    if (table) {
        table->Enqueue(req);
    }
}

static void UnicastTableProcess(Agent *agent, const string &vrf_name,
                                DBRequest &req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

void Inet6UnicastAgentRouteTable::ReEvaluatePaths(const string &vrf_name, 
                                                  const Ip6Address &addr, 
                                                  uint8_t plen) {
    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    Inet6UnicastRouteKey *rt_key = new Inet6UnicastRouteKey(NULL, vrf_name, 
                                                            addr, plen);

    rt_key->sub_op_ = AgentKey::RESYNC;
    rt_req.key.reset(rt_key);
    rt_req.data.reset(NULL);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &rt_req);
}

/////////////////////////////////////////////////////////////////////////////
// Inet4UnicastAgentRouteEntry functions
/////////////////////////////////////////////////////////////////////////////
Inet6UnicastRouteEntry::Inet6UnicastRouteEntry(VrfEntry *vrf,
                                               const Ip6Address &addr,
                                               uint8_t plen,
                                               bool is_multicast) :
    AgentRoute(vrf, is_multicast), addr_(GetIp6SubnetAddress(addr, plen)),
    plen_(plen) {
}

string Inet6UnicastRouteEntry::ToString() const {
    ostringstream str;
    str << addr_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

int Inet6UnicastRouteEntry::CompareTo(const Route &rhs) const {
    const Inet6UnicastRouteEntry &a = 
        static_cast<const Inet6UnicastRouteEntry &>(rhs);

    if (addr_ < a.addr_) {
        return -1;
    }

    if (addr_ > a.addr_) {
        return 1;
    }

    if (plen_ < a.plen_) {
        return -1;
    }

    if (plen_ > a.plen_) {
        return 1;
    }

    return 0;
}

DBEntryBase::KeyPtr Inet6UnicastRouteEntry::GetDBRequestKey() const {
    Agent *agent = 
        (static_cast<Inet6UnicastAgentRouteTable *>(get_table()))->agent();
    Inet6UnicastRouteKey *key = 
        new Inet6UnicastRouteKey(agent->local_peer(),
                                 vrf()->GetName(), addr_, plen_);
    return DBEntryBase::KeyPtr(key);
}

void Inet6UnicastRouteEntry::SetKey(const DBRequestKey *key) {
    Agent *agent = 
        (static_cast<Inet6UnicastAgentRouteTable *>(get_table()))->agent();
    const Inet6UnicastRouteKey *k = 
        static_cast<const Inet6UnicastRouteKey*>(key);
    SetVrf(agent->vrf_table()->FindVrfFromName(k->vrf_name()));
    Ip6Address tmp(k->addr());
    set_addr(tmp);
    set_plen(k->plen());
}

/////////////////////////////////////////////////////////////////////////////
// AgentRouteData virtual functions
/////////////////////////////////////////////////////////////////////////////
#if 0
bool Inet4UnicastEcmpRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    agent->GetNextHopTable()->Process(nh_req_);
    CompositeNHKey key(vrf_name_, dest_addr_, plen_, local_ecmp_nh_);
    nh =
        static_cast<NextHop *>(agent->GetNextHopTable()->FindActiveEntry(&key));

    assert(nh);
    if (path->label() != label_) {
        path->set_label(label_);
        ret = true;
    }

    path->set_tunnel_bmap(TunnelType::MplsType());
    TunnelType::Type new_tunnel_type = 
        TunnelType::ComputeType(path->tunnel_bmap());
    if (path->tunnel_type() != new_tunnel_type) {
        path->set_tunnel_type(new_tunnel_type);
        ret = true;
    }

    SecurityGroupList path_sg_list;
    path_sg_list = path->sg_list();
    if (path_sg_list != sg_list_) {
        path->set_sg_list(sg_list_);
        ret = true;
    }

    path->set_dest_vn_name(vn_name_);
    ret = true;
    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}
#endif

/////////////////////////////////////////////////////////////////////////////
// Sandesh functions
/////////////////////////////////////////////////////////////////////////////

bool Inet6UnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {
    Inet6UcRouteResp *resp = static_cast<Inet6UcRouteResp *>(sresp);

    RouteUcSandeshData data;
    data.set_src_ip(addr_.to_string());
    data.set_src_plen(plen_);
    data.set_src_vrf(vrf()->GetName());
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

    std::vector<RouteUcSandeshData> &list = 
        const_cast<std::vector<RouteUcSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

bool Inet6UnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, Ip6Address addr,
                                            uint8_t plen, bool stale) const {
    if (addr_ == addr && plen_ == plen) {
        return DBEntrySandesh(sresp, stale);
    }

    return false;
}

void Inet6UcRouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentInet6UcRtSandesh *sand;
    if (get_src_ip().empty()) {
        sand = new AgentInet6UcRtSandesh(vrf, context(), get_stale());
    } else {
        boost::system::error_code ec;
        Ip6Address src_ip = Ip6Address::from_string(get_src_ip(), ec);
        sand = 
            new AgentInet6UcRtSandesh(vrf, context(), src_ip, 
                                      (uint8_t)get_prefix_len(), get_stale());
    }
    sand->DoSandesh();
}

/////////////////////////////////////////////////////////////////////////////
// Helper functions to enqueue request or process inline
/////////////////////////////////////////////////////////////////////////////

// Request to delete an entry
void 
Inet6UnicastAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                       const Ip6Address &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Inline delete request
void 
Inet6UnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                    const Ip6Address &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void 
Inet6UnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                          const Ip6Address &addr, 
                                          uint8_t plen,
                                          const std::string &dest_vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(agent->local_peer(), vrf_name,
                                           addr, plen));

    PacketInterfaceKey intf_key(nil_uuid(), agent->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    req.data.reset(data);

    UnicastTableEnqueue(agent, vrf_name, &req);
}

#if 0
// Create Route with VLAN NH
void 
Inet6UnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const Ip6Address &addr,
                                               uint8_t plen,
                                               const uuid &intf_uuid,
                                               uint16_t tag,
                                               uint32_t label,
                                               const string &dest_vn_name,
                                               const SecurityGroupList &sg_list) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                   sg_list));
    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

// Create Route with VLAN NH
void
Inet6UnicastAgentRouteTable::AddVlanNHRoute(const Peer *peer, 
                                            const string &vm_vrf,
                                            const Ip6Address &addr, 
                                            uint8_t plen,
                                            const uuid &intf_uuid, 
                                            uint16_t tag,
                                            uint32_t label,
                                            const string &dest_vn_name,
                                            const SecurityGroupList &sg_list) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                   sg_list));

    UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}
#endif

void
Inet6UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                                const string &vm_vrf,
                                                const Ip6Address &addr,
                                                uint8_t plen,
                                                LocalVmRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, addr, plen));

    req.data.reset(data);

    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

    //TODO: change subnet_gw_ip to Ip6Address
void
Inet6UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                                const string &vm_vrf,
                                                const Ip6Address &addr,
                                                uint8_t plen,
                                                const uuid &intf_uuid,
                                                const string &vn_name,
                                                uint32_t label,
                                                const SecurityGroupList &sg_list,
                                                bool force_policy,
                                                const PathPreference 
                                                &path_preference,
                                                const Ip6Address &subnet_gw_ip) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label,
                                    VxLanTable::kInvalidvxlan_id, force_policy,
                                    vn_name, InterfaceNHFlags::INET4, sg_list,
                                    path_preference, subnet_gw_ip));

    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void 
Inet6UnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer, 
                                             const string &vm_vrf,
                                             const Ip6Address &addr, 
                                             uint8_t plen,
                                             const uuid &intf_uuid,
                                             const string &vn_name,
                                             uint32_t label,
                                             const SecurityGroupList &sg_list,
                                             bool force_policy,
                                             const PathPreference 
                                             &path_preference,
                                             const Ip6Address &subnet_gw_ip) {

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label, VxLanTable::kInvalidvxlan_id,
                                    force_policy, vn_name,
                                    InterfaceNHFlags::INET4, sg_list,
                                    path_preference, subnet_gw_ip));
    UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void 
Inet6UnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer, 
                                                 const string &vm_vrf,
                                                 const Ip6Address &vm_addr,
                                                 uint8_t plen,
                                                 AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet6UnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    req.data.reset(data);
    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}
 
void
Inet6UnicastAgentRouteTable::AddSubnetRoute(const string &vrf_name,
                                            const Ip6Address &dst_addr,
                                            uint8_t plen,
                                            const string &vn_name,
                                            uint32_t vxlan_id) {
    Agent *agent = Agent::GetInstance();
    struct ether_addr flood_mac;

    memcpy(&flood_mac, ether_aton("ff:ff:ff:ff:ff:ff"),
           sizeof(struct ether_addr));
    AgentRoute *route = Layer2AgentRouteTable::FindRoute(agent, vrf_name,
                                                         flood_mac);

    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    ComponentNHKeyList component_nh_list;

    if (route != NULL) {
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            if (path->peer()->GetType() != Peer::BGP_PEER) {
                continue;
            }
            NextHopKey *evpn_peer_key =
                static_cast<NextHopKey *>((path->nexthop(agent)->
                                           GetDBRequestKey()).release());
            DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
            nh_req.key.reset(evpn_peer_key);
            nh_req.data.reset(new CompositeNHData());

            //Add route with this peer
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.key.reset(new Inet6UnicastRouteKey(path->peer(),
                                                   vrf_name, dst_addr, plen));
            req.data.reset(new SubnetRoute(vn_name, vxlan_id, nh_req));
            UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
        }
    }

    //Add local perr path with discard NH
    DBRequest dscd_nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    dscd_nh_req.key.reset(new DiscardNHKey());
    dscd_nh_req.data.reset(NULL);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new Inet6UnicastRouteKey(Agent::GetInstance()->local_peer(),
                                            vrf_name, dst_addr, plen));
    req.data.reset(new SubnetRoute(vn_name, vxlan_id, dscd_nh_req));
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}
