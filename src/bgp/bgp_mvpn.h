/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_MVPN_H_
#define SRC_BGP_BGP_MVPN_H_

#include <tbb/reader_writer_lock.h>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <boost/scoped_ptr.hpp>

#include "base/lifetime.h"
#include "base/address.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_table.h"
#include "db/db_entry.h"

class BgpPath;
class BgpRoute;
class BgpServer;
class BgpTable;
class ErmVpnRoute;
class ErmVpnTable;
struct MvpnDBState;
class MvpnManager;
class MvpnManagerPartition;
class MvpnPrefix;
class MvpnProjectManager;
class MvpnProjectManagerPartition;
class MvpnRoute;
class MvpnState;
class MvpnTable;
class PathResolver;
class RoutingInstance;
class UpdateInfo;

typedef boost::intrusive_ptr<MvpnState> MvpnStatePtr;

// This struct represents a MVPN Neighbor discovered using BGP or using auto
// exported routes from one routing-instance into another.
//
// Each received Type1 Intra AS Auto-Discovery ad Type2 Inter AS Auto Discovery
// routes from MVPN BGP Peers is maintained as MVPN neighbors inside a map in
// each MvpnManager object. RouteDistinguisher is used as the key to this map.
//
// Typically neighbor information is used in mvpn when the Type I and Type II
// routes also carry PMSI tunnel information to send multicast traffic to all
// intended receivers. In Phase I, PMSI information is not sent encoded inside
// Type I/Type II AD routes. Also, in phase I, PMSI information if received in
// Type I/Type II routes from remote neighbors is not processed.
struct MvpnNeighbor {
public:
    MvpnNeighbor();
    MvpnNeighbor(const RouteDistinguisher &rd, const IpAddress &originator);
    std::string ToString() const;
    const RouteDistinguisher &rd() const;
    const IpAddress &originator() const;
    uint32_t source_as() const;
    bool operator==(const MvpnNeighbor &rhs) const;

private:
    friend class MvpnManagerPartition;

    RouteDistinguisher rd_;
    IpAddress originator_;
    uint32_t source_as_;
};

// This class manages Mvpn routes with in a partition of an MvpnTable.
//
// It holds a back pointer to the parent MvpnManager class along with the
// partition id this object belongs to.
//
// Upon route change notification, based on the route-type for which a route
// notification has been received, different set of actions are undertaken in
// this class. This class only handles routes in which customer 'Group' info is
// encoded, such as Type3 S-PMSI routes.
//
// Notification for a Type7 SourceTreeJoinRoute route add/change/delete
//     When this route is successfully imported into a vrf, new Type3 S-PMSI
//     route is originated/updated into that vrf.mvpn.0 if there is at least
//     one associated Join route from the agent with flags marked as "Sender"
//     or "SenderAndReciver".
//
// Notification for a Type3 S-PMSI route add/change/delete
//     When sender indicates that it has a sender for a particular <S,G> entry,
//     (With Leaf-Information required in the PMSI tunnel attribute), then this
//     class originates/updates a new Type4 LeafAD route into vrf.mvpn.0 table.
//     LeafAD route however is originated only if a usuable GlobalErmVpnRoute
//     is avaiable for stitching. On the other hand, if such a route was already
//     originated before and now is no longer feasible, it is deleted instead.
//
// Notification for a Type4 Leaf AD route add/change/delete
//     When leaf-ad routes are imported into a vrf and notified, the routes are
//     stored inside a map using route as the key and best path attributes as
//     values in the associated MvpnState. If the route is deleted, then it is
//     deleted from the map as well. If a usable Type5 source active route is
//     present in the vrf, then it is notified so that sender agent can be
//     updated with the right set of path attributes (PMSI) in order to be able
//     to replicate multicast traffic in the data plane.
class MvpnManagerPartition {
public:
    MvpnManagerPartition(MvpnManager *manager, int part_id);
    virtual ~MvpnManagerPartition();
    MvpnProjectManagerPartition *GetProjectManagerPartition();
    const MvpnProjectManagerPartition *GetProjectManagerPartition() const;

private:
    friend class MvpnManager;

    MvpnTable *table();
    int listener_id() const;

    void ProcessType3SPMSIRoute(MvpnRoute *spmsi_rt);
    void ProcessType4LeafADRoute(MvpnRoute *leaf_ad);
    void ProcessType5SourceActiveRoute(MvpnRoute *join_rt);
    void ProcessType7SourceTreeJoinRoute(MvpnRoute *join_rt);

    MvpnStatePtr GetState(MvpnRoute *route);
    MvpnStatePtr GetState(MvpnRoute *route) const;
    MvpnStatePtr LocateState(MvpnRoute *route);
    void NotifyForestNode(const Ip4Address &source, const Ip4Address &group);
    bool GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
            Ip4Address *address, std::vector<std::string> *encap) const;

    MvpnManager *manager_;
    int part_id_;

    DISALLOW_COPY_AND_ASSIGN(MvpnManagerPartition);
};

// This class manages MVPN routes for a given vrf.mvpn.0 table.
//
// In each *.mvpn.0 table, an instance of this class is created when ever the
// mvpn table itself is created (which happens when its parent routing-instance
// gets created.
//
// This class allocated one instance of MvpnManagerPartition for each DB
// partition. While all <S,G> specific operations are essentially manages inside
// MvpnManagerPartition object (in order to get necessary protection from
// concurrency across different db partitions), all other operations which are
// <S,G> agnostic are mainly handled in this class.
//
// Specifically, all MVPN BGP neighbors are maintained in std::map NeighborMap.
// Neighbors are created or updated when Type1/Type2 paths are received and are
// deleted when those routes are deleted. All access to this map is protected
// by a mutex because even though the map itself may be created, updated, or
// deleted always serially from with in the same db task, map will get accessed
// (read) concurrently from task running of different DB partitions.
//
// This class also provides DeleteActor and maintains a LifetimeRef to parent
// MvpnTable object in order to ensure orderly cleanup during table deletion.
class MvpnManager {
public:
    typedef std::vector<MvpnManagerPartition *> PartitionList;
    typedef PartitionList::const_iterator const_iterator;
    typedef std::map<RouteDistinguisher, MvpnNeighbor> NeighborMap;

    MvpnManager(MvpnTable *table, ErmVpnTable *ermvpn_table);
    virtual ~MvpnManager();
    bool FindNeighbor(const RouteDistinguisher &rd, MvpnNeighbor *nbr) const;
    MvpnProjectManager *GetProjectManager();
    void ManagedDelete();
    BgpRoute *RouteReplicate(BgpServer *server, BgpTable *src_table,
        BgpRoute *source_rt, const BgpPath *src_path, ExtCommunityPtr comm);
    void ResolvePath(RoutingInstance *rtinstance, BgpRoute *rt, BgpPath *path);
    MvpnTable *table();
    const MvpnTable *table() const;
    int listener_id() const;
    virtual void Terminate();
    virtual void Initialize();
    size_t neighbors_count() const;
    const NeighborMap &neighbors() const;
    void ReOriginateType1Route(const Ip4Address &old_identifier);
    void OriginateType1Route();
    bool MayDelete() const;
    const LifetimeActor *deleter() const;
    bool deleted() const;
    LifetimeActor *deleter();
    virtual void UpdateSecondaryTablesForReplication(MvpnRoute *rt,
            BgpTable::TableSet *secondary_tables) const;
    tbb::reader_writer_lock &neighbors_mutex() { return neighbors_mutex_; }

private:
    friend class MvpnManagerPartition;
    class DeleteActor;

    void AllocPartitions();
    void FreePartitions();
    void ProcessType1ADRoute(MvpnRoute *route);
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);
    bool FindResolvedNeighbor(const BgpPath *path,
                              MvpnNeighbor *neighbor) const;
    void SetDBState(MvpnRoute *route, MvpnDBState *mvpn_dbstate);
    void ClearDBState(MvpnRoute *route);

    MvpnTable *table_;
    ErmVpnTable *ermvpn_table_;
    int listener_id_;
    int identifier_listener_id_;
    tbb::atomic<int> db_states_count_;
    PartitionList partitions_;

    NeighborMap neighbors_;
    mutable tbb::reader_writer_lock neighbors_mutex_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<MvpnManager> table_delete_ref_;
    LifetimeRef<MvpnManager> ermvpn_table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(MvpnManager);
};

// This class holds Mvpn state for a particular <S,G> at any given time.
//
// In MVPN state machinery, different types of routes are sent and received at
// different phases of processing. This class holds all relevant information
// associated with an <S,G>.
//
// This is a refcounted class which is referred by DB States of different
// routes. When the refcount reaches 0, (last referring db state is deleted),
// this object is deleted from the container map and then destroyed.
//
// global_ermvpn_tree_rt_
//     This is a reference to GlobalErmVpnRoute associated with the ErmVpnTree
//     used in the data plane for this <S,G>. This route is created/updated
//     when ErmVpn notifies changes to ermvpn routes.
//
// spmsi_rt_
//     This is the 'only' Type3 SPMSI sender route originated for this S,G.
//     When an agent indicates that it has an active sender for a particular
//     <S,G> via Join route, then this route is originated (if there is atleast
//     one active receiver)
//
// spmsi_routes_received_
//     This is a set of all Type3 spmsi routes received for this <S-G>. It is
//     possible that when these routes are received, then there is no ermvpn
//     tree route to use for forwarding in the data plane. In such a case, later
//     when global_ermvpn_tree_rt_ does get updated, all leaf ad routes in this
//     set are notified and re-evaluated.
//
// leafad_routes_attr_received_
//     This is a map of all type 4 leaf ad routes originated (in response to
//     received/imported type-3 spmsi routes. For each route, associated path
//     attributes of the best path are stored as value inside the map. Whenever
//     this map changes or when a new type-5 source active route is received,
//     the correspnding sender agent is notified with the olist that contains
//     the PMSI attributes as received in leafad routes path attributes.
//
// states_
//     This is the parent map that holds 'this' MvpnState pointer as the value
//     for the associated SG key. When the refcount reaches zero, it indicates
//     that there is no reference to this state from of the DB States associated
//     with any Mvpn route. Hence at that time, this state is removed this map
//     states_ and destroyed. This map actually sits inside the associated
//     MvpnProjectManagerParitition object.
class MvpnState {
public:
    typedef std::set<MvpnRoute *> RoutesSet;
    typedef std::map<MvpnRoute *, BgpAttrPtr> RoutesMap;

    // Simple structure to hold <S,G>. Source as "0.0.0.0" can be used to encode
    // <*,G> as well.
    struct SG {
        SG(const Ip4Address &source, const Ip4Address &group);
        SG(const IpAddress &source, const IpAddress &group);
        explicit SG(const ErmVpnRoute *route);
        explicit SG(const MvpnRoute *route);
        bool operator<(const SG &other) const;

        IpAddress source;
        IpAddress group;
    };

    typedef std::map<SG, MvpnState *> StatesMap;
    MvpnState(const SG &sg, StatesMap *states, MvpnProjectManager *pm);

    virtual ~MvpnState();
    const SG &sg() const;
    ErmVpnRoute *global_ermvpn_tree_rt();
    const ErmVpnRoute *global_ermvpn_tree_rt() const;
    MvpnRoute *spmsi_rt();
    const MvpnRoute *spmsi_rt() const;
    void set_global_ermvpn_tree_rt(ErmVpnRoute *global_ermvpn_tree_rt);
    void set_spmsi_rt(MvpnRoute *spmsi_rt);
    RoutesSet &spmsi_routes_received();
    RoutesMap &leafad_routes_attr_received();
    const RoutesSet &spmsi_routes_received() const;
    const RoutesMap &leafad_routes_attr_received() const;
    const StatesMap *states() const { return states_; }
    StatesMap *states() { return states_; }
    MvpnRoute *source_active_rt();
    const MvpnRoute *source_active_rt() const;
    void set_source_active_rt(MvpnRoute *source_active_rt);
    MvpnProjectManager *project_manager() { return project_manager_; }
    const MvpnProjectManager *project_manager() const {
        return project_manager_;
    }
    int refcount() const { return refcount_; }

private:
    friend class MvpnDBState;
    friend class MvpnManagerPartition;
    friend class MvpnProjectManagerPartition;
    friend void intrusive_ptr_add_ref(MvpnState *mvpn_state);
    friend void intrusive_ptr_release(MvpnState *mvpn_state);

    const ErmVpnTable *table() const;

    SG sg_;
    ErmVpnRoute *global_ermvpn_tree_rt_;
    MvpnRoute *spmsi_rt_;
    MvpnRoute *source_active_rt_;
    RoutesSet spmsi_routes_received_;
    RoutesMap leafad_routes_attr_received_;
    StatesMap *states_;
    MvpnProjectManager *project_manager_;
    tbb::atomic<int> refcount_;

    DISALLOW_COPY_AND_ASSIGN(MvpnState);
};

// This class holds a reference to MvpnState along with associated route
// pointer. This is stored as DBState inside the table along with the
// associated route.
//
// Note: Routes are never deleted until the DB state is deleted. MvpnState which
// is refcounted is also deleted only when there is no MvpnDBState that refers
// to it.
struct MvpnDBState : public DBState {
    explicit MvpnDBState(MvpnStatePtr state);
    ~MvpnDBState();
    MvpnStatePtr state();
    MvpnRoute *route();
    void set_state(MvpnStatePtr state);
    void set_route(MvpnRoute *route);

private:
    MvpnStatePtr state_;
    MvpnRoute *route_;

    DISALLOW_COPY_AND_ASSIGN(MvpnDBState);
};

// This class glues mvpn and ermvpn modules, inside a particular DB partition.
//
// Each MVPN is associated with a parent MvpnProjectManager virtual-network via
// configuration. This parent MvpnProjectManager's ermvpn tree is the one used
// for all multicast packets replication in the data plane for the given MVPN.
//
// Inside each RoutingInstance object, name of this parent manager virtual
// network is stored in mvpn_project_manager_network_ string. When ever this
// information is set/modified/cleared in the routing instance, all associated
// Type3 S-PMSI MPVN received routes should be notified for re-evaluation.
//
// MvpnState::StatesMap states_
//     A Map of <<S,G>, MvpnState> is maintained to hold MvpnState for all
//     <S,G>s that fall into a specific DB partition.
//
// This provides APIs to create/update/delete MvpnState as required. MvpnState
// is refcounted. When the refcount reaches 1, it is deleted from the StatesMap
// and destroyed.
class MvpnProjectManagerPartition {
public:
    typedef MvpnState::SG SG;

    MvpnProjectManagerPartition(MvpnProjectManager*manager, int part_id);
    virtual ~MvpnProjectManagerPartition();
    MvpnStatePtr GetState(const SG &sg);
    MvpnStatePtr GetState(const SG &sg) const;
    MvpnStatePtr LocateState(const SG &sg);
    MvpnStatePtr CreateState(const SG &sg);
    const MvpnState::StatesMap &states() const { return states_; }
    MvpnState::StatesMap &states() { return states_; }

private:
    friend class MvpnProjectManager;
    friend class MvpnManagerPartition;

    ErmVpnRoute *GetGlobalTreeRootRoute(ErmVpnRoute *rt) const;
    ErmVpnTable *table();
    const ErmVpnTable *table() const;
    void RouteListener(DBEntryBase *db_entry);
    int listener_id() const;
    void NotifyForestNode(const Ip4Address &source, const Ip4Address &group);
    bool GetForestNodePMSI(ErmVpnRoute *rt, uint32_t *label,
            Ip4Address *address, std::vector<std::string> *encap) const;
    bool IsUsableGlobalTreeRootRoute(ErmVpnRoute *ermvpn_route) const;

    // Back pointer to the parent MvpnProjectManager
    MvpnProjectManager *manager_;

    // Partition id of the manged DB partition.
    int part_id_;
    MvpnState::StatesMap states_;;

    DISALLOW_COPY_AND_ASSIGN(MvpnProjectManagerPartition);
};

// This class glues mvpn and ermvpn modules
//
// It maintains a list of MvpnProjectManagerPartition objects, one for each DB
// partition.
//
// It listens to changes to ErmVpn table and for any applicable change to
// GlobalErmVpnRoute, it notifies all applicable received SPMSI routes so that
// those routes can be replicated/deleted based on the current state of the
// GlobalErmVpnRoute associated with a given <S,G>.
//
// This class also provides DeleteActor and maintains a LifetimeRef to parent
// MvpnTable object in order to ensure orderly cleanup during table deletion.
class MvpnProjectManager {
public:
    class DeleteActor;
    typedef std::vector<MvpnProjectManagerPartition *> PartitionList;
    typedef PartitionList::const_iterator const_iterator;

    explicit MvpnProjectManager(ErmVpnTable *table);
    virtual ~MvpnProjectManager();
    MvpnProjectManagerPartition *GetPartition(int part_id);
    const MvpnProjectManagerPartition *GetPartition(int part_id) const;
    void ManagedDelete();
    virtual void Terminate();
    ErmVpnTable *table();
    const ErmVpnTable *table() const;
    int listener_id() const;
    const LifetimeActor *deleter() const;
    LifetimeActor *deleter();
    bool deleted() const;
    virtual void Initialize();
    MvpnStatePtr GetState(MvpnRoute *route) const;
    MvpnStatePtr GetState(MvpnRoute *route);
    MvpnStatePtr GetState(ErmVpnRoute *route) const;
    UpdateInfo *GetUpdateInfo(MvpnRoute *route);
    const PartitionList &partitions() const { return partitions_; }
    bool MayDelete() const;
    void GetMvpnSourceAddress(ErmVpnRoute *ermvpn_route,
                              Ip4Address *address) const;

private:
    void AllocPartitions();
    void FreePartitions();
    void RouteListener(DBTablePartBase *tpart, DBEntryBase *db_entry);
    UpdateInfo *GetType7UpdateInfo(MvpnRoute *route);

    // Parent ErmVpn table.
    ErmVpnTable *table_;
    int listener_id_;
    PartitionList partitions_;

    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<MvpnProjectManager> table_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(MvpnProjectManager);
};

// Increment refcont atomically.
inline void intrusive_ptr_add_ref(MvpnState *mvpn_state) {
    mvpn_state->refcount_.fetch_and_increment();
}

// Decrement refcount of an mvpn_state. If the refcount falls to 1, it implies
// that there is no more reference to this particular state from any other data
// structure. Hence, it can be deleted from the container map and destroyed as
// well.
inline void intrusive_ptr_release(MvpnState *mvpn_state) {
    int prev = mvpn_state->refcount_.fetch_and_decrement();
    if (prev > 1)
        return;
    if (mvpn_state->states()) {
        MvpnState::StatesMap::iterator iter =
            mvpn_state->states()->find(mvpn_state->sg());
        if (iter != mvpn_state->states()->end()) {
            assert(iter->second == mvpn_state);
            mvpn_state->states()->erase(iter);

            // Attempt project manager deletion as it could be held up due to
            // this map being non-empty so far..
            if (mvpn_state->project_manager()->deleter()->IsDeleted())
                mvpn_state->project_manager()->deleter()->RetryDelete();
        }
    }
    delete mvpn_state;
}

#define MVPN_RT_LOG(rt, ...) \
    RTINSTANCE_LOG(MvpnRoute, this->table()->routing_instance(), \
                   SandeshLevel::UT_DEBUG, \
                   RTINSTANCE_LOG_FLAG_ALL, \
                   (rt)->GetPrefix().source().to_string(), \
                   (rt)->GetPrefix().group().to_string(), \
                   (rt)->GetType(), (rt)->ToString(), ##__VA_ARGS__)

#define MVPN_ERMVPN_RT_LOG(rt, ...) \
    RTINSTANCE_LOG(MvpnErmVpnRoute, this->table()->routing_instance(), \
                   SandeshLevel::UT_DEBUG, \
                   RTINSTANCE_LOG_FLAG_ALL, \
                   (rt)->GetPrefix().source().to_string(), \
                   (rt)->GetPrefix().group().to_string(), \
                   (rt)->GetType(), (rt)->ToString(), ##__VA_ARGS__)

#define MVPN_LOG(type, ...) \
    RTINSTANCE_LOG(type, this->table()->routing_instance(), \
        SandeshLevel::SYS_DEBUG, RTINSTANCE_LOG_FLAG_ALL, ##__VA_ARGS__)

#define MVPN_TRACE(type, ...) \
    RTINSTANCE_LOG(type, this->table()->routing_instance(), \
        SandeshLevel::UT_DEBUG, RTINSTANCE_LOG_FLAG_ALL, ##__VA_ARGS__)

#endif  // SRC_BGP_BGP_MVPN_H_
