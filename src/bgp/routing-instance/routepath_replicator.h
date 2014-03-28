/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_routepath_replicator_h
#define ctrlplane_routepath_replicator_h

#include <list>

#include <boost/ptr_container/ptr_map.hpp>
#include <tbb/mutex.h>

#include "bgp/bgp_table.h"
#include "bgp/community.h"
#include "bgp/rtarget/rtarget_address.h"
#include "db/db_table_walker.h"

#include <sandesh/sandesh_trace.h>

class BgpRoute;
class BgpServer;
class RtGroup;
class RouteTarget;
class TaskTrigger;

class TableState {
public:
    typedef std::list<RtGroup *> GroupList;
    TableState(BgpTable *table, DBTableBase::ListenerId id);
    ~TableState();

    void ManagedDelete();

    const GroupList &GetGroupList() const {
        return list_;
    }
    GroupList *MutableGroupList() {
        return &list_;
    }
    DBTableBase::ListenerId GetListenerId() const {
        return id_;
    }

private:
    DBTableBase::ListenerId id_;
    LifetimeRef<TableState> table_delete_ref_;
    GroupList list_;
    DISALLOW_COPY_AND_ASSIGN(TableState);
};


// BulkSyncState
// This class holds the information of the TableWalk requests posted from config sync
class BulkSyncState {
public:
    BulkSyncState() {
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
           : table_(table), peer_(peer), path_id_(path_id), src_(src), rt_(rt) {
        }   
        bool operator<(const SecondaryRouteInfo &rhs) const {
            if (table_ != rhs.table_) 
                return (table_ < rhs.table_);
            if (peer_ != rhs.peer_)
                return (peer_ < rhs.peer_);
            if (path_id_ != rhs.path_id_)
                return (path_id_ < rhs.path_id_);
            if (src_ != rhs.src_)
                return (src_ < rhs.src_);
            return (rt_ < rhs.rt_);
        }
        bool operator>(const SecondaryRouteInfo &rhs) const {
            if (table_ != rhs.table_) 
                return (table_ > rhs.table_);
            if  (peer_ != rhs.peer_)
                return (peer_ > rhs.peer_);
            if (path_id_ != rhs.path_id_)
                return (path_id_ > rhs.path_id_);
            if (src_ != rhs.src_)
                return (src_ > rhs.src_);
            return (rt_ > rhs.rt_);
        }
        std::string ToString() const; 
    };  

    typedef std::set<SecondaryRouteInfo> ReplicatedRtPathList;

    // Get the list of replicated route for given Primary Route
    const ReplicatedRtPathList &GetList() const {
        return replicate_list_;
    }

    ReplicatedRtPathList *GetMutableList() {
        return &replicate_list_;
    }

    // Add a replicated route to List
    void AddReplicatedRt(BgpTable *dest, const IPeer *peer, BgpRoute *rt);

    // Remove a replicated route from List
    // DBState should be removed once the list is empty
    bool RemoveReplicatedRt(BgpTable *dest, const IPeer *peer);

private:
    ReplicatedRtPathList  replicate_list_;
};

// Matrix of RouteTarget and BgpTable that imports & exports route belonging
// to a given route target.
// This class contains the Map of RouteTarget to RtGroup
class RoutePathReplicator {
public:
    RoutePathReplicator(BgpServer *server, Address::Family family);
    virtual ~RoutePathReplicator();
    // Add a given BgpTable to RtGroup of given RouteTarget
    // It will create a new RtGroup if none exists
    // Register to DB for callback if not yet done
    void Join(BgpTable *table, const RouteTarget &rt, bool import);

    // Remove a BgpTable from RtGroup of given RouteTarget
    // If the last group is going away, the RtGroup will be removed
    // Unregister from DB for callback
    void Leave(BgpTable *table, const RouteTarget &rt, bool import);

    // Table listener
    // Attach RT based on routing instance import_list
    // Export a route (Clone the BgpPath) to different routing instance
    // based on export_list of routing instance
    bool BgpTableListener(DBTablePartBase *root, DBEntryBase *entry);

    // WalkComplete function
    void BulkReplicationDone(DBTableBase *table);

    BgpServer *server() { return server_; }
    Address::Family family() { return family_; }

    const RtReplicated *GetReplicationState(BgpTable *table, 
                                            BgpRoute *rt) const;

    void RequestWalk(BgpTable *table);

    SandeshTraceBufferPtr trace_buffer() const { return trace_buf_; }

    bool UnregisterTables();

private:
    typedef std::map<BgpTable *, TableState *> RouteReplicatorTableState;
    typedef std::map<BgpTable *, BulkSyncState *> BulkSyncOrders;
    typedef std::set<BgpTable *> UnregTableList;

    bool StartWalk();

    void AddVpnTable(RtGroup *rtgroup);

    void DeleteSecondaryPath(BgpTable  *table, BgpRoute *rt,
                             const RtReplicated::SecondaryRouteInfo &rtinfo);
    void DBStateSync(BgpTable *table, BgpRoute *rt, DBTableBase::ListenerId id,
                     RtReplicated *dbstate, 
                     RtReplicated::ReplicatedRtPathList &current);

    // Mutex to protect unreg_table_list_, table_state_ and bulk_sync_ 
    // from multiple DBTable task
    tbb::mutex mutex_;
    RouteReplicatorTableState table_state_;
    BulkSyncOrders bulk_sync_;
    UnregTableList unreg_table_list_;
    BgpServer *server_;
    Address::Family family_;
    boost::scoped_ptr<TaskTrigger> walk_trigger_;
    boost::scoped_ptr<TaskTrigger> unreg_trigger_;
    SandeshTraceBufferPtr trace_buf_;
};

#endif // ctrlplane_routepath_replicator_h
