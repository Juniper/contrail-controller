/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routepath_replicator.h"

#include <boost/foreach.hpp>

#include <utility>

#include "base/set_util.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/routing-instance/routing_instance_analytics_types.h"

using std::ostringstream;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

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

class TableState::DeleteActor : public LifetimeActor {
public:
    DeleteActor(TableState *ts)
        : LifetimeActor(ts->replicator()->server()->lifetime_manager()),
          ts_(ts) {
    }

    virtual bool MayDelete() const {
        return ts_->MayDelete();
    }

    virtual void Shutdown() {
    }

    virtual void Destroy() {
        ts_->replicator()->DeleteTableState(ts_->table());
    }

private:
    TableState *ts_;
};

TableState::TableState(RoutePathReplicator *replicator, BgpTable *table)
    : replicator_(replicator),
      table_(table),
      listener_id_(DBTableBase::kInvalidId),
      deleter_(new DeleteActor(this)),
      table_delete_ref_(this, table->deleter()) {
    assert(table->deleter() != NULL);
}

TableState::~TableState() {
}

void TableState::ManagedDelete() {
    deleter()->Delete();
}

bool TableState::deleted() const {
    return deleter()->IsDeleted();
}

LifetimeActor *TableState::deleter() {
    return deleter_.get();
}

const LifetimeActor *TableState::deleter() const {
    return deleter_.get();
}

bool TableState::MayDelete() const {
    if (list_.empty() && !route_count() &&
        !replicator()->BulkSyncExists(table()))
        return true;
    return false;
}

void TableState::RetryDelete() {
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
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

uint32_t TableState::route_count() const {
    return table_->GetDBStateCount(listener_id());
}

RtReplicated::RtReplicated(RoutePathReplicator *replicator)
    : replicator_(replicator) {
}

void RtReplicated::AddRouteInfo(BgpTable *table, BgpRoute *rt,
    ReplicatedRtPathList::const_iterator it) {
    pair<ReplicatedRtPathList::iterator, bool> result;
    result = replicate_list_.insert(*it);
    assert(result.second);
}

void RtReplicated::DeleteRouteInfo(BgpTable *table, BgpRoute *rt,
    ReplicatedRtPathList::const_iterator it) {
    replicator_->DeleteSecondaryPath(table, rt, *it);
    replicate_list_.erase(it);
}

//
// Return the list of secondary table names for the given primary path.
// We go through all SecondaryRouteInfos and skip the ones that don't
// match the primary path.
//
vector<string> RtReplicated::GetTableNameList(const BgpPath *path) const {
    vector<string> table_list;
    BOOST_FOREACH(const SecondaryRouteInfo &rinfo, replicate_list_) {
        if (rinfo.peer_ != path->GetPeer())
            continue;
        if (rinfo.path_id_ != path->GetPathId())
            continue;
        if (rinfo.src_ != path->GetSource())
            continue;
        table_list.push_back(rinfo.table_->name());
    }
    return table_list;
}

RoutePathReplicator::RoutePathReplicator(BgpServer *server,
    Address::Family family)
    : server_(server),
      family_(family),
      vpn_table_(NULL),
      walk_trigger_(new TaskTrigger(
          boost::bind(&RoutePathReplicator::StartWalk, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
      trace_buf_(SandeshTraceBufferCreate("RoutePathReplicator", 500)) {
}

RoutePathReplicator::~RoutePathReplicator() {
    assert(table_state_list_.empty());
}

void RoutePathReplicator::Initialize() {
    assert(!vpn_table_);

    RoutingInstanceMgr *mgr = server_->routing_instance_mgr();
    assert(mgr);
    RoutingInstance *master =
        mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
    assert(master);
    vpn_table_ = master->GetTable(family_);
    assert(vpn_table_);
    assert(AddTableState(vpn_table_));
}

TableState *RoutePathReplicator::AddTableState(BgpTable *table,
    RtGroup *group) {
    assert(table->IsVpnTable() || group);

    TableStateList::iterator loc = table_state_list_.find(table);
    if (loc == table_state_list_.end()) {
        TableState *ts = new TableState(this, table);
        DBTableBase::ListenerId id = table->Register(
            boost::bind(&RoutePathReplicator::RouteListener, this, ts, _1, _2),
            "RoutePathReplicator");
        ts->set_listener_id(id);
        if (group)
            ts->AddGroup(group);
        table_state_list_.insert(make_pair(table, ts));
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
    RPR_TRACE(UnregTable, table->name());
    table->Unregister(ts->listener_id());
    table_state_list_.erase(table);
    delete ts;
}

TableState *RoutePathReplicator::FindTableState(BgpTable *table) {
    TableStateList::iterator loc = table_state_list_.find(table);
    return (loc != table_state_list_.end() ? loc->second : NULL);
}

const TableState *RoutePathReplicator::FindTableState(
    const BgpTable *table) const {
    TableStateList::const_iterator loc =
        table_state_list_.find(const_cast<BgpTable *>(table));
    return (loc != table_state_list_.end() ? loc->second : NULL);
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
        bulk_sync_.insert(make_pair(table, state));
    }
}

bool
RoutePathReplicator::StartWalk() {
    CHECK_CONCURRENCY("bgp::Config");

    // For each member table, start a walker to replicate
    for (BulkSyncOrders::iterator it = bulk_sync_.begin();
         it != bulk_sync_.end(); ++it) {
        if (it->second->GetWalkerId() != DBTableWalker::kInvalidWalkerId) {
            // Walk is in progress.
            continue;
        }
        BgpTable *table = it->first;
        RPR_TRACE(Walk, table->name());
        TableState *ts = FindTableState(table);
        assert(ts);
        DB *db = server()->database();
        DBTableWalker::WalkId id = db->GetWalker()->WalkTable(table, NULL,
            boost::bind(&RoutePathReplicator::RouteListener, this, ts, _1, _2),
            boost::bind(&RoutePathReplicator::BulkReplicationDone, this, _1));
        it->second->SetWalkerId(id);
        it->second->SetWalkAgain(false);
    }
    return true;
}

void
RoutePathReplicator::BulkReplicationDone(DBTableBase *dbtable) {
    CHECK_CONCURRENCY("db::DBTable");
    tbb::mutex::scoped_lock lock(mutex_);
    BgpTable *table = static_cast<BgpTable *>(dbtable);
    RPR_TRACE(WalkDone, table->name());
    BulkSyncOrders::iterator loc = bulk_sync_.find(table);
    assert(loc != bulk_sync_.end());
    BulkSyncState *bulk_sync_state = loc->second;
    if (bulk_sync_state->WalkAgain()) {
        bulk_sync_state->SetWalkerId(DBTableWalker::kInvalidWalkerId);
        walk_trigger_->Set();
        return;
    }
    delete bulk_sync_state;
    bulk_sync_.erase(loc);
    TableState *ts = FindTableState(table);
    ts->RetryDelete();
}

void RoutePathReplicator::JoinVpnTable(RtGroup *group) {
    CHECK_CONCURRENCY("bgp::Config");
    TableState *vpn_ts = FindTableState(vpn_table_);
    if (!vpn_ts || vpn_ts->FindGroup(group))
        return;
    RPR_TRACE(TableJoin, vpn_table_->name(), group->rt().ToString(), true);
    group->AddImportTable(family(), vpn_table_);
    RPR_TRACE(TableJoin, vpn_table_->name(), group->rt().ToString(), false);
    group->AddExportTable(family(), vpn_table_);
    AddTableState(vpn_table_, group);
}

void RoutePathReplicator::LeaveVpnTable(RtGroup *group) {
    CHECK_CONCURRENCY("bgp::Config");
    TableState *vpn_ts = FindTableState(vpn_table_);
    if (!vpn_ts)
        return;
    RPR_TRACE(TableLeave, vpn_table_->name(), group->rt().ToString(), true);
    group->RemoveImportTable(family(), vpn_table_);
    RPR_TRACE(TableLeave, vpn_table_->name(), group->rt().ToString(), false);
    group->RemoveExportTable(family(), vpn_table_);
    RemoveTableState(vpn_table_, group);
}

//
// Add a given BgpTable to RtGroup of given RouteTarget.
// It will create a new RtGroup if none exists.
// In case of export RouteTarget, create TableState if it doesn't exist.
//
void RoutePathReplicator::Join(BgpTable *table, const RouteTarget &rt,
                               bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RPR_TRACE(TableJoin, table->name(), rt.ToString(), import);

    bool first = false;
    RtGroup *group = server()->rtarget_group_mgr()->LocateRtGroup(rt);
    if (import) {
        first = group->AddImportTable(family(), table);
        server()->rtarget_group_mgr()->NotifyRtGroup(rt);
        if (family_ == Address::INETVPN)
            server_->NotifyAllStaticRoutes();
        BOOST_FOREACH(BgpTable *sec_table, group->GetExportTables(family())) {
            if (sec_table->IsVpnTable() || sec_table->empty())
                continue;
            RequestWalk(sec_table);
        }
        walk_trigger_->Set();
    } else {
        first = group->AddExportTable(family(), table);
        AddTableState(table, group);
        if (!table->empty()) {
            RequestWalk(table);
            walk_trigger_->Set();
        }
    }

    // Join the vpn table when group is created.
    if (first)
        JoinVpnTable(group);
}

//
// Remove a BgpTable from RtGroup of given RouteTarget.
// If the last group is going away, the RtGroup will be removed
// In case of export RouteTarget, trigger remove of TableState appropriate.
//
void RoutePathReplicator::Leave(BgpTable *table, const RouteTarget &rt,
                                bool import) {
    CHECK_CONCURRENCY("bgp::Config");

    RtGroup *group = server()->rtarget_group_mgr()->GetRtGroup(rt);
    assert(group);
    RPR_TRACE(TableLeave, table->name(), rt.ToString(), import);

    if (import) {
        group->RemoveImportTable(family(), table);
        server()->rtarget_group_mgr()->NotifyRtGroup(rt);
        if (family_ == Address::INETVPN)
            server_->NotifyAllStaticRoutes();
        BOOST_FOREACH(BgpTable *sec_table, group->GetExportTables(family())) {
            if (sec_table->IsVpnTable() || sec_table->empty())
                continue;
            RequestWalk(sec_table);
        }
        walk_trigger_->Set();
    } else {
        group->RemoveExportTable(family(), table);
        RemoveTableState(table, group);
        if (!table->empty()) {
            RequestWalk(table);
            walk_trigger_->Set();
        }
    }

    // Leave the vpn table when the last VRF has left the group.
    if (!group->HasVrfTables(family())) {
        LeaveVpnTable(group);
        server()->rtarget_group_mgr()->RemoveRtGroup(rt);
    }
}

void RoutePathReplicator::DBStateSync(BgpTable *table, TableState *ts,
    BgpRoute *rt, RtReplicated *dbstate,
    const RtReplicated::ReplicatedRtPathList *future) {
    set_synchronize(dbstate->GetMutableList(), future,
        boost::bind(&RtReplicated::AddRouteInfo, dbstate, table, rt, _1),
        boost::bind(&RtReplicated::DeleteRouteInfo, dbstate, table, rt, _1));

    if (dbstate->GetList().empty()) {
        rt->ClearState(table, ts->listener_id());
        delete dbstate;
        if (table->GetDBStateCount(ts->listener_id()) == 0)
            ts->RetryDelete();
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
    // Add RouteTargets exported by the instance for a non-master instance.
    ExtCommunityPtr extcomm_ptr;
    if (!rtinstance->IsMasterRoutingInstance()) {
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

//
// Concurrency: Called in the context of the DB partition task.
//
// This function handles
//   1. Table Notification for path replication
//   2. Table walk for import/export of new targets
//
// Replicate a path (clone the BgpPath) to secondary BgpTables based on the
// export targets of the primary BgpTable.
// If primary table is a VRF table attach it's export targets to replicated
// path in the VPN table.
//
bool RoutePathReplicator::RouteListener(TableState *ts,
    DBTablePartBase *root, DBEntryBase *entry) {
    CHECK_CONCURRENCY("db::DBTable");

    BgpTable *table = static_cast<BgpTable *>(root->parent());
    BgpRoute *rt = static_cast<BgpRoute *>(entry);
    const RoutingInstance *rtinstance = table->routing_instance();

    DBTableBase::ListenerId id = ts->listener_id();
    assert(id != DBTableBase::kInvalidId);

    // Get the DBState.
    RtReplicated *dbstate =
        static_cast<RtReplicated *>(rt->GetState(table, id));
    RtReplicated::ReplicatedRtPathList replicated_path_list;

    //
    // Cleanup if the route is not usable.
    // If route aggregation is enabled, contributing route/more specific route
    // for a aggregate route will NOT be replicated to destination table
    //
    if (!rt->IsUsable() || (table->IsRouteAggregationSupported() &&
                            !rtinstance->deleted() &&
                            table->IsContributingRoute(rt))) {
        if (!dbstate) {
            return true;
        }
        DBStateSync(table, ts, rt, dbstate, &replicated_path_list);
        return true;
    }

    // Create and set new DBState on the route.  This will get cleaned up via
    // via the call to DBStateSync if we don't need to replicate the route to
    // any tables.
    if (dbstate == NULL) {
        dbstate = new RtReplicated(this);
        rt->SetState(table, id, dbstate);
    }

    // Get the export route target list from the routing instance.
    ExtCommunity::ExtCommunityList export_list;
    if (!rtinstance->IsMasterRoutingInstance()) {
        BOOST_FOREACH(RouteTarget rtarget, rtinstance->GetExportList()) {
            export_list.push_back(rtarget.GetExtCommunity());
        }
    }

    // Replicate all feasible and non-replicated paths.
    for (Route::PathList::iterator it = rt->GetPathList().begin();
        it != rt->GetPathList().end(); ++it) {
        BgpPath *path = static_cast<BgpPath *>(it.operator->());

        // Skip if the source peer is down.
        if (!path->IsStale() && path->GetPeer() && !path->GetPeer()->IsReady())
            continue;

        // No need to replicate the replicated path.
        if (path->IsReplicated())
            continue;

        // Do not replicate non-ecmp paths.
        if (rt->BestPath()->PathCompare(*path, true))
            break;

        const BgpAttr *attr = path->GetAttr();
        const ExtCommunity *ext_community = attr->ext_community();

        ExtCommunityPtr extcomm_ptr =
          UpdateExtCommunity(server(), rtinstance, ext_community, export_list);
        ext_community = extcomm_ptr.get();
        if (!ext_community)
            continue;

        // Go through all extended communities.
        //
        // Get the vn_index from the OriginVn extended community.
        // For each RouteTarget extended community, get the list of tables
        // to which we need to replicate the path.
        int vn_index = 0;
        RtGroup::RtGroupMemberList secondary_tables;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (ExtCommunity::is_origin_vn(comm)) {
                OriginVn origin_vn(comm);
                vn_index = origin_vn.vn_index();
            } else if (ExtCommunity::is_route_target(comm)) {
                RtGroup *group =
                    server()->rtarget_group_mgr()->GetRtGroup(comm);
                if (!group)
                    continue;
                RtGroup::RtGroupMemberList import_list =
                    group->GetImportTables(family());
                if (import_list.empty())
                    continue;
                secondary_tables.insert(import_list.begin(), import_list.end());
            }
        }

        // Skip if we don't need to replicate the path to any tables.
        if (secondary_tables.empty())
            continue;

        // Add OriginVn when replicating self-originated routes from a VRF.
        if (!vn_index && !rtinstance->IsMasterRoutingInstance() &&
            path->IsVrfOriginated() && rtinstance->virtual_network_index()) {
            vn_index = rtinstance->virtual_network_index();
            OriginVn origin_vn(server_->autonomous_system(), vn_index);
            extcomm_ptr = server_->extcomm_db()->ReplaceOriginVnAndLocate(
                extcomm_ptr.get(), origin_vn.GetExtCommunity());
        }

        // Replicate path to all destination tables.
        BOOST_FOREACH(BgpTable *dest, secondary_tables) {
            // Skip if destination is same as source table.
            if (dest == table)
                continue;

            const RoutingInstance *dest_rtinstance = dest->routing_instance();
            ExtCommunityPtr new_extcomm_ptr = extcomm_ptr;

            // If the origin vn is unresolved, see if route has a RouteTarget
            // that's in the set of export RouteTargets for the dest instance.
            // If so, we set the origin vn for the replicated route to be the
            // vn for the dest instance.
            if (!vn_index && dest_rtinstance->virtual_network_index() &&
                dest_rtinstance->HasExportTarget(ext_community)) {
                int dest_vn_index = dest_rtinstance->virtual_network_index();
                OriginVn origin_vn(server_->autonomous_system(), dest_vn_index);
                new_extcomm_ptr =
                        server_->extcomm_db()->ReplaceOriginVnAndLocate(
                                extcomm_ptr.get(), origin_vn.GetExtCommunity());
            }

            // Replicate the route to the destination table.  The destination
            // table may decide to not replicate based on it's own policy e.g.
            // multicast routes are never leaked across routing-instances.
            BgpRoute *replicated_rt = dest->RouteReplicate(
                server_, table, rt, path, new_extcomm_ptr);
            if (!replicated_rt)
                continue;

            // Add information about the secondary path to the replicated path
            // list.
            RtReplicated::SecondaryRouteInfo rtinfo(dest, path->GetPeer(),
                path->GetPathId(), path->GetSource(), replicated_rt);
            pair<RtReplicated::ReplicatedRtPathList::iterator, bool> result;
            result = replicated_path_list.insert(rtinfo);
            assert(result.second);
            RPR_TRACE_ONLY(Replicate, table->name(), rt->ToString(),
                           path->ToString(),
                           BgpPath::PathIdString(path->GetPathId()),
                           dest->name(), replicated_rt->ToString());
        }
    }

    // Update the DBState to reflect the new list of secondary paths. The
    // DBState will get cleared if the list is empty.
    DBStateSync(table, ts, rt, dbstate, &replicated_path_list);
    return true;
}

const RtReplicated *RoutePathReplicator::GetReplicationState(
        BgpTable *table, BgpRoute *rt) const {
    const TableState *ts = FindTableState(table);
    if (!ts)
        return NULL;
    RtReplicated *dbstate =
        static_cast<RtReplicated *>(rt->GetState(table, ts->listener_id()));
    return dbstate;
}

//
// Return the list of secondary table names for the given primary path.
//
vector<string> RoutePathReplicator::GetReplicatedTableNameList(
    const BgpTable *table, const BgpRoute *rt, const BgpPath *path) const {
    const TableState *ts = FindTableState(table);
    if (!ts)
        return vector<string>();
    const RtReplicated *dbstate = static_cast<const RtReplicated *>(
        rt->GetState(table, ts->listener_id()));
    if (!dbstate)
        return vector<string>();
    return dbstate->GetTableNameList(path);
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

string RtReplicated::SecondaryRouteInfo::ToString() const {
    ostringstream out;
    out << table_->name() << "(" << table_ << ")" << ":" <<
        peer_->ToString() << "(" << peer_ << ")" << ":" <<
        rt_->ToString() << "(" << rt_ << ")";
    return out.str();
}
