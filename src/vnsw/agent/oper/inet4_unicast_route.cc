/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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

Inet4UnicastRouteEntry::Inet4UnicastRouteEntry(VrfEntry *vrf,
                                               const Ip4Address &addr,
                                               uint8_t plen,
                                               bool is_multicast) :
    AgentRoute(vrf, is_multicast), addr_(GetIp4SubnetAddress(addr, plen)),
    plen_(plen) {
}

AgentRoute *
Inet4UnicastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const {
    Inet4UnicastRouteEntry * entry = 
        new Inet4UnicastRouteEntry(vrf, dip_, plen_, is_multicast); 
    return static_cast<AgentRoute *>(entry);
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindLPM(const Ip4Address &ip) {
    Inet4UnicastRouteEntry key(NULL, ip, 32, false);
    return tree_.LPMFind(&key);
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindLPM(const Inet4UnicastRouteEntry &rt_key) {
    return tree_.LPMFind(&rt_key);
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindResolveRoute(const Ip4Address &ip) {
    uint8_t plen = 32;
    Inet4UnicastRouteEntry *rt = NULL;
    do {
        Inet4UnicastRouteEntry key(NULL, ip, plen, false);
        rt = tree_.LPMFind(&key);
        if (rt) {
            const NextHop *nh = rt->GetActiveNextHop();
            if (nh && nh->GetType() == NextHop::RESOLVE)
                return rt;
        }
    } while (rt && --plen);

    return NULL;
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindResolveRoute(const string &vrf_name, 
                                              const Ip4Address &ip) {
    VrfEntry *vrf = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    Inet4UnicastAgentRouteTable *rt_table = 
              static_cast<Inet4UnicastAgentRouteTable *>
              (vrf->GetInet4UnicastRouteTable());
    return rt_table->FindResolveRoute(ip);
}

string Inet4UnicastRouteEntry::ToString() const {
    ostringstream str;
    str << addr_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

int Inet4UnicastRouteEntry::CompareTo(const Route &rhs) const {
    const Inet4UnicastRouteEntry &a = 
        static_cast<const Inet4UnicastRouteEntry &>(rhs);

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

DBEntryBase::KeyPtr Inet4UnicastRouteEntry::GetDBRequestKey() const {
    Agent *agent = 
        (static_cast<Inet4UnicastAgentRouteTable *>(get_table()))->agent();
    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(agent->GetLocalPeer(),
                                 vrf()->GetName(), addr_, plen_);
    return DBEntryBase::KeyPtr(key);
}

void Inet4UnicastRouteEntry::SetKey(const DBRequestKey *key) {
    Agent *agent = 
        (static_cast<Inet4UnicastAgentRouteTable *>(get_table()))->agent();
    const Inet4UnicastRouteKey *k = 
        static_cast<const Inet4UnicastRouteKey*>(key);
    SetVrf(agent->vrf_table()->FindVrfFromName(k->vrf_name()));
    Ip4Address tmp(k->addr());
    set_addr(tmp);
    set_plen(k->plen());
}

DBTableBase *
Inet4UnicastAgentRouteTable::CreateTable(DB *db, const std::string &name) {
    AgentRouteTable *table = new Inet4UnicastAgentRouteTable(db, name);
    table->Init();
    return table;
}

bool Inet4UnicastArpRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;

    ArpNHKey key(vrf_name_, addr_);
    NextHop *nh = 
        static_cast<NextHop *>(agent->GetNextHopTable()->FindActiveEntry(&key));
    path->set_unresolved(false);
    path->set_dest_vn_name(agent->GetFabricVnName());
    if (path->dest_vn_name() != agent->GetFabricVnName()) {
        ret = true;
    }
    ret = true;
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
} 

bool Inet4UnicastGatewayRoute::AddChangePath(Agent *agent, AgentPath *path) {
    path->set_vrf_name(vrf_name_);

    Inet4UnicastAgentRouteTable *table = NULL;
    table = static_cast<Inet4UnicastAgentRouteTable *>
        (agent->GetVrfTable()->GetInet4UnicastRouteTable(vrf_name_));
    Inet4UnicastRouteEntry *rt = table->FindRoute(gw_ip_); 
    if (rt == NULL || rt->plen() == 0) {
        path->set_unresolved(true);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        path->set_unresolved(true);
        Inet4UnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_);
    } else {
        path->set_unresolved(false);
    }

    //Reset to new gateway route, no nexthop for indirect route
    path->set_gw_ip(gw_ip_);
    path->ResetDependantRoute(rt);
    if (path->dest_vn_name() != agent->GetFabricVnName()) {
        path->set_dest_vn_name(agent->GetFabricVnName());
    }

    return true;
}

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

static void UnicastTableEnqueue(Agent *agent, const string &vrf_name, 
                                DBRequest *req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Enqueue(req);
    }
}

static void UnicastTableProcess(Agent *agent, const string &vrf_name,
                                DBRequest &req) {
    AgentRouteTable *table = 
        agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

void 
Inet4UnicastAgentRouteTable::CheckAndAddArpReq(const string &vrf_name, 
                                               const Ip4Address &ip) {

    if (ip == Agent::GetInstance()->GetRouterId() ||
        !IsIp4SubnetMember(ip, Agent::GetInstance()->GetRouterId(),
                           Agent::GetInstance()->GetPrefixLen())) {
        // TODO: add Arp request for GW
        // Currently, default GW Arp is added during init
        return;
    }
    AddArpReq(vrf_name, ip);
}

void Inet4UnicastAgentRouteTable::ReEvaluatePaths(const string &vrf_name, 
                                                 const Ip4Address &addr, 
                                                 uint8_t plen) {
    DBRequest  rt_req;
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(NULL, vrf_name, 
                                                            addr, plen);

    rt_key->sub_op_ = AgentKey::RESYNC;
    rt_req.key.reset(rt_key);
    rt_req.data.reset(NULL);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &rt_req);
}

bool Inet4UnicastRouteEntry::DBEntrySandesh(Sandesh *sresp) const {
    Inet4UcRouteResp *resp = static_cast<Inet4UcRouteResp *>(sresp);

    RouteUcSandeshData data;
    data.set_src_ip(addr_.to_string());
    data.set_src_plen(plen_);
    data.set_src_vrf(vrf()->GetName());
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
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

bool Inet4UnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, Ip4Address addr,
                                            uint8_t plen) const {
    if (addr_ == addr && plen_ == plen) {
        return DBEntrySandesh(sresp);
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh functions
/////////////////////////////////////////////////////////////////////////////

void UnresolvedRoute::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    int count = 0;
    Inet4UcRouteResp *resp = new Inet4UcRouteResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable());
    Inet4UnicastAgentRouteTable::UnresolvedRouteTree::const_iterator it;
    it = rt_table->unresolved_route_begin();
    for (;it != rt_table->unresolved_route_end(); it++) {
        count++;
        const AgentRoute *rt = *it;
        rt->DBEntrySandesh(resp);
        if (count == 1) {
            resp->set_context(context()+"$");
            resp->Response();
            count = 0;
            resp = new Inet4UcRouteResp();
        }
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void Inet4UcRouteReq::HandleRequest() const {
    VrfEntry *vrf = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentInet4UcRtSandesh *sand;
    if (get_src_ip().empty()) {
        sand = new AgentInet4UcRtSandesh(vrf, context());
    } else {
        boost::system::error_code ec;
        Ip4Address src_ip = Ip4Address::from_string(get_src_ip(), ec);
        sand = 
            new AgentInet4UcRtSandesh(vrf, context(), src_ip, 
                                      (uint8_t)get_prefix_len());
    }
    sand->DoSandesh();
}

/////////////////////////////////////////////////////////////////////////////
// Helper functions to enqueue request or process inline
/////////////////////////////////////////////////////////////////////////////

// Request to delete an entry
void 
Inet4UnicastAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                       const Ip4Address &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Inline delete request
void 
Inet4UnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                    const Ip4Address &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void 
Inet4UnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                          const Ip4Address &addr, 
                                          uint8_t plen,
                                          const std::string &dest_vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(agent->GetLocalPeer(), vrf_name,
                                           addr, plen));

    PacketInterfaceKey intf_key(nil_uuid(), agent->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    req.data.reset(data);

    UnicastTableEnqueue(agent, vrf_name, &req);
}

// Create Route with VLAN NH
void 
Inet4UnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const Ip4Address &addr,
                                               uint8_t plen,
                                               const uuid &intf_uuid,
                                               uint16_t tag,
                                               uint32_t label,
                                               const string &dest_vn_name,
                                               const SecurityGroupList &sg_list) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                   sg_list));
    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

// Create Route with VLAN NH
void
Inet4UnicastAgentRouteTable::AddVlanNHRoute(const Peer *peer, 
                                            const string &vm_vrf,
                                            const Ip4Address &addr, 
                                            uint8_t plen,
                                            const uuid &intf_uuid, 
                                            uint16_t tag,
                                            uint32_t label,
                                            const string &dest_vn_name,
                                            const SecurityGroupList &sg_list) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                   sg_list));

    UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void
Inet4UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                                const string &vm_vrf,
                                                const Ip4Address &addr,
                                                uint8_t plen,
                                                const uuid &intf_uuid,
                                                const string &vn_name,
                                                uint32_t label,
                                                const SecurityGroupList &sg_list,
                                                bool force_policy) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label,
                                    VxLanTable::kInvalidvxlan_id, force_policy,
                                    vn_name, InterfaceNHFlags::INET4, sg_list));

    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void 
Inet4UnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer, 
                                             const string &vm_vrf,
                                             const Ip4Address &addr, 
                                             uint8_t plen,
                                             const uuid &intf_uuid,
                                             const string &vn_name,
                                             uint32_t label,
                                             const SecurityGroupList &sg_list,
                                             bool force_policy) {

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label, VxLanTable::kInvalidvxlan_id,
                                    force_policy, vn_name,
                                    InterfaceNHFlags::INET4, sg_list));
    UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void 
Inet4UnicastAgentRouteTable::AddSubnetBroadcastRoute(const Peer *peer, 
                                                     const string &vrf_name,
                                                     const Ip4Address &src_addr,
                                                     const Ip4Address &grp_addr,
                                                     const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vrf_name, grp_addr, 32));

    MulticastRoute *data = new MulticastRoute(src_addr, grp_addr,
                                              vn_name, vrf_name, 0,
                                              Composite::L3COMP);
    req.data.reset(data);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

static void MakeRemoteVmRouteReq(Agent *agent, DBRequest &rt_req,
                                 const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr, uint8_t plen,
                                 const Ip4Address &server_ip,
                                 TunnelType::TypeBmap bmap, uint32_t label,
                                 const string &dest_vn_name,
                                 const SecurityGroupList &sg_list) {
    // Make Tunnel-NH request
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new TunnelNHKey(agent->GetDefaultVrf(),
                                     agent->GetRouterId(), server_ip, false,
                                     TunnelType::ComputeType(bmap)));
    nh_req.data.reset(new TunnelNHData());

    // Make route request pointing to Tunnel-NH created above
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    rt_req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    RemoteVmRoute *rt_data = 
        new RemoteVmRoute(agent->GetDefaultVrf(), server_ip, label,
                          dest_vn_name, bmap, sg_list, nh_req);
    rt_req.data.reset(rt_data);
}

// Create Route for a Remote VM
// Also creates Tunnel-NH for the route
void 
Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                 const string &vm_vrf,
                                                 const Ip4Address &vm_addr,
                                                 uint8_t plen,
                                                 const Ip4Address &server_ip,
                                                 TunnelType::TypeBmap bmap,
                                                 uint32_t label,
                                                 const string &dest_vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req;
    MakeRemoteVmRouteReq(agent, req, peer, vm_vrf, vm_addr, plen, server_ip,
                         bmap, label, dest_vn_name, SecurityGroupList());
    UnicastTableEnqueue(agent, vm_vrf, &req);
}

void 
Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                 const string &vm_vrf,
                                                 const Ip4Address &vm_addr,
                                                 uint8_t plen,
                                                 const Ip4Address &server_ip,
                                                 TunnelType::TypeBmap bmap,
                                                 uint32_t label,
                                                 const string &dest_vn_name,
                                                 const SecurityGroupList &sg) {
    Agent *agent = Agent::GetInstance();
    DBRequest req;
    MakeRemoteVmRouteReq(agent, req, peer, vm_vrf, vm_addr, plen, server_ip,
                         bmap, label, dest_vn_name, sg);
    UnicastTableEnqueue(agent, vm_vrf, &req);
}

void 
Inet4UnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer, 
                                                 const string &vm_vrf,
                                                 const Ip4Address &vm_addr,
                                                 uint8_t plen,
                                                 std::vector<ComponentNHData> 
                                                 comp_nh_list,
                                                 uint32_t label,
                                                 const string &dest_vn_name,
                                                 const SecurityGroupList &sg) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(vm_vrf, vm_addr, plen, false));

    CompositeNH::CreateCompositeNH(vm_vrf, vm_addr, false,
                                   comp_nh_list);
    nh_req.data.reset(new CompositeNHData(comp_nh_list,
                                          CompositeNHData::REPLACE));

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    req.data.reset(new Inet4UnicastEcmpRoute(vm_addr, plen, dest_vn_name,
                                             label, false, vm_vrf, sg, nh_req));
    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

void
Inet4UnicastAgentRouteTable::AddLocalEcmpRoute(const Peer *peer,
                                               const string &vm_vrf,
                                               const Ip4Address &vm_addr,
                                               uint8_t plen,
                                               std::vector<ComponentNHData> 
                                               comp_nh_list,
                                               uint32_t label,
                                               const string &dest_vn_name, 
                                               const SecurityGroupList &sg) {
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(vm_vrf, vm_addr, plen, true));
    nh_req.data.reset(new CompositeNHData(comp_nh_list,
                                          CompositeNHData::REPLACE));

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    req.data.reset(new Inet4UnicastEcmpRoute(vm_addr, plen, dest_vn_name,
                                             label, true, vm_vrf, sg, nh_req));
    UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void 
Inet4UnicastAgentRouteTable::AddArpReq(const string &vrf_name, 
                                       const Ip4Address &ip) {

    Agent *agent = Agent::GetInstance();
    DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new ArpNHKey(vrf_name, ip));
    nh_req.data.reset(new ArpNHData());
    agent->GetNextHopTable()->Enqueue(&nh_req);

    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new Inet4UnicastRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                              vrf_name, ip, 32));
    rt_req.data.reset(new Inet4UnicastArpRoute(vrf_name, ip));
    UnicastTableEnqueue(agent, vrf_name, &rt_req);
}
                                
void 
Inet4UnicastAgentRouteTable::ArpRoute(DBRequest::DBOperation op, 
                                      const Ip4Address &ip, 
                                      const struct ether_addr &mac,
                                      const string &vrf_name, 
                                      const Interface &intf,
                                      bool resolved,
                                      const uint8_t plen) {
    Agent *agent = Agent::GetInstance();
    DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new ArpNHKey(vrf_name, ip));
    ArpNHData *arp_data = new ArpNHData(mac,
               static_cast<InterfaceKey *>(intf.GetDBRequestKey().release()), 
               resolved); 
    nh_req.data.reset(arp_data);

    DBRequest  rt_req(op);
    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                 vrf_name, ip, plen);
    Inet4UnicastArpRoute *data = NULL;

    switch(op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE:
        agent->GetNextHopTable()->Enqueue(&nh_req);
        data = new Inet4UnicastArpRoute(vrf_name, ip);
        break;

    case DBRequest::DB_ENTRY_DELETE: {
        VrfEntry *vrf = agent->GetVrfTable()->FindVrfFromName(vrf_name);
        Inet4UnicastRouteEntry *rt = 
            static_cast<Inet4UnicastRouteEntry *>(vrf->
                          GetInet4UnicastRouteTable()->Find(rt_key));
        assert(resolved==false);
        agent->GetNextHopTable()->Enqueue(&nh_req);

        // If no other route is dependent on this, remove the route; else ignore
        if (rt && rt->IsDependantRouteEmpty() && rt->IsTunnelNHListEmpty()) {
            data = new Inet4UnicastArpRoute(vrf_name, ip);
        } else {
            rt_key->sub_op_ = AgentKey::RESYNC;
        }
        break;
    }

    default:
        assert(0);
    }
 
    rt_req.key.reset(rt_key);
    rt_req.data.reset(data);
    UnicastTableEnqueue(agent, vrf_name, &rt_req);
}

void Inet4UnicastAgentRouteTable::AddResolveRoute(const string &vrf_name, 
                                                  const Ip4Address &ip, 
                                                  const uint8_t plen) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(agent->GetLocalPeer(), vrf_name, ip,
                                           plen));
    req.data.reset(new ResolveRoute());
    UnicastTableEnqueue(agent, vrf_name, &req);
}

// Create Route for a interface NH.
// Used to create interface-nh pointing routes to vhost interfaces
void Inet4UnicastAgentRouteTable::AddInetInterfaceRoute(const Peer *peer,
                                                        const string &vm_vrf,
                                                        const Ip4Address &addr,
                                                        uint8_t plen,
                                                        const string &interface,
                                                        uint32_t label,
     const string &vn_name) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen));

    InetInterfaceKey intf_key(interface);
    InetInterfaceRoute *data = new InetInterfaceRoute
        (intf_key, label, TunnelType::GREType(), vn_name);
    req.data.reset(data);

    UnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

static void AddVHostRecvRouteInternal(DBRequest *req, const Peer *peer,
                                      const string &vrf,
                                      const string &interface,
                                      const Ip4Address &addr, uint8_t plen,
                                      const string &vn_name, bool policy) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new Inet4UnicastRouteKey(peer, vrf, addr, plen));

    InetInterfaceKey intf_key(interface);
    req->data.reset(new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                                    TunnelType::AllType(), policy, vn_name));
}

void Inet4UnicastAgentRouteTable::AddVHostRecvRoute(const Peer *peer,
                                                    const string &vrf,
                                                    const string &interface,
                                                    const Ip4Address &addr,
                                                    uint8_t plen,
                                                    const string &vn_name,
                                                    bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    static_cast<ReceiveRoute *>(req.data.get())->EnableProxyArp();
    UnicastTableProcess(Agent::GetInstance(), vrf, req);
}

void Inet4UnicastAgentRouteTable::AddVHostRecvRouteReq
    (const Peer *peer, const string &vrf, const string &interface,
     const Ip4Address &addr, uint8_t plen, const string &vn_name, bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    static_cast<ReceiveRoute *>(req.data.get())->EnableProxyArp();
    UnicastTableEnqueue(Agent::GetInstance(), vrf, &req);
}

void 
Inet4UnicastAgentRouteTable::AddVHostSubnetRecvRoute(const Peer *peer,
                                                     const string &vrf,
                                                     const string &interface,
                                                     const Ip4Address &addr,
                                                     uint8_t plen,
                                                     const string &vn_name,
                                                     bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    UnicastTableProcess(Agent::GetInstance(), vrf, req);
}

void Inet4UnicastAgentRouteTable::AddDropRoute(const string &vm_vrf,
                                               const Ip4Address &addr,
                                               uint8_t plen) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new Inet4UnicastRouteKey(agent->GetLocalPeer(), vm_vrf,
                                           GetIp4SubnetAddress(addr, plen),
                                           plen));
    req.data.reset(new DropRoute());
    UnicastTableEnqueue(agent, vm_vrf, &req);
}

void Inet4UnicastAgentRouteTable::DelVHostSubnetRecvRoute(const string &vm_vrf,
                                                          const Ip4Address &addr,
                                                          uint8_t plen) {
    DeleteReq(Agent::GetInstance()->GetLocalPeer(), vm_vrf,
              GetIp4SubnetAddress(addr, plen), 32);
}

static void AddGatewayRouteInternal(DBRequest *req, const string &vrf_name,
                                    const Ip4Address &dst_addr, uint8_t plen,
                                    const Ip4Address &gw_ip,
                                    const string &vn_name) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                            vrf_name, dst_addr, plen));
    req->data.reset(new Inet4UnicastGatewayRoute(gw_ip, vrf_name));
}

void Inet4UnicastAgentRouteTable::AddGatewayRoute(const string &vrf_name,
                                                  const Ip4Address &dst_addr,
                                                  uint8_t plen,
                                                  const Ip4Address &gw_ip,
                                                  const string &vn_name) {
    DBRequest req;
    AddGatewayRouteInternal(&req, vrf_name, dst_addr, plen, gw_ip, vn_name);
    UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

void
Inet4UnicastAgentRouteTable::AddGatewayRouteReq(const string &vrf_name,
                                                const Ip4Address &dst_addr,
                                                uint8_t plen,
                                                const Ip4Address &gw_ip,
                                                const string &vn_name) {
    DBRequest req;
    AddGatewayRouteInternal(&req, vrf_name, dst_addr, plen, gw_ip, vn_name);
    UnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}
