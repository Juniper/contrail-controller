/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/path_resolver.h"

#include <boost/foreach.hpp>

#include "base/lifetime.h"
#include "base/set_util.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/extended-community/vrf_route_import.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"
#include "bgp/rtarget/rtarget_address.h"

using std::make_pair;
using std::string;
using std::vector;

//
// Return true if the prefix for the BgpRoute is the same as given IpAddress.
//
static bool RoutePrefixIsAddress(Address::Family family, const BgpRoute *route,
    const IpAddress &address) {
    if (family == Address::INET) {
        const InetRoute *inet_route = static_cast<const InetRoute *>(route);
        if (inet_route->GetPrefix().addr() == address.to_v4() &&
            inet_route->GetPrefix().prefixlen() == Address::kMaxV4PrefixLen) {
            return true;
        }
    } else if (family == Address::INET6) {
        const Inet6Route *inet6_route = static_cast<const Inet6Route *>(route);
        if (inet6_route->GetPrefix().addr() == address.to_v6() &&
            inet6_route->GetPrefix().prefixlen() == Address::kMaxV6PrefixLen) {
            return true;
        }
    }
    return false;
}

// Return true if prefix matches the address.
bool PathResolver::RoutePrefixMatch(const BgpRoute *route,
                                    const IpAddress &address) {
    const InetRoute *inet_route = dynamic_cast<const InetRoute *>(route);
    if (inet_route) {
        Ip4Prefix prefix(address.to_v4(), Address::kMaxV4PrefixLen);
        return prefix.IsMoreSpecific(inet_route->GetPrefix());
    }

    const Inet6Route *inet6_route = dynamic_cast<const Inet6Route *>(route);
    assert(inet6_route);
    Inet6Prefix prefix(address.to_v6(), Address::kMaxV6PrefixLen);
    return prefix.IsMoreSpecific(inet6_route->GetPrefix());
}

class PathResolver::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(PathResolver *resolver)
        : LifetimeActor(resolver->table()->server()->lifetime_manager()),
          resolver_(resolver) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        return resolver_->MayDelete();
    }

    virtual void Destroy() {
        resolver_->table()->DestroyPathResolver();
    }

private:
    PathResolver *resolver_;
};

//
// Constructor for PathResolver.
//
// A new PathResolver is created from BgpTable::CreatePathResolver for inet
// and inet6 tables in all non-default RoutingInstances.
//
// The listener_id if used to set state on BgpRoutes for BgpPaths that have
// requested resolution.
//
PathResolver::PathResolver(BgpTable *table)
    : table_(table),
      listener_id_(table->Register(
          boost::bind(&PathResolver::RouteListener, this, _1, _2),
          "PathResolver")),
      nexthop_longest_match_(false),
      nexthop_reg_unreg_trigger_(new TaskTrigger(
          boost::bind(&PathResolver::ProcessResolverNexthopRegUnregList, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
          0)),
      nexthop_update_trigger_(new TaskTrigger(
          boost::bind(&PathResolver::ProcessResolverNexthopUpdateList, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverNexthop"),
          0)),
      deleter_(new DeleteActor(this)),
      table_delete_ref_(this, table->deleter()) {
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        partitions_.push_back(new PathResolverPartition(part_id, this));
    }
}

//
// Destructor for PathResolver.
//
// A PathResolver is deleted via LifetimeManager deletion.
// Actual destruction of the object happens via BgpTable::DestroyPathResolver.
//
// Need to do a deep delete of the partitions vector to ensure deletion of all
// PathResolverPartitions.
//
PathResolver::~PathResolver() {
    assert(listener_id_ != DBTableBase::kInvalidId);
    table_->Unregister(listener_id_);
    STLDeleteValues(&partitions_);
    nexthop_reg_unreg_trigger_->Reset();
    nexthop_update_trigger_->Reset();
}

//
// Get the address family for PathResolver.
//
Address::Family PathResolver::family() const {
    return table_->family();
}

//
// Request PathResolver to start resolution for the given BgpPath.
// This API needs to be called explicitly when the BgpPath needs resolution.
// This is typically when the BgpPath is added, but may also be needed when
// the BgpPath changes nexthop.
//
void PathResolver::StartPathResolution(BgpRoute *route, const BgpPath *path,
        BgpTable *nh_table) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::RouteAggregation",
        "bgp::Config", "bgp::ConfigHelper");

    if (!nh_table)
        nh_table = table_;
    assert(nh_table->family() == Address::INET ||
           nh_table->family() == Address::INET6);
    int part_id = route->get_table_partition()->index();
    partitions_[part_id]->StartPathResolution(route, path, nh_table);
}

//
// Request PathResolver to stop resolution for the given BgpPath.
// This API needs to be called explicitly when the BgpPath does not require
// resolution. This is typically when the BgpPath is deleted, but may also be
// needed when the BgpPath changes nexthop.
//
void PathResolver::StopPathResolution(int part_id, const BgpPath *path) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::RouteAggregation",
        "bgp::Config", "bgp::ConfigHelper");

    partitions_[part_id]->StopPathResolution(path);
}

//
// Return the BgpConditionListener for the given family.
//
BgpConditionListener *PathResolver::get_condition_listener(
    Address::Family family) {
    return table_->server()->condition_listener(family);
}

//
// Add a ResolverNexthop to the register/unregister list and start the Task
// to process the list.
//
// Note that the operation (register/unregister) is not explicitly part of
// the list - it's inferred based on the state of the ResolverNexthop when
// the list is processed.
//
void PathResolver::RegisterUnregisterResolverNexthop(
    ResolverNexthop *rnexthop) {
    tbb::mutex::scoped_lock lock(mutex_);
    nexthop_reg_unreg_list_.insert(rnexthop);
    nexthop_reg_unreg_trigger_->Set();
}

//
// Add a ResolverNexthop to the update list and start the Task to process the
// list.
//
void PathResolver::UpdateResolverNexthop(ResolverNexthop *rnexthop) {
    tbb::mutex::scoped_lock lock(mutex_);
    nexthop_update_list_.insert(rnexthop);
    nexthop_update_trigger_->Set();
}

//
// Get the PathResolverPartition for the given part_id.
//
PathResolverPartition *PathResolver::GetPartition(int part_id) {
    return partitions_[part_id];
}

PathResolverPartition *PathResolver::GetPartition(int part_id) const {
    return partitions_[part_id];
}

//
// Find ResolverRouteState for the given BgpRoute.
//
ResolverRouteState *PathResolver::FindResolverRouteState(BgpRoute *route) {
    ResolverRouteState *state = static_cast<ResolverRouteState *>(
        route->GetState(table_, listener_id_));
    return state;
}

//
// Find or create ResolverRouteState for the given BgpRoute.
//
// Note that the refcount for ResolverRouteState gets incremented when the
// ResolverPath takes an intrusive_ptr to it.
//
ResolverRouteState *PathResolver::LocateResolverRouteState(BgpRoute *route) {
    ResolverRouteState *state = static_cast<ResolverRouteState *>(
        route->GetState(table_, listener_id_));
    if (state) {
        return state;
    } else {
        return (new ResolverRouteState(this, route));
    }
}

//
// Find or create the ResolverNexthop with the given IpAddress.
// Called when a new ResolverPath is being created.
//
// A newly created ResolverNexthop is added to the map.
//
ResolverNexthop *PathResolver::LocateResolverNexthop(IpAddress address,
    BgpTable *table) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::RouteAggregation",
                      "bgp::Config", "bgp::ConfigHelper");

    tbb::mutex::scoped_lock lock(mutex_);
    ResolverNexthopKey key(address, table);
    ResolverNexthopMap::iterator loc = nexthop_map_.find(key);
    if (loc != nexthop_map_.end()) {
        return loc->second;
    } else {
        ResolverNexthop *rnexthop = new ResolverNexthop(this, address, table);
        nexthop_map_.insert(make_pair(key, rnexthop));
        return rnexthop;
    }
}

//
// Remove the ResolverNexthop from the map and the update list.
// Called when ResolverPath is being unregistered from BgpConditionListener
// as part of register/unregister list processing.
//
// If the ResolverNexthop is being unregistered, it's moved to the delete
// list till the BgpConditionListener invokes the remove complete callback.
//
// Note that a ResolverNexthop object cannot be resurrected once it has been
// removed from the map - it's destined to get destroyed eventually. A new
// object for the same IpAddress gets created if a new ResolverPath needs to
// use one.
//
void PathResolver::RemoveResolverNexthop(ResolverNexthop *rnexthop) {
    CHECK_CONCURRENCY("bgp::Config");

    ResolverNexthopKey key(rnexthop->address(), rnexthop->table());
    ResolverNexthopMap::iterator loc = nexthop_map_.find(key);
    assert(loc != nexthop_map_.end());
    nexthop_map_.erase(loc);
    nexthop_update_list_.erase(rnexthop);
}

//
// Callback for BgpConditionListener::RemoveMatchCondition operation for
// a ResolverNexthop.
//
// It's safe to unregister the ResolverNexthop at this point. However the
// operation cannot be done in the context of db::Walker Task. Enqueue the
// ResolverNexthop to the register/unregister list.
//
void PathResolver::UnregisterResolverNexthopDone(BgpTable *table,
    ConditionMatch *match) {
    CHECK_CONCURRENCY("db::Walker");

    ResolverNexthop *rnexthop = dynamic_cast<ResolverNexthop *>(match);
    assert(rnexthop);
    assert(rnexthop->registered());
    assert(rnexthop->deleted());
    assert(nexthop_delete_list_.find(rnexthop) != nexthop_delete_list_.end());
    RegisterUnregisterResolverNexthop(rnexthop);
}

//
// Handle processing of a ResolverNexthop on the register/unregister list.
//
// Return true if the ResolverNexthop can be deleted immediately.
//
bool PathResolver::ProcessResolverNexthopRegUnreg(ResolverNexthop *rnexthop) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpTable *table = rnexthop->table();
    Address::Family family = table->family();
    BgpConditionListener *condition_listener = get_condition_listener(family);

    if (rnexthop->registered()) {
        if (rnexthop->deleted()) {
            // Unregister the ResolverNexthop from BgpConditionListener since
            // remove operation has been completed. This is the final step in
            // the lifetime of ResolverNexthop - the ResolverNexthop will get
            // deleted when unregister is called.
            nexthop_delete_list_.erase(rnexthop);
            condition_listener->UnregisterMatchCondition(table, rnexthop);
        } else if (rnexthop->empty()) {
            // Remove the ResolverNexthop from BgpConditionListener as there
            // are no more ResolverPaths using it. Insert it into the delete
            // list while remove and unregister operations are still pending.
            // This prevents premature deletion of the PathResolver itself.
            // Note that BgpConditionListener marks the ResolverNexthop as
            // deleted as part of the remove operation.
            RemoveResolverNexthop(rnexthop);
            nexthop_delete_list_.insert(rnexthop);
            BgpConditionListener::RequestDoneCb cb = boost::bind(
                &PathResolver::UnregisterResolverNexthopDone, this, _1, _2);
            condition_listener->RemoveMatchCondition(table, rnexthop, cb);
        }
    } else {
        if (!rnexthop->empty()) {
            // Register ResolverNexthop to BgpConditionListener since there's
            // one or more ResolverPaths using it. Skip if the BgpTable is in
            // the process of being deleted - the BgpConditionListener does
            // not, and should not need to, handle this scenario.
            if (!table->IsDeleted()) {
                condition_listener->AddMatchCondition(
                    table, rnexthop, BgpConditionListener::RequestDoneCb());
                rnexthop->set_registered();
            }
        } else {
            // The ResolverNexthop can be deleted right away since there are
            // no ResolverPaths using it. This can happen in couple of corner
            // cases:
            // 1. ResolverPaths are added and deleted rapidly i.e. before the
            // ResolverNexthop has been registered with BgpConditionListener.
            // 2. The ResolverNexthop's BgpTable was in the process of being
            // deleted when we attempted to register the ResolverNexthop. In
            // that case, we come here after the last ResolverPath using the
            // ResolverNexthop has been deleted.
            RemoveResolverNexthop(rnexthop);
            return true;
        }
    }

    return false;
}

//
// Handle processing of all ResolverNexthops on the register/unregister list.
//
bool PathResolver::ProcessResolverNexthopRegUnregList() {
    CHECK_CONCURRENCY("bgp::Config");

    for (ResolverNexthopList::iterator it = nexthop_reg_unreg_list_.begin();
         it != nexthop_reg_unreg_list_.end(); ++it) {
        ResolverNexthop *rnexthop = *it;
        if (ProcessResolverNexthopRegUnreg(rnexthop))
            delete rnexthop;
    }
    nexthop_reg_unreg_list_.clear();

    RetryDelete();
    return true;
}

//
// Handle processing of all ResolverNexthops on the update list.
//
bool PathResolver::ProcessResolverNexthopUpdateList() {
    CHECK_CONCURRENCY("bgp::ResolverNexthop");

    for (ResolverNexthopList::iterator it = nexthop_update_list_.begin();
         it != nexthop_update_list_.end(); ++it) {
        ResolverNexthop *rnexthop = *it;
        assert(!rnexthop->deleted());
        rnexthop->TriggerAllResolverPaths();
    }
    nexthop_update_list_.clear();
    return true;
}

//
// Return true if the DeleteActor is marked deleted.
//
bool PathResolver::IsDeleted() const {
    return deleter_->IsDeleted();
}

//
// Cascade delete from BgpTable delete_ref to self.
//
void PathResolver::ManagedDelete() {
    deleter_->Delete();
}

//
// Return true if it's safe to delete the PathResolver.
//
bool PathResolver::MayDelete() const {
    if (!nexthop_map_.empty())
        return false;
    if (!nexthop_delete_list_.empty())
        return false;
    if (!nexthop_reg_unreg_list_.empty())
        return false;
    assert(nexthop_update_list_.empty());
    return true;
}

//
// Attempt to enqueue a delete for the PathResolver.
//
void PathResolver::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

//
// Dummy callback - required in order to get a listener_id for use with
// BgpConditionListener.
//
bool PathResolver::RouteListener(DBTablePartBase *root, DBEntryBase *entry) {
    return true;
}

//
// Get size of the map.
// For testing only.
//
size_t PathResolver::GetResolverNexthopMapSize() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return nexthop_map_.size();
}

//
// Get size of the delete list.
// For testing only.
//
size_t PathResolver::GetResolverNexthopDeleteListSize() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return nexthop_delete_list_.size();
}

//
// Disable processing of the register/unregister list.
// For testing only.
//
void PathResolver::DisableResolverNexthopRegUnregProcessing() {
    nexthop_reg_unreg_trigger_->set_disable();
}

//
// Enable processing of the register/unregister list.
// For testing only.
//
void PathResolver::EnableResolverNexthopRegUnregProcessing() {
    nexthop_reg_unreg_trigger_->set_enable();
}

//
// Get size of the update list.
// For testing only.
//
size_t PathResolver::GetResolverNexthopRegUnregListSize() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return nexthop_reg_unreg_list_.size();
}

//
// Disable processing of the update list.
// For testing only.
//
void PathResolver::DisableResolverNexthopUpdateProcessing() {
    nexthop_update_trigger_->set_disable();
}

//
// Enable processing of the update list.
// For testing only.
//
void PathResolver::EnableResolverNexthopUpdateProcessing() {
    nexthop_update_trigger_->set_enable();
}

//
// Get size of the update list.
// For testing only.
//
size_t PathResolver::GetResolverNexthopUpdateListSize() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return nexthop_update_list_.size();
}

//
// Disable processing of the path update list in all partitions.
// For testing only.
//
void PathResolver::DisableResolverPathUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        partitions_[part_id]->DisableResolverPathUpdateProcessing();
    }
}

//
// Enable processing of the path update list in all partitions.
// For testing only.
//
void PathResolver::EnableResolverPathUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        partitions_[part_id]->EnableResolverPathUpdateProcessing();
    }
}

//
// Pause processing of the path update list in all partitions.
// For testing only.
//
void PathResolver::PauseResolverPathUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        partitions_[part_id]->PauseResolverPathUpdateProcessing();
    }
}

//
// Resume processing of the path update list in all partitions.
// For testing only.
//
void PathResolver::ResumeResolverPathUpdateProcessing() {
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        partitions_[part_id]->ResumeResolverPathUpdateProcessing();
    }
}

//
// Get size of the update list.
// For testing only.
//
size_t PathResolver::GetResolverPathUpdateListSize() const {
    size_t total = 0;
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        total += partitions_[part_id]->GetResolverPathUpdateListSize();
    }
    return total;
}

//
// Fill introspect information.
//
void PathResolver::FillShowInfo(ShowPathResolver *spr, bool summary) const {
    spr->set_name(table_->name());

    size_t path_count = 0;
    size_t modified_path_count = 0;
    vector<ShowPathResolverPath> sprp_list;
    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        const PathResolverPartition *partition = partitions_[part_id];
        path_count += partition->rpath_map_.size();
        modified_path_count += partition->rpath_update_list_.size();
        if (summary)
            continue;
        for (PathResolverPartition::PathToResolverPathMap::const_iterator it =
             partition->rpath_map_.begin(); it != partition->rpath_map_.end();
             ++it) {
            const ResolverPath *rpath = it->second;
            ShowPathResolverPath sprp;
            sprp.set_prefix(rpath->route()->ToString());
            sprp.set_nexthop(rpath->rnexthop()->address().to_string());
            sprp.set_resolved_path_count(rpath->resolved_path_count());
            sprp_list.push_back(sprp);
        }
    }
    spr->set_path_count(path_count);
    spr->set_modified_path_count(modified_path_count);
    spr->set_nexthop_count(nexthop_map_.size());
    spr->set_modified_nexthop_count(nexthop_reg_unreg_list_.size() +
        nexthop_delete_list_.size() + nexthop_update_list_.size());

    if (summary)
        return;

    vector<ShowPathResolverNexthop> sprn_list;
    for (ResolverNexthopMap::const_iterator it = nexthop_map_.begin();
         it != nexthop_map_.end(); ++it) {
        const ResolverNexthop *rnexthop = it->second;
        const BgpTable *table = rnexthop->table();
        ShowPathResolverNexthop sprn;
        sprn.set_address(rnexthop->address().to_string());
        sprn.set_table(table->name());
        const BgpRoute *route = rnexthop->GetRoute();
        if (route) {
            ShowRouteBrief show_route;
            route->FillRouteInfo(table, &show_route);
            sprn.set_nexthop_route(show_route);
        }
        sprn_list.push_back(sprn);
    }

    spr->set_paths(sprp_list);
    spr->set_nexthops(sprn_list);
}

//
//
// Constructor for PathResolverPartition.
// A new PathResolverPartition is created when a PathResolver is created.
//
PathResolverPartition::PathResolverPartition(int part_id,
    PathResolver *resolver)
    : part_id_(part_id),
      resolver_(resolver),
      rpath_update_trigger_(new TaskTrigger(
          boost::bind(&PathResolverPartition::ProcessResolverPathUpdateList,
              this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::ResolverPath"),
          part_id)) {
}

//
// Destructor for PathResolverPartition.
// All PathResolverPartitions for a PathResolver are destroyed when the
// PathResolver is destroyed.
//
PathResolverPartition::~PathResolverPartition() {
    assert(rpath_update_list_.empty());
    rpath_update_trigger_->Reset();
}

//
// Start resolution for the given BgpPath.
// Create a ResolverPath object and trigger resolution for it.
// A ResolverNexthop is also created if required.
//
// Note that the ResolverPath gets linked to the ResolverNexthop via the
// ResolverPath constructor.
//
void PathResolverPartition::StartPathResolution(BgpRoute *route,
        const BgpPath *path, BgpTable *nh_table) {
    if (!path->IsResolutionFeasible())
        return;
    if (table()->IsDeleted() || nh_table->IsDeleted())
        return;

    Address::Family family = table()->family();
    IpAddress address = path->GetAttr()->nexthop();
    if (table() == nh_table && RoutePrefixIsAddress(family, route, address))
        return;

    ResolverNexthop *rnexthop =
        resolver_->LocateResolverNexthop(address, nh_table);
    assert(!FindResolverPath(path));
    ResolverPath *rpath = CreateResolverPath(path, route, rnexthop);
    TriggerPathResolution(rpath);
}

//
// Stop resolution for the given BgpPath.
// The ResolverPath is removed from the map right away, but the deletion of
// any resolved BgpPaths and the ResolverPath itself happens asynchronously.
//
void PathResolverPartition::StopPathResolution(const BgpPath *path) {
    ResolverPath *rpath = RemoveResolverPath(path);
    if (!rpath)
        return;
    TriggerPathResolution(rpath);
}

//
// Add a ResolverPath to the update list and start Task to process the list.
//
void PathResolverPartition::TriggerPathResolution(ResolverPath *rpath) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::ResolverNexthop",
        "bgp::Config", "bgp::ConfigHelper", "bgp::RouteAggregation");

    rpath_update_list_.insert(rpath);
    rpath_update_trigger_->Set();
}

//
// Add a ResolverPath to the update list and start Task to process the list.
// This is used to defer re-evaluation of the ResolverPath when the update
// list is being processed.
//
void PathResolverPartition::DeferPathResolution(ResolverPath *rpath) {
    CHECK_CONCURRENCY("bgp::ResolverPath");

    rpath_update_list_.insert(rpath);
    rpath_update_trigger_->Set();
}

//
// Get the BgpTable partition corresponding to this PathResolverPartition.
//
DBTablePartBase *PathResolverPartition::table_partition() {
    return table()->GetTablePartition(part_id_);
}

//
// Create a new ResolverPath for the BgpPath.
// The ResolverPath is inserted into the map.
//
ResolverPath *PathResolverPartition::CreateResolverPath(const BgpPath *path,
    BgpRoute *route, ResolverNexthop *rnexthop) {
    ResolverPath *rpath = new ResolverPath(this, path, route, rnexthop);
    rpath_map_.insert(make_pair(path, rpath));
    return rpath;
}

//
// Find the ResolverPath for given BgpPath.
//
ResolverPath *PathResolverPartition::FindResolverPath(const BgpPath *path) {
    PathToResolverPathMap::iterator loc = rpath_map_.find(path);
    return (loc != rpath_map_.end() ? loc->second : NULL);
}

//
// Remove the ResolverPath for given BgpPath.
// The ResolverPath is removed from the map and it's back pointer to the
// BgpPath is cleared.
// Actual deletion of the ResolverPath happens asynchronously.
//
ResolverPath *PathResolverPartition::RemoveResolverPath(const BgpPath *path) {
    PathToResolverPathMap::iterator loc = rpath_map_.find(path);
    if (loc == rpath_map_.end()) {
        return NULL;
    } else {
        ResolverPath *rpath = loc->second;
        rpath_map_.erase(loc);
        rpath->clear_path();
        return rpath;
    }
}

//
// Handle processing of all ResolverPaths on the update list.
//
bool PathResolverPartition::ProcessResolverPathUpdateList() {
    CHECK_CONCURRENCY("bgp::ResolverPath");

    ResolverPathList update_list;
    rpath_update_list_.swap(update_list);
    for (ResolverPathList::iterator it = update_list.begin();
         it != update_list.end(); ++it) {
        ResolverPath *rpath = *it;
        if (rpath->UpdateResolvedPaths())
            delete rpath;
    }

    return rpath_update_list_.empty();
}

//
// Disable processing of the update list.
// For testing only.
//
void PathResolverPartition::DisableResolverPathUpdateProcessing() {
    rpath_update_trigger_->set_disable();
}

//
// Enable processing of the update list.
// For testing only.
//
void PathResolverPartition::EnableResolverPathUpdateProcessing() {
    rpath_update_trigger_->set_enable();
}

//
// Pause processing of the update list.
// For testing only.
//
void PathResolverPartition::PauseResolverPathUpdateProcessing() {
    rpath_update_trigger_->set_deferred();
}

//
// Resume processing of the update list.
// For testing only.
//
void PathResolverPartition::ResumeResolverPathUpdateProcessing() {
    rpath_update_trigger_->clear_deferred();
}

//
// Get size of the update list.
// For testing only.
//
size_t PathResolverPartition::GetResolverPathUpdateListSize() const {
    return rpath_update_list_.size();
}

//
// Constructor for ResolverRouteState.
// Gets called when the first ResolverPath for a BgpRoute is created.
//
// Set State on the BgpRoute to ensure that it doesn't go away.
//
ResolverRouteState::ResolverRouteState(PathResolver *resolver,
    BgpRoute *route)
    : resolver_(resolver),
      route_(route),
      refcount_(0) {
    route_->SetState(resolver_->table(), resolver_->listener_id(), this);
}

//
// Destructor for ResolverRouteState.
// Gets called via when the refcount goes to 0. This happens when the last
// ResolverPath for a BgpRoute is deleted.
//
// Remove State on the BgpRoute so that deletion can proceed.
//
ResolverRouteState::~ResolverRouteState() {
    route_->ClearState(resolver_->table(), resolver_->listener_id());
}

//
// Constructor for ResolverPath.
// Add the ResolverPath as a dependent of the ResolverNexthop.
//
// Note that it's the caller's responsibility to add the ResolverPath to the
// map in the PathResolverPartition.
//
ResolverPath::ResolverPath(PathResolverPartition *partition,
    const BgpPath *path, BgpRoute *route, ResolverNexthop *rnexthop)
    : partition_(partition),
      path_(path),
      route_(route),
      rnexthop_(rnexthop),
      state_(partition_->resolver()->LocateResolverRouteState(route)) {
    rnexthop->AddResolverPath(partition->part_id(), this);
}

//
// Destructor for ResolverPath.
// Remove the ResolverPath as a dependent of the ResolverNexthop. This may
// trigger unregistration and eventual deletion of the ResolverNexthop if
// there are no more ResolverPaths using it.
//
// Note that the ResolverPath would have been removed from the map in the
// PathResolverPartition much earlier i.e. when resolution is stopped.
//
ResolverPath::~ResolverPath() {
    rnexthop_->RemoveResolverPath(partition_->part_id(), this);
}

//
// Add the BgpPath specified by the iterator to the resolved path list.
// Also inserts the BgpPath to the BgpRoute.
//
void ResolverPath::AddResolvedPath(ResolvedPathList::const_iterator it) {
    BgpPath *path = *it;
    const IPeer *peer = path->GetPeer();
    resolved_path_list_.insert(path);
    route_->InsertPath(path);
    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Added resolved path " << route_->ToString() <<
        " peer " << (peer ? peer->ToString() : "None") <<
        " path_id " << BgpPath::PathIdString(path->GetPathId()) <<
        " nexthop " << path->GetAttr()->nexthop().to_string() <<
        " label " << path->GetLabel() <<
        " in table " << partition_->table()->name());
}

//
// Delete the BgpPath specified by the iterator from the resolved path list.
// Also deletes the BgpPath from the BgpRoute.
//
void ResolverPath::DeleteResolvedPath(ResolvedPathList::const_iterator it) {
    BgpPath *path = *it;
    const IPeer *peer = path->GetPeer();
    BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
        "Deleted resolved path " << route_->ToString() <<
        " peer " << (peer ? peer->ToString() : "None") <<
        " path_id " << BgpPath::PathIdString(path->GetPathId()) <<
        " nexthop " << path->GetAttr()->nexthop().to_string() <<
        " label " << path->GetLabel() <<
        " in table " << partition_->table()->name());
    route_->DeletePath(path);
    resolved_path_list_.erase(it);
}

//
// Find or create the matching resolved BgpPath.
//
BgpPath *ResolverPath::LocateResolvedPath(const IPeer *peer, uint32_t path_id,
    const BgpAttr *attr, uint32_t label) {
    for (ResolvedPathList::iterator it = resolved_path_list_.begin();
         it != resolved_path_list_.end(); ++it) {
        BgpPath *path = *it;
        if (path->GetPeer() == peer &&
            path->GetPathId() == path_id &&
            path->GetAttr() == attr &&
            path->GetLabel() == label) {
            return path;
        }
    }

    BgpPath::PathSource src = path_->GetSource();
    uint32_t flags =
        (path_->GetFlags() & ~BgpPath::ResolveNexthop) | BgpPath::ResolvedPath;
    return (new BgpPath(peer, path_id, src, attr, flags, label));
}

//
// Return an extended community that's built by combining the values in the
// original path's attributes with values from the nexthop path's attributes.
//
// Pick up the security groups, tunnel encapsulation load balance, source-as
// and vrf import target values from the nexthop path's attributes.
//
static ExtCommunityPtr UpdateExtendedCommunity(ExtCommunityDB *extcomm_db,
    const BgpAttr *attr, const BgpAttr *nh_attr) {
    ExtCommunityPtr ext_community = attr->ext_community();
    const ExtCommunity *nh_ext_community = nh_attr->ext_community();
    if (!nh_ext_community)
        return ext_community;

    ExtCommunity::ExtCommunityList rtarget;
    ExtCommunity::ExtCommunityList sgid_list;
    ExtCommunity::ExtCommunityList encap_list;
    ExtCommunity::ExtCommunityValue const *source_as = NULL;
    ExtCommunity::ExtCommunityValue const *lb = NULL;
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &value,
        nh_ext_community->communities()) {
        if (ExtCommunity::is_security_group(value) ||
                ExtCommunity::is_security_group4(value)) {
            sgid_list.push_back(value);
        } else if (ExtCommunity::is_tunnel_encap(value)) {
            encap_list.push_back(value);
        } else if (ExtCommunity::is_vrf_route_import(value)) {
            VrfRouteImport vit(value);
            rtarget.push_back(RouteTarget(vit.GetIPv4Address(),
                                          vit.GetNumber()).GetExtCommunity());
        } else if (ExtCommunity::is_source_as(value)) {
            source_as = &value;
        } else if (ExtCommunity::is_load_balance(value) && !lb) {
            lb = &value;
        }
    }

    // Replace rtarget, sgid list, encap list and load balance.
    ext_community = extcomm_db->ReplaceRTargetAndLocate(ext_community.get(),
                                                        rtarget);
    ext_community = extcomm_db->ReplaceSGIDListAndLocate(
        ext_community.get(), sgid_list);
    ext_community = extcomm_db->ReplaceTunnelEncapsulationAndLocate(
        ext_community.get(), encap_list);
    if (source_as) {
        ext_community = extcomm_db->ReplaceSourceASAndLocate(
                ext_community.get(), *source_as);
    }
    if (lb) {
        ext_community = extcomm_db->ReplaceLoadBalanceAndLocate(
            ext_community.get(), *lb);
    }
    return ext_community;
}

//
// Update resolved BgpPaths for the ResolverPath based on the BgpRoute for
// the ResolverNexthop.
//
// Return true if the ResolverPath can be deleted.
//
// Note that the ResolverPath can only be deleted if resolution for it has
// been stopped. It must not be deleted simply because there is no viable
// BgpRoute for the ResolverNexthop.
//
bool ResolverPath::UpdateResolvedPaths() {
    CHECK_CONCURRENCY("bgp::ResolverPath");

    // Defer updates if we can't get a write lock on the ResolverRouteState
    // for the BgpRoute we're trying to modify.
    tbb::spin_rw_mutex::scoped_lock rt_write_lock;
    if (!rt_write_lock.try_acquire(state_->rw_mutex(), true)) {
        partition_->DeferPathResolution(this);
        return false;
    }

    // Defer updates if we can't get a read lock on the ResolverRouteState
    // for the BgpRoute to the ResolverNexthop. This ResolverRouteState will
    // exist only if the BgpRoute in question itself has resolved paths.
    tbb::spin_rw_mutex::scoped_lock nh_read_lock;
    ResolverRouteState *nh_state = rnexthop_->GetResolverRouteState();
    if (state_ != nh_state &&
        nh_state && !nh_read_lock.try_acquire(nh_state->rw_mutex(), false)) {
        partition_->DeferPathResolution(this);
        return false;
    }

    BgpServer *server = partition_->table()->server();
    BgpAttrDB *attr_db = server->attr_db();
    ExtCommunityDB *extcomm_db = server->extcomm_db();

    // Go through paths of the nexthop route and build the list of future
    // resolved paths.
    ResolvedPathList future_resolved_path_list;
    const BgpRoute *nh_route = rnexthop_->GetRoute();
    const IPeer *peer = path_ ? path_->GetPeer() : NULL;
    Route::PathList::const_iterator it;
    if (path_ && nh_route)
        it = nh_route->GetPathList().begin();
    for (; path_ && nh_route && it != nh_route->GetPathList().end(); ++it) {
        const BgpPath *nh_path = static_cast<const BgpPath *>(it.operator->());

        // Start with attributes of the original path.
        BgpAttrPtr attr(path_->GetAttr());

        // Infeasible nexthop paths are not considered.
        if (!nh_path->IsFeasible())
            break;

        // Take snapshot of all ECMP nexthop paths.
        if (nh_route->BestPath()->PathCompare(*nh_path, true))
            break;

        // Skip paths with duplicate forwarding information.  This ensures
        // that we generate only one path with any given next hop when there
        // are multiple nexthop paths from the original source received via
        // different peers e.g. directly via XMPP and via BGP.
        if (nh_route->DuplicateForwardingPath(nh_path))
            continue;

        // Skip if there's no source rd.
        RouteDistinguisher source_rd = nh_path->GetSourceRouteDistinguisher();
        if (source_rd.IsZero())
            continue;

        // Use source rd from the nexthop path.
        attr = attr_db->ReplaceSourceRdAndLocate(attr.get(), source_rd);

        // Use nexthop address from the nexthop path.
        attr = attr_db->ReplaceNexthopAndLocate(attr.get(),
            nh_path->GetAttr()->nexthop());

        // Update extended community based on the nexthop path and use it.
        ExtCommunityPtr ext_community =
            UpdateExtendedCommunity(extcomm_db, attr.get(), nh_path->GetAttr());
        attr = attr_db->ReplaceExtCommunityAndLocate(attr.get(), ext_community);

        // Locate the resolved path.
        uint32_t path_id = nh_path->GetAttr()->nexthop().to_v4().to_ulong();
        BgpPath *resolved_path =
            LocateResolvedPath(peer, path_id, attr.get(), nh_path->GetLabel());
        future_resolved_path_list.insert(resolved_path);
    }

    // Reconcile the current and future resolved paths and notify/delete the
    // route as appropriate.
    bool modified = set_synchronize(&resolved_path_list_,
        &future_resolved_path_list,
        boost::bind(&ResolverPath::AddResolvedPath, this, _1),
        boost::bind(&ResolverPath::DeleteResolvedPath, this, _1));
    if (route_->HasPaths()) {
        if (modified) {
            partition_->table_partition()->Notify(route_);
        }
    } else {
        partition_->table_partition()->Delete(route_);
    }

    return (!path_);
}

//
// Constructor for ResolverNexthop.
// Initialize the vector of paths_lists to the number of DB partitions.
//
ResolverNexthop::ResolverNexthop(PathResolver *resolver, IpAddress address,
    BgpTable *table)
    : resolver_(resolver),
      address_(address),
      table_(table),
      registered_(false),
      rpath_lists_(DB::PartitionCount()),
      table_delete_ref_(this, table->deleter()) {
}

//
// Destructor for ResolverNexthop.
// A deep delete of the path_lists vector is not required.
//
ResolverNexthop::~ResolverNexthop() {
    assert(routes_.empty());
}

//
// Implement virtual method for ConditionMatch base class.
//
string ResolverNexthop::ToString() const {
    return (string("ResolverNexthop ") + address_.to_string());
}

//
// Implement virtual method for ConditionMatch base class.
//
bool ResolverNexthop::Match(BgpServer *server, BgpTable *table,
    BgpRoute *route, bool deleted) {
    CHECK_CONCURRENCY("db::DBTable");

    // Ignore if the route doesn't match the address.
    Address::Family family = table->family();
    assert(family == Address::INET || family == Address::INET6);

    // If longest match based lookup is not enabled, do an exact match between
    // the route and the address.
    if (!resolver_->nexthop_longest_match()) {
        if (!RoutePrefixIsAddress(family, route, address_))
            return false;
    } else {
        if (!resolver_->RoutePrefixMatch(route, address_))
            return false;
    }

    // Do not resolve over self.
    if (route->table() == table && route->IsUsable() &&
            route->BestPath()->GetAttr()->nexthop() == address_) {
        return false;
    }

    // Set or remove MatchState as appropriate.
    BgpConditionListener *condition_listener =
        resolver_->get_condition_listener(family);
    bool state_added = condition_listener->CheckMatchState(table, route, this);

    // In order to also cover the case when route does not change but its path
    // attrs, trigger updates by default, unless non longest matching route is
    // inserted or removed.
    bool longest_match_updated = true;
    if (deleted) {
        if (state_added) {
            longest_match_updated = RemoveRoute(route);
            condition_listener->RemoveMatchState(table, route, this);
        } else {
            return false;
        }
    } else {
        if (!state_added) {
            longest_match_updated = InsertRoute(route);
            condition_listener->SetMatchState(table, route, this);
        }
    }

    // Nothing more to do if the ConditionMatch has been removed.
    if (ConditionMatch::deleted())
        return false;

    // Trigger re-evaluation of all dependent ResolverPaths if the longest
    // matching route was updated.
    if (longest_match_updated)
        resolver_->UpdateResolverNexthop(this);
    return true;
}

//
// Add the given ResolverPath to the list of dependents in the partition.
// Add to register/unregister list when the first dependent ResolverPath for
// the partition is added.
//
// This may cause the ResolverNexthop to get added to register/unregister
// list multiple times - once for the first ResolverPath in each partition.
// This case is handled by PathResolver::ProcessResolverNexthopRegUnreg.
//
// Do not attempt to access other partitions due to concurrency issues.
//
void ResolverNexthop::AddResolverPath(int part_id, ResolverPath *rpath) {
    CHECK_CONCURRENCY("db::DBTable", "bgp::RouteAggregation",
        "bgp::Config", "bgp::ConfigHelper");

    if (rpath_lists_[part_id].empty())
        resolver_->RegisterUnregisterResolverNexthop(this);
    rpath_lists_[part_id].insert(rpath);
}

//
// Remove given ResolverPath from the list of dependents in the partition.
// Add to register/unregister list when the last dependent ResolverPath for
// the partition is removed.
//
// This may cause the ResolverNexthop to get added to register/unregister
// list multiple times - once for the last ResolverPath in each partition.
// This case is handled by PathResolver::ProcessResolverNexthopRegUnreg.
//
// Do not attempt to access other partitions due to concurrency issues.
//
void ResolverNexthop::RemoveResolverPath(int part_id, ResolverPath *rpath) {
    CHECK_CONCURRENCY("bgp::ResolverPath");

    rpath_lists_[part_id].erase(rpath);
    if (rpath_lists_[part_id].empty())
        resolver_->RegisterUnregisterResolverNexthop(this);
}

ResolverRouteState *ResolverNexthop::GetResolverRouteState() {
    if (!GetRoute())
        return NULL;
    PathResolver *nh_resolver = table_->path_resolver();
    if (!nh_resolver)
        return NULL;
    return nh_resolver->FindResolverRouteState(GetRoute());
}

//
// Trigger update of resolved BgpPaths for all ResolverPaths that depend on
// the ResolverNexthop. Actual update of the resolved BgpPaths happens when
// the PathResolverPartitions process their update lists.
//
void ResolverNexthop::TriggerAllResolverPaths() const {
    CHECK_CONCURRENCY("bgp::ResolverNexthop");

    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        for (ResolverPathList::iterator it = rpath_lists_[part_id].begin();
             it != rpath_lists_[part_id].end(); ++it) {
            ResolverPath *rpath = *it;
            resolver_->GetPartition(part_id)->TriggerPathResolution(rpath);
        }
    }
}

//
// Return true if there are no dependent ResolverPaths in all partitions.
//
bool ResolverNexthop::empty() const {
    CHECK_CONCURRENCY("bgp::Config");

    for (int part_id = 0; part_id < DB::PartitionCount(); ++part_id) {
        if (!rpath_lists_[part_id].empty())
            return false;
    }
    return true;
}

bool ResolverNexthop::ResolverRouteCompare::operator()(
        const BgpRoute *lhs, const BgpRoute *rhs) const {
    const InetRoute *lhs4 = dynamic_cast<const InetRoute *>(lhs);
    const InetRoute *rhs4 = dynamic_cast<const InetRoute *>(rhs);
    if (lhs4) {
        return lhs4->GetPrefix().prefixlen() > rhs4->GetPrefix().prefixlen();
    }

    const Inet6Route *lhs6 = dynamic_cast<const Inet6Route *>(lhs);
    const Inet6Route *rhs6 = dynamic_cast<const Inet6Route *>(rhs);
    return lhs6->GetPrefix().prefixlen() > rhs6->GetPrefix().prefixlen();
}

// Insert route into the set of routes sorted based on the prefix length in
// descending order. Return true if the route inserted is the new longest match.
bool ResolverNexthop::InsertRoute(BgpRoute *route) {
    tbb::mutex::scoped_lock lock(routes_mutex_);
    routes_.insert(route);
    bool longest_match = (*(routes_.begin()) == route);
    return longest_match;
}

// Remove route from the set of routes sorted based on the prefix length in
// descending order. Return true if the route removed was the old longest match.
bool ResolverNexthop::RemoveRoute(BgpRoute *route) {
    tbb::mutex::scoped_lock lock(routes_mutex_);
    bool longest_match = (*(routes_.begin()) == route);
    routes_.erase(route);
    return longest_match;
}

const BgpRoute *ResolverNexthop::GetRoute() const {
    tbb::mutex::scoped_lock lock(routes_mutex_);
    return !routes_.empty() ? *(routes_.begin()) : NULL;
}

BgpRoute *ResolverNexthop::GetRoute() {
    tbb::mutex::scoped_lock lock(routes_mutex_);
    return !routes_.empty() ? *(routes_.begin()) : NULL;
}
