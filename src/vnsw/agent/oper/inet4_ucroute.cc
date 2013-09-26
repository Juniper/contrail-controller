/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>
#include <route/route.h>

#include <oper/vrf.h>
#include <oper/inet4_route.h>
#include <oper/inet4_ucroute.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

Inet4UcRouteTable *Inet4UcRouteTable::uc_route_table_;

Inet4UcRouteTable::Inet4UcRouteTable(DB *db, const std::string &name) :
    Inet4RouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {
}

Inet4UcRouteTable::~Inet4UcRouteTable() {
}

auto_ptr<DBEntry> Inet4UcRouteTable::AllocEntry(const DBRequestKey *k) const {
    const Inet4UcRouteKey *key = static_cast<const Inet4UcRouteKey *>(k);
    VrfKey vrf_key(key->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->Find(&vrf_key, true));
    Inet4UcRoute *route = new Inet4UcRoute(vrf, key->addr_, key->plen_);
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(route));
}

DBTableBase *Inet4UcRouteTable::CreateTable(DB *db, const std::string &name) {
    Inet4UcRouteTable *table;
    table = new Inet4UcRouteTable(db, name);
    table->Init();

    size_t index = name.rfind(VrfTable::GetInet4UcSuffix());
    assert(index != string::npos);
    string vrf = name.substr(0, index);
    VrfEntry *vrf_entry = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf);
    assert(vrf_entry);
    table->SetVrfEntry(vrf_entry);
    table->SetVrfDeleteRef(vrf_entry->deleter());

    if (uc_route_table_ == NULL)
        uc_route_table_ = table;

    return table;
};

bool Inet4UcRouteTable::DelPeerRoutes(DBTablePartBase *part, 
                                    DBEntryBase *entry, Peer *peer) {
    Inet4UcRoute *route = static_cast<Inet4UcRoute *>(entry);
    if (route) {
        DeleteRoute(part, route, peer);
    }
    return true;
}

// Algorithm to select an active path from multiple potential paths.
// Its a simple algorithm now to select path from 'lower' peer
// HOST and LOCAL_VM routes have 'peer' set to NULL. So, they take precedence
// always
bool Inet4UcRouteTable::PathSelection(const Path &path1, const Path &path2) {
    const AgentPath &l_path = dynamic_cast<const AgentPath &> (path1);
    const AgentPath &r_path = dynamic_cast<const AgentPath &> (path2);
    return l_path.GetPeer()->ComparePath(r_path.GetPeer());
}

void Inet4UcRouteTable::EvaluateUnresolvedNH(void) {
    //Trigger a change on all unresolved route
    for (UnresolvedNHTree::iterator it = unresolved_nh_tree_.begin();
         it != unresolved_nh_tree_.end(); ++it) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        const NextHop *nh = *it;
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;
        req.key = key;
        req.data.reset(NULL);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    }
    unresolved_nh_tree_.clear();
}

void Inet4UcRouteTable::AddUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.insert(nh);
}

void Inet4UcRouteTable::RemoveUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.erase(nh);
}

void Inet4UcRouteTable::EvaluateUnresolvedRoutes(void) {
    //Trigger a change on all unresolved route
    for (UnresolvedRouteTree::iterator it = unresolved_rt_tree_.begin();
         it !=  unresolved_rt_tree_.end(); ++it) {
       const Inet4UcRoute *rt = *it;
       RouteResyncReq(rt->GetVrfEntry()->GetName(), rt->GetIpAddress(), 
                      rt->GetPlen());
    }
    unresolved_rt_tree_.clear();
}

void Inet4UcRouteTable::AddUnresolvedRoute(const Inet4UcRoute *rt) {
    unresolved_rt_tree_.insert(rt);
}

void Inet4UcRouteTable::RemoveUnresolvedRoute(const Inet4UcRoute *rt) {
    unresolved_rt_tree_.erase(rt);
}

void Inet4UcRouteTable::DeleteRoute(DBTablePartBase *part,
                                  Inet4UcRoute *rt, const Peer *peer) {
    bool notify = false;
    RouteInfo rt_info;

    if (rt == NULL) {
        return;
    }
    // Remember to notify if path being deleted is active one
    const AgentPath *active_path = static_cast<const AgentPath *>(rt->GetActivePath());
    if (active_path == NULL) {
        return;
    }
    notify = true;
    if (rt->FindPath(peer)) {
        rt->FillTrace(rt_info, Inet4Route::DELETE_PATH, rt->FindPath(peer));
        OPER_TRACE(Route, rt_info);
    }

    if (notify) {
        AGENT_ROUTE_LOG("Deleted route", rt->ToString(), GetVrfName(), 
                        peer);
    }

    // Remove path from the route
    rt->RemovePath(peer);

    // Delete route if no more paths 
    if (rt->GetActivePath() == NULL) {
       RouteInfo rt_info_del;
        rt->FillTrace(rt_info_del, Inet4Route::DELETE, NULL);
        OPER_TRACE(Route, rt_info_del);
        RemoveUnresolvedRoute(rt);
        rt->UpdateGatewayRoutes();
        rt->UpdateNH();
        tree_.Remove(rt);
        part->Delete(rt);
        return;
    } else if (notify) {
        // Notify
        part->Notify(rt);
    }

    //cleanup subnet-broadcast composite-nhs 
#if 0
    if (rt->IsSbcast() && 
        rt->GetActiveNextHop()->GetType() == NextHop::COMPOSITE) {
  
        boost::system::error_code ec;
        CompositeNH::HandleControlNodePeerDown(rt->GetVrfEntry()->GetName(),
                                  rt->GetIpAddress(),
                                  IpAddress::from_string("0.0.0.0", ec).to_v4());
    }
#endif
}

//  Input handler for Route Table.
//  Adds a route entry if not present.
//      Adds path to route entry
//      Paths are sorted in order of their precedence
//  A DELETE request always removes path from the peer
//      Route entry with no paths is automatically deleted
void Inet4UcRouteTable::Input(DBTablePartition *part, DBClient *client,
                            DBRequest *req) {
    Inet4UcRouteKey *key = static_cast<Inet4UcRouteKey *>(req->key.get());
    Inet4RouteData *data = static_cast<Inet4RouteData *>(req->data.get());
    Inet4UcRouteTable *table = static_cast<Inet4UcRouteTable *>(part->parent());
    Inet4UcRoute *rt = NULL;
    AgentPath *path = NULL;
    bool notify = false;
    bool route_added = false;
    RouteInfo rt_info;
    bool sync = false;

    VrfEntry *vrf = table->FindVrfEntry(key->vrf_name_);
    if (!vrf && req->oper == DBRequest::DB_ENTRY_DELETE) {
        return;
    } else {
        if (!vrf) {
            LOG(DEBUG, "Route ignrored. VRF <" << key->vrf_name_ 
                << "> not found.");
            return;
        }
    }

    Inet4UcRouteTable *vrf_table = vrf->GetInet4UcRouteTable();
    if (vrf_table != this) {
        DBTablePartition *p = static_cast<DBTablePartition *>
            (vrf_table->GetTablePartition(key));
        vrf_table->Input(p, client, req);
        return;
    }

    rt = static_cast<Inet4UcRoute *>(part->Find(key));
    if (key->sub_op_ == AgentKey::RESYNC) {
        if (rt) {
            rt->Sync();
            notify = true;
        }
    } else if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if(vrf->IsDeleted()) {
            //Route change, received on deleted VRF
            return;
        }

        if (rt && rt->IsDeleted()) {
            rt->ClearDelete();
            tree_.Insert(rt);
            notify = true;
        }

        if (data->op_ == Inet4RouteData::CHANGE) {
            // Add route if not present already
            if (rt == NULL) {
                //If route is a gateway route first check
                //if its corresponding direct route is present
                //or not, if not present dont add the route
                //just maintain it in unresolved list
                Inet4Route::RtType t = Inet4Route::INET4_UCAST;
                if (data->type_ == Inet4RouteData::SBCAST_ROUTE) {
                    t = Inet4Route::INET4_SBCAST;
                }

                rt = new Inet4UcRoute(vrf, key->addr_, key->plen_, t);
                assert(rt->GetVrfEntry() != NULL);
                part->Add(rt);
                // Mark path as NULL so that its allocated below
                path = NULL;
                tree_.Insert(rt);
                rt->FillTrace(rt_info, Inet4Route::ADD, NULL);
                OPER_TRACE(Route, rt_info);
                route_added = true;
            } else {
                // RT present. Check if path is also present by peer
                path = rt->FindPath(key->peer_);
            }

            // Allocate path if not yet present
            if (path == NULL) {
                path = new AgentPath(key->peer_, rt);
                rt->InsertPath(path);
                path->RouteChange(Agent::GetInstance()->GetNextHopTable(), key, data, sync);
                rt->FillTrace(rt_info, Inet4Route::ADD_PATH, path);
                OPER_TRACE(Route, rt_info);
                notify = true;
            } else {
                // Let path know of route change and update itself
                notify = path->RouteChange(Agent::GetInstance()->GetNextHopTable(), key, 
                                           data, sync);
                rt->FillTrace(rt_info, Inet4Route::CHANGE_PATH, path);
                OPER_TRACE(Route, rt_info);
                //AGENT_ROUTE_LOG("Path change", rt->ToString(), GetVrfName(),
                //                key->peer_);
            }

            if (sync == true) {
                rt->Sync();
            }
            if (route_added == true) {
                AGENT_ROUTE_LOG("Added route", rt->ToString(), GetVrfName(),
                                key->peer_);
            }
        }

        if (route_added) {
            EvaluateUnresolvedRoutes();
            EvaluateUnresolvedNH();
        }
        //If this route has a unresolved path, insert to unresolved list 
        if (rt->HasUnresolvedPath() == true) {
            AddUnresolvedRoute(rt);
        }
    } else {
        DeleteRoute(part, rt, key->peer_);
    }

    //Route changed, trigger change on dependent routes
    if (notify) {
        part->Notify(rt);
        rt->UpdateGatewayRoutes();
        rt->UpdateNH();
    }
}

bool Inet4UcRouteTable::DelExplicitRoute(DBTablePartBase *part,
                                     DBEntryBase *entry) {
    Inet4UcRoute *route = static_cast<Inet4UcRoute *>(entry);
    if (route && !route->IsDeleted()) {
        //Remove all contral-node injected routes
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end();) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const Peer *peer = path->GetPeer();
            it++;
            if (peer && peer->GetType() == Peer::BGP_PEER) {
                DeleteRoute(part, route, path->GetPeer());
            }
        }
    }
    return true;
}

Inet4UcRoute *Inet4UcRouteTable::FindLPM(const Ip4Address &ip) {
    Ip4Address addr = ip::address_v4(ip.to_ulong());
    Inet4UcRoute key(NULL, addr, 32);
    return tree_.LPMFind(&key);
}

Inet4UcRoute *Inet4UcRouteTable::FindRoute(const string &vrf_name, 
        const Ip4Address &ip) {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    Inet4UcRouteTable *rt_table = vrf->GetInet4UcRouteTable();
    return rt_table->FindLPM(ip);
}

Inet4UcRoute *Inet4UcRouteTable::FindResolveRoute(const Ip4Address &ip) {
    uint8_t plen = 32;
    Inet4UcRoute *rt = NULL;
    do {
        Inet4UcRoute key(NULL, ip, plen);
        rt = tree_.LPMFind(&key);
        if (rt) {
            const NextHop *nh = rt->GetActiveNextHop();
            if (nh && nh->GetType() == NextHop::RESOLVE)
                return rt;
        }
    } while (rt && --plen);

    return NULL;
}

Inet4UcRoute *Inet4UcRouteTable::FindResolveRoute(const string &vrf_name, 
        const Ip4Address &ip) {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
    Inet4UcRouteTable *rt_table = vrf->GetInet4UcRouteTable();
    return rt_table->FindResolveRoute(ip);
}

Inet4UcRoute::Inet4UcRoute(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen, RtType type) :
        Inet4Route(vrf, addr, plen, type) { 
};

Inet4UcRoute::Inet4UcRoute(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen) :
        Inet4Route(vrf, addr, plen) {
};

int Inet4UcRoute::CompareTo(const Route &rhs) const {
    const Inet4UcRoute &a = static_cast<const Inet4UcRoute &>(rhs);

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
  
bool Inet4UcRoute::IsLess(const DBEntry &rhs) const {
    int cmp = CompareTo(static_cast<const Route &>(rhs));
    return (cmp < 0);
};

void Inet4UcRoute::InsertPath(const AgentPath *path) {
	const Path *prev_front = front();
    insert(path);
    Sort(&Inet4UcRouteTable::PathSelection, prev_front);
}

void Inet4UcRoute::RemovePath(const Peer *peer) {
    for(Route::PathList::iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->GetPeer() == peer) {
            const Path *prev_front = front();
            remove(path);
            path->ClearSecurityGroupList();
            Sort(&Inet4UcRouteTable::PathSelection, prev_front);
            delete path;
            return;
        }
    }
}

AgentPath *Inet4UcRoute::FindPath(const Peer *peer) const {
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->GetPeer() == peer) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
}

// First path in list is always treated as active path.
const AgentPath *Inet4UcRoute::GetActivePath() const {
    return static_cast<const AgentPath *>(front());
}

const NextHop *Inet4UcRoute::GetActiveNextHop() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;

    return path->GetNextHop();
}

const Peer *Inet4UcRoute::GetActivePeer() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;
    return path->GetPeer();
}

DBEntryBase::KeyPtr Inet4UcRoute::GetDBRequestKey() const {
    Inet4UcRouteKey *key = new Inet4UcRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                           GetVrfEntry()->GetName(), 
                                           GetIpAddress(), GetPlen());
    return DBEntryBase::KeyPtr(key);
}

void Inet4UcRoute::SetKey(const DBRequestKey *key) {
    const Inet4UcRouteKey *k = static_cast<const Inet4UcRouteKey *>(key);
    SetVrf(Inet4UcRouteTable::GetInstance()->FindVrfEntry(k->vrf_name_));
    Ip4Address tmp(k->addr_);
    SetAddr(tmp);
    SetPlen(k->plen_);
}

bool Inet4UcRoute::Inet4UcNotifyRouteEntryWalk(AgentXmppChannel *bgp_xmpp_peer,
                                               DBState *vrf_entry_state,
                                               bool subnet_only,
                                               bool associate,
                                               DBTablePartBase *part,
                                               DBEntryBase *entry) {
    Inet4UcRoute *route = static_cast<Inet4UcRoute *>(entry);
    VrfExport::State *vs = static_cast<VrfExport::State *>(vrf_entry_state);

    if (subnet_only && !route->IsSbcast()) {
        return true;
    }

    Inet4RouteExport::State *state =
        static_cast<Inet4RouteExport::State *>(route->GetState(part->parent(),
                                          vs->inet4_unicast_export_->GetListenerId()));
    if (state) {
        state->force_chg_ = true;
    }

    vs->inet4_unicast_export_->Notify(bgp_xmpp_peer, associate, part, entry);
    return true;
}


//If a direct route has changed, invoke a change on
//tunnel NH dependent on it
void Inet4UcRoute::UpdateNH(void) {
    for (Inet4UcRoute::tunnel_nh_iterator iter = tunnel_nh_list_.begin(); 
         iter != tunnel_nh_list_.end(); iter++) {
        NextHop *nh = static_cast<NextHop *>(iter.operator->());
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;
        req.key = key;
        req.data.reset(NULL);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);
    }
}

//If a direct route get modified invariably trigger change
//on all dependent indirect routes, coz if a nexthop has 
//changed we need to update the same in datapath for indirect
//routes
void Inet4UcRoute::UpdateGatewayRoutes(void) {
    for (Inet4UcRoute::iterator iter = begin(); iter != end(); iter++) {
        Inet4UcRoute *rt = iter.operator->();
        Inet4UcRouteTable::RouteResyncReq(rt->GetVrfEntry()->GetName(), 
                                          rt->GetIpAddress(),
                                          rt->GetPlen());
    }
}

bool Inet4UcRoute::HasUnresolvedPath(void) {
    for(Route::PathList::const_iterator it = GetPathList().begin();
            it != GetPathList().end(); it++) {
        const AgentPath *path =
            static_cast<const AgentPath *>(it.operator->());
        if (path->IsUnresolved() == true) {
            return true;
        }
    }

    return false;
}

bool Inet4UcRoute::Sync(void) {
    bool ret = false;
    for(Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->Sync(this) == true) {
            if (GetActivePath() == path) {
                ret = true;
            }
        }
    }
    return ret;
}

// Utility function to delete a route
void Inet4UcRouteTable::DeleteReq(const Peer *peer, const string &vrf_name,
                                const Ip4Address &addr, uint8_t plen) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_DELETE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(peer, vrf_name, addr, plen);
    req.key.reset(key);
    req.data.reset(NULL);
    uc_route_table_->Enqueue(&req);
}

// Utility function to create a route to trap packets to agent.
// Assumes that Interface-NH for "HOST Interface" is already present
void Inet4UcRouteTable::AddHostRoute(const string &vrf_name,
                                   const Ip4Address &addr, uint8_t plen,
		 		   const std::string &dest_vn_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(Agent::GetInstance()->GetLocalPeer(),
                                           vrf_name, addr, plen);
    req.key.reset(key);

    HostInterfaceKey intf_key(nil_uuid(), Agent::GetInstance()->GetHostInterfaceName());
    Inet4UcHostRoute *data = new Inet4UcHostRoute(intf_key, dest_vn_name);
    req.data.reset(data);

    uc_route_table_->Enqueue(&req);
}
// Create Route with VLAN NH
void Inet4UcRouteTable::AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                                       const Ip4Address &addr, uint8_t plen,
                                       const uuid &intf_uuid, uint16_t tag,
                                       uint32_t label,
                                       const string &dest_vn_name,
                                       const SecurityGroupList &sg_list) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(peer, vm_vrf, addr, plen);
    req.key.reset(key);

    VmPortInterfaceKey intf_key(intf_uuid, "");
    Inet4UcVlanNhRoute *data = new Inet4UcVlanNhRoute(intf_key, tag, label,
                                                      dest_vn_name, sg_list);
    req.data.reset(data);

    uc_route_table_->Enqueue(&req);
}


// Create Route for a local VM
// Assumes that Interface-NH for "VM Port" is already present
void Inet4UcRouteTable::AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                        const Ip4Address &addr, uint8_t plen,
                                        const uuid &intf_uuid,
                                        const string &vn_name,
                                        uint32_t label, bool force_policy) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmPortInterfaceKey intf_key(intf_uuid, "");
    Inet4UcLocalVmRoute *data = new Inet4UcLocalVmRoute(intf_key, label,
                                                        force_policy, vn_name);
    req.data.reset(data);

    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                      const Ip4Address &addr, uint8_t plen,
                                      const uuid &intf_uuid,
                                      const string &vn_name,
                                      uint32_t label,
                                      const SecurityGroupList &sg_list) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(peer, vm_vrf, addr, plen); 
    req.key.reset(key);

    VmPortInterfaceKey intf_key(intf_uuid, "");
    Inet4UcLocalVmRoute *data = new Inet4UcLocalVmRoute(intf_key, label, false, vn_name);
    data->sg_list_ = sg_list;
    req.data.reset(data);

    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::AddSubnetBroadcastRoute(const Peer *peer, const string &vrf_name,
                                      const Ip4Address &src_addr, 
                                      const Ip4Address &grp_addr,
                                      const string &vn_name) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *key = new Inet4UcRouteKey(peer, vrf_name, grp_addr, 32); 
    req.key.reset(key);

    Inet4UcSbcastRoute *data = new Inet4UcSbcastRoute(src_addr, grp_addr,
                                                      vn_name);
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

// Create Route for a local VM. Policy is disabled
void Inet4UcRouteTable::AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                      const Ip4Address &addr, uint8_t plen,
                                      const uuid &intf_uuid, const string &vn_name,
                                      uint32_t label) {
    AddLocalVmRoute(peer, vm_vrf, addr, plen, intf_uuid, vn_name, label, false);
}

// Create Route for a Remote VM
// Also creates Tunnel-NH for the route
void Inet4UcRouteTable::AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                       const Ip4Address &vm_addr,uint8_t plen,
                                       const Ip4Address &server_ip,
                                       TunnelType::TypeBmap bmap,
                                       uint32_t label,
                                       const string &dest_vn_name) {
    DBRequest req;
    // First enqueue request to add/change Interface NH
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *nh_key = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(),
                                         Agent::GetInstance()->GetRouterId(), server_ip,
                                         false, TunnelType::ComputeType(bmap));
    req.key.reset(nh_key);

    TunnelNHData *nh_data = new TunnelNHData();
    req.data.reset(nh_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);

    // Enqueue request for route pointing to Interface NH created above
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(peer, vm_vrf, vm_addr, plen);
    req.key.reset(rt_key);

    Inet4UcRemoteVmRoute *rt_data = new Inet4UcRemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(),
                                                         server_ip, label,
                                                         dest_vn_name, bmap);
    req.data.reset(rt_data);
    uc_route_table_->Enqueue(&req);
}


void Inet4UcRouteTable::AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                         const Ip4Address &vm_addr,uint8_t plen,
                                         const Ip4Address &server_ip,
                                         TunnelType::TypeBmap bmap,
                                         uint32_t label,
                                         const string &dest_vn_name,
                                         const SecurityGroupList &sg_list) {
    DBRequest req;
    // First enqueue request to add/change Interface NH
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *nh_key = new TunnelNHKey(Agent::GetInstance()->GetDefaultVrf(), 
                                         Agent::GetInstance()->GetRouterId(), server_ip,
                                         false, TunnelType::ComputeType(bmap));
    req.key.reset(nh_key);

    TunnelNHData *nh_data = new TunnelNHData();
    req.data.reset(nh_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&req);

    // Enqueue request for route pointing to Interface NH created above
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(peer, vm_vrf, vm_addr, plen);
    req.key.reset(rt_key);

    Inet4UcRemoteVmRoute *rt_data = 
        new Inet4UcRemoteVmRoute(Agent::GetInstance()->GetDefaultVrf(), server_ip, label,
                                 dest_vn_name, bmap);
    rt_data->sg_list_ = sg_list;
    req.data.reset(rt_data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                       const Ip4Address &vm_addr,uint8_t plen,
                                       std::vector<ComponentNHData> comp_nh_list,
                                       uint32_t label,
                                       const string &dest_vn_name, 
                                       bool local_ecmp_nh) {
    CompositeNH::CreateCompositeNH(vm_vrf, vm_addr, local_ecmp_nh, comp_nh_list);
    
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(peer, vm_vrf, 
                                                  vm_addr, plen);
    req.key.reset(rt_key);
    Inet4UcEcmpRoute *rt_data = new Inet4UcEcmpRoute(vm_addr, dest_vn_name, 
                                                     label, local_ecmp_nh);
    req.data.reset(rt_data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::RouteResyncReq(const string &vrf_name, 
                                       const Ip4Address &ip, uint8_t plen) {
    DBRequest  rt_req;
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(NULL, vrf_name, ip, plen);
    rt_key->sub_op_ = AgentKey::RESYNC;
    rt_req.key.reset(rt_key);
    rt_req.data.reset(NULL);
    uc_route_table_->Enqueue(&rt_req);

}
//Create a Direct route with unresolved ARP NH
void Inet4UcRouteTable::AddArpReq(const string &vrf_name, const Ip4Address &ip) {

    DBRequest  nh_req;
    nh_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    NextHopKey *key = new ArpNHKey(vrf_name, ip);
    nh_req.key.reset(key);

    ArpNHData *arp_data = new ArpNHData();
    nh_req.data.reset(arp_data);
    Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);

    DBRequest  rt_req;
    rt_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                              vrf_name, ip, 32);
    rt_req.key.reset(rt_key);
    Inet4RouteData *data = new Inet4RouteData(Inet4RouteData::ARP_ROUTE);
    rt_req.data.reset(data);
    uc_route_table_->Enqueue(&rt_req);
}
                                
void Inet4UcRouteTable::ArpRoute(DBRequest::DBOperation op, 
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
    ArpNHData *arp_data =
        new ArpNHData(mac,
                      static_cast<InterfaceKey *>(intf.GetDBRequestKey().release()), 
                      resolved);
    nh_req.data.reset(arp_data);

    DBRequest  rt_req;
    rt_req.oper = op;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                              vrf_name, ip, plen);
    Inet4RouteData *data = NULL;

    switch(op) {
    case DBRequest::DB_ENTRY_ADD_CHANGE:
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);
        data = new Inet4RouteData(Inet4RouteData::ARP_ROUTE);
        break;

    case DBRequest::DB_ENTRY_DELETE: {
        VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromName(vrf_name);
        Inet4UcRoute *rt = 
            static_cast<Inet4UcRoute *>(vrf->GetInet4UcRouteTable()->Find(rt_key));
        assert(resolved==false);
        Agent::GetInstance()->GetNextHopTable()->Enqueue(&nh_req);

        // If no other route is dependent on this, remove the route; else ignore
        if (rt && rt->gw_route_empty() && rt->tunnel_nh_list_empty()) {
            data = new Inet4RouteData(Inet4RouteData::ARP_ROUTE);
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
    uc_route_table_->Enqueue(&rt_req);
}

void Inet4UcRouteTable::AddResolveRoute(const string &vrf_name, 
                                      const Ip4Address &ip, 
                                      const uint8_t plen) {
    DBRequest  req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(Peer::GetPeer(LOCAL_PEER_NAME),
                                              vrf_name, ip, plen);
    req.key.reset(rt_key);
    Inet4RouteData *data = new Inet4RouteData(Inet4RouteData::RESOLVE_ROUTE);
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

// Create Route for Vhost interface
void Inet4UcRouteTable::AddVHostRecvRoute(const Peer *peer,
                                          const string &vm_vrf,
                                          const string &interface_name,
                                          const Ip4Address &addr, uint8_t plen,
                                          const string &vn,
                                          bool policy) {
    ReceiveNH::CreateReq(interface_name);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(peer, vm_vrf, addr, plen);
    req.key.reset(rt_key);
    VirtualHostInterfaceKey intf_key(nil_uuid(), interface_name);
    Inet4UcReceiveRoute *data = new Inet4UcReceiveRoute(intf_key, policy, vn);
    data->EnableProxyArp();
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::AddVHostRecvRoute(const string &vm_vrf,
                                          const string &interface_name,
                                          const Ip4Address &addr,
                                          bool policy) {
    AddVHostRecvRoute(Agent::GetInstance()->GetLocalPeer(), vm_vrf,
                      interface_name, addr, 32,
                      Agent::GetInstance()->GetFabricVnName(), policy);
}

void Inet4UcRouteTable::AddVHostSubnetRecvRoute(
    const string &vm_vrf, const string &interface_name,
    const Ip4Address &addr, uint8_t plen, bool policy) {
    Ip4Address subnet_addr(addr.to_ulong() | 
                             ~(0xFFFFFFFF << (32 - plen)));
    ReceiveNH::CreateReq(interface_name);

    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(Agent::GetInstance()->GetLocalPeer(), 
                                              vm_vrf, subnet_addr, 32);
    req.key.reset(rt_key);
    VirtualHostInterfaceKey intf_key(nil_uuid(), interface_name);
    Inet4UcReceiveRoute *data = new Inet4UcReceiveRoute(intf_key, policy,
                                                        Agent::GetInstance()->GetFabricVnName());
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::AddDropRoute(const string &vm_vrf,
                                     const Ip4Address &addr, uint8_t plen) {
    Ip4Address subnet_addr(addr.to_ulong() | ~(0xFFFFFFFF << (32 - plen)));
    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(Agent::GetInstance()->GetLocalPeer(), 
                                                  vm_vrf, subnet_addr, plen);
    Inet4UcDropRoute *data = new Inet4UcDropRoute();
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(rt_key);
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::DelVHostSubnetRecvRoute(
    const string &vm_vrf, const Ip4Address &addr, uint8_t plen) {
    Ip4Address subnet_addr(addr.to_ulong() | ~(0xFFFFFFFF << (32 - plen)));
    DeleteReq(Agent::GetInstance()->GetLocalPeer(), vm_vrf, subnet_addr, 32);
}

void Inet4UcRouteTable::AddGatewayRoute(const Peer *peer, const string &vrf_name,
                                       const Ip4Address &dst_addr,uint8_t plen,
                                       const Ip4Address &gw_ip) {
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    Inet4UcRouteKey *rt_key = new Inet4UcRouteKey(peer, vrf_name, dst_addr, plen);
    Inet4UcGatewayRoute *data = new Inet4UcGatewayRoute(gw_ip);

    req.key.reset(rt_key);
    req.data.reset(data);
    uc_route_table_->Enqueue(&req);
}

void Inet4UcRouteTable::Inet4UcRouteTableWalkerNotify(VrfEntry *vrf,
                                                  AgentXmppChannel *bgp_xmpp_peer,
                                                  DBState *state,
                                                  bool subnet_only,
                                                  bool associate) {
    boost::system::error_code ec;
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);
        
    if (vrf_state->inet4_uc_walkid_ != DBTableWalker::kInvalidWalkerId) {

        AGENT_DBWALK_TRACE(AgentDBWalkLog, "Cancel walk ", "Inet4UcRouteTable",
                           vrf_state->inet4_uc_walkid_,
                           bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                           "Add/Withdraw Route");

        walker->WalkCancel(vrf_state->inet4_uc_walkid_);
    }

    vrf_state->inet4_uc_walkid_ = walker->WalkTable(this, NULL,
        boost::bind(&Inet4UcRoute::Inet4UcNotifyRouteEntryWalk, bgp_xmpp_peer,
                    state, subnet_only, associate, _1, _2),
        boost::bind(&Inet4UcRouteTable::Inet4UcRouteNotifyDone, this, _1, state));

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Start walk ", "Inet4UcRouteTable",
                       vrf_state->inet4_uc_walkid_,
                       bgp_xmpp_peer->GetBgpPeer()->GetName(), 
                       (associate)? "Add route": "Withdraw Route");
}

void Inet4UcRouteTable::Inet4UcRouteNotifyDone(DBTableBase *base,
                                               DBState *state) {
    VrfExport::State *vrf_state = static_cast<VrfExport::State *>(state);

    AGENT_DBWALK_TRACE(AgentDBWalkLog, "Done walk ", "Inet4UcRouteTable",
                       vrf_state->inet4_uc_walkid_, "peer-unknown", 
                       "Add/Del Route");

    vrf_state->inet4_uc_walkid_ = DBTableWalker::kInvalidWalkerId;
}

void UnresolvedNH::HandleRequest() const {

    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(0);
    if (!vrf) {
        ErrorResp *resp = new ErrorResp();
        resp->set_context(context());
        resp->Response();
        return;
    }
   
    int count = 0;
    std::string empty(""); 
    Inet4UcRouteTable::const_nh_iterator it;
    NhListResp *resp = new NhListResp();

    it = vrf->GetInet4UcRouteTable()->unresolved_nh_begin();
    for (;it != vrf->GetInet4UcRouteTable()->unresolved_nh_end(); it++) {
        count++;
        const NextHop *nh = *it;
        nh->DBEntrySandesh(resp, empty);
        if (count == 1) {
            resp->set_context(context()+"$");
            resp->Response();
            count = 0;
            resp = new NhListResp();
        }
    }

    resp->set_context(context());
    resp->Response();
    return;
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
    Inet4UcRouteTable::const_rt_iterator it;
    Inet4UcRouteResp *resp = new Inet4UcRouteResp();

    it = vrf->GetInet4UcRouteTable()->unresolved_route_begin();
    for (;it != vrf->GetInet4UcRouteTable()->unresolved_route_end(); it++) {
        count++;
        const Inet4UcRoute *rt = *it;
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

bool Inet4UcRoute::DBEntrySandesh(Sandesh *sresp) const {
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
            if (path->GetSecurityGroupList().size()) {
                pdata.set_sg_list(path->GetSecurityGroupList());
            }
            data.path_list.push_back(pdata);
        }
    }

    std::vector<RouteUcSandeshData> &list = 
        const_cast<std::vector<RouteUcSandeshData>&>(resp->get_route_list());
    list.push_back(data);
    return true;
}

bool Inet4UcRoute::DBEntrySandesh(Sandesh *sresp, Ip4Address addr,
                                  uint8_t plen) const {
    if (GetIpAddress() == addr && GetPlen() == plen) {
        return DBEntrySandesh(sresp);
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Route Table utility functions to add different kind of routes
/////////////////////////////////////////////////////////////////////////////

void Inet4UcRouteReq::HandleRequest() const {
    VrfEntry *vrf = Agent::GetInstance()->GetVrfTable()->FindVrfFromId(get_vrf_index());
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
        sand = new AgentInet4UcRtSandesh(vrf, context(), src_ip, (uint8_t)get_prefix_len());
    }
    sand->DoSandesh();
}
