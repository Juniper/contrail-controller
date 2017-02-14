/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/tunnel_nh.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/multicast.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;
using namespace boost::asio;

SandeshTraceBufferPtr AgentDBwalkTraceBuf(SandeshTraceBufferCreate(
    AGENT_DBWALK_TRACE_BUF, 1000));

class AgentRouteTable::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(AgentRouteTable *rt_table) : 
        LifetimeActor(rt_table->agent()->lifetime_manager()), 
        table_(rt_table) { 
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        return table_->MayDelete();
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
        assert(table_->vrf_entry_.get() != NULL);
        table_->vrf_entry_->SetRouteTableDeleted(table_->GetTableType());
        //Release refernces
        table_->vrf_delete_ref_.Reset(NULL);
        table_->vrf_entry_ = NULL;
    }

  private:
    AgentRouteTable *table_;
};

bool RouteComparator::operator() (const AgentRoute *rt1,
                                  const AgentRoute *rt2) {
    return rt1->IsLess(*rt2);
}

bool NHComparator::operator() (const NextHop *nh1, const NextHop *nh2) {
    return nh1->IsLess(*nh2);
}

AgentRouteTable::AgentRouteTable(DB *db, const std::string &name):
    RouteTable(db, name), agent_(NULL), vrf_entry_(NULL, this), deleter_(NULL),
    vrf_delete_ref_(this, NULL) {
        OperDBTraceBuf = SandeshTraceBufferCreate("OperRoute", 5000);
}

AgentRouteTable::~AgentRouteTable() {
}

const string &AgentRouteTable::GetSuffix(Agent::RouteTableType type) {
    static const string uc_suffix(".uc.route.0");
    static const string mc_suffix(".mc.route.0");
    static const string evpn_suffix(".evpn.route.0");
    static const string l2_suffix(".l2.route.0");
    static const string uc_inet6_suffix(".uc.route6.0");

    switch (type) {
    case Agent::INET4_UNICAST:
        return uc_suffix;
    case Agent::INET4_MULTICAST:
        return mc_suffix;
    case Agent::EVPN:
        return evpn_suffix;
    case Agent::BRIDGE:
        return l2_suffix;
    case Agent::INET6_UNICAST:
        return uc_inet6_suffix;
    default:
        return Agent::NullString();
    }
}

// Set VRF and delete life-time actor reference to VRF
void AgentRouteTable::SetVrf(VrfEntry *vrf) {
    agent_ = (static_cast<VrfTable *>(vrf->get_table()))->agent();
    vrf_entry_ = vrf;
    vrf_id_ = vrf->vrf_id();
    vrf_delete_ref_.Reset(vrf->deleter());
    deleter_.reset(new DeleteActor(this));
}

// Allocate a route entry
auto_ptr<DBEntry> AgentRouteTable::AllocEntry(const DBRequestKey *k) const {
    const AgentRouteKey *key = static_cast<const AgentRouteKey*>(k);
    VrfKey vrf_key(key->vrf_name());
    AgentRoute *route = 
        static_cast<AgentRoute *>(key->AllocRouteEntry(vrf_entry_.get(),
                                                       false));
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(route));
}

// Delete all paths from BGP Peer. Delete route if no path left
bool AgentRouteTable::DeleteAllBgpPath(DBTablePartBase *part,
                                       DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route && !route->IsDeleted()) {
        std::list<AgentPath *> to_be_deleted_path_list;
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end();) {
            AgentPath *path =
                static_cast<AgentPath *>(it.operator->());
            const Peer *peer = path->peer();
            it++;
            if (peer && peer->GetType() == Peer::BGP_PEER) {
                to_be_deleted_path_list.push_back(path);
            }
            if (peer && peer->GetType() == Peer::MULTICAST_FABRIC_TREE_BUILDER) {
                to_be_deleted_path_list.push_back(path);
            }
        }
        //Now delete all selected paths.
        std::list<AgentPath *>::iterator to_be_deleted_path_list_it =
            to_be_deleted_path_list.begin();
        while (to_be_deleted_path_list_it != to_be_deleted_path_list.end()) {
            AgentPath *path =
                static_cast<AgentPath *>(*to_be_deleted_path_list_it);
            DeletePathFromPeer(part, route, path);
            to_be_deleted_path_list_it++;
        }
    }
    return true;
}

void AgentRouteTable::DeleteRouteDone(DBTableBase *base, 
                                      RouteTableWalkerState *state) {
    LOG(DEBUG, "Deleted all BGP injected routes for " << base->name());
    delete state;
}

bool AgentRouteTable::DelExplicitRouteWalkerCb(DBTablePartBase *part,
                                  DBEntryBase *entry) {
    return DeleteAllBgpPath(part, entry);
}

// Algorithm to select an active path from multiple potential paths.
// Uses comparator in path for selection
bool AgentRouteTable::PathSelection(const Path &path1, const Path &path2) {
    const AgentPath &l_path = dynamic_cast<const AgentPath &> (path1);
    const AgentPath &r_path = dynamic_cast<const AgentPath &> (path2);
    return l_path.IsLess(r_path);
}

// Re-evaluate all unresolved NH. Flush and enqueue RESYNC for all NH in the 
// unresolved NH tree
void AgentRouteTable::EvaluateUnresolvedNH(void) {
    for (UnresolvedNHTree::iterator it = unresolved_nh_tree_.begin();
         it != unresolved_nh_tree_.end(); ++it) {

        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key = (*it)->GetDBRequestKey();
        (static_cast<NextHopKey *>(req.key.get()))->sub_op_ = AgentKey::RESYNC;
        agent_->nexthop_table()->Enqueue(&req);
    }

    unresolved_nh_tree_.clear();
}

void AgentRouteTable::AddUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.insert(nh);
}

void AgentRouteTable::RemoveUnresolvedNH(const NextHop *nh) {
    unresolved_nh_tree_.erase(nh);
}

// Re-evaluate all unresolved routes. Flush and enqueue RESYNC for all routes
// in the unresolved route tree
void AgentRouteTable::EvaluateUnresolvedRoutes(void) {
    for (UnresolvedRouteTree::iterator it = unresolved_rt_tree_.begin();
         it !=  unresolved_rt_tree_.end(); ++it) {
       const AgentRoute *rt = *it;
       rt->EnqueueRouteResync(); 
    }

    unresolved_rt_tree_.clear();
}

void AgentRouteTable::AddUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.insert(rt);
}

void AgentRouteTable::RemoveUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.erase(rt);
}

// Delete path from the given peer. 
// If all paths are deleted, 
//     delete the route and notify
// Else
//     Notify the DBEntry if any path is deleted
//
// Ideally, route must be notified only when active-path is deleted,
// But, notification of deleting non-active path is needed in one case.
//
// For VM spawned locally, we path BGP_PEER path with higher priority than 
// LOCAL_VM peer path. But, controller-peer needs to know deletion of 
// LOCAL_VM path to retract the route.  So, force notify deletion of any path.
void AgentRouteTable::DeletePathFromPeer(DBTablePartBase *part,
                                         AgentRoute *rt,
                                         AgentPath *path) {
    if (rt == NULL) {
        return;
    }
    bool deleted_path_was_active_path = (rt->GetActivePath() == path);
    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::DELETE_PATH, path);
    OPER_TRACE_ROUTE(Route, rt_info);

    if (path == NULL) {
        return;
    }

    CompositeNH *cnh = dynamic_cast<CompositeNH *>(path->nexthop());
    //Recompute paths since one is going off before deleting.
    rt->ReComputePathDeletion(path);
    // Remove path from the route
    rt->RemovePath(path);
    if (cnh) {
        cnh->UpdateEcmpHashFieldsUponRouteDelete(agent_, vrf_name());
    }

    // Delete route if no more paths 
    if (rt->GetActivePath() == NULL) {
        RouteInfo rt_info_del;
        rt->FillTrace(rt_info_del, AgentRoute::DELETE, NULL);
        OPER_TRACE_ROUTE(Route, rt_info_del);
        PreRouteDelete(rt);
        RemoveUnresolvedRoute(rt);
        rt->UpdateDependantRoutes();
        rt->ResyncTunnelNextHop();
        ProcessDelete(rt);
        part->Delete(rt);
    } else {
        // Notify deletion of path. 
        part->Notify(rt);
        UpdateDerivedRoutes(rt, NULL, deleted_path_was_active_path);
    }
}

// Inline processing of Route request.
void AgentRouteTable::Process(DBRequest &req) {
    agent()->ConcurrencyCheck();
    DBTablePartition *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
    tpart->Process(NULL, &req);
}

//  Input handler for Route Table.
//  Adds a route entry if not present.
//      Adds path to route entry
//      Paths are sorted in order of their precedence
//  A DELETE request always removes path from the peer
//      Route entry with no paths is automatically deleted
void AgentRouteTable::Input(DBTablePartition *part, DBClient *client,
                            DBRequest *req) {
    AgentRouteKey *key = static_cast<AgentRouteKey *>(req->key.get());
    AgentRouteData *data = static_cast<AgentRouteData *>(req->data.get());
    bool notify = false;
    bool route_added = false;

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(key->vrf_name());
    // Ignore request if VRF not found. Note, we process the DELETE
    // request even if VRF is in deleted state
    if (vrf == NULL) {
        if (req->oper == DBRequest::DB_ENTRY_DELETE) {
            LOG(DEBUG, "VRF <" << key->vrf_name() << 
                "> not found. Ignore route DELETE");
        } else {
            LOG(DEBUG, "VRF <" << key->vrf_name() << "> not found. "
                "> not found. Ignore route ADD/CHANGE");
        }
        return;
    }

    // We dont force DBRequest to be enqueued to the right DB Table.
    // Find the right DBTable from VRF and invoke Input from right table
    AgentRouteTable *route_table = vrf->GetRouteTable(key->GetRouteTableType());
    if (route_table != this) {
        if (route_table == NULL) {
            // route table for the VRF is already deleted
            // vrf should be in deleted state
            assert(vrf->IsDeleted());
            LOG(DEBUG, "Route Table " << key->GetRouteTableType() <<
                " for VRF <" << key->vrf_name() <<
                "> not found. Ignore route Request for " <<
                key->ToString());
            return;
        }
        DBTablePartition *p = static_cast<DBTablePartition *>
            (route_table->GetTablePartition(key));
        route_table->Input(p, client, req);
        return;
    }

    AgentPath *path = NULL;
    AgentRoute *rt = static_cast<AgentRoute *>(part->Find(key));
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        if (key->peer()->SkipAddChangeRequest()) {
            AGENT_ROUTE_LOG(this,
                            "Route operation ignored. Deleted Peer ",
                            key->ToString(), vrf_name(),
                            key->peer()->GetName());
            return;
        }

        if (vrf->IsDeleted() &&
            vrf->allow_route_add_on_deleted_vrf() == false) {
            return;
        }

        if (key->sub_op_ == AgentKey::RESYNC) {
            if (rt && (rt->IsDeleted() == false)) {
                if (data) {
                    path = key->peer() ? rt->FindPath(key->peer()) : NULL;
                    if (path != NULL) {
                        bool ecmp = path->path_preference().is_ecmp();
                        // AddChangePath should be triggered only if a path
                        // is available from the given peer
                        notify = data->AddChangePath(agent_, path, rt);
                        //If a path transition from ECMP to non ECMP
                        //remote the path from ecmp peer
                        if (ecmp && ecmp != path->path_preference().is_ecmp()) {
                            rt->ReComputePathDeletion(path);
                        } else if (rt->ReComputePathAdd(path)) {
                            notify = true;
                        }
                    }
                } else {
                    //Ignore RESYNC if received on non-existing
                    //or deleted route entry
                    rt->Sync();
                    notify = true;
                }
            }
        } else if (key->sub_op_ == AgentKey::ADD_DEL_CHANGE) {
            // Renew the route if its in deleted state
            if (rt && rt->IsDeleted()) {
                rt->ClearDelete();
                ProcessAdd(rt);
                notify = true;
            }

            // Add route if not present already
            if (rt == NULL) {
                //If route is a gateway route first check
                //if its corresponding direct route is present
                //or not, if not present dont add the route
                //just maintain it in unresolved list
                rt = static_cast<AgentRoute *>(key->AllocRouteEntry
                                               (vrf, data->is_multicast()));
                assert(rt->vrf() != NULL);
                part->Add(rt);
                // Mark path as NULL so that its allocated below
                path = NULL;
                ProcessAdd(rt);
                RouteInfo rt_info;
                rt->FillTrace(rt_info, AgentRoute::ADD, NULL);
                OPER_TRACE_ROUTE_ENTRY(Route, this, rt_info);
                route_added = true;
            } else {
                // RT present. Check if path is also present by peer
                path = rt->FindPathUsingKeyData(key, data);
            }

            //Update route with information sent in data
            if (data && data->UpdateRoute(rt)) {
                notify = true;
            }

            // Allocate path if not yet present
            if (path == NULL) {
                path = data->CreateAgentPath(key->peer(), rt);
                rt->InsertPath(path);
                rt->ProcessPath(agent_, part, path, data);
                notify = true;

                RouteInfo rt_info;
                rt->FillTrace(rt_info, AgentRoute::ADD_PATH, path);
                OPER_TRACE_ROUTE_ENTRY(Route, this, rt_info);
            } else {
                bool ecmp = path->path_preference().is_ecmp();
                notify = rt->ProcessPath(agent_, part, path, data);
                //If a path transition from ECMP to non ECMP
                //remote the path from ecmp peer
                if (ecmp && ecmp != path->path_preference().is_ecmp()) {
                    rt->ReComputePathDeletion(path);
                }

                RouteInfo rt_info;

                rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
                OPER_TRACE_ROUTE_ENTRY(Route, this, rt_info);
            }

            if (path->RouteNeedsSync()) 
                rt->Sync();

            if (route_added) {
                EvaluateUnresolvedRoutes();
                EvaluateUnresolvedNH();
            }

            //Used for routes which have use more than one peer information
            //to compute the nexthops.
            if (rt->ReComputePathAdd(path)) {
                notify = true;
            }
        } else {
            assert(0);
        }
    } else if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        assert (key->sub_op_ == AgentKey::ADD_DEL_CHANGE);
        if (rt)
            rt->DeletePathUsingKeyData(key, data, false);
    } else {
        assert(0);
    }

    //If this route has a unresolved path, insert to unresolved list
    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE ||
        key->sub_op_ == AgentKey::RESYNC) {
        if (rt && rt->HasUnresolvedPath() == true) {
            AddUnresolvedRoute(rt);
        }
    }

    //Route changed, trigger change on dependent routes
    if (notify) {
        bool was_active_path = (path == rt->GetActivePath());
        const Path *prev_front = rt->front();
        if (prev_front) {
            rt->Sort(&AgentRouteTable::PathSelection, prev_front);
        }
        part->Notify(rt);
        rt->UpdateDependantRoutes();
        rt->ResyncTunnelNextHop();
        //Since newly added path became active path, send path with path_changed
        //flag as true. Path can be NULL for route resync requests.
        UpdateDerivedRoutes(rt, path, was_active_path ||
                            (path == rt->GetActivePath()));
    }
}

LifetimeActor *AgentRouteTable::deleter() {
    return deleter_.get();
}

//Delete all the routes
void AgentRouteTable::ManagedDelete() {
    DBTableWalker *walker = agent_->db()->GetWalker();
    RouteTableWalkerState *state = new RouteTableWalkerState(deleter());
    walker->WalkTable(this, NULL, 
         boost::bind(&AgentRouteTable::DelExplicitRouteWalkerCb, this, _1, _2),
         boost::bind(&AgentRouteTable::DeleteRouteDone, this, _1, state));
    deleter_->Delete();
}

void AgentRouteTable::RetryDelete() {
    if (!deleter()->IsDeleted()) {
        return;
    }
    if (empty()) {
        vrf_entry()->RetryDelete();
    }
    deleter()->RetryDelete();
}

// Find entry not in deleted state
AgentRoute *AgentRouteTable::FindActiveEntry(const AgentRouteKey *key) {
    AgentRoute *entry = static_cast<AgentRoute *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

AgentRoute *AgentRouteTable::FindActiveEntryNoLock(const AgentRouteKey *key) {
    DBTable *table = static_cast<DBTable *>(this);
    AgentRoute *entry = static_cast<AgentRoute *>(table->FindNoLock(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

AgentRoute *AgentRouteTable::FindActiveEntry(const AgentRoute *key) {
    AgentRoute *entry = static_cast<AgentRoute *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

AgentRoute *AgentRouteTable::FindActiveEntryNoLock(const AgentRoute *key) {
    DBTable *table = static_cast<DBTable *>(this);
    AgentRoute *entry = static_cast<AgentRoute *>(table->FindNoLock(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

const std::string &AgentRouteTable::vrf_name() const {
    return vrf_entry_->GetName();
}

VrfEntry *AgentRouteTable::vrf_entry() const {
    return vrf_entry_.get();
}

void AgentRouteTable::NotifyEntry(AgentRoute *e) {
    agent()->ConcurrencyCheck();
    DBTablePartBase *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(e));
    tpart->Notify(e);
}

uint32_t AgentRoute::GetActiveLabel() const {
    return GetActivePath()->label();
};

const string &AgentRoute::dest_vn_name() const { 
    assert(GetActivePath()->dest_vn_list().size() <= 1);
    return *GetActivePath()->dest_vn_list().begin();
};

string AgentRoute::ToString() const {
    return "Route Entry";
}

bool AgentRoute::IsLess(const DBEntry &rhs) const {
    int cmp = CompareTo(static_cast<const Route &>(rhs));
    return (cmp < 0);
};

uint32_t AgentRoute::vrf_id() const {
    return vrf_->vrf_id();
}

void AgentRoute::InsertPath(const AgentPath *path) {
	const Path *prev_front = front();
    insert(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
}

void AgentRoute::RemovePathInternal(AgentPath *path) {
    remove(path);
    path->clear_sg_list();
}

void AgentRoute::RemovePath(AgentPath *path) {
    const Path *prev_front = front();
    RemovePathInternal(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
    delete path;
    return;
}

AgentPath *AgentRoute::FindLocalVmPortPath() const {
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() == NULL) {
            continue;
        }
        if (path->peer()->export_to_controller()) {
            return const_cast<AgentPath *>(path);
        }

        if (path->peer()->GetType() == Peer::ECMP_PEER ||
            path->peer()->GetType() == Peer::VGW_PEER ||
            path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER ||
            path->peer()->GetType() == Peer::MULTICAST_TOR_PEER ||
            path->peer()->GetType() == Peer::OVS_PEER) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
}

void AgentRoute::DeletePathInternal(AgentPath *path) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(get_table());
    table->DeletePathFromPeer(get_table_partition(), this, path);
}

//Deletes the path created by peer in key.
//Ideally only peer match is required in path however for few cases
//more than peer comparision be required.
//force_delete is used to specify if only peer check needs to be done or
//extra checks other than peer check has to be done.
//
//Explicit route walker used for deleting paths will set force_delete as true.
void AgentRoute::DeletePathUsingKeyData(const AgentRouteKey *key,
                                        const AgentRouteData *data,
                                        bool force_delete) {
    AgentPath *peer_path = FindPathUsingKeyData(key, data);
    bool delete_path = true;
    if (data && peer_path) {
        delete_path = data->
            CanDeletePath(static_cast<AgentRouteTable *>(get_table())->
                          agent(), peer_path, this);
    }
    if (delete_path)
        DeletePathInternal(peer_path);
}

AgentPath *AgentRoute::FindPathUsingKeyData(const AgentRouteKey *key,
                                            const AgentRouteData *data) const {
    return FindPath(key->peer());
}

AgentPath *AgentRoute::FindPath(const Peer *peer) const {
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() == peer) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
}

// First path in list is always treated as active path.
const AgentPath *AgentRoute::GetActivePath() const {
    const AgentPath *path = static_cast<const AgentPath *>(front());
    return (path ? path->UsablePath() : NULL);
}

const NextHop *AgentRoute::GetActiveNextHop() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;

    return path->ComputeNextHop(static_cast<AgentRouteTable *>(get_table())->
                            agent());
}

bool AgentRoute::IsRPFInvalid() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL) {
        return false;
    }

    return path->is_subnet_discard();
}

// If a direct route has changed, invoke a change on tunnel NH dependent on it
void AgentRoute::ResyncTunnelNextHop(void) {
    for (AgentRoute::TunnelNhDependencyList::iterator iter = 
         tunnel_nh_list_.begin(); iter != tunnel_nh_list_.end(); iter++) {

        NextHop *nh = static_cast<NextHop *>(iter.operator->());
        DBEntryBase::KeyPtr key = nh->GetDBRequestKey();
        NextHopKey *nh_key = static_cast<NextHopKey *>(key.get());
        nh_key->sub_op_ = AgentKey::RESYNC;

        DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
        req.key = key;
        req.data.reset(NULL);
        Agent *agent = (static_cast<AgentRouteTable *>(get_table()))->agent();
        agent->nexthop_table()->Enqueue(&req);
    }
}

// Enqueue request to RESYNC a route
void AgentRoute::EnqueueRouteResync(void) const {
    DBRequest  req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key = GetDBRequestKey();
    (static_cast<AgentKey *>(req.key.get()))->sub_op_ = AgentKey::RESYNC;
    Agent *agent = (static_cast<AgentRouteTable *>(get_table()))->agent();
    agent->fabric_inet4_unicast_table()->Enqueue(&req);
}

//If a direct route get modified invariably trigger change
//on all dependent indirect routes, coz if a nexthop has 
//changed we need to update the same in datapath for indirect
//routes
void AgentRoute::UpdateDependantRoutes(void) {
    for (AgentRoute::RouteDependencyList::iterator iter = 
         dependant_routes_.begin(); iter != dependant_routes_.end(); iter++) {
        AgentRoute *rt = iter.operator->();
        rt->EnqueueRouteResync();
    }
}

bool AgentRoute::HasUnresolvedPath(void) {
    for(Route::PathList::const_iterator it = GetPathList().begin();
            it != GetPathList().end(); it++) {
        const AgentPath *path =
            static_cast<const AgentPath *>(it.operator->());
        if (path->unresolved() == true) {
            return true;
        }
    }

    return false;
}

// Invoke SYNC on all paths to re-evaluate NH/active state
bool AgentRoute::Sync(void) {
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

bool AgentRoute::WaitForTraffic() const {
    for(Route::PathList::const_iterator it = GetPathList().begin();
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() && path->peer()->GetType() == Peer::INET_EVPN_PEER) {
            continue;
        }
        if (path->path_preference().wait_for_traffic() == true) {
            return true;
        }
    }
    return false;
}

bool AgentRoute::ProcessPath(Agent *agent, DBTablePartition *part,
                             AgentPath *path, AgentRouteData *data) {
    bool ret = data->AddChangePath(agent, path, this);
    if (RecomputeRoutePath(agent, part, path, data)) {
        ret = true;
    }
    return ret;
}

AgentPath *AgentRouteData::CreateAgentPath(const Peer *peer,
                                           AgentRoute *rt) const {
    return (new AgentPath(peer, rt));
}

bool AgentRouteData::AddChangePath(Agent *agent, AgentPath *path,
                                   const AgentRoute *rt) {
    path->set_peer_sequence_number(sequence_number_);
    return AddChangePathExtended(agent, path, rt);
}
