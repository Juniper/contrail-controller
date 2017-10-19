/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <init/agent_param.h>
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
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;
using namespace boost::asio;

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

/////////////////////////////////////////////////////////////////////////////
// AgentRouteTable routines
/////////////////////////////////////////////////////////////////////////////
AgentRouteTable::AgentRouteTable(DB *db, const std::string &name):
    RouteTable(db, name), agent_(NULL), vrf_id_(0), vrf_entry_(NULL, this),
    deleter_(NULL), vrf_delete_ref_(this, NULL), unresolved_rt_tree_(),
    unresolved_nh_tree_() {
    OperDBTraceBuf = SandeshTraceBufferCreate("OperRoute", 5000);
}

AgentRouteTable::~AgentRouteTable() {
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

// Algorithm to select an active path from multiple potential paths.
// Uses comparator in path for selection
bool AgentRouteTable::PathSelection(const Path &path1, const Path &path2) {
    const AgentPath &l_path = dynamic_cast<const AgentPath &> (path1);
    const AgentPath &r_path = dynamic_cast<const AgentPath &> (path2);
    return l_path.IsLess(r_path);
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

//Delete all the routes
void AgentRouteTable::ManagedDelete() {
    RouteTableWalkerState *state = new RouteTableWalkerState(deleter());
    DBTable::DBTableWalkRef walk_ref = AllocWalker(
         boost::bind(&AgentRouteTable::DelExplicitRouteWalkerCb, this, _1, _2),
         boost::bind(&AgentRouteTable::DeleteRouteDone, this, _1, _2, state));
    //On managed delete, walk to delete paths need to be done once as no route
    //should be added in deleted vrf.
    //Once the walk is over walkdone will reset walk_ref.
    WalkTable(walk_ref);
    deleter_->Delete();
}

void AgentRouteTable::DeleteRouteDone(DBTable::DBTableWalkRef walk_ref,
                                      DBTableBase *base,
                                      RouteTableWalkerState *state) {
    LOG(DEBUG, "Deleted all BGP injected routes for " << base->name());
    ReleaseWalker(walk_ref);
    delete state;
}

bool AgentRouteTable::DelExplicitRouteWalkerCb(DBTablePartBase *part,
                                  DBEntryBase *entry) {
    AgentRoute *route = static_cast<AgentRoute *>(entry);
    if (route == NULL)
        return true;

    return route->DeleteAllBgpPath(part, this);
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

void AgentRouteTable::NotifyEntry(AgentRoute *e) {
    agent()->ConcurrencyCheck();
    DBTablePartBase *tpart =
        static_cast<DBTablePartition *>(GetTablePartition(e));
    tpart->Notify(e);
}

/////////////////////////////////////////////////////////////////////////////
// Agent route input processing
/////////////////////////////////////////////////////////////////////////////

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

    VrfEntry *vrf = agent_->vrf_table()->FindVrfFromName(key->vrf_name());
    // Ignore request if VRF not found.
    // VRF in deleted state is handled below based on operation
    if (vrf == NULL)
        return;

    // We dont force DBRequest to be enqueued to the right DB Table.
    // Find the right DBTable from VRF and invoke Input from right table
    AgentRouteTable *req_table = vrf->GetRouteTable(key->GetRouteTableType());
    if (req_table != this) {
        if (req_table == NULL) {
            // If route table is already deleted from VRF, it means VRF should
            // also be in deleted state
            assert(vrf->IsDeleted());
            return;
        }

        DBTablePartition *p =
            static_cast<DBTablePartition *>(req_table->GetTablePartition(key));
        req_table->Input(p, client, req);
        return;
    }

    AgentRoute *rt = static_cast<AgentRoute *>(part->Find(key));
    if (req->oper == DBRequest::DB_ENTRY_DELETE) {
        if (rt)
            rt->DeleteInput(part, this, key, data);
        return;
    }

    if (req->oper == DBRequest::DB_ENTRY_ADD_CHANGE) {
        AddChangeInput(part, vrf, rt, key, data);
        return;
    }

    assert(0);
}

// Handle RESYNC and ADD_CHANGE requests
void AgentRouteTable::AddChangeInput(DBTablePartition *part, VrfEntry *vrf,
                                     AgentRoute *rt, AgentRouteKey *key,
                                     AgentRouteData *data) {
    if (key->peer()->SkipAddChangeRequest()) {
        AGENT_ROUTE_LOG(this, "Route operation ignored. Deleted Peer ",
                        key->ToString(), vrf_name(), key->peer()->GetName());
        return;
    }

    if (vrf->IsDeleted() && vrf->allow_route_add_on_deleted_vrf() == false)
        return;

    AgentPath *path = NULL;
    bool notify = false;
    if (key->sub_op_ == AgentKey::RESYNC) {
        // Process RESYNC only if route present and not-deleted
        if (rt && (rt->IsDeleted() == false))
            notify |= rt->SubOpResyncInput(vrf, this, &path, key, data);
    } else if (key->sub_op_ == AgentKey::ADD_DEL_CHANGE) {
        bool route_added = (rt == NULL);
        rt = LocateRoute(part, vrf, rt, key, data, &notify);
        notify |= rt->SubOpAddChangeInput(vrf, this, &path, key, data,
                                          route_added);
    } else {
        assert(0);
    }

    // If this route has a unresolved path, insert to unresolved list
    if (rt && rt->HasUnresolvedPath() == true)
        AddUnresolvedRoute(rt);

    // Route changed, trigger change on dependent routes
    if (notify) {
        bool active_path_changed = (path == rt->GetActivePath());
        const Path *prev_front = rt->front();
        if (prev_front) {
            rt->Sort(&AgentRouteTable::PathSelection, prev_front);
        }
        part->Notify(rt);
        rt->UpdateDependantRoutes();
        rt->ResyncTunnelNextHop();
        // Since newly added path became active path, send path with
        // path_changed flag as true. Path can be NULL for resync requests
        active_path_changed |= (path == rt->GetActivePath());
        rt->UpdateDerivedRoutes(this, path, active_path_changed);
    }
}

AgentRoute *AgentRouteTable::LocateRoute(DBTablePartition *part,
                                         VrfEntry *vrf, AgentRoute *rt,
                                         AgentRouteKey *key,
                                         AgentRouteData *data, bool *notify) {
    // Return if route already present and not deleted
    if (rt != NULL && rt->IsDeleted() == false)
        return rt;

    // Add route if not present already
    if (rt == NULL) {
        rt = static_cast<AgentRoute *>
            (key->AllocRouteEntry(vrf, data->is_multicast()));
        assert(rt->vrf() != NULL);
        part->Add(rt);
    }

    // Renew the route if its in deleted state
    if (rt->IsDeleted()) {
        assert(rt->IsDeleted());
        rt->ClearDelete();
        *notify = true;
    }

    ProcessAdd(rt);
    RouteInfo rt_info;
    rt->FillTrace(rt_info, AgentRoute::ADD, NULL);
    OPER_TRACE_ROUTE_ENTRY(Route, this, rt_info);
    return rt;
}

// Re-evaluate all unresolved NH. Flush and enqueue RESYNC for all NH in the
// unresolved NH tree
void AgentRouteTable::EvaluateUnresolvedNH(void) {
    for (UnresolvedNHTree::iterator it = unresolved_nh_tree_.begin();
         it != unresolved_nh_tree_.end(); ++it) {
        (*it)->EnqueueResync();

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
       (*it)->EnqueueRouteResync();
    }

    unresolved_rt_tree_.clear();
}

void AgentRouteTable::AddUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.insert(rt);
}

void AgentRouteTable::RemoveUnresolvedRoute(const AgentRoute *rt) {
    unresolved_rt_tree_.erase(rt);
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

LifetimeActor *AgentRouteTable::deleter() {
    return deleter_.get();
}

const std::string &AgentRouteTable::vrf_name() const {
    return vrf_entry_->GetName();
}

VrfEntry *AgentRouteTable::vrf_entry() const {
    return vrf_entry_.get();
}

/////////////////////////////////////////////////////////////////////////////
// AgentRoute routines
/////////////////////////////////////////////////////////////////////////////

// Hnadle RESYNC operation for a route
bool AgentRoute::SubOpResyncInput(VrfEntry *vrf, AgentRouteTable *table,
                                  AgentPath **path_ptr, AgentRouteKey *key,
                                  AgentRouteData *data) {
    // Handle change to route itself and not to a particular path
    if (data == NULL || key->peer() == NULL) {
        Sync();
        return true;
    }

    // Get path to update
    AgentPath *path = FindPath(key->peer());
    if (path == NULL)
        return false;

    bool ret = false;
    *path_ptr = path;
    bool old_ecmp = path->path_preference().is_ecmp();
    if (data->AddChangePath(table->agent(), path, this))
        ret = true;

    // Transition from ECMP to non-ECMP should result in removal of member
    // from ECMP path
    if (old_ecmp && old_ecmp != path->path_preference().is_ecmp()) {
        ReComputePathDeletion(path);
        return ret;
    }

    if (ReComputePathAdd(path))
        ret = true;

    return ret;
}

bool AgentRoute::SubOpAddChangeInput(VrfEntry *vrf, AgentRouteTable *table,
                                     AgentPath **path_ptr, AgentRouteKey *key,
                                     AgentRouteData *data, bool route_added) {
    bool ret = false;
    // Update route level attributes first
    if (data && data->UpdateRoute(this))
        ret = true;

    AgentRoute::Trace event;
    AgentPath *path = FindPathUsingKeyData(key, data);
    // Allocate path if not yet present
    if (path == NULL) {
        path = data->CreateAgentPath(key->peer(), this);
        InsertPath(path);
        data->AddChangePath(table->agent(), path, this);
        ret = true;
        event = AgentRoute::ADD_PATH;
    } else {
        bool ecmp = path->path_preference().is_ecmp();
        if (data->AddChangePath(table->agent(), path, this))
            ret = true;

        // Transition from ECMP to non-ECMP should result in removal of member
        // from ECMP path
        if (ecmp && ecmp != path->path_preference().is_ecmp()) {
            ReComputePathDeletion(path);
        }
        event = AgentRoute::CHANGE_PATH;
    }

    // Trace log for path add/change
    RouteInfo rt_info;
    FillTrace(rt_info, event, path);
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);

    if (path->RouteNeedsSync()) 
        ret |= Sync();

    if (route_added) {
        table->EvaluateUnresolvedRoutes();
        table->EvaluateUnresolvedNH();
    }

    // Do necessary recompute on addition of path
    if (ReComputePathAdd(path))
        ret = true;

    *path_ptr = path;
    return ret;
}

// Handle DELETE operation for a route. Deletes path created by peer in key.
// Ideally only peer match is required in path however for few cases
// more than peer comparision be required.
void AgentRoute::DeleteInput(DBTablePartition *part, AgentRouteTable *table,
                             AgentRouteKey *key, AgentRouteData *data) {
    assert (key->sub_op_ == AgentKey::ADD_DEL_CHANGE);
    bool force_delete = false;
    // If peer in key is deleted, set force_delete to true.
    if (key->peer()->IsDeleted() || key->peer()->SkipAddChangeRequest())
        force_delete = true;

    // In case of multicast routes, BGP can give multiple paths.
    // So, iterate thru all paths and identify paths that can be deleted
    for (Route::PathList::iterator it = GetPathList().begin();
         it != GetPathList().end(); ) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        // Current path can be deleted below. Incremnt the iterator and dont
        // use it again below
        it++;

        if (key->peer() != path->peer())
            continue;

        bool check_can_delete =
            ((key->peer()->GetType() == Peer::BGP_PEER) ||
             (key->peer()->GetType() == Peer::EVPN_PEER));

        if (force_delete)
            check_can_delete = false;

        // There are two ways to receive delete of BGP peer path in l2 route.
        // First is via withdraw meesage from control node in which
        // force_delete will be false and vxlan_id will be matched to
        // decide.
        // Second can be via route walkers where on peer going down or vrf
        // delete, paths from BGP peer should be deleted irrespective of
        // vxlan_id.
        if (check_can_delete && data &&
            data->CanDeletePath(table->agent(), path, this) == false) {
            continue;
        }

        DeletePathFromPeer(part, table, path);
    }
}

void AgentRoute::InsertPath(const AgentPath *path) {
	const Path *prev_front = front();
    insert(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
}

void AgentRoute::RemovePath(AgentPath *path) {
    const Path *prev_front = front();
    remove(path);
    Sort(&AgentRouteTable::PathSelection, prev_front);
    return;
}

// Delete all paths from BGP Peer. Delete route if no path left
bool AgentRoute::DeleteAllBgpPath(DBTablePartBase *part,
                                  AgentRouteTable *table) {
    for(Route::PathList::iterator it = GetPathList().begin();
        it != GetPathList().end();) {
        AgentPath *path = static_cast<AgentPath *>(it.operator->());
        it++;

        const Peer *peer = path->peer();
        if (peer == NULL)
            continue;

        if (peer->GetType() == Peer::BGP_PEER ||
            peer->GetType() == Peer::MULTICAST_FABRIC_TREE_BUILDER) {
            DeletePathFromPeer(part, table, path);
        }
    }

    return true;
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
void AgentRoute::DeletePathFromPeer(DBTablePartBase *part,
                                    AgentRouteTable *table, AgentPath *path) {

    RouteInfo rt_info;
    FillTrace(rt_info, AgentRoute::DELETE_PATH, path);
    OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info);

    if (path == NULL) {
        return;
    }

    // Assign path to auto-pointer to delete it on return
    std::auto_ptr<AgentPath> path_ref(path);

    // TODO : Move this to end of delete processing?
    // Path deletion can result in changes such as ECMP-NH, Mulitcast NH etc
    // The algirthms expect path to be present in route still. So, do the
    // necessary recompute before path is deleted from route
    ReComputePathDeletion(path);

    // Store if this was active path
    bool active_path = (GetActivePath() == path);
    // Remove path from the route
    RemovePath(path);

    // TODO : Move this code to ECMP Hash management code.
    CompositeNH *cnh = dynamic_cast<CompositeNH *>(path->nexthop());
    if (cnh) {
        path->ResetEcmpHashFields();
        cnh->UpdateEcmpHashFieldsUponRouteDelete(table->agent(),
                                                 table->vrf_name());
    }

    // Delete route if no more paths
    if (GetActivePath() == NULL) {
        RouteInfo rt_info_del;
        FillTrace(rt_info_del, AgentRoute::DELETE, NULL);
        OPER_TRACE_ROUTE_ENTRY(Route, table, rt_info_del);
        DeleteDerivedRoutes(table);
        table->RemoveUnresolvedRoute(this);
        UpdateDependantRoutes();
        ResyncTunnelNextHop();
        table->ProcessDelete(this);
        part->Delete(this);
    } else {
        // Notify deletion of path.
        part->Notify(this);
        UpdateDerivedRoutes(table, NULL, active_path);
    }
}

AgentPath *AgentRoute::FindLocalPath() const {
    for(Route::PathList::const_iterator it = GetPathList().begin(); 
        it != GetPathList().end(); it++) {
        const AgentPath *path = static_cast<const AgentPath *>(it.operator->());
        if (path->peer() == NULL) {
            continue;
        }
        if (path->peer()->GetType() == Peer::LOCAL_PEER ||
            path->peer()->GetType() == Peer::LINKLOCAL_PEER) {
            return const_cast<AgentPath *>(path);
        }
    }
    return NULL;
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

uint32_t AgentRoute::GetActiveLabel() const {
    return GetActivePath()->label();
};

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

    if (agent->tsn_enabled() && !agent->forwarding_enabled()) {
        bool is_inet_rt = false;
        bool service_address = false;
        if ((rt->GetTableType() == Agent::INET4_UNICAST) ||
            (rt->GetTableType() == Agent::INET6_UNICAST)) {
            is_inet_rt = true;
            service_address = agent->params()->
                IsConfiguredTsnHostRoute(rt->GetAddressString());
        }
        Peer::Type type = path->peer()->GetType();
        bool local_route = false;
        if ((type == Peer::LINKLOCAL_PEER) ||
            (type == Peer::LOCAL_PEER))
            local_route = true;
        if (rt->FindLocalPath())
            local_route = true;
        if (is_inet_rt && (!service_address && !local_route) &&
            (rt->vrf()->GetName().compare(agent->fabric_vrf_name()) != 0))
            path->set_inactive(true);
    }

    return AddChangePathExtended(agent, path, rt);
}
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

bool AgentRoute::IsRPFInvalid() const {
    const AgentPath *path = GetActivePath();
    if (path == NULL) {
        return false;
    }

    return path->is_subnet_discard();
}
