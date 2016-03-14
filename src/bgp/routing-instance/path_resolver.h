/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_PATH_RESOLVER_H_
#define SRC_BGP_ROUTING_INSTANCE_PATH_RESOLVER_H_

#include <boost/scoped_ptr.hpp>
#include <tbb/mutex.h>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <utility>

#include "base/lifetime.h"
#include "base/util.h"
#include "bgp/bgp_condition_listener.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "net/address.h"

class BgpAttr;
class BgpPath;
class BgpRoute;
class BgpServer;
class BgpTable;
class DBEntryBase;
class DBTablePartBase;
class DeleteActor;
class IPeer;
class PathResolverPartition;
class ResolverNexthop;
class ResolverPath;
class ShowPathResolver;
class TaskTrigger;

//
// This represents an instance of the resolver per BgpTable. A BgpTable that
// with BgpPaths that need resolution will have an instance of PathResolver.
//
// The [Start|Update|Stop]PathResolution APIs are invoked by PathResolution
// clients explicitly as required. They have to be invoked in context of the
// db::DBTable Task.
//
// The listener_id is used by ResolverPath to set ResolverRouteState on the
// BgpRoute for the BgpPaths in question. The PathResolver doesn't actually
// listen to notifications for routes in the BgpTable.
//
// The nexthop map keeps track of all ResolverNexthop for this instance. In
// addition, a given ResolverNexthop may be on the register/unregister list
// and the update list. Entries are added to the map and the lists from the
// db::DBTable Task. A mutex is used to serialize updates to the map and the
// lists. Note that there's no no concurrent access when entries are removed
// from the map and the lists, since the remove operations happen from Tasks
// that are mutually exclusive.
//
// The register/unregister list is processed in the context of bgp::Config
// Task. ResolverNexthops are added to this list when we need to add/remove
// or unregister them to/from the BgpConditionListener. This is done since
// the BgpConditionListener expects these operations to be made in context
// of bgp::Config Task.
//
// When a ResolverNexthop is removed the BgpConditionListener, it is added
// to the delete list. This is required because remove call is asynchronous.
// When BgpConditionListener invokes the remove request done callback, the
// ResolverNexthop is added to the register/unregister list again. It gets
// erased from the delete list and unregistered from BgpConditionListener
// after the list is processed again.
//
// The update list is processed in the context of bgp::ResolverNexthop Task.
// When an entry on this list is processed all it's dependent ResolverPaths
// are queued for re-evaluation in the PathResolverPartition.
//
// Concurrency Notes:
//
// Resolution APIs are invoked from db::DBTable Task.
// Updates to ResolverNexthop (via Match method) happen from db::DBTable Task.
// ResolverNexthop register/unregister list is processed in bgp::Config Task.
// ResolverNexthop update is processed in bgp::ResolverNexthop Task.
// ResolverPath update is processed in bgp::ResolverPath Task.
//
// bgp::Config is mutually exclusive with bgp::ResolverPath
// bgp::Config is mutually exclusive with bgp::ResolverNexthop
// db::DBTable is mutually exclusive with bgp::ResolverPath
// db::DBTable is mutually exclusive with bgp::ResolverNexthop
// bgp::ResolverPath is mutually exclusive with bgp::ResolverNexthop
//
class PathResolver {
public:
    explicit PathResolver(BgpTable *table);
    ~PathResolver();

    void StartPathResolution(int part_id, const BgpPath *path, BgpRoute *route,
        BgpTable *nh_table = NULL);
    void UpdatePathResolution(int part_id, const BgpPath *path, BgpRoute *route,
        BgpTable *nh_table = NULL);
    void StopPathResolution(int part_id, const BgpPath *path);

    BgpTable *table() { return table_; }
    Address::Family family() const;
    DBTableBase::ListenerId listener_id() const { return listener_id_; }
    BgpConditionListener *get_condition_listener(Address::Family family);

    bool IsDeleted() const;
    void ManagedDelete();
    bool MayDelete() const;
    void RetryDelete();

    void FillShowInfo(ShowPathResolver *spr, bool summary) const;

private:
    friend class PathResolverPartition;
    friend class ResolverNexthop;
    template <typename U> friend class PathResolverTest;

    class DeleteActor;
    typedef std::pair<IpAddress, BgpTable *> ResolverNexthopKey;
    typedef std::map<ResolverNexthopKey, ResolverNexthop *> ResolverNexthopMap;
    typedef std::set<ResolverNexthop *> ResolverNexthopList;

    PathResolverPartition *GetPartition(int part_id);

    ResolverNexthop *LocateResolverNexthop(IpAddress address, BgpTable *table);
    void RemoveResolverNexthop(ResolverNexthop *rnexthop);
    void UpdateResolverNexthop(ResolverNexthop *rnexthop);
    void RegisterUnregisterResolverNexthop(ResolverNexthop *rnexthop);

    void UnregisterResolverNexthopDone(BgpTable *table, ConditionMatch *match);
    bool ProcessResolverNexthopRegUnreg(ResolverNexthop *rnexthop);
    bool ProcessResolverNexthopRegUnregList();
    bool ProcessResolverNexthopUpdateList();

    bool RouteListener(DBTablePartBase *root, DBEntryBase *entry);

    size_t GetResolverNexthopMapSize() const;
    size_t GetResolverNexthopDeleteListSize() const;

    void DisableResolverNexthopRegUnregProcessing();
    void EnableResolverNexthopRegUnregProcessing();
    size_t GetResolverNexthopRegUnregListSize() const;

    void DisableResolverNexthopUpdateProcessing();
    void EnableResolverNexthopUpdateProcessing();
    size_t GetResolverNexthopUpdateListSize() const;

    void DisableResolverPathUpdateProcessing();
    void EnableResolverPathUpdateProcessing();
    void PauseResolverPathUpdateProcessing();
    void ResumeResolverPathUpdateProcessing();
    size_t GetResolverPathUpdateListSize() const;

    BgpTable *table_;
    DBTableBase::ListenerId listener_id_;
    mutable tbb::mutex mutex_;
    ResolverNexthopMap nexthop_map_;
    ResolverNexthopList nexthop_reg_unreg_list_;
    boost::scoped_ptr<TaskTrigger> nexthop_reg_unreg_trigger_;
    ResolverNexthopList nexthop_update_list_;
    boost::scoped_ptr<TaskTrigger> nexthop_update_trigger_;
    ResolverNexthopList nexthop_delete_list_;
    std::vector<PathResolverPartition *> partitions_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<PathResolver> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(PathResolver);
};

//
// This represents one partition in the PathResolver. It keeps tracks of all
// the ResolverPaths for BgpRoutes in the partition. It has a map of BgpPath
// to ResolverPath.
//
// The update list contains ResolverPaths whose resolved BgpPath list need to
// be updated. Entries are added to the list as described in the comments for
// ResolverPath class. The list is processed in context of bgp::ResolverPath
// Task with the partition index as the Task instance id. This allows all the
// PathResolverPartitions to work concurrently.

// Mutual exclusion of db::DBTable and bgp::ResolverPath Tasks ensures that
// it's safe to add/delete/update resolved BgpPaths from the bgp::ResolverPath
// Task. It also ensures that it's safe to access the BgpPaths of the nexthop
// BgpRoute from the bgp::ResolverPath Task.
//
class PathResolverPartition {
public:
    PathResolverPartition(int part_id, PathResolver *resolver);
    ~PathResolverPartition();

    void StartPathResolution(const BgpPath *path, BgpRoute *route,
        BgpTable *nh_table);
    void UpdatePathResolution(const BgpPath *path, BgpRoute *route,
        BgpTable *nh_table);
    void StopPathResolution(const BgpPath *path);

    void TriggerPathResolution(ResolverPath *rpath);

    int part_id() const { return part_id_; }
    DBTableBase::ListenerId listener_id() const {
        return resolver_->listener_id();
    }
    PathResolver *resolver() const { return resolver_; }
    BgpTable *table() const { return resolver_->table(); }
    DBTablePartBase *table_partition();

private:
    friend class PathResolver;

    typedef std::map<const BgpPath *, ResolverPath *> PathToResolverPathMap;
    typedef std::set<ResolverPath *> ResolverPathList;

    ResolverPath *CreateResolverPath(const BgpPath *path, BgpRoute *route,
        ResolverNexthop *rnexthop);
    ResolverPath *FindResolverPath(const BgpPath *path);
    ResolverPath *RemoveResolverPath(const BgpPath *path);
    bool ProcessResolverPathUpdateList();

    void DisableResolverPathUpdateProcessing();
    void EnableResolverPathUpdateProcessing();
    void PauseResolverPathUpdateProcessing();
    void ResumeResolverPathUpdateProcessing();
    size_t GetResolverPathUpdateListSize() const;

    int part_id_;
    PathResolver *resolver_;
    PathToResolverPathMap rpath_map_;
    ResolverPathList rpath_update_list_;
    boost::scoped_ptr<TaskTrigger> rpath_update_trigger_;

    DISALLOW_COPY_AND_ASSIGN(PathResolverPartition);
};

//
// This is used to take a reference on a BgpRoute with at least one BgpPath
// that is being resolved by the PathResolver. Each ResolverPath for the
// BgpRoute in question has an intrusive pointer to the ResolverRouteState.
//
// The refcount doesn't need to be atomic because it's updated/accessed from
// exactly one DBPartition or PathResolverPartition.
//
class ResolverRouteState : public DBState {
public:
    ResolverRouteState(PathResolverPartition *partition, BgpRoute *route);
    virtual ~ResolverRouteState();

    static ResolverRouteState *LocateState(PathResolverPartition *partition,
        BgpRoute *route);

private:
    friend void intrusive_ptr_add_ref(ResolverRouteState *state);
    friend void intrusive_ptr_release(ResolverRouteState *state);

    PathResolverPartition *partition_;
    BgpRoute *route_;
    uint32_t refcount_;
};

inline void intrusive_ptr_add_ref(ResolverRouteState *state) {
    state->refcount_++;
}

inline void intrusive_ptr_release(ResolverRouteState *state) {
    assert(state->refcount_ != 0);
    if (--state->refcount_ == 0)
        delete state;
}

typedef boost::intrusive_ptr<ResolverRouteState> ResolverRouteStatePtr;

//
// This represents a BgpPath for which resolution has been requested. It's
// inserted into a map keyed by BgpPath pointer in a PathResolverPartition.
//
// If the client requests an update the ResolverPath is inserted into an
// update list in the PathResolverPartition. Similarly, if there's a change
// in the underlying BgpRoute for the ResolverNexthop, all of the impacted
// ResolverPaths are added to the update list in the PathResolverPartition.
// The latter happens when the ResolverNexthop update list in PathResolver
// is processed.
//
// A ResolverPath keeps a list of resolved BgpPaths it has added. A resolved
// path is added for each ecmp eligible BgpPath of the BgpRoute that tracks
// the nexthop of ResolverPath. The nexthop is represented by ResolverNexthop.
//
// The resolved paths of the ResolverPath are reconciled when the update list
// in the PathResolverPartition is processed. New resolved paths may get added,
// existing resolved paths may get updated and stale resolved paths could get
// deleted.
//
// The attributes of a resolved BgpPath are a combination of the attributes
// of the original BgpPath and the BgpPath of the nexthop being tracked. As a
// general rule, forwarding information (e.g. nexthop address, label, tunnel
// encapsulation) is obtained from tracking BgpPath while routing information
// (e.g. communities, as path, local pref) is obtained from original BgpPath.
//
class ResolverPath {
public:
    ResolverPath(PathResolverPartition *partition, const BgpPath *path,
        BgpRoute *route, ResolverNexthop *rnexthop);
    ~ResolverPath();

    bool UpdateResolvedPaths();

    PathResolverPartition *partition() const { return partition_; }
    BgpRoute *route() const { return route_; }
    const ResolverNexthop *rnexthop() const { return rnexthop_; }
    void clear_path() { path_ = NULL; }
    size_t resolved_path_count() const { return resolved_path_list_.size(); }

private:
    typedef std::set<BgpPath *> ResolvedPathList;

    void AddResolvedPath(ResolvedPathList::const_iterator it);
    void DeleteResolvedPath(ResolvedPathList::const_iterator it);
    BgpPath *LocateResolvedPath(const IPeer *peer, uint32_t path_id,
        const BgpAttr *attr, uint32_t label);

    PathResolverPartition *partition_;
    const BgpPath *path_;
    BgpRoute *route_;
    ResolverNexthop *rnexthop_;
    ResolverRouteStatePtr state_;
    ResolvedPathList resolved_path_list_;

    DISALLOW_COPY_AND_ASSIGN(ResolverPath);
};

//
// This represents a nexthop IP address to be resolved using the specified
// BgpTable. This need not be the BgpTable associated with the PathResolver.
// Each ResolverNexthop is inserted into a map keyed by ResolverNexthopKey
// in the PathResolver.
//
// A ResolverNexthop is created when resolution is requested for the first
// BgpPath with the associated IP address. At creation, the ResolverNexthop
// is added to the PathResolver's registration/unregistration list so that
// the ConditionMatch can be added to the BgpConditionListener. This list
// gets processed in the context of the bgp::Config Task.
//
// A ResolverNexthop maintains a vector of ResolverPathList, one entry per
// partition. Each ResolverPathList is a set of ResolverPaths that use the
// ResolverNexthop in question. When there's a change to the BgpRoute for
// the IP address being tracked, the ResolverNexthop is added to the update
// list in the PathResolver. The PathResolver processes the entries in this
// list in the context of the bgp::ResolverNexthop Task. The action is to
// trigger re-evaluation of all ResolverPaths that use the ResolverNexthop.
//
// When the last ResolverPath in a partition using a ResolverNexthop gets
// removed, the ResolverNexthop is added to the registration/unregistration
// in the PathResolver. The list is processed in the bgp::Config Task. If
// the ResolverNexthop is empty i.e. not being used by any ResolverPaths,
// the ConditionMatch is removed from the BgpConditionListener and is also
// erased from the map in the PathResolver. When the remove done callback
// gets invoked from BgpConditionListener, the ResolverNexthop is added to
// the register/unregistration list again. It's finally unregistered when
// the list is processed again.
//
// The registered flag keeps track of whether the ResolverNexthop has been
// registered with the BgpConditionListener.  It's needed to handle corner
// cases where a ResolverNexthop gets added to registration/unregistration
// list but all ResolverPaths using it get removed before the ResolverNexthop
// has been registered.
//
// A pointer to the BgpRoute for the matching IpAddress is maintained so
// that the PathResolverPartition can access it's BgpPaths. A reference
// to the BgpRoute is kept by setting ConditionMatchState for the route.
//
// A delete reference to BgpTable is maintained to ensure that the BgpTable
// does not get destroyed while there are ResolverNexthops tracking it.
//
class ResolverNexthop : public ConditionMatch {
public:
    ResolverNexthop(PathResolver *resolver, IpAddress address, BgpTable *table);
    virtual ~ResolverNexthop();

    virtual std::string ToString() const;
    virtual bool Match(BgpServer *server, BgpTable *table, BgpRoute *route,
        bool deleted);
    void AddResolverPath(int part_id, ResolverPath *rpath);
    void RemoveResolverPath(int part_id, ResolverPath *rpath);

    void TriggerAllResolverPaths() const;

    void ManagedDelete() { }

    IpAddress address() const { return address_; }
    BgpTable *table() const { return table_; }
    const BgpRoute *route() const { return route_; }
    bool empty() const;
    bool registered() const { return registered_; }
    void set_registered() { registered_ = true; }

private:
    typedef std::set<ResolverPath *> ResolverPathList;

    PathResolver *resolver_;
    IpAddress address_;
    BgpTable *table_;
    bool registered_;
    BgpRoute *route_;
    std::vector<ResolverPathList> rpath_lists_;
    LifetimeRef<ResolverNexthop> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(ResolverNexthop);
};

#endif  // SRC_BGP_ROUTING_INSTANCE_PATH_RESOLVER_H_
