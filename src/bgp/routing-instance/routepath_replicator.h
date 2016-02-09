/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_ROUTEPATH_REPLICATOR_H_
#define SRC_BGP_ROUTING_INSTANCE_ROUTEPATH_REPLICATOR_H_

#include <boost/ptr_container/ptr_map.hpp>
#include <sandesh/sandesh_trace.h>
#include <tbb/mutex.h>

#include <list>
#include <map>
#include <set>
#include <string>

#include "base/lifetime.h"
#include "base/util.h"
#include "bgp/bgp_path.h"
#include "db/db_table_walker.h"

class BgpRoute;
class BgpServer;
class BgpTable;
class RtGroup;
class RoutePathReplicator;
class RouteTarget;
class TaskTrigger;

//
// This keeps track of a RoutePathReplicator's listener state for a BgpTable.
// An instance is created for each VRF table and for the VPN table.
//
// An entry for VPN table is created when RoutePathReplicator is initialized.
// An entry for a VRF table is created when processing a Join for the first
// export route target for the VRF.
// TableState is removed when a Bgp table is deleted.
// TableState takes a delete reference to the BgpTable and DeleteActor of the
// TableState manages the unregister of this listener from the BgpTable and
// delete of TableState object.
//
// A TableState entry keeps track of all the export route targets for a VRF
// by maintaining the GroupList. TableState cannot be deleted if GroupList
// is non-empty and table has replicated routes. Replicated routes are tracked
// using the DBStateCount of this listener
//
class TableState {
public:
    typedef std::set<RtGroup *> GroupList;

    TableState(RoutePathReplicator *replicator, BgpTable *table);
    ~TableState();

    void ManagedDelete();
    bool MayDelete() const;
    void RetryDelete();

    LifetimeActor *deleter();
    const LifetimeActor *deleter() const;
    bool deleted() const;

    void AddGroup(RtGroup *group);
    void RemoveGroup(RtGroup *group);
    const RtGroup *FindGroup(RtGroup *group) const;
    bool empty() const { return list_.empty(); }

    DBTableBase::ListenerId listener_id() const { return listener_id_; }
    void set_listener_id(DBTableBase::ListenerId listener_id) {
        assert(listener_id_ == DBTableBase::kInvalidId);
        listener_id_ = listener_id;
    }

    uint32_t route_count() const;

    RoutePathReplicator *replicator() {
        return replicator_;
    }

    const RoutePathReplicator *replicator() const {
        return replicator_;
    }

    BgpTable *table() const {
        return table_;
    }

    BgpTable *table() {
        return table_;
    }

private:
    class DeleteActor;
    RoutePathReplicator *replicator_;
    BgpTable *table_;
    DBTableBase::ListenerId listener_id_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<TableState> table_delete_ref_;
    GroupList list_;

    DISALLOW_COPY_AND_ASSIGN(TableState);
};


// BulkSyncState
// This class holds the information of the TableWalk requests posted from
// config sync.
class BulkSyncState {
public:
    BulkSyncState() : id_(DBTable::kInvalidId), walk_again_(false) {
    }

    DBTableWalker::WalkId GetWalkerId() {
        return id_;
    }

    void SetWalkerId(DBTableWalker::WalkId id) {
        id_ = id;
    }

    void SetWalkAgain(bool walk) {
        walk_again_ = walk;
    }

    bool WalkAgain() {
        return walk_again_;
    }

private:
    DBTableWalker::WalkId id_;
    bool walk_again_;
    DISALLOW_COPY_AND_ASSIGN(BulkSyncState);
};

//
// This keeps track of the replication state for a route in the primary table.
// The ReplicatedRtPathList is a set of SecondaryRouteInfo, where each element
// represents a secondary path in a secondary table. An entry is added to the
// set when a path is replicated to a secondary table removed when it's not
// replicated anymore.
//
// Changes to ReplicatedRtPathList may be triggered by changes in the primary
// route, changes in the export targets of the primary table or changes in the
// import targets of secondary tables.
//
// A RtReplicated is deleted when the route in the primary table is no longer
// replicated to any secondary tables.
//
class RtReplicated : public DBState {
public:
    struct SecondaryRouteInfo {
    public:
        BgpTable *table_;
        const IPeer *peer_;
        uint32_t path_id_;
        BgpPath::PathSource  src_;
        BgpRoute *rt_;

        SecondaryRouteInfo(BgpTable *table, const IPeer *peer,
            uint32_t path_id, BgpPath::PathSource src, BgpRoute *rt)
            : table_(table),
              peer_(peer),
              path_id_(path_id),
              src_(src),
              rt_(rt) {
        }
        int CompareTo(const SecondaryRouteInfo &rhs) const {
            KEY_COMPARE(table_, rhs.table_);
            KEY_COMPARE(peer_, rhs.peer_);
            KEY_COMPARE(path_id_, rhs.path_id_);
            KEY_COMPARE(src_, rhs.src_);
            KEY_COMPARE(rt_, rhs.rt_);
            return 0;
        }
        bool operator<(const SecondaryRouteInfo &rhs) const {
            return (CompareTo(rhs) < 0);
        }
        bool operator>(const SecondaryRouteInfo &rhs) const {
            return (CompareTo(rhs) > 0);
        }

        std::string ToString() const;
    };

    typedef std::set<SecondaryRouteInfo> ReplicatedRtPathList;

    explicit RtReplicated(RoutePathReplicator *replicator);

    void AddRouteInfo(BgpTable *table, BgpRoute *rt,
        ReplicatedRtPathList::const_iterator it);
    void DeleteRouteInfo(BgpTable *table, BgpRoute *rt,
        ReplicatedRtPathList::const_iterator it);

    const ReplicatedRtPathList &GetList() const { return replicate_list_; }
    ReplicatedRtPathList *GetMutableList() { return &replicate_list_; }
    std::vector<std::string> GetTableNameList(const BgpPath *path) const;

private:
    RoutePathReplicator *replicator_;
    ReplicatedRtPathList replicate_list_;
};

//
// This class implements functionality to import and export BgpPaths between
// VRF BgpTables and a VPN BgpTable for a given address family. The exporting
// table is referred to as the primary table and the importing tables are
// referred to as secondary tables.
//
// The rules for importing and exporting paths are based on route targets of
// the VRF tables and the route targets in VPN routes. They can be summarized
// as follows:
//
// o The VPN table unconditionally imports routes from all VRF tables.
// o The VPN table potentially exports routes to all VRF tables.
// o A path in a VRF table is considered to have export targets of the table.
// o A path in the VPN table has route targets in it's attributes.
// o A path in a primary table is imported into a secondary table if one or
// o more of the targets for the path is in the list of import targets for
//   the secondary table.
// o Secondary paths are never exported to other secondary tables.
//
// Dependency tracking mechanisms are needed to implement replication in an
// an efficient manner. RoutePathReplicator does the following:
//
// 1. When an export target is added to or removed from a VRF table, walk all
//    routes in the VRF table to re-evaluate the new set of secondary paths.
//    The DBTableWalker provides this functionality.
// 2. When an import target is added to or removed from a VRF table, walk all
//    VRF tables that have the target in question as an export target.  The
//    list of tables that export a target is maintained in the RTargetGroupMgr.
//    This list is updated by the replicator (by calling RTargetGroupMgr APIs)
//    based on configuration changes in the routing instance.
// 3. When an import target is added to or removed from a VRF tables, walk all
//    VPN routes with the target in question.  This dependency is maintained
//    by RTargetGroupMgr.
// 4. When a route is updated, calculate new set of secondary paths by going
//    through all VRF tables that import one of the targets for the route in
//    question.  The list of VRF tables is obtained from the RTargetGroupMgr.
// 5. When a route is updated, remove secondary paths that are not required
//    anymore.  The list of previous secondary paths for a primary route is
//    maintained using RtReplicated and reconciled/synchronized with the new
//    list obtained from 4.
//
// The TableStateList keeps track of the TableState for each VRF from which
// routes could be exported. It also has an entry for the TableState for the
// VPN table. This entry is created when the replicator is initialized and
// deleted during shutdown.
//
// The BulkSyncOrders list keeps track of all the table walk requests that
// need to be triggered. Requests are added to this list because of 1 and 2.
// This list and StartWalk and BulkSyncOrders methods handle the complexity
// of multiple walk requests for the same table - either when the previous
// walk has already started or not.
//
// The UnregTableList keeps track of tables for which we need to delete the
// TableState. Requests are enqueued from the db::DBTable task when a table
// walk finishes and the TableState is empty.
//
class RoutePathReplicator {
public:
    RoutePathReplicator(BgpServer *server, Address::Family family);
    virtual ~RoutePathReplicator();

    void Initialize();
    void DeleteVpnTableState();

    void Join(BgpTable *table, const RouteTarget &rt, bool import);
    void Leave(BgpTable *table, const RouteTarget &rt, bool import);

    const RtReplicated *GetReplicationState(BgpTable *table,
                                            BgpRoute *rt) const;
    std::vector<std::string> GetReplicatedTableNameList(const BgpTable *table,
        const BgpRoute *route, const BgpPath *path) const;

    SandeshTraceBufferPtr trace_buffer() const { return trace_buf_; }

private:
    friend class ReplicationTest;
    friend class RtReplicated;
    friend class TableState;

    typedef std::map<BgpTable *, TableState *> TableStateList;
    typedef std::map<BgpTable *, BulkSyncState *> BulkSyncOrders;
    typedef std::set<BgpTable *> UnregTableList;

    bool StartWalk();
    void RequestWalk(BgpTable *table);
    void BulkReplicationDone(DBTableBase *dbtable);
    bool UnregisterTables();

    TableState *AddTableState(BgpTable *table, RtGroup *group = NULL);
    void RemoveTableState(BgpTable *table, RtGroup *group);
    void DeleteTableState(BgpTable *table);
    void UnregisterTableState(BgpTable *table);
    TableState *FindTableState(BgpTable *table);
    const TableState *FindTableState(const BgpTable *table) const;

    void JoinVpnTable(RtGroup *group);
    void LeaveVpnTable(RtGroup *group);

    bool RouteListener(TableState *ts, DBTablePartBase *root,
                       DBEntryBase *entry);
    void DeleteSecondaryPath(BgpTable  *table, BgpRoute *rt,
                             const RtReplicated::SecondaryRouteInfo &rtinfo);
    void DBStateSync(BgpTable *table, TableState *ts, BgpRoute *rt,
                     RtReplicated *dbstate,
                     const RtReplicated::ReplicatedRtPathList *future);

    bool BulkSyncExists(BgpTable *table) const {
        return (bulk_sync_.find(table) != bulk_sync_.end());
    }

    BgpServer *server() { return server_; }
    Address::Family family() const { return family_; }
    const BgpServer *server() const { return server_; };

    // Mutex to protect bulk_sync_ from multiple DBTable tasks.
    tbb::mutex mutex_;
    BulkSyncOrders bulk_sync_;
    TableStateList table_state_list_;
    BgpServer *server_;
    Address::Family family_;
    BgpTable *vpn_table_;
    boost::scoped_ptr<TaskTrigger> walk_trigger_;
    SandeshTraceBufferPtr trace_buf_;

    DISALLOW_COPY_AND_ASSIGN(RoutePathReplicator);
};

#endif  // SRC_BGP_ROUTING_INSTANCE_ROUTEPATH_REPLICATOR_H_
