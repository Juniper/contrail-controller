/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include "base/task_annotations.h"
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/agent_route.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

RouteEntry *
Inet4UnicastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const 
{
    Inet4UnicastRouteEntry * entry = new Inet4UnicastRouteEntry(vrf, 
                                                                GetAddress(),
                                                                GetPlen(),
                                                                is_multicast); 
    return static_cast<RouteEntry *>(entry);
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindRoute(const string &vrf_name, 
                                       const Ip4Address &ip) {
    VrfEntry *vrf = 
        Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    Inet4UnicastAgentRouteTable *rt_table = 
        static_cast<Inet4UnicastAgentRouteTable *>
        (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    return rt_table->FindLPM(ip);
}

Inet4UnicastRouteEntry *
Inet4UnicastAgentRouteTable::FindLPM(const Ip4Address &ip) {
    Inet4UnicastRouteEntry key(NULL, ip);
    return tree_.LPMFind(&key);
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
              (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
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

    if (GetIpAddress() < a.GetIpAddress()) {
        return -1;
    }

    if (GetIpAddress() > a.GetIpAddress()) {
        return 1;
    }

    if (GetPlen() < a.GetPlen()) {
        return -1;
    }

    if (GetPlen() > a.GetPlen()) {
        return 1;
    }

    return 0;
}

DBEntryBase::KeyPtr Inet4UnicastRouteEntry::GetDBRequestKey() const {
    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                 GetVrfEntry()->GetName(), 
                                 GetIpAddress(), GetPlen());
    return DBEntryBase::KeyPtr(key);
}

void Inet4UnicastRouteEntry::SetKey(const DBRequestKey *key) {
    const Inet4UnicastRouteKey *k = 
        static_cast<const Inet4UnicastRouteKey*>(key);
    SetVrf(AgentRouteTableAPIS::GetInstance()->
           GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
           FindVrfEntry(k->GetVrfName()));
    Ip4Address tmp(k->GetAddress());
    SetAddr(tmp);
    SetPlen(k->GetPlen());
}

DBTableBase *
Inet4UnicastAgentRouteTable::CreateTable(DB *db, const std::string &name) {
    return AgentRouteTableAPIS::GetInstance()->
        CreateInet4UnicastTable(db, name); 
}

bool Inet4UnicastArpRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    ArpNHKey key(vrf_name_, addr_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->
                                GetNextHopTable()->FindActiveEntry(&key));
    path->SetUnresolved(false);
    path->SetDestVnName(Agent::GetInstance()->GetFabricVnName());
    if (path->GetDestVnName() != Agent::GetInstance()->GetFabricVnName()) {
        ret = true;
    }
    ret = true;
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
} 

bool Inet4UnicastGatewayRoute::AddChangePath(AgentPath *path) {
    path->SetVrfName(vrf_name_);
    Inet4UnicastRouteEntry *rt = 
        Inet4UnicastAgentRouteTable::FindRoute(vrf_name_, gw_ip_); 
    if (rt == NULL || rt->GetPlen() == 0) {
        path->SetUnresolved(true);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        path->SetUnresolved(true);
        Inet4UnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_);
    } else {
        path->SetUnresolved(false);
    }

    //Reset to new gateway route, no nexthop for indirect route
    path->SetGatewayIp(gw_ip_);
    path->ResetDependantRoute(rt);
    if (path->GetDestVnName() != Agent::GetInstance()->GetFabricVnName()) {
        path->SetDestVnName(Agent::GetInstance()->GetFabricVnName());
    }

    return true;
}

bool Inet4UnicastEcmpRoute::AddChangePath(AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    Agent::GetInstance()->GetNextHopTable()->Process(nh_req_);
    CompositeNHKey key(vrf_name_, dest_addr_, plen_, local_ecmp_nh_);
    nh = static_cast<NextHop *>(Agent::GetInstance()->GetNextHopTable()->
                                FindActiveEntry(&key));

    assert(nh);
    if (path->GetLabel() != label_) {
        path->SetLabel(label_);
        ret = true;
    }

    SecurityGroupList path_sg_list;
    path_sg_list = path->GetSecurityGroupList();
    if (path_sg_list != sg_list_) {
        path->SetSecurityGroupList(sg_list_);
        ret = true;
    }

    path->SetDestVnName(vn_name_);
    ret = true;
    path->SetUnresolved(false);
    if (path->ChangeNH(nh) == true)
        ret = true;

    return ret;
}

void 
Inet4UnicastAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                       const Ip4Address &addr, uint8_t plen) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(peer, vrf_name, addr, plen);
    req.key.reset(key);
    req.data.reset(NULL);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void 
Inet4UnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                    const Ip4Address &addr, uint8_t plen) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Inet4UnicastRouteKey *key =
        new Inet4UnicastRouteKey(peer, vrf_name, addr, plen);
    req.key.reset(key);
    req.data.reset(NULL);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Process(req);
}



// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void 
Inet4UnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                          const Ip4Address &addr, 
                                          uint8_t plen,
                                          const std::string &dest_vn_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                 vrf_name, addr, plen);
    req.key.reset(key);

    PacketInterfaceKey intf_key(nil_uuid(),
                                Agent::GetInstance()->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
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
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key =
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen);
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    VlanNhRoute *data = new VlanNhRoute(intf_key, tag, label,
                                        dest_vn_name, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
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
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen);
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    VlanNhRoute *data = new VlanNhRoute(intf_key, tag, label,
                                        dest_vn_name, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Process(req);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void Inet4UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                                     const string &vm_vrf,
                                                     const Ip4Address &addr,
                                                     uint8_t plen,
                                                     const uuid &intf_uuid,
                                                     const string &vn_name,
                                                     uint32_t label, 
                                                     bool force_policy) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    SecurityGroupList sg_list;
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                          TunnelType::AllType(),
                                          force_policy, vn_name,
                                          InterfaceNHFlags::INET4, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void Inet4UnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                                  const string &vm_vrf,
                                                  const Ip4Address &addr,
                                                  uint8_t plen,
                                                  const uuid &intf_uuid,
                                                  const string &vn_name,
                                                  uint32_t label, 
                                                  bool force_policy) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key =
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    SecurityGroupList sg_list;
    LocalVmRoute *data = new LocalVmRoute(intf_key, label, 
                                          TunnelType::AllType(),
                                          force_policy, vn_name,
                                          InterfaceNHFlags::INET4, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Process(req);
}

void
Inet4UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                                const string &vm_vrf,
                                                const Ip4Address &addr,
                                                uint8_t plen,
                                                const uuid &intf_uuid,
                                                const string &vn_name,
                                                uint32_t label,
                                                const SecurityGroupList &sg_list) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key =
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, label, 
                                          TunnelType::AllType(),
                                          false, vn_name,
                                          InterfaceNHFlags::INET4, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void 
Inet4UnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer, 
                                             const string &vm_vrf,
                                             const Ip4Address &addr, 
                                             uint8_t plen,
                                             const uuid &intf_uuid,
                                             const string &vn_name,
                                             uint32_t label,
                                             const SecurityGroupList &sg_list) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmInterfaceKey intf_key(intf_uuid, "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, label, 
                                          TunnelType::AllType(),
                                          false, vn_name,
                                          InterfaceNHFlags::INET4, sg_list);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Process(req);
}

void 
Inet4UnicastAgentRouteTable::AddSubnetBroadcastRoute(const Peer *peer, 
                                                     const string &vrf_name,
                                                     const Ip4Address &src_addr,
                                                     const Ip4Address &grp_addr,
                                                     const string &vn_name)
{
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = new Inet4UnicastRouteKey(peer, vrf_name, 
                                                         grp_addr, 32); 
    req.key.reset(key);

    MulticastRoute *data = new MulticastRoute(src_addr, grp_addr,
                                              vn_name, vrf_name, 0,
                                              Composite::L3COMP);
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

// Create Route for a local VM. Policy is disabled
void 
Inet4UnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer, 
                                                const string &vm_vrf,
                                                const Ip4Address &addr, 
                                                uint8_t plen,
                                                const uuid &intf_uuid, 
                                                const string &vn_name,
                                                uint32_t label) {
    AddLocalVmRouteReq(peer, vm_vrf, addr, plen, intf_uuid, vn_name, 
                       label, false);
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
    DBRequest nh_req;
    // First enqueue request to add/change Interface NH
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *nh_key =
        new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                        Agent::GetInstance()->GetRouterId(), server_ip,
                        false, TunnelType::ComputeType(bmap));
    nh_req.key.reset(nh_key);

    TunnelNHData *nh_data = new TunnelNHData();
    nh_req.data.reset(nh_data);

    DBRequest req;
    // Enqueue request for route pointing to Interface NH created above
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, vm_addr, plen);
    req.key.reset(rt_key);

    SecurityGroupList sg_list;
    RemoteVmRoute *rt_data = 
        new RemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(),
                          server_ip, label, 
                          dest_vn_name, bmap, sg_list, nh_req);
    req.data.reset(rt_data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
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
                                                 const SecurityGroupList &sg_list) 
{
    DBRequest nh_req;
    // First enqueue request to add/change Interface NH
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *nh_key =
        new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                        Agent::GetInstance()->GetRouterId(), server_ip,
                        false, TunnelType::ComputeType(bmap));
    nh_req.key.reset(nh_key);

    TunnelNHData *nh_data = new TunnelNHData();
    nh_req.data.reset(nh_data);

    DBRequest req;
    // Enqueue request for route pointing to Interface NH created above
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, 
                                 vm_addr, plen);
    req.key.reset(rt_key);

    RemoteVmRoute *rt_data = 
        new RemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(),
                          server_ip, label, 
                          dest_vn_name, bmap, sg_list, nh_req);
    req.data.reset(rt_data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
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
    DBRequest nh_req;
    NextHopKey *key;
    CompositeNHData *data;

    CompositeNH::CreateCompositeNH(vm_vrf, vm_addr, false,
                                   comp_nh_list);
    key = new CompositeNHKey(vm_vrf, vm_addr, plen, false);
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(key);
    data = new CompositeNHData(comp_nh_list, CompositeNHData::REPLACE);
    nh_req.data.reset(data);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(peer, vm_vrf, 
                                                  vm_addr, plen);
    req.key.reset(rt_key);
    Inet4UnicastEcmpRoute *rt_data = new Inet4UnicastEcmpRoute(vm_addr, plen,
                                                               dest_vn_name, 
                                                               label, 
                                                               false,
                                                               vm_vrf, sg, 
                                                               nh_req);
    req.data.reset(rt_data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
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
    DBRequest nh_req;
    NextHopKey *key;
    CompositeNHData *data;

    key = new CompositeNHKey(vm_vrf, vm_addr, plen, true);
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    nh_req.key.reset(key);
    data = new CompositeNHData(comp_nh_list, CompositeNHData::REPLACE);
    nh_req.data.reset(data);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(peer, vm_vrf,
                                                  vm_addr, plen);
    req.key.reset(rt_key);
    Inet4UnicastEcmpRoute *rt_data = new Inet4UnicastEcmpRoute(vm_addr, plen,
                                                               dest_vn_name,
                                                               label,
                                                               true,
                                                               vm_vrf, sg, 
                                                               nh_req);
    req.data.reset(rt_data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Process(req);
}

void 
Inet4UnicastAgentRouteTable::AddArpReq(const string &vrf_name, 
                                       const Ip4Address &ip) {

    DBRequest  nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new ArpNHKey(vrf_name, ip);
    nh_req.key.reset(key);

    ArpNHData *arp_data = new ArpNHData();
    nh_req.data.reset(arp_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);

    DBRequest  rt_req;
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                 vrf_name, ip, 32);
    rt_req.key.reset(rt_key);
    Inet4UnicastArpRoute *data = new Inet4UnicastArpRoute(vrf_name, ip);
    rt_req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&rt_req);
}
                                
void 
Inet4UnicastAgentRouteTable::ArpRoute(DBRequest::DBOperation op, 
                                      const Ip4Address &ip, 
                                      const struct ether_addr &mac,
                                      const string &vrf_name, 
                                      const Interface &intf,
                                      bool resolved,
                                      const uint8_t plen) {
    DBRequest  nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    NextHopKey *key = new ArpNHKey(vrf_name, ip);
    nh_req.key.reset(key);
    ArpNHData *arp_data = new ArpNHData(mac,
               static_cast<InterfaceKey *>(intf.GetDBRequestKey().release()), 
               resolved); 
    nh_req.data.reset(arp_data);

    DBRequest  rt_req;
    rt_req.oper = op;
    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                 vrf_name, ip, plen);
    Inet4UnicastArpRoute *data = NULL;

    switch(op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE:
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);
        data = new Inet4UnicastArpRoute(vrf_name, ip);
        break;

    case DBRequest::DB_ENTRY_DELETE: {
        VrfEntry *vrf = 
            Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
        Inet4UnicastRouteEntry *rt = 
            static_cast<Inet4UnicastRouteEntry *>(vrf->
                          GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->
                          Find(rt_key));
        assert(resolved==false);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);

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
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&rt_req);
}

void Inet4UnicastAgentRouteTable::AddResolveRoute(const string &vrf_name, 
                                                  const Ip4Address &ip, 
                                                  const uint8_t plen) {
    DBRequest  req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                 vrf_name, ip, plen);
    req.key.reset(rt_key);
    ResolveRoute *data = new ResolveRoute();
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

// Create Route for a interface NH.
// Used to create interface-nh pointing routes to vhost interfaces
void Inet4UnicastAgentRouteTable::AddVHostInterfaceRoute
    (const Peer *peer, const string &vm_vrf, const Ip4Address &addr,
     uint8_t plen, const string &interface, uint32_t label,
     const string &vn_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *key = 
        new Inet4UnicastRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VirtualHostInterfaceKey intf_key(interface);
    VirtualHostInterfaceRoute *data = new VirtualHostInterfaceRoute
        (intf_key, label, TunnelType::AllType(), vn_name);
    req.data.reset(data);

    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

// Create Route for Vhost interface
void 
Inet4UnicastAgentRouteTable::AddVHostRecvRoute(const Peer *peer,
                                               const string &vm_vrf,
                                               const string &interface_name,
                                               const Ip4Address &addr,
                                               uint8_t plen,
                                               const string &vn,
                                               bool policy) {
    ReceiveNH::CreateReq(interface_name);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(peer, 
                                              vm_vrf, addr, plen);
    req.key.reset(rt_key);
    VirtualHostInterfaceKey intf_key(interface_name);
    ReceiveRoute *data = new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                                          TunnelType::AllType(),
                                          policy, vn);
    data->EnableProxyArp();
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void 
Inet4UnicastAgentRouteTable::AddVHostRecvRoute(const string &vm_vrf,
                                               const string &interface_name,
                                               const Ip4Address &addr,
                                               bool policy) {
    Inet4UnicastAgentRouteTable::AddVHostRecvRoute(Agent::GetInstance()->
                                                   GetLocalPeer(), 
                                                   vm_vrf, interface_name, addr,
                                                   32, Agent::GetInstance()->
                                                   GetFabricVnName(), 
                                                   policy);
}

void 
Inet4UnicastAgentRouteTable::AddVHostSubnetRecvRoute(const string &vm_vrf, 
                                                     const string &interface_name,
                                                     const Ip4Address &addr, 
                                                     uint8_t plen, 
                                                     bool policy) {
    Ip4Address subnet_addr(addr.to_ulong() | 
                             ~(0xFFFFFFFF << (32 - plen)));
    ReceiveNH::CreateReq(interface_name);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                 vm_vrf, subnet_addr, 32);
    req.key.reset(rt_key);
    VirtualHostInterfaceKey intf_key(interface_name);
    ReceiveRoute *data =
            new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                             TunnelType::AllType(), policy,
                             Agent::GetInstance()->GetFabricVnName());
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void Inet4UnicastAgentRouteTable::AddDropRoute(const string &vm_vrf,
                                     const Ip4Address &addr, uint8_t plen) {
    Ip4Address subnet_addr(addr.to_ulong() | ~(0xFFFFFFFF << (32 - plen)));
    Inet4UnicastRouteKey *rt_key = 
      new Inet4UnicastRouteKey(Agent::GetInstance()->GetLocalPeer(),
                               vm_vrf, subnet_addr, plen);
    DropRoute *data = new DropRoute();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(rt_key);
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void Inet4UnicastAgentRouteTable::DelVHostSubnetRecvRoute(
    const string &vm_vrf, const Ip4Address &addr, uint8_t plen) {
    Ip4Address subnet_addr(addr.to_ulong() | ~(0xFFFFFFFF << (32 - plen)));
    DeleteReq(Agent::GetInstance()->GetLocalPeer(), vm_vrf, subnet_addr, 32);
}

void 
Inet4UnicastAgentRouteTable::AddGatewayRoute(const Peer *peer, 
                                             const string &vrf_name,
                                             const Ip4Address &dst_addr,
                                             uint8_t plen,
                                             const Ip4Address &gw_ip) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UnicastRouteKey *rt_key = 
        new Inet4UnicastRouteKey(peer, vrf_name, dst_addr, plen);
    Inet4UnicastGatewayRoute *data = 
        new Inet4UnicastGatewayRoute(gw_ip, vrf_name);

    req.key.reset(rt_key);
    req.data.reset(data);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&req);
}

void Inet4UnicastAgentRouteTable::RouteResyncReq(const string &vrf_name, 
                                                 const Ip4Address &addr, 
                                                 uint8_t plen) {
    DBRequest  rt_req;
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UnicastRouteKey *rt_key = new Inet4UnicastRouteKey(NULL, vrf_name, 
                                                            addr, plen);

    rt_key->sub_op_ = AgentKey::RESYNC;
    rt_req.key.reset(rt_key);
    rt_req.data.reset(NULL);
    AgentRouteTableAPIS::GetInstance()->
        GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)->Enqueue(&rt_req);

}

void Inet4UnicastRouteEntry::RouteResyncReq() const {
    Inet4UnicastAgentRouteTable::RouteResyncReq(GetVrfEntry()->GetName(), 
                                                addr_, plen_); 
}

void UnresolvedRoute::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    int count = 0;
    Inet4UnicastAgentRouteTable::const_rt_iterator it;
    Inet4UcRouteResp *resp = new Inet4UcRouteResp();

    //TODO - Convert inet4ucroutetable to agentroutetable
    AgentRouteTable *rt_table = static_cast<AgentRouteTable *>
        (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST));
    it = rt_table->unresolved_route_begin();
    for (;it != rt_table->unresolved_route_end(); it++) {
        count++;
        const RouteEntry *rt = *it;
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

bool Inet4UnicastRouteEntry::DBEntrySandesh(Sandesh *sresp) const {
    Inet4UcRouteResp *resp = static_cast<Inet4UcRouteResp *>(sresp);

    RouteUcSandeshData data;
    data.set_src_ip(GetIpAddress().to_string());
    data.set_src_plen(GetPlen());
    data.set_src_vrf(GetVrfEntry()->GetName());
    for (Route::PathList::const_iterator it = GetPathList().begin();
         it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            PathSandeshData pdata;
            path->GetNextHop()->SetNHSandeshData(pdata.nh);
            pdata.set_label(path->GetLabel());
            pdata.set_peer(const_cast<Peer *>(path->GetPeer())->GetName());
            pdata.set_dest_vn(path->GetDestVnName());
            pdata.set_unresolved(path->IsUnresolved() ? "true" : "false");
            if (!path->GetGatewayIp().is_unspecified()) {
                pdata.set_gw_ip(path->GetGatewayIp().to_string());
                pdata.set_vrf(path->GetVrfName());
            }
            if (path->GetProxyArp()) {
                pdata.set_proxy_arp("ProxyArp");
            } else {
                pdata.set_proxy_arp("NoProxyArp");
            }
            pdata.set_sg_list(path->GetSecurityGroupList());
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
    if (GetIpAddress() == addr && GetPlen() == plen) {
        return DBEntrySandesh(sresp);
    }

    return false;
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
