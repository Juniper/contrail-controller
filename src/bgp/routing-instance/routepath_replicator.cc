/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routepath_replicator.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include <utility>

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
#include "bgp/routing-instance/rtarget_group_mgr.h"
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
    Rpr##obj##Log::Send("RoutingInstance",                                     \
            SandeshLevel::SYS_DEBUG, __FILE__, __LINE__, __VA_ARGS__);         \
    Rpr##obj::TraceMsg(trace_buf_, __FILE__, __LINE__, __VA_ARGS__);           \
} while (false)

#define RPR_TRACE_ONLY(obj, ...)                                               \
do {                                                                           \
    if (LoggingDisabled()) break;                                              \
    bgp_log_test::LogServerName(server());                                     \
    Rpr##obj::TraceMsg(trace_buf_, __FILE__, __LINE__, __VA_ARGS__);           \
} while (false)

TableState::TableState(RoutePathReplicator *replicator, BgpTable *table,
    DBTableBase::ListenerId id)
    : replicator_(replicator),
      table_(table),
      id_(id),
      table_delete_ref_(this, table->deleter()) {
    route_count_ = 0;
    assert(table->deleter() != NULL);
}

TableState::~TableState() {
}

void TableState::ManagedDelete() {
    if (!table_->IsVpnTable())
        return;
    deleted_ = true;
    replicator_->DeleteVpnTableState();
}

bool TableState::MayDelete() const {
    return (deleted_ && list_.empty() && route_count_ == 0);
}

void TableState::AddGroup(RtGroup *group) {
    list_.insert(group);
}

void TableState::RemoveGroup(RtGroup *group) {
    list_.erase(group);
}

const RtGroup *TableState::FindGroup(RtGroup *group) const {
    GroupList::const_iterator it = list_.find(group);
    return (it != list_.end() ? *it : NULL);
}

RoutePathReplicator::RoutePathReplicator(BgpServer *server,
    Address::Family family)
    : server_(server),
      family_(family),
      vpntable_(NULL),
      vpn_ts_(NULL),
      walk_trigger_(new TaskTrigger(
          boost::bind(&RoutePathReplicator::StartWalk, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
      unreg_trigger_(new TaskTrigger(
          boost::bind(&RoutePathReplicator::UnregisterTables, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
      trace_buf_(SandeshTraceBufferCreate("RoutePathReplicator", 500)) {
}

RoutePathReplicator::~RoutePathReplicator() {
    assert(!vpn_ts_);
    assert(table_state_.empty());
}

void RoutePathReplicator::Initialize() {
    assert(!vpntable_);
    assert(!vpn_ts_);

    RoutingInstanceMgr *mgr = server_->routing_instance_mgr();
    assert(mgr);
    RoutingInstance *master =
        mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);
    vpntable_ = master->GetTable(family_);
    assert(vpntable_);
    vpn_ts_ = AddTableState(vpntable_);
    assert(vpn_ts_);
}

void RoutePathReplicator::DeleteVpnTableState() {
    if (!vpn_ts_ || !vpn_ts_->MayDelete())
        return;
    unreg_table_list_.insert(vpntable_);
    unreg_trigger_->Set();
}

TableState *RoutePathReplicator::AddTableState(BgpTable *table,
    RtGroup *group) {
    assert(table->IsVpnTable() || group);

    RouteReplicatorTableState::iterator loc = table_state_.find(table);
    if (loc == table_state_.end()) {
        DBTableBase::ListenerId id = table->Register(
            boost::bind(&RoutePathReplicator::BgpTableListener, this, _1, _2));
        TableState *ts = new TableState(this, table, id);
        if (group)
            ts->AddGroup(group);
        table_state_.insert(std::make_pair(table, ts));
        RPR_TRACE(RegTable, table->name());
        return ts;
    } else {
        assert(group);
        TableState *ts = loc->second;
        ts->AddGroup(group);
        return ts;
    }
}

void RoutePathReplicator::RemoveTableState(BgpTable *table, RtGroup *group) {
    TableState *ts = FindTableState(table);
    assert(ts);
    ts->RemoveGroup(group);
}

void RoutePathReplicator::DeleteTableState(BgpTable *table) {
    TableState *ts = FindTableState(table);
    assert(ts);
    if (!ts->empty())
        return;

    RPR_TRACE(UnregTable, table->name());
    table->Unregister(ts->GetListenerId());
    table_state_.erase(table);
    if (ts == vpn_ts_)
        vpn_ts_ = NULL;
    delete ts;
}

TableState *RoutePathReplicator::FindTableState(BgpTable *table) {
    RouteReplicatorTableState::iterator loc = table_state_.find(table);
    return (loc != table_state_.end() ? loc->second : NULL);
}

const TableState *RoutePathReplicator::FindTableState(BgpTable *table) const {
    RouteReplicatorTableState::const_iterator loc = table_state_.find(table);
    return (loc != table_state_.end() ? loc->second : NULL);
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
    for (BulkSyncOrders::iterator it = bulk_sync_.begin();
         it != bulk_sync_.end(); ++it) {
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
         it != unreg_table_list_.end(); ++it) {
        DeleteTableState(*it);
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
    assert(loc != bulk_sync_.end());
    BulkSyncState *bulk_sync_state = loc->second;
    if (bulk_sync_state->WalkAgain()) {
        bulk_sync_state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        walk_trigger_->Set();
        return;
    }
    delete bulk_sync_state;
    bulk_sync_.erase(loc);
    const TableState *ts = FindTableState(bgptable);
    if (ts->empty())
        unreg_table_list_.insert(bgptable);
    unreg_trigger_->Set();
}

void RoutePathReplicator::JoinVpnTable(RtGroup *group) {
    CHECK_CONCURRENCY("bgp::Config");
    if (!vpn_ts_ || vpn_ts_->FindGroup(group))
        return;
    RPR_TRACE(TableJoin, vpntable_->name(), group->rt().ToString(), true);
    group->AddImportTable(family(), vpntable_);
    RPR_TRACE(TableJoin, vpntable_->name(), group->rt().ToString(), false);
    group->AddExportTable(family(), vpntable_);
    AddTableState(vpntable_, group);
}

void RoutePathReplicator::LeaveVpnTable(RtGroup *group) {
    CHECK_CONCURRENCY("bgp::Config");
    if (!vpn_ts_)
        return;
    RPR_TRACE(TableLeave, vpntable_->name(), group->rt().ToString(), true);
    group->RemoveImportTable(family(), vpntable_);
    RPR_TRACE(TableLeave, vpntable_->name(), group->rt().ToString(), false);
    group->RemoveExportTable(family(), vpntable_);
    RemoveTableState(vpntable_, group);
    DeleteVpnTableState();
}

void RoutePathReplicator::Join(BgpTable *table, const RouteTarget &rt,
                               bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RPR_TRACE(TableJoin, table->name(), rt.ToString(), import);

    bool first = false;
    RtGroup *group = server()->rtarget_group_mgr()->LocateRtGroup(rt);
    if (import) {
        first = group->AddImportTable(family(), table);
        server()->rtarget_group_mgr()->NotifyRtGroup(rt);
        BOOST_FOREACH(BgpTable *bgptable, group->GetExportTables(family())) {
            if (bgptable->IsVpnTable())
                continue;
            RequestWalk(bgptable);
        }
        walk_trigger_->Set();
    } else {
        first = group->AddExportTable(family(), table);
        AddTableState(table, group);
        RequestWalk(table);
        walk_trigger_->Set();
    }

    // Join the vpn table when group is created.
    if (first)
        JoinVpnTable(group);
}

void RoutePathReplicator::Leave(BgpTable *table, const RouteTarget &rt,
                                bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RtGroup *group = server()->rtarget_group_mgr()->GetRtGroup(rt);
    assert(group);
    RPR_TRACE(TableLeave, table->name(), rt.ToString(), import);

    if (import) {
        group->RemoveImportTable(family(), table);
        server()->rtarget_group_mgr()->NotifyRtGroup(rt);
        BOOST_FOREACH(BgpTable *bgptable, group->GetExportTables(family())) {
            if (bgptable->IsVpnTable())
                continue;
            RequestWalk(bgptable);
        }
        walk_trigger_->Set();
    } else {
        group->RemoveExportTable(family(), table);
        RemoveTableState(table, group);
        RequestWalk(table);
        walk_trigger_->Set();
    }

    // Leave the vpn table when the last VRF has left the group.
    if (!group->HasVrfTables(family())) {
        LeaveVpnTable(group);
        server()->rtarget_group_mgr()->RemoveRtGroup(rt);
    }
}

void
RoutePathReplicator::DBStateSync(BgpTable *table,
                                 const TableState *ts,
                                 BgpRoute *rt,
                                 RtReplicated *dbstate,
                                 RtReplicated::ReplicatedRtPathList &current) {
    RtReplicated::ReplicatedRtPathList::iterator cur_it = current.begin();
    RtReplicated::ReplicatedRtPathList::iterator dbstate_next_it, dbstate_it;
    dbstate_it = dbstate_next_it = dbstate->GetMutableList()->begin();
    std::pair<RtReplicated::ReplicatedRtPathList::iterator, bool> r;

    while (cur_it != current.end() &&
           dbstate_it != dbstate->GetMutableList()->end()) {
        if (*cur_it < *dbstate_it) {
            // Add to DBstate
            r = dbstate->GetMutableList()->insert(*cur_it);
            assert(r.second);
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
        r = dbstate->GetMutableList()->insert(*cur_it);
        assert(r.second);
    }
    for (dbstate_next_it = dbstate_it;
         dbstate_it != dbstate->GetMutableList()->end();
         dbstate_it = dbstate_next_it) {
        dbstate_next_it++;
        DeleteSecondaryPath(table, rt, *dbstate_it);
        dbstate->GetMutableList()->erase(dbstate_it);
    }
    if (dbstate->GetList().empty()) {
        rt->ClearState(table, ts->GetListenerId());
        delete dbstate;
        uint32_t prev_route_count = ts->decrement_route_count();
        if (prev_route_count == 1 && ts == vpn_ts_)
            DeleteVpnTableState();
    }
}

//
// Update the ExtCommunity with the RouteTargets from the export list
// and the OriginVn. The OriginVn is derived from the RouteTargets in
// vpn routes.
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

    // Bail if we have a vpn route without extended communities.
    if (!ext_community)
        return ExtCommunityPtr(NULL);

    // Nothing to do if we already have the OriginVn community with our AS.
    BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                  ext_community->communities()) {
        if (!ExtCommunity::is_origin_vn(comm))
            continue;
        OriginVn origin_vn(comm);
        if (origin_vn.as_number() != server->autonomous_system())
            continue;
        return ExtCommunityPtr(ext_community);
    }

    // Add the OriginVn if we have a valid vn index.
    int vn_index =
        server->routing_instance_mgr()->GetVnIndexByExtCommunity(ext_community);
    if (vn_index) {
        OriginVn origin_vn(server->autonomous_system(), vn_index);
        extcomm_ptr = server->extcomm_db()->ReplaceOriginVnAndLocate(
            ext_community, origin_vn.GetExtCommunity());
        return extcomm_ptr;
    } else {
        extcomm_ptr = server->extcomm_db()->RemoveOriginVnAndLocate(
            ext_community);
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
    const TableState *ts = FindTableState(table);
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
        DBStateSync(table, ts, rt, dbstate, replicated_path_list);
        return true;
    }

    if (dbstate == NULL) {
        dbstate = new RtReplicated();
        rt->SetState(table, id, dbstate);
        ts->increment_route_count();
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

        // Skip if the source peer is down
        if (!path->IsStale() && path->GetPeer() && !path->GetPeer()->IsReady())
            continue;

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

        // Go through all extended communities.
        //
        // Get the vn_index from the OriginVn extended community.
        // For each RouteTarget extended community, get the list of tables
        // to which we need to replicate the path.
        int vn_index = 0;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (ExtCommunity::is_origin_vn(comm)) {
                OriginVn origin_vn(comm);
                vn_index = origin_vn.vn_index();
            } else if (ExtCommunity::is_route_target(comm)) {
                RtGroup *rtgroup =
                    server()->rtarget_group_mgr()->GetRtGroup(comm);
                if (!rtgroup) continue;
                RtGroup::RtGroupMemberList import_list =
                    rtgroup->GetImportTables(family());
                if (import_list.empty()) continue;
                super_set.insert(import_list.begin(), import_list.end());
            }
        }

        if (super_set.empty()) continue;

        // Add OriginVn when replicating self-originated routes from a VRF.
        if (!rtinstance->IsDefaultRoutingInstance() &&
            path->IsVrfOriginated() && rtinstance->virtual_network_index()) {
            vn_index = rtinstance->virtual_network_index();
            OriginVn origin_vn(server_->autonomous_system(), vn_index);
            extcomm_ptr = server_->extcomm_db()->ReplaceOriginVnAndLocate(
                extcomm_ptr.get(), origin_vn.GetExtCommunity());
        }

        // To all destination tables.. call replicate
        BOOST_FOREACH(BgpTable *dest, super_set) {
            // same as source table... skip
            if (dest == table) continue;

            const RoutingInstance *dest_rtinstance = dest->routing_instance();
            ExtCommunityPtr new_extcomm_ptr = extcomm_ptr;

            // If the origin vn is unresolved, see if route has a RouteTarget
            // that's in the set of export RouteTargets for the dest instance.
            // If so, we set the origin vn for the replicated route to be the
            // vn for the dest instance.
            if (!vn_index &&
                dest_rtinstance->HasExportTarget(ext_community)) {
                int dest_vn_index = dest_rtinstance->virtual_network_index();
                OriginVn origin_vn(server_->autonomous_system(), dest_vn_index);
                new_extcomm_ptr =
                        server_->extcomm_db()->ReplaceOriginVnAndLocate(
                                extcomm_ptr.get(), origin_vn.GetExtCommunity());
            }

            BgpRoute *replicated = dest->RouteReplicate(
                    server_, table, rt, path, new_extcomm_ptr);
            if (replicated) {
                RtReplicated::SecondaryRouteInfo rtinfo(dest, path->GetPeer(),
                            path->GetPathId(), path->GetSource(), replicated);
                std::pair<RtReplicated::ReplicatedRtPathList::iterator, bool> r;
                r = replicated_path_list.insert(rtinfo);
                assert(r.second);
                RPR_TRACE_ONLY(Replicate, table->name(), rt->ToString(),
                          path->ToString(),
                          BgpPath::PathIdString(path->GetPathId()),
                          dest->name(), replicated->ToString());
            }
        }
    }

    DBStateSync(table, ts, rt, dbstate, replicated_path_list);
    return true;
}

const RtReplicated *RoutePathReplicator::GetReplicationState(
        BgpTable *table, BgpRoute *rt) const {
    const TableState *ts = FindTableState(table);
    if (!ts)
        return NULL;
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

    assert(rt_secondary->RemoveSecondaryPath(rt, src, peer, path_id));
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
