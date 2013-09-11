/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routepath_replicator.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group.h"
#include "bgp/routing-instance/routing_instance_analytics_types.h"
#include "db/db_table_partition.h"
#include "db/db_table_walker.h"

//
// RoutePathReplication trace macro. Optionally logs the server name as well for
// easier debugging in multi server unit tests
//
#define RPR_TRACE(obj, ...)                                                    \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    bgp_log_test::LogServerName(server());                                     \
    Rpr##obj##Log::Send(g_vns_constants.CategoryNames.find(                    \
            Category::ROUTING_INSTANCE)->second,                               \
            SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, __VA_ARGS__);         \
    Rpr##obj::TraceMsg(trace_buf_, __FILE__, __LINE__, __VA_ARGS__);           \
} while (false)

#define RPR_TRACE_ONLY(obj, ...)                                               \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    bgp_log_test::LogServerName(server());                                     \
    Rpr##obj::TraceMsg(trace_buf_, __FILE__, __LINE__, __VA_ARGS__);           \
} while (false)

TableState::TableState(BgpTable *table, DBTableBase::ListenerId id)
    : id_(id), table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

TableState::~TableState() {
}

// TODO: verify that the RoutePathReplicator is going to Leave this table.
void TableState::ManagedDelete() {
}

RoutePathReplicator::RoutePathReplicator(
        BgpServer *server, Address::Family family)
        : server_(server),
          family_(family),
          walk_trigger_(new TaskTrigger(
          boost::bind(&RoutePathReplicator::StartWalk, this),
              TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
          unreg_trigger_(new TaskTrigger(
          boost::bind(&RoutePathReplicator::UnregisterTables, this),
              TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
          trace_buf_(SandeshTraceBufferCreate("RoutePathReplicator", 500)) {
}

RoutePathReplicator::~RoutePathReplicator() {
}

// Search a RtGroup
RtGroup *RoutePathReplicator::GetRtGroup(const RouteTarget &rt) {
    RtGroupMap::iterator loc = rt_group_map_.find(rt);
    if (loc != rt_group_map_.end()) {
        return loc->second;
    }
    return NULL;
}

// Search a RtGroup
RtGroup *
RoutePathReplicator::GetRtGroup(
    const ExtCommunity::ExtCommunityValue &community) {
    RouteTarget rt(community);
    return GetRtGroup(rt);
}

RtGroup *
RoutePathReplicator::LocateRtGroup(const RouteTarget &rt) {
    RtGroup *group = GetRtGroup(rt);
    if (group == NULL) {
        group = new RtGroup(rt);
        rt_group_map_.insert(rt, group);
        // Master routingInstance imports/exports all RouteTargets
        RoutingInstance *master =
            server()->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance);
        if (master) {
            BgpTable *table = master->GetTable(family());
            if (table) {
                Join(table, rt, true);
                Join(table, rt, false);
            }
        }
    }
    return group;
}

void
RoutePathReplicator::RequestWalk(BgpTable *table) {
    CHECK_CONCURRENCY("bgp::Config");
    BulkSyncState *state = NULL;
    BulkSyncOrders::iterator loc = bulk_sync_.find(table);
    if (loc != bulk_sync_.end()) {
        // Accumulate the walk request till walk is started.
        // After the walk is started don't cancel/interrupt the walk
        // instead remember the request to walk again
        // Walk will restarted after completion of current walk
        // This situation is possible in cases where DBWalker yeilds or
        // config task requests for walk before the previous walk is finished
        if (loc->second->GetWalkerId() != DBTableWalker::kInvalidWalkerId) {
            // This will be reset when the walk actually starts
            loc->second->SetWalkAgain(true);
        }
        return;
    } else {
        state = new BulkSyncState();
        state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        bulk_sync_.insert(std::make_pair(table, state));
    }
}

bool
RoutePathReplicator::StartWalk() {
    CHECK_CONCURRENCY("bgp::Config");
    DBTableWalker::WalkCompleteFn walk_complete
        = boost::bind(&RoutePathReplicator::BulkReplicationDone, this, _1);

    DBTableWalker::WalkFn walker
        = boost::bind(&RoutePathReplicator::BgpTableListener, this, _1, _2);
    // For each member table, start a walker to replicate
    for(BulkSyncOrders::iterator it = bulk_sync_.begin();
        it != bulk_sync_.end(); it++) {
        if (it->second->GetWalkerId() != DBTableWalker::kInvalidWalkerId) {
            // Walk is in progress.
            continue;
        }
        RPR_TRACE(Walk, it->first->name());
        DB *db = server()->database();
        DBTableWalker::WalkId id = db->GetWalker()->WalkTable(
            it->first, NULL, walker, walk_complete);
        it->second->SetWalkerId(id);
        it->second->SetWalkAgain(false);
    }
    return true;
}

bool 
RoutePathReplicator::UnregisterTables() {
    for (UnregTableList::iterator it = unreg_table_list_.begin(); 
         it != unreg_table_list_.end(); it++) {
        BgpTable *bgptable = *it;
        RtGroupTableState::iterator ts_it = table_state_.find(bgptable);
        assert(ts_it != table_state_.end());
        TableState *ts = ts_it->second;
        if (ts->GetGroupList().empty()) {
            bgptable->Unregister(ts->GetListenerId());
            table_state_.erase(bgptable);
            RPR_TRACE(UnregTable, bgptable->name());
            delete ts;
        }
    }
    unreg_table_list_.clear();
    return true;
}

void
RoutePathReplicator::BulkReplicationDone(DBTableBase *table) {
    tbb::mutex::scoped_lock lock(mutex_);
    BgpTable *bgptable = static_cast<BgpTable *>(table);
    RPR_TRACE(WalkDone, table->name());
    BulkSyncOrders::iterator loc = bulk_sync_.find(bgptable);
    assert (loc != bulk_sync_.end());
    BulkSyncState *bulk_sync_state = loc->second;
    if (bulk_sync_state->WalkAgain()) {
        bulk_sync_state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        walk_trigger_->Set();
        return;
    }
    delete bulk_sync_state;
    bulk_sync_.erase(loc);
    RtGroupTableState::iterator ts_it = table_state_.find(bgptable);
    assert(ts_it != table_state_.end());
    TableState *ts = ts_it->second;
    if (ts->GetGroupList().empty()) {
        unreg_table_list_.insert(bgptable);
    }
    unreg_trigger_->Set();
}

void RoutePathReplicator::Join(BgpTable *table, const RouteTarget &rt,
                               bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RtGroup *group = LocateRtGroup(rt);

    // Add the Table to Group
    if (import)
        group->AddImportTable(table);
    else
        group->AddExportTable(table);

    RPR_TRACE(TableJoin, table->name(), rt.ToString(), import);
    if (import) {
        BOOST_FOREACH(BgpTable *bgptable, group->GetExportTables()) {
            RequestWalk(bgptable);
        }
        walk_trigger_->Set();
        return;
    }

    RtGroupTableState::iterator loc = table_state_.find(table);
    if (loc == table_state_.end()) {
        // Route Replication/export is done in DB notification
        DBTableBase::ListenerId id = table->Register(
            boost::bind(&RoutePathReplicator::BgpTableListener, this, _1, _2));
        TableState *ts = new TableState(table, id);
        ts->MutableGroupList()->push_back(group);
        table_state_.insert(std::make_pair(table, ts));
        RPR_TRACE(RegTable, table->name());
    } else {
        TableState *ts = loc->second;
        ts->MutableGroupList()->push_back(group);
    }

    RequestWalk(table);
    walk_trigger_->Set();
}

void RoutePathReplicator::Leave(BgpTable *table, const RouteTarget &rt,
                                bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RtGroup *group = GetRtGroup(rt);
    assert(group);

    RPR_TRACE(TableLeave, table->name(), rt.ToString(), import);

    if (import) {
        group->RemoveImportTable(table);
        BOOST_FOREACH(BgpTable *bgptable, group->GetExportTables()) {
            RequestWalk(bgptable);
        }
    } else {
        group->RemoveExportTable(table);
        RequestWalk(table);
    }

    if (!import) {
        RtGroupTableState::iterator loc = table_state_.find(table);
        assert(loc != table_state_.end());
        TableState *ts = loc->second;
        ts->MutableGroupList()->remove(group);
        // The unregister from DBTable and delete of the TableState
        // after the TableWalk is completed (started by BgpBulkSync)
    }

    if ((group->GetImportTables().size() == 1) &&
        (group->GetExportTables().size() == 1)) {
        RoutingInstance *master =
            server()->routing_instance_mgr()->GetRoutingInstance(
                                         BgpConfigManager::kMasterInstance);
        if (master) {
            BgpTable *vpntable = master->GetTable(family());
            if ((vpntable == group->GetExportTables().front()) &&
                (vpntable == group->GetImportTables().front())) {
                // Delete the group if last vrf inet table left the RT
                RtGroupTableState::iterator loc = table_state_.find(vpntable);
                assert(loc != table_state_.end());
                TableState *ts = loc->second;
                ts->MutableGroupList()->remove(group);
                group->RemoveImportTable(vpntable);
                group->RemoveExportTable(vpntable);
                rt_group_map_.erase(rt);
            }
        }
    } else if (group->empty()) {
        // Delete the group if no tables are import/export this group
        rt_group_map_.erase(rt);
    }

    walk_trigger_->Set();
}

void 
RoutePathReplicator::DBStateSync(BgpTable *table, BgpRoute *rt, 
                                 DBTableBase::ListenerId id,
                                 RtReplicated *dbstate,
                                 RtReplicated::ReplicatedRtPathList &current) {
    RtReplicated::ReplicatedRtPathList::iterator cur_it = current.begin();
    RtReplicated::ReplicatedRtPathList::iterator dbstate_next_it, dbstate_it;
    dbstate_it = dbstate_next_it = dbstate->GetMutableList()->begin();
    while (cur_it != current.end() && 
           dbstate_it != dbstate->GetMutableList()->end()) {
        if (*cur_it < *dbstate_it) {
            // Add to DBstate
            dbstate->GetMutableList()->insert(*cur_it);
            cur_it++;
        } else if (*cur_it > *dbstate_it) {
            // Remove from DBstate
            dbstate_next_it++;
            DeleteSecondaryPath(table, rt, *dbstate_it);
            dbstate->GetMutableList()->erase(dbstate_it);
            dbstate_it = dbstate_next_it;
        } else {
            // Update
            cur_it++;
            dbstate_it++;
        }
        dbstate_next_it = dbstate_it;
    }
    for (; cur_it != current.end(); ++cur_it) {
        dbstate->GetMutableList()->insert(*cur_it);
    }
    for (dbstate_next_it = dbstate_it; 
         dbstate_it != dbstate->GetMutableList()->end(); 
         dbstate_it = dbstate_next_it) {
        dbstate_next_it++;
        DeleteSecondaryPath(table, rt, *dbstate_it);
        dbstate->GetMutableList()->erase(dbstate_it);
    }
    if (dbstate->GetList().empty()) {
        rt->ClearState(table, id);
        delete dbstate;
    }
}

//
// Update the ExtCommunity with the RouteTargets from the export list
// and the OriginVn. The OriginVn is derived from the RouteTargets in
// l3vpn routes.
//
static ExtCommunityPtr UpdateExtCommunity(BgpServer *server,
        const RoutingInstance *rtinstance, const ExtCommunity *ext_community,
        const ExtCommunity::ExtCommunityList &export_list) {

    // Add RouteTargets exported by the instance for a non-default instance.
    ExtCommunityPtr extcomm_ptr;
    if (!rtinstance->IsDefaultRoutingInstance()) {
        extcomm_ptr =
            server->extcomm_db()->AppendAndLocate(ext_community, export_list);
        return extcomm_ptr;
    }

    // Bail if we have a l3vpn/evpn route without extended communities.
    if (!ext_community)
        return ExtCommunityPtr(NULL);

    // Nothing to do if we already have the OriginVn community.
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (!ExtCommunity::is_origin_vn(comm))
            continue;
        return ExtCommunityPtr(ext_community);
    }


    // Add the OriginVn if we have a valid vn index.
    int vn_index =
        server->routing_instance_mgr()->GetVnIndexByExtCommunity(ext_community);
    if (vn_index) {
        OriginVn origin_vn(server->autonomous_system(), vn_index);
        extcomm_ptr = server->extcomm_db()->AppendAndLocate(
            ext_community, origin_vn.GetExtCommunity());
        return extcomm_ptr;
    }

    return ExtCommunityPtr(ext_community);
}

// concurrency: db-partition
// This function handles
//   1. Table Notification for route replication
//   2. Table walk for import/export of new RouteTargets
bool RoutePathReplicator::BgpTableListener(DBTablePartBase *root,
                                           DBEntryBase *entry) {
    BgpTable *table = static_cast<BgpTable *>(root->parent());
    BgpRoute *rt = static_cast<BgpRoute *>(entry);
    const RoutingInstance *rtinstance = table->routing_instance();

    // Get the Listener id
    RtGroupTableState::iterator loc = table_state_.find(table);
    assert(loc != table_state_.end());
    TableState *ts = loc->second;
    DBTableBase::ListenerId id = ts->GetListenerId();
    assert(id != DBTableBase::kInvalidId);

    // Get the dbstate
    RtReplicated *dbstate = 
        static_cast<RtReplicated *>(rt->GetState(table, id));

    RtReplicated::ReplicatedRtPathList replicated_path_list;

    // Cleanup if the route is marked for deletion, or there is no best path or
    // if the best path is infeasible
    if (entry->IsDeleted() || !rt->BestPath() ||
            !rt->BestPath()->IsFeasible()) {
        if (!dbstate) {
            return true;
        }
        DBStateSync(table, rt, id, dbstate, replicated_path_list);
        return true;
    }

    if (dbstate == NULL) {
        dbstate = new RtReplicated();
        rt->SetState(table, id, dbstate);
    }

    // Get the export route target list from the routing instance
    ExtCommunity::ExtCommunityList export_list;
    if (!rtinstance->IsDefaultRoutingInstance()) {
        BOOST_FOREACH(RouteTarget rtarget, rtinstance->GetExportList()) {
            export_list.push_back(rtarget.GetExtCommunity());
        }
    }

    // Replicate all feasible and non replicated paths.
    for (Route::PathList::iterator it = rt->GetPathList().begin(); 
        it != rt->GetPathList().end(); it++) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());
        // No need to replicate the replicated path
        if (path->IsReplicated()) continue;

        // Do not replicate non-ecmp paths
        if (rt->BestPath()->PathCompare(*path, true)) break;

        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();

        ExtCommunityPtr extcomm_ptr =
            UpdateExtCommunity(server(), rtinstance, ext_community, export_list);
        ext_community = extcomm_ptr.get();
        if (!ext_community)
            continue;

        RtGroup::RtGroupMemberList super_set;

        // For each RouteTarget extended community, get the list of tables to
        // replicate to.
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm, 
                      ext_community->communities()) {
            if (!ExtCommunity::is_route_target(comm))
                continue;
            RtGroup *rtgroup = GetRtGroup(comm);
            if (rtgroup) {
                super_set.insert(super_set.end(), 
                                 rtgroup->GetImportTables().begin(),
                                 rtgroup->GetImportTables().end());
            }
        }

        if (super_set.empty()) continue;

        // Duplicate tables to be removed
        super_set.sort();
        super_set.unique();

        // To all destination tables.. call replicate
        BOOST_FOREACH(BgpTable *dest, super_set) {
            // same as source table... skip
            if (dest == table) continue;

            BgpRoute *replicated = dest->RouteReplicate(server(), table,
                                        rt, path, extcomm_ptr);
            if (replicated) {
                RtReplicated::SecondaryRouteInfo rtinfo(dest, path->GetPeer(),
                            path->GetPathId(), path->GetSource(), replicated);
                replicated_path_list.insert(rtinfo);
                RPR_TRACE_ONLY(Replicate, table->name(), rt->ToString(),
                          path->ToString(),
                          BgpPath::PathIdString(path->GetPathId()),
                          dest->name(), replicated->ToString());
            }
        }
    }

    DBStateSync(table, rt, id, dbstate, replicated_path_list);
    return true;
}

const RtReplicated *RoutePathReplicator::GetReplicationState(
        BgpTable *table, BgpRoute *rt) const {
    RtGroupTableState::const_iterator loc = table_state_.find(table);
    if (loc == table_state_.end()) return NULL;

    const TableState *ts = loc->second;
    RtReplicated *dbstate = 
        static_cast<RtReplicated *>(rt->GetState(table, ts->GetListenerId()));
    return dbstate;
}

void RoutePathReplicator::DeleteSecondaryPath(BgpTable *table, BgpRoute *rt, 
                              const RtReplicated::SecondaryRouteInfo &rtinfo) {
    BgpRoute *rt_secondary = rtinfo.rt_;
    BgpTable *secondary_table = rtinfo.table_;
    const IPeer *peer = rtinfo.peer_;
    uint32_t path_id = rtinfo.path_id_;
    BgpPath::PathSource src = rtinfo.src_;

    assert(rt_secondary->RemoveSecondaryPath(rt, peer, path_id, src));
    DBTablePartBase *partition = 
        secondary_table->GetTablePartition(rt_secondary);
    if (rt_secondary->count() == 0) {
        RPR_TRACE_ONLY(Flush, secondary_table->name(), rt_secondary->ToString(),
                       peer ? peer->ToString() : "Nil",
                       BgpPath::PathIdString(path_id), table->name(),
                       rt->ToString(), "Delete");
        partition->Delete(rt_secondary);
    } else {
        partition->Notify(rt_secondary);
        RPR_TRACE_ONLY(Flush, secondary_table->name(), rt_secondary->ToString(),
                       peer ? peer->ToString() : "Nil",
                       BgpPath::PathIdString(path_id), table->name(),
                       rt->ToString(), "Path update");
    }
}

std::string RtReplicated::SecondaryRouteInfo::ToString() const {
    std::ostringstream out;
    out << table_->name() << "(" << table_ << ")" << ":" << 
        peer_->ToString() << "(" << peer_ << ")" << ":" << 
        rt_->ToString() << "(" << rt_ << ")";
    return out.str();
}
