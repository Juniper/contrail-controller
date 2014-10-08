/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include "base/task_annotations.h"
#include <boost/foreach.hpp>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/vxlan.h>
#include <oper/mirror_table.h>
#include <oper/multicast.h>
#include <controller/controller_export.h>
#include <controller/controller_peer.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

AgentRoute *
InetUnicastRouteKey::AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const {
    InetUnicastRouteEntry * entry =
        new InetUnicastRouteEntry(vrf, dip_, plen_, is_multicast);
    return static_cast<AgentRoute *>(entry);
}

AgentRouteKey *InetUnicastRouteKey::Clone() const {
    return (new InetUnicastRouteKey(peer_, vrf_name_, dip_, plen_));
}

/////////////////////////////////////////////////////////////////////////////
// InetUnicastAgentRouteTable functions
/////////////////////////////////////////////////////////////////////////////
InetUnicastAgentRouteTable::InetUnicastAgentRouteTable(DB *db,
                                                       const std::string &name) :
    AgentRouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {

    if (name.find("uc.route.0") != std::string::npos) {
        type_ = Agent::INET4_UNICAST;
    } else if (name.find("uc.route6.0") != std::string::npos) {
        type_ = Agent::INET6_UNICAST;
    } else if (name.find("l2.route.0") != std::string::npos) {
        type_ = Agent::LAYER2;;
    } else if (name.find("mc.route.0") != std::string::npos) {
        type_ = Agent::INET4_MULTICAST;
    } else {
        type_ = Agent::INVALID;
    }
}

DBTableBase *
InetUnicastAgentRouteTable::CreateTable(DB *db, const std::string &name) {
    AgentRouteTable *table = new InetUnicastAgentRouteTable(db, name);
    table->Init();
    return table;
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindLPM(const IpAddress &ip) {
    uint32_t plen = 128;
    if (ip.is_v4()) {
        plen = 32;
    }
    InetUnicastRouteEntry key(NULL, ip, plen, false);
    return tree_.LPMFind(&key);
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindLPM(const InetUnicastRouteEntry &rt_key) {
    return tree_.LPMFind(&rt_key);
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindResolveRoute(const Ip4Address &ip) {
    uint8_t plen = 32;
    InetUnicastRouteEntry *rt = NULL;
    do {
        InetUnicastRouteEntry key(NULL, ip, plen, false);
        rt = tree_.LPMFind(&key);
        if (rt) {
            const NextHop *nh = rt->GetActiveNextHop();
            if (nh && nh->GetType() == NextHop::RESOLVE)
                return rt;
        }
    } while (rt && --plen);

    return NULL;
}

InetUnicastRouteEntry *
InetUnicastAgentRouteTable::FindResolveRoute(const string &vrf_name,
                                             const Ip4Address &ip) {
    VrfEntry *vrf =
        Agent::GetInstance()->vrf_table()->FindVrfFromName(vrf_name);
    InetUnicastAgentRouteTable *rt_table =
              static_cast<InetUnicastAgentRouteTable *>
              (vrf->GetInet4UnicastRouteTable());
    return rt_table->FindResolveRoute(ip);
}

static void Inet4UnicastTableEnqueue(Agent *agent, DBRequest *req) {
    AgentRouteTable *table = agent->fabric_inet4_unicast_table();
    if (table) {
        table->Enqueue(req);
    }
}

static void Inet6UnicastTableEnqueue(Agent *agent, const string &vrf_name,
                                     DBRequest *req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    if (table) {
        table->Enqueue(req);
    }
}

static void InetUnicastTableEnqueue(Agent *agent, const string &vrf,
                                    DBRequest *req) {
    InetUnicastRouteKey *key = static_cast<InetUnicastRouteKey *>(req->key.get());
    if (key->addr().is_v4()) {
        Inet4UnicastTableEnqueue(agent, req);
    } else if (key->addr().is_v6()) {
        Inet6UnicastTableEnqueue(agent, vrf, req);
    }
}

static void Inet4UnicastTableProcess(Agent *agent, const string &vrf_name,
                                     DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

static void Inet6UnicastTableProcess(Agent *agent, const string &vrf_name,
                                     DBRequest &req) {
    AgentRouteTable *table =
        agent->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    if (table) {
        table->Process(req);
    }
}

static void InetUnicastTableProcess(Agent *agent, const string &vrf_name,
                                    DBRequest &req) {
    InetUnicastRouteKey *key = static_cast<InetUnicastRouteKey *>(req.key.get());
    if (key->addr().is_v4()) {
        Inet4UnicastTableProcess(agent, vrf_name, req);
    } else if (key->addr().is_v6()) {
        Inet6UnicastTableProcess(agent, vrf_name, req);
    }
}

void InetUnicastAgentRouteTable::ReEvaluatePaths(const string &vrf_name,
                                                 const IpAddress &addr,
                                                 uint8_t plen) {
    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    InetUnicastRouteKey *rt_key = new InetUnicastRouteKey(NULL, vrf_name,
                                                            addr, plen);

    rt_key->sub_op_ = AgentKey::RESYNC;
    rt_req.key.reset(rt_key);
    rt_req.data.reset(NULL);
    InetUnicastTableEnqueue(Agent::GetInstance(), vrf_name, &rt_req);
}

/////////////////////////////////////////////////////////////////////////////
// Inet4UnicastAgentRouteEntry functions
/////////////////////////////////////////////////////////////////////////////
InetUnicastRouteEntry::InetUnicastRouteEntry(VrfEntry *vrf,
                                             const IpAddress &addr,
                                             uint8_t plen,
                                             bool is_multicast) :
    AgentRoute(vrf, is_multicast), plen_(plen) {
    if (addr.is_v4()) {
        addr_ = GetIp4SubnetAddress(addr.to_v4(), plen);
    } else {
        addr_ = GetIp6SubnetAddress(addr.to_v6(), plen);
    }
}

string InetUnicastRouteKey::ToString() const {
    ostringstream str;
    str << dip_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

string InetUnicastRouteEntry::ToString() const {
    ostringstream str;
    str << addr_.to_string();
    str << "/";
    str << (int)plen_;
    return str.str();
}

int InetUnicastRouteEntry::CompareTo(const Route &rhs) const {
    const InetUnicastRouteEntry &a =
        static_cast<const InetUnicastRouteEntry &>(rhs);

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

DBEntryBase::KeyPtr InetUnicastRouteEntry::GetDBRequestKey() const {
    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *>(get_table()))->agent();
    InetUnicastRouteKey *key =
        new InetUnicastRouteKey(agent->local_peer(),
                                 vrf()->GetName(), addr_, plen_);
    return DBEntryBase::KeyPtr(key);
}

void InetUnicastRouteEntry::SetKey(const DBRequestKey *key) {
    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *>(get_table()))->agent();
    const InetUnicastRouteKey *k = 
        static_cast<const InetUnicastRouteKey*>(key);
    SetVrf(agent->vrf_table()->FindVrfFromName(k->vrf_name()));
    IpAddress tmp(k->addr());
    set_addr(tmp);
    set_plen(k->plen());
}

bool InetUnicastRouteEntry::ModifyEcmpPath(const IpAddress &dest_addr,
                                            uint8_t plen, const string &vn_name,
                                            uint32_t label, bool local_ecmp_nh,
                                            const string &vrf_name,
                                            SecurityGroupList sg_list,
                                            const PathPreference &path_preference,
                                            DBRequest &nh_req,
                                            Agent* agent, AgentPath *path) {
    bool ret = false;
    NextHop *nh = NULL;

    agent->nexthop_table()->Process(nh_req);
    nh = static_cast<NextHop *>(agent->nexthop_table()->
                                FindActiveEntry(nh_req.key.get()));
    assert(nh);
    if (path->label() != label) {
        path->set_label(label);
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
    if (path_sg_list != sg_list) {
        path->set_sg_list(sg_list);
        ret = true;
    }

    if (path_preference != path->path_preference()) {
        path->set_path_preference(path_preference);
        ret = true;
    }

    path->set_dest_vn_name(vn_name);
    ret = true;
    path->set_unresolved(false);
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
}

// Function to create a ECMP path from path1 and path2
// Creates Composite-NH with 2 Component-NH (one for each of path1 and path2)
// Creates a new MPLS Label for the ECMP path
AgentPath *InetUnicastRouteEntry::AllocateEcmpPath(Agent *agent,
                                                   const AgentPath *path1,
                                                   const AgentPath *path2) {
    // Allocate and insert a path
    AgentPath *path = new AgentPath(agent->ecmp_peer(), NULL);
    InsertPath(path);

    // Allocate a new label for the ECMP path
    uint32_t label = agent->mpls_table()->AllocLabel();

    // Create Component NH to be added to ECMP path
    DBEntryBase::KeyPtr key1 = path1->nexthop(agent)->GetDBRequestKey();
    NextHopKey *nh_key1 = static_cast<NextHopKey *>(key1.release());
    std::auto_ptr<const NextHopKey> nh_akey1(nh_key1);
    nh_key1->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data1(new ComponentNHKey(path1->label(),
                                                            nh_akey1));

    DBEntryBase::KeyPtr key2 = path2->nexthop(agent)->GetDBRequestKey();
    NextHopKey *nh_key2 = static_cast<NextHopKey *>(key2.release());
    std::auto_ptr<const NextHopKey> nh_akey2(nh_key2);
    nh_key2->SetPolicy(false);
    ComponentNHKeyPtr component_nh_data2(new ComponentNHKey(path2->label(),
                                                            nh_akey2));

    ComponentNHKeyList component_nh_list;
    component_nh_list.push_back(component_nh_data1);
    component_nh_list.push_back(component_nh_data2);

    // Directly call AddChangePath to update NH in the ECMP path
    // It will also create CompositeNH if necessary
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        true, component_nh_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_, path2->dest_vn_name(),
                                           label, true, vrf()->GetName(),
                                           path2->sg_list(),
                                           path2->path_preference(), nh_req,
                                           agent, path);

    //Make MPLS label point to Composite NH
    MplsLabel::CreateEcmpLabel(label, Composite::LOCAL_ECMP, component_nh_list,
                               vrf()->GetName());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    OPER_TRACE(Route, rt_info);
    AGENT_ROUTE_LOG("Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));

    return path;
}

// Handle deletion of a path in route. If the path being deleted is part of
// ECMP, then deletes the Component-NH for the path.
// Delete ECMP path if there is single Component-NH in Composite-NH
bool InetUnicastRouteEntry::EcmpDeletePath(AgentPath *path) {
    if (path->peer() == NULL) {
        return false;
    }

    if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER) {
        return false;
    }

    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    // Composite-NH is made from LOCAL_VM_PORT_PEER, count number of paths
    // with LOCAL_VM_PORT_PEER
    int count = 0;
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *it_path =
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER &&
            it_path != path)
            count++;
    }

    AgentPath *ecmp = NULL;
    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count >= 1) {
        ecmp = FindPath(agent->ecmp_peer());
        if (ecmp == NULL) {
            return false;
        }
    }

    if (count == 1 && ecmp) {
        // There is single path of type LOCAL_VM_PORT_PEER. Delete the ECMP path
        remove(ecmp);
        //Enqueue MPLS label delete request
        MplsLabel::Delete(ecmp->label());
        delete ecmp;
    } else if (count > 1) {
        // Remove Component-NH for the path being deleted
        DeleteComponentNH(agent, path);
    }

    return true;
}

bool InetUnicastRouteEntry::ReComputePathAdd(AgentPath *path) {
    // ECMP path are managed by route module. Update ECMP path with
    // addition of new path
    return EcmpAddPath(path);
}

bool InetUnicastRouteEntry::ReComputePathDeletion(AgentPath *path) {
    // ECMP path are managed by route module. Update ECMP path with
    // deletion of new path
    return EcmpDeletePath(path);
}

// Handle add/update of a path in route. 
// If there are more than one path of type LOCAL_VM_PORT_PEER, creates/updates
// Composite-NH for them
bool InetUnicastRouteEntry::EcmpAddPath(AgentPath *path) {

    if (path->peer() == NULL) {
        return false;
    }

    // We are interested only in path from LOCAL_VM_PORT_PEER
    if (path->peer()->GetType() != Peer::LOCAL_VM_PORT_PEER ||
        path->path_preference().ecmp() == false) {
        return false;
    }

    Agent *agent = 
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    // Count number of paths from LOCAL_VM_PORT_PEER already present
    const AgentPath *ecmp = NULL;
    const AgentPath *vm_port_path = NULL;
    int count = 0;
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *it_path = 
            static_cast<const AgentPath *>(it.operator->());

        if (it_path->peer() == agent->ecmp_peer())
            ecmp = it_path;

        if (it_path->peer() &&
            it_path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
            count++;
            if (it_path != path)
                vm_port_path = it_path;
        }
    }

    if (count == 0) {
        return false;
    }

    // Sanity check. When more than one LOCAL_VM_PORT_PEER, ECMP must be present
    if (count > 2) {
        assert(ecmp != NULL);
    }

    if (count == 1) {
        assert(ecmp == NULL);
        return false;
    }

    if (count == 2 && ecmp == NULL) {
        // This is second path being added, make ECMP 
        AllocateEcmpPath(agent, vm_port_path, path);
    } else if (count > 2) {
        // ECMP already present, add/update Component-NH for the path
        AppendEcmpPath(agent, path);
    }
    return true;
}

void InetUnicastRouteEntry::AppendEcmpPath(Agent *agent, AgentPath *path) {
    AgentPath *ecmp_path = FindPath(agent->ecmp_peer());
    assert(ecmp_path);

    DBEntryBase::KeyPtr key = path->nexthop(agent)->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path->nexthop(agent));
    component_nh_key_list = comp_nh->AddComponentNHKey(comp_nh_key_ptr);

    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        true, component_nh_key_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_, path->dest_vn_name(),
                               ecmp_path->label(), true, vrf()->GetName(),
                               path->sg_list(), path->path_preference(),
                               nh_req, agent, ecmp_path);

    //Make MPLS label point to composite NH
    MplsLabel::CreateEcmpLabel(ecmp_path->label(), Composite::LOCAL_ECMP,
                               component_nh_key_list, vrf()->GetName());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    OPER_TRACE(Route, rt_info);
    AGENT_ROUTE_LOG("Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));
}

void InetUnicastRouteEntry::DeleteComponentNH(Agent *agent, AgentPath *path) {
    AgentPath *ecmp_path = FindPath(agent->ecmp_peer());

    assert(ecmp_path);
    DBEntryBase::KeyPtr key = path->nexthop(agent)->GetDBRequestKey();
    NextHopKey *nh_key = static_cast<NextHopKey *>(key.release());
    std::auto_ptr<const NextHopKey> nh_akey(nh_key);
    nh_key->SetPolicy(false);
    ComponentNHKeyPtr comp_nh_key_ptr(new ComponentNHKey(path->label(), nh_akey));

    ComponentNHKeyList component_nh_key_list;
    const CompositeNH *comp_nh =
        static_cast<const CompositeNH *>(ecmp_path->nexthop(agent));
    component_nh_key_list = comp_nh->DeleteComponentNHKey(comp_nh_key_ptr);

    // Form the request for Inet4UnicastEcmpRoute and invoke AddChangePath
    DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new CompositeNHKey(Composite::LOCAL_ECMP,
                                        true, component_nh_key_list,
                                        vrf()->GetName()));
    nh_req.data.reset(new CompositeNHData());

    InetUnicastRouteEntry::ModifyEcmpPath(addr_, plen_,
                               ecmp_path->dest_vn_name(),
                               ecmp_path->label(), true, vrf()->GetName(),
                               ecmp_path->sg_list(), ecmp_path->path_preference(),
                               nh_req, agent, ecmp_path);

    //Make MPLS label point to composite NH
    MplsLabel::CreateEcmpLabel(ecmp_path->label(), Composite::LOCAL_ECMP,
                               component_nh_key_list, vrf()->GetName());

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
    OPER_TRACE(Route, rt_info);
    AGENT_ROUTE_LOG("Path change", ToString(), vrf()->GetName(),
                    GETPEERNAME(agent->ecmp_peer()));
}

const NextHop* InetUnicastRouteEntry::GetLocalNextHop() const {
    Agent *agent =
        (static_cast<InetUnicastAgentRouteTable *> (get_table()))->agent();

    if (FindPath(agent->ecmp_peer())) {
        return FindPath(agent->ecmp_peer())->nexthop(agent);
    }
   
    //If a route is leaked, and it points to local composite nexthop
    //then choose that
    if (GetActivePath()->local_ecmp_mpls_label()) {
        return GetActivePath()->local_ecmp_mpls_label()->nexthop();
    }

    //Choose the first local vm peer path
    for (Route::PathList::const_iterator it = GetPathList().begin();
            it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path) {
            if (path->peer() && 
                path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER) {
                return path->nexthop(agent);
            }
        }
    }

    const NextHop *nh = GetActiveNextHop();
    if (nh->GetType() == NextHop::COMPOSITE) {
        const CompositeNH *comp_nh = static_cast<const CompositeNH *>(nh);
        //Get the local composite NH
        return comp_nh->GetLocalNextHop();
    }
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////
// AgentRouteData virtual functions
/////////////////////////////////////////////////////////////////////////////

bool Inet4UnicastArpRoute::AddChangePath(Agent *agent, AgentPath *path) {
    bool ret = false;

    ArpNHKey key(vrf_name_, addr_);
    NextHop *nh = 
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key));
    path->set_unresolved(false);
    path->set_dest_vn_name(agent->fabric_vn_name());
    if (path->dest_vn_name() != agent->fabric_vn_name()) {
        ret = true;
    }
    ret = true;
    if (path->ChangeNH(agent, nh) == true)
        ret = true;

    return ret;
} 

bool Inet4UnicastGatewayRoute::AddChangePath(Agent *agent, AgentPath *path) {
    path->set_vrf_name(vrf_name_);

    InetUnicastAgentRouteTable *table = NULL;
    table = static_cast<InetUnicastAgentRouteTable *>
        (agent->vrf_table()->GetInet4UnicastRouteTable(vrf_name_));
    InetUnicastRouteEntry *rt = table->FindRoute(gw_ip_); 
    if (rt == NULL || rt->plen() == 0) {
        path->set_unresolved(true);
    } else if (rt->GetActiveNextHop()->GetType() == NextHop::RESOLVE) {
        path->set_unresolved(true);
        InetUnicastAgentRouteTable::AddArpReq(vrf_name_, gw_ip_);
    } else {
        path->set_unresolved(false);
    }

    //Reset to new gateway route, no nexthop for indirect route
    path->set_gw_ip(gw_ip_);
    path->ResetDependantRoute(rt);
    if (path->dest_vn_name() != agent->fabric_vn_name()) {
        path->set_dest_vn_name(agent->fabric_vn_name());
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh functions
/////////////////////////////////////////////////////////////////////////////

bool InetUnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, bool stale) const {

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

    if (addr_.is_v4()) {
        Inet4UcRouteResp *v4_resp = static_cast<Inet4UcRouteResp *>(sresp);
        std::vector<RouteUcSandeshData> &list =
        const_cast<std::vector<RouteUcSandeshData>&>(v4_resp->get_route_list());
        list.push_back(data);
    } else {
        Inet6UcRouteResp *v6_resp = static_cast<Inet6UcRouteResp *>(sresp);
        std::vector<RouteUcSandeshData> &list =
        const_cast<std::vector<RouteUcSandeshData>&>(v6_resp->get_route_list());
        list.push_back(data);
    }
    return true;
}

bool InetUnicastRouteEntry::DBEntrySandesh(Sandesh *sresp, IpAddress addr,
                                            uint8_t plen, bool stale) const {
    if (addr_ == addr && plen_ == plen) {
        return DBEntrySandesh(sresp, stale);
    }

    return false;
}

void UnresolvedRoute::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->vrf_table()->FindVrfFromId(0);
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
    InetUnicastAgentRouteTable::UnresolvedRouteTree::const_iterator it;
    it = rt_table->unresolved_route_begin();
    for (;it != rt_table->unresolved_route_end(); it++) {
        count++;
        const AgentRoute *rt = *it;
        rt->DBEntrySandesh(resp, false);
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
        Agent::GetInstance()->vrf_table()->FindVrfFromId(get_vrf_index());
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }

    AgentInet4UcRtSandesh *sand;
    if (get_src_ip().empty()) {
        sand = new AgentInet4UcRtSandesh(vrf, context(), get_stale());
    } else {
        boost::system::error_code ec;
        Ip4Address src_ip = Ip4Address::from_string(get_src_ip(), ec);
        sand = 
            new AgentInet4UcRtSandesh(vrf, context(), src_ip, 
                                      (uint8_t)get_prefix_len(), get_stale());
    }
    sand->DoSandesh();
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
InetUnicastAgentRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                      const IpAddress &addr, uint8_t plen,
                                      AgentRouteData *data) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Inline delete request
void 
InetUnicastAgentRouteTable::Delete(const Peer *peer, const string &vrf_name,
                                   const IpAddress &addr, uint8_t plen) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new InetUnicastRouteKey(peer, vrf_name, addr, plen));
    req.data.reset(NULL);
    InetUnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void 
InetUnicastAgentRouteTable::AddHostRoute(const string &vrf_name,
                                         const IpAddress &addr,
                                         uint8_t plen,
                                         const std::string &dest_vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vrf_name,
                                           addr, plen));

    PacketInterfaceKey intf_key(nil_uuid(), agent->GetHostInterfaceName());
    HostRoute *data = new HostRoute(intf_key, dest_vn_name);
    req.data.reset(data);

    InetUnicastTableEnqueue(Agent::GetInstance(), vrf_name, &req);
}

// Create Route with VLAN NH
void 
InetUnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                              const string &vm_vrf,
                                              const Ip4Address &addr,
                                              uint8_t plen,
                                              VlanNhRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));
    req.data.reset(data);
    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

void
InetUnicastAgentRouteTable::AddVlanNHRouteReq(const Peer *peer,
                                              const string &vm_vrf,
                                              const Ip4Address &addr,
                                              uint8_t plen,
                                              const uuid &intf_uuid,
                                              uint16_t tag,
                                              uint32_t label,
                                              const string &dest_vn_name,
                                              const SecurityGroupList &sg_list,
                                              const PathPreference
                                              &path_preference) {
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    VlanNhRoute *data = new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                        sg_list, path_preference);
    AddVlanNHRouteReq(peer, vm_vrf, addr, plen, data);
}

// Create Route with VLAN NH
void
InetUnicastAgentRouteTable::AddVlanNHRoute(const Peer *peer,
                                           const string &vm_vrf,
                                           const Ip4Address &addr,
                                           uint8_t plen,
                                           const uuid &intf_uuid,
                                           uint16_t tag,
                                           uint32_t label,
                                           const string &dest_vn_name,
                                           const SecurityGroupList &sg_list,
                                           const PathPreference
                                           &path_preference) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new VlanNhRoute(intf_key, tag, label, dest_vn_name,
                                   sg_list, path_preference));

    Inet4UnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void
InetUnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const IpAddress &addr,
                                               uint8_t plen,
                                               LocalVmRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    req.data.reset(data);

    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}

void
InetUnicastAgentRouteTable::AddLocalVmRouteReq(const Peer *peer,
                                               const string &vm_vrf,
                                               const IpAddress &addr,
                                               uint8_t plen,
                                               const uuid &intf_uuid,
                                               const string &vn_name,
                                               uint32_t label,
                                               const SecurityGroupList &sg_list,
                                               bool force_policy,
                                               const PathPreference
                                               &path_preference,
                                               const IpAddress &subnet_gw_ip) {
    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    LocalVmRoute *data = new LocalVmRoute(intf_key, label,
                                    VxLanTable::kInvalidvxlan_id, force_policy,
                                    vn_name, InterfaceNHFlags::INET4, sg_list,
                                    path_preference, subnet_gw_ip);

    AddLocalVmRouteReq(peer, vm_vrf, addr, plen, data);
}

// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void 
InetUnicastAgentRouteTable::AddLocalVmRoute(const Peer *peer,
                                            const string &vm_vrf,
                                            const IpAddress &addr,
                                            uint8_t plen,
                                            const uuid &intf_uuid,
                                            const string &vn_name,
                                            uint32_t label,
                                            const SecurityGroupList &sg_list,
                                            bool force_policy,
                                            const PathPreference
                                            &path_preference,
                                            const IpAddress &subnet_gw_ip) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));

    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, intf_uuid, "");
    req.data.reset(new LocalVmRoute(intf_key, label, VxLanTable::kInvalidvxlan_id,
                                    force_policy, vn_name,
                                    InterfaceNHFlags::INET4, sg_list,
                                    path_preference, subnet_gw_ip));
    InetUnicastTableProcess(Agent::GetInstance(), vm_vrf, req);
}

void 
InetUnicastAgentRouteTable::AddRemoteVmRouteReq(const Peer *peer,
                                                const string &vm_vrf,
                                                const IpAddress &vm_addr,
                                                uint8_t plen,
                                                AgentRouteData *data) {
    if (Agent::GetInstance()->simulate_evpn_tor())
        return;
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, vm_addr, plen));
    req.data.reset(data);
    InetUnicastTableEnqueue(Agent::GetInstance(), vm_vrf, &req);
}
 
void
InetUnicastAgentRouteTable::AddArpReq(const string &vrf_name,
                                      const Ip4Address &ip) {
    Agent *agent = Agent::GetInstance();
    DBRequest  nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    nh_req.key.reset(new ArpNHKey(vrf_name, ip));
    nh_req.data.reset(new ArpNHData());
    agent->nexthop_table()->Enqueue(&nh_req);

    DBRequest  rt_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    rt_req.key.reset(new InetUnicastRouteKey(agent->local_peer(),
                                              vrf_name, ip, 32));
    rt_req.data.reset(new Inet4UnicastArpRoute(vrf_name, ip));
    Inet4UnicastTableEnqueue(agent, &rt_req);
}

void
InetUnicastAgentRouteTable::ArpRoute(DBRequest::DBOperation op,
                                     const Ip4Address &ip,
                                     const MacAddress &mac,
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
    InetUnicastRouteKey *rt_key =
        new InetUnicastRouteKey(agent->local_peer(), vrf_name, ip, plen);
    Inet4UnicastArpRoute *data = NULL;

    switch(op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE:
        agent->nexthop_table()->Enqueue(&nh_req);
        data = new Inet4UnicastArpRoute(vrf_name, ip);
        break;

    case DBRequest::DB_ENTRY_DELETE: {
        VrfEntry *vrf = agent->vrf_table()->FindVrfFromName(vrf_name);
        InetUnicastRouteEntry *rt = 
            static_cast<InetUnicastRouteEntry *>(vrf->
                          GetInet4UnicastRouteTable()->Find(rt_key));
        assert(resolved==false);
        agent->nexthop_table()->Enqueue(&nh_req);

        // If no other route is dependent on this, remove the route; else ignore
        if (rt && rt->IsDependantRouteEmpty() && rt->IsTunnelNHListEmpty()) {
            data = new Inet4UnicastArpRoute(vrf_name, ip);
        } else {
            rt_key->sub_op_ = AgentKey::RESYNC;
            rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        }
        break;
    }

    default:
        assert(0);
    }

    rt_req.key.reset(rt_key);
    rt_req.data.reset(data);
    Inet4UnicastTableEnqueue(agent, &rt_req);
}

void
InetUnicastAgentRouteTable::CheckAndAddArpReq(const string &vrf_name,
                                              const Ip4Address &ip) {

    if (ip == Agent::GetInstance()->router_id() ||
        !IsIp4SubnetMember(ip, Agent::GetInstance()->router_id(),
                           Agent::GetInstance()->vhost_prefix_len())) {
        // TODO: add Arp request for GW
        // Currently, default GW Arp is added during init
        return;
    }
    AddArpReq(vrf_name, ip);
}

void InetUnicastAgentRouteTable::AddResolveRoute(const string &vrf_name,
                                                 const Ip4Address &ip, 
                                                 const uint8_t plen) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vrf_name, ip,
                                          plen));
    req.data.reset(new ResolveRoute());
    Inet4UnicastTableEnqueue(agent, &req);
}

// Create Route for a interface NH.
// Used to create interface-nh pointing routes to vhost interfaces
void InetUnicastAgentRouteTable::AddInetInterfaceRouteReq(const Peer *peer,
                                                          const string &vm_vrf,
                                                          const Ip4Address &addr,
                                                          uint8_t plen,
                                                          const string &interface,
                                                          uint32_t label,
                                                          const string &vn_name) {
    InetInterfaceKey intf_key(interface);
    InetInterfaceRoute *data = new InetInterfaceRoute
        (intf_key, label, TunnelType::GREType(), vn_name);

    AddInetInterfaceRouteReq(peer, vm_vrf, addr, plen, data);
}

void InetUnicastAgentRouteTable::AddInetInterfaceRouteReq(const Peer *peer,
                                                          const string &vm_vrf,
                                                          const Ip4Address &addr,
                                                          uint8_t plen,
                                                          InetInterfaceRoute *data) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(peer, vm_vrf, addr, plen));
    req.data.reset(data);

    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

static void AddVHostRecvRouteInternal(DBRequest *req, const Peer *peer,
                                      const string &vrf,
                                      const string &interface,
                                      const Ip4Address &addr, uint8_t plen,
                                      const string &vn_name, bool policy) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(peer, vrf, addr, plen));

    InetInterfaceKey intf_key(interface);
    req->data.reset(new ReceiveRoute(intf_key, MplsTable::kInvalidLabel,
                                    TunnelType::AllType(), policy, vn_name));
}

void InetUnicastAgentRouteTable::AddVHostRecvRoute(const Peer *peer,
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
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf, req);
}

void InetUnicastAgentRouteTable::AddVHostRecvRouteReq
    (const Peer *peer, const string &vrf, const string &interface,
     const Ip4Address &addr, uint8_t plen, const string &vn_name, bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    static_cast<ReceiveRoute *>(req.data.get())->EnableProxyArp();
    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

void
InetUnicastAgentRouteTable::AddVHostSubnetRecvRoute(const Peer *peer,
                                                    const string &vrf,
                                                    const string &interface,
                                                    const Ip4Address &addr,
                                                    uint8_t plen,
                                                    const string &vn_name,
                                                    bool policy) {
    DBRequest req;
    AddVHostRecvRouteInternal(&req, peer, vrf, interface, addr, plen,
                              vn_name, policy);
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf, req);
}

void InetUnicastAgentRouteTable::AddDropRoute(const string &vm_vrf,
                                              const Ip4Address &addr,
                                              uint8_t plen,
                                              const string &vn_name) {
    Agent *agent = Agent::GetInstance();
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new InetUnicastRouteKey(agent->local_peer(), vm_vrf,
                                           GetIp4SubnetAddress(addr, plen),
                                           plen));
    req.data.reset(new DropRoute(vn_name));
    Inet4UnicastTableEnqueue(agent, &req);
}

void InetUnicastAgentRouteTable::DelVHostSubnetRecvRoute(const string &vm_vrf,
                                                         const Ip4Address &addr,
                                                         uint8_t plen) {
    DeleteReq(Agent::GetInstance()->local_peer(), vm_vrf,
              GetIp4SubnetAddress(addr, plen), 32, NULL);
}

static void AddGatewayRouteInternal(DBRequest *req, const string &vrf_name,
                                    const Ip4Address &dst_addr, uint8_t plen,
                                    const Ip4Address &gw_ip,
                                    const string &vn_name) {
    req->oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req->key.reset(new InetUnicastRouteKey(Agent::GetInstance()->local_peer(),
                                            vrf_name, dst_addr, plen));
    req->data.reset(new Inet4UnicastGatewayRoute(gw_ip, vrf_name));
}

void InetUnicastAgentRouteTable::AddGatewayRoute(const string &vrf_name,
                                                 const Ip4Address &dst_addr,
                                                 uint8_t plen,
                                                 const Ip4Address &gw_ip,
                                                 const string &vn_name) {
    DBRequest req;
    AddGatewayRouteInternal(&req, vrf_name, dst_addr, plen, gw_ip, vn_name);
    Inet4UnicastTableProcess(Agent::GetInstance(), vrf_name, req);
}

void
InetUnicastAgentRouteTable::AddGatewayRouteReq(const string &vrf_name,
                                               const Ip4Address &dst_addr,
                                               uint8_t plen,
                                               const Ip4Address &gw_ip,
                                               const string &vn_name) {
    DBRequest req;
    AddGatewayRouteInternal(&req, vrf_name, dst_addr, plen, gw_ip, vn_name);
    Inet4UnicastTableEnqueue(Agent::GetInstance(), &req);
}

void
InetUnicastAgentRouteTable::AddSubnetRoute(const string &vrf_name,
                                           const IpAddress &dst_addr,
                                           uint8_t plen,
                                           const string &vn_name,
                                           uint32_t vxlan_id) {
    Agent *agent_ptr = agent();
    AgentRouteTable *table = NULL;
    if (dst_addr.is_v4()) {
        table = agent_ptr->vrf_table()->GetInet4UnicastRouteTable(vrf_name);
    } else if (dst_addr.is_v6()) {
        table = agent_ptr->vrf_table()->GetInet6UnicastRouteTable(vrf_name);
    }

    AgentRoute *route = Layer2AgentRouteTable::FindRoute(agent_ptr, vrf_name,
                                                         MacAddress::BroadcastMac());

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
                static_cast<NextHopKey *>((path->nexthop(agent_ptr)->
                                           GetDBRequestKey()).release());
            DBRequest nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
            nh_req.key.reset(evpn_peer_key);
            nh_req.data.reset(new CompositeNHData());

            //Add route with this peer
            DBRequest req;
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
            req.key.reset(new InetUnicastRouteKey(path->peer(),
                                                   vrf_name, dst_addr, plen));
            req.data.reset(new SubnetRoute(vn_name, vxlan_id, nh_req));
            if (table) {
                table->Process(req);
            }
        }
    }

    //Add local perr path with discard NH
    DBRequest dscd_nh_req(DBRequest::DB_ENTRY_ADD_CHANGE);
    dscd_nh_req.key.reset(new DiscardNHKey());
    dscd_nh_req.data.reset(NULL);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetUnicastRouteKey(agent_ptr->local_peer(),
                                            vrf_name, dst_addr, plen));
    req.data.reset(new SubnetRoute(vn_name, vxlan_id, dscd_nh_req));
    if (table) {
        table->Process(req);
    }
}
