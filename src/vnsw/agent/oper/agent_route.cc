/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <cmn/agent_cmn.h>
#include <oper/route_common.h>
#include <oper/vrf.h>
#include <oper/tunnel_nh.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <controller/controller_export.h>
#include <oper/agent_sandesh.h>

using namespace std;
using namespace boost::asio;

SandeshTraceBufferPtr AgentDBwalkTraceBuf(SandeshTraceBufferCreate(
    AGENT_DBWALK_TRACE_BUF, 1000));

class AgentRouteTable::DeleteActor : public LifetimeActor {
  public:
    DeleteActor(AgentRouteTable *rt_table) : 
        LifetimeActor(rt_table->agent()->GetLifetimeManager()), 
        table_(rt_table) { 
    }
    virtual ~DeleteActor() { 
    }
    virtual bool MayDelete() const {
        if (table_->HasListeners() || table_->Size() != 0) {
            return false;
        }
        return true;
    }
    virtual void Shutdown() {
    }
    virtual void Destroy() {
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
    RouteTable(db, name), agent_(NULL), deleter_(NULL),
    vrf_delete_ref_(this, NULL) {
}

AgentRouteTable::~AgentRouteTable() {
}

const string &AgentRouteTable::GetSuffix(Agent::RouteTableType type) {
    static const string uc_suffix(".uc.route.0");
    static const string mc_suffix(".mc.route.0");
    static const string l2_suffix(".l2.route.0");

    switch (type) {
    case Agent::INET4_UNICAST:
        return uc_suffix;
    case Agent::INET4_MULTICAST:
        return mc_suffix;
    case Agent::LAYER2:
        return l2_suffix;
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
    VrfEntry *vrf = 
        static_cast<VrfEntry *>(agent_->GetVrfTable()->Find(&vrf_key, true));
    AgentRoute *route = 
        static_cast<AgentRoute *>(key->AllocRouteEntry(vrf, false));
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(route));
}

// Delete all paths from BGP Peer. Delete route if no path left
bool AgentRouteTable::DeleteAllBgpPath(DBTablePartBase *part,
                                       DBEntryBase *entry) {
    AgentRoute *route = static_cast<Inet4UnicastRouteEntry *>(entry);
    if (route && !route->IsDeleted()) {
        for(Route::PathList::iterator it = route->GetPathList().begin();
            it != route->GetPathList().end();) {
            const AgentPath *path =
                static_cast<const AgentPath *>(it.operator->());
            const Peer *peer = path->peer();
            it++;
            if (peer && peer->GetType() == Peer::BGP_PEER) {
                DeletePathFromPeer(part, route, path->peer());
            }
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

    // Stale path should take last precedence
    if (l_path.is_stale() != r_path.is_stale()) {
        return (l_path.is_stale() < r_path.is_stale());
    }
    return l_path.peer()->IsLess(r_path.peer());
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
                                         AgentRoute *rt, const Peer *peer) {
    if (rt == NULL) {
        return;
    }

    // Find path for the peer
    AgentPath *path = rt->FindPath(peer);

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::DELETE_PATH, path);
    OPER_TRACE(Route, rt_info);

    if (path == NULL) {
        return;
    }

    // Remove path from the route
    rt->RemovePath(path);
    // Local path(non BGP type) is going away and so will route.
    // For active peers reflector will remove the route but for 
    // non active peers explicitly squash the paths.
    if (peer && (peer->GetType() != Peer::BGP_PEER)) {
        rt->SquashStalePaths(NULL);
    }

    // Delete route if no more paths 
    if (rt->GetActivePath() == NULL) {
        RouteInfo rt_info_del;
        rt->FillTrace(rt_info_del, AgentRoute::DELETE, NULL);
        OPER_TRACE(Route, rt_info_del);
        RemoveUnresolvedRoute(rt);
        rt->UpdateDependantRoutes();
        rt->ResyncTunnelNextHop();
        ProcessDelete(rt);
        part->Delete(rt);
    } else {
        // Notify deletion of path. 
        part->Notify(rt);
    }
}

// Inline processing of Route request.
void AgentRouteTable::Process(DBRequest &req) {
    CHECK_CONCURRENCY("db::DBTable");
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

    VrfEntry *vrf = agent_->GetVrfTable()->FindVrfFromName(key->vrf_name());
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
        DBTablePartition *p = static_cast<DBTablePartition *>
            (route_table->GetTablePartition(key));
        route_table->Input(p, client, req);
        return;
    }

    AgentPath *path = NULL;
    AgentRoute *rt = static_cast<AgentRoute *>(part->Find(key));

    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        // Ignore ADD_CHANGE if received on deleted VRF
        if(vrf->IsDeleted()) {
            return;
        }

        if (key->sub_op_ == AgentKey::RESYNC) {
            // Ignore RESYNC if received on non-existing or deleted route entry
            if (rt && (rt->IsDeleted() == false)) {
                rt->Sync();
                notify = true;
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
                OPER_TRACE(Route, rt_info);
                route_added = true;
                AGENT_ROUTE_LOG("Added route", rt->ToString(), vrf_name(),
                                key->peer());
            } else {
                // RT present. Check if path is also present by peer
                path = rt->FindPath(key->peer());
            }

            // Allocate path if not yet present
            if (path == NULL) {
                path = new AgentPath(key->peer(), rt);
                rt->InsertPath(path);
                data->AddChangePath(agent_, path);
                notify = true;

                RouteInfo rt_info;
                rt->FillTrace(rt_info, AgentRoute::ADD_PATH, path);
                OPER_TRACE(Route, rt_info);
            } else {
                // Let path know of route change and update itself
                path->set_is_stale(false);
                notify = data->AddChangePath(agent_, path);
                RouteInfo rt_info;

                rt->FillTrace(rt_info, AgentRoute::CHANGE_PATH, path);
                OPER_TRACE(Route, rt_info);
                AGENT_ROUTE_LOG("Path change", rt->ToString(), vrf_name(),
                                key->peer());
            }

            if (path->RouteNeedsSync()) 
                rt->Sync();

            if (route_added) {
                EvaluateUnresolvedRoutes();
                EvaluateUnresolvedNH();
            }

            // ECMP path are managed by route module. Update ECMP path with 
            // addition of new path
            if (rt->EcmpAddPath(path)) {
                notify = true;
            }
        } else {
            assert(0);
        }
    } else if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        assert (key->sub_op_ == AgentKey::ADD_DEL_CHANGE);
        DeletePathFromPeer(part, rt, key->peer());
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
        part->Notify(rt);
        rt->UpdateDependantRoutes();
        rt->ResyncTunnelNextHop();
    }
}

LifetimeActor *AgentRouteTable::deleter() {
    return deleter_.get();
}

//Delete all the routes
void AgentRouteTable::ManagedDelete() {
    DBTableWalker *walker = agent_->GetDB()->GetWalker();
    RouteTableWalkerState *state = new RouteTableWalkerState(deleter());
    walker->WalkTable(this, NULL, 
         boost::bind(&AgentRouteTable::DelExplicitRouteWalkerCb, this, _1, _2),
         boost::bind(&AgentRouteTable::DeleteRouteDone, this, _1, state));
    deleter_->Delete();
}

// If the table has entries, deletion cannot be resumed
void AgentRouteTable::MayResumeDelete(bool is_empty) {
    if (!deleter()->IsDeleted()) {
        return;
    }

    if (!is_empty) {
        return;
    }

    agent_->GetLifetimeManager()->Enqueue(deleter());
}

// Find entry not in deleted state
AgentRoute *AgentRouteTable::FindActiveEntry(const AgentRouteKey *key) {
    AgentRoute *entry = static_cast<AgentRoute *>(Find(key));
    if (entry && entry->IsDeleted()) {
        return NULL;
    }
    return entry;
}

uint32_t AgentRoute::GetMplsLabel() const { 
    return GetActivePath()->label();
};

const string &AgentRoute::dest_vn_name() const { 
    return GetActivePath()->dest_vn_name();
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
    // ECMP path are managed by route module. Update ECMP path with this path
    // delete
    EcmpDeletePath(path);
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
        if (path->peer()->GetType() == Peer::ECMP_PEER ||
            path->peer()->GetType() == Peer::LOCAL_VM_PORT_PEER ||
            path->peer()->GetType() == Peer::VGW_PEER) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
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

void AgentRoute::SquashStalePaths(const AgentPath *exception_path) {
    Route::PathList::iterator it = GetPathList().begin();

    while (it != GetPathList().end()) {
        // Delete all stale path except for the path sent(exception_path)
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        if (path->is_stale() && (path != exception_path)) {
            // Since we squash stales, at any point of time there should be only
            // one stale other than exception_path in list
            RemovePath(path);
            return;
        }
        it++;
    }
}

// First path in list is always treated as active path.
const AgentPath *AgentRoute::GetActivePath() const {
    return static_cast<const AgentPath *>(front());
}

const NextHop *AgentRoute::GetActiveNextHop() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL)
        return NULL;

    return path->nexthop(static_cast<AgentRouteTable *>(get_table())->agent());
}

bool AgentRoute::IsRPFInvalid() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL) {
        return false;
    }

    return path->is_subnet_discard();
}

// This is for handling shared tree across different multicast routes,
// Unsubscribe shud be sent when all routes using tree are gone. Similarly
// subscription should be sent only for first route.
// Currently shared tree is only used for flood multicast.
// TODO: Move this to controller code
bool AgentRoute::CanDissociate() const {
    bool can_dissociate = IsDeleted();
    if (is_multicast()) {
        const NextHop *nh = GetActiveNextHop();
        const CompositeNH *cnh = static_cast<const CompositeNH *>(nh);
        if (cnh && cnh->ComponentNHCount() == 0) 
            return true;
        if (GetTableType() == Agent::LAYER2) {
            const MulticastGroupObject *obj = 
                MulticastHandler::GetInstance()->
                FindFloodGroupObject(vrf()->GetName());
            if (obj) {
                can_dissociate &= !obj->Ipv4Forwarding();
            }
        }

        if (GetTableType() == Agent::INET4_MULTICAST) {
            const MulticastGroupObject *obj = 
                MulticastHandler::GetInstance()->
                FindFloodGroupObject(vrf()->GetName());
            if (obj) {
                can_dissociate &= !obj->layer2_forwarding();
            }
        }
    }
    return can_dissociate;
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
    get_table()->Enqueue(&req);
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

void AgentRouteTable::StalePathFromPeer(DBTablePartBase *part, AgentRoute *rt,
                                        const Peer *peer) {
    if (rt == NULL) {
        return;
    }

    // Find path for the peer
    AgentPath *path = rt->FindPath(peer);

    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::STALE_PATH, path);
    OPER_TRACE(Route, rt_info);

    if (path == NULL) {
        return;
    }

    if (rt && (rt->IsDeleted() == false)) {
        path->set_is_stale(true);
        // Remove all stale path except the path received
        rt->SquashStalePaths(path);
        rt->GetPathList().sort(&AgentRouteTable::PathSelection);
        rt->Sync();
        part->Notify(rt);
    }
}
