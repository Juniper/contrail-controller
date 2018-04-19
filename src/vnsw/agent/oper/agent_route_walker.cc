/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <assert.h>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>
#include <cmn/agent_db.h>
#include <oper/agent_route_walker.h>
#include <oper/vrf.h>
#include <oper/agent_route.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>

using namespace std;

SandeshTraceBufferPtr AgentDBwalkTraceBuf(SandeshTraceBufferCreate(
    AGENT_DBWALK_TRACE_BUF, 1000));

RouteWalkerDBState::RouteWalkerDBState() : walker_ref_map_() {
}

static void
RemoveWalkReferencesInRoutetable(RouteWalkerDBState::AgentRouteWalkerRefMapIter &it,
                                 RouteWalkerDBState *state) {
    for (uint8_t table_type = Agent::ROUTE_TABLE_START;
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        it->second[table_type] = DBTable::DBTableWalkRef();
    }
}

AgentRouteWalkerManager::AgentRouteWalkerManager(Agent *agent) : agent_(agent),
    walk_ref_list_(), marked_for_deletion_(false) {
    vrf_listener_id_ = agent->vrf_table()->Register(
                       boost::bind(&AgentRouteWalkerManager::VrfNotify, this,
                                   _1, _2));
}

AgentRouteWalkerManager::~AgentRouteWalkerManager() {
    //Since walker ref list is going off, mark all walkers to have NULL
    //managers.
    //TODO
}

void AgentRouteWalkerManager::ReleaseWalker(AgentRouteWalker *walker) {
    assert(std::find(walk_ref_list_.begin(), walk_ref_list_.end(), walker)
           != walk_ref_list_.end());
    walker->ReleaseVrfWalkReference();
}

void
AgentRouteWalkerManager::RemoveWalkReferencesInVrf(RouteWalkerDBState *state,
                                                   VrfEntry *vrf) {
    for (RouteWalkerDBState::AgentRouteWalkerRefMapIter it =
         state->walker_ref_map_.begin();
         it != state->walker_ref_map_.end(); it++) {
        RemoveWalkReferencesInRoutetable(it, state);
    }
}

//Handles VRF addition and deletion.
//Releases all vrf walker reference and route table walker references.
void AgentRouteWalkerManager::VrfNotify(DBTablePartBase *partition,
                                        DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(partition->parent(),
                                                        vrf_listener_id_));
    if (vrf->IsDeleted()) {
        if (!state)
            return;
        RemoveWalkReferencesInVrf(state, vrf);
        vrf->ClearState(partition->parent(), vrf_listener_id_);
        delete state;
        return;
    }

    if (!state) {
        CreateState(vrf);
    }
}

RouteWalkerDBState *AgentRouteWalkerManager::CreateState(VrfEntry *vrf) {
    RouteWalkerDBState *state = new RouteWalkerDBState();
    vrf->SetState(vrf->get_table(), vrf_listener_id_, state);
    return state;
}

void AgentRouteWalkerManager::RemoveWalker(AgentRouteWalkerPtr walker) {
    walk_ref_list_.erase(std::find(walk_ref_list_.begin(), walk_ref_list_.end(),
                                   walker));
}

void AgentRouteWalkerManager::Shutdown() {
    marked_for_deletion_ = true;
    TryUnregister();
}

void AgentRouteWalkerManager::TryUnregister() {
    if (walk_ref_list_.size() != 0)
        return;
    agent_->vrf_table()->Unregister(vrf_listener_id_);
}

void AgentRouteWalkerManager::ValidateAgentRouteWalker
(AgentRouteWalkerPtr walker) const {
   assert (std::find(walk_ref_list_.begin(), walk_ref_list_.end(), walker) !=
           walk_ref_list_.end());
}

void AgentRouteWalkerManager::RegisterWalker(AgentRouteWalker *walker) {
    if (marked_for_deletion_)
        return;
    walk_ref_list_.insert(walker);
    walker->set_mgr(this);
}

AgentRouteWalker::AgentRouteWalker(const std::string &name,
                                   Agent *agent) : agent_(agent), name_(name),
    route_walk_count_(), walk_done_cb_(), route_walk_done_for_vrf_cb_(),
    mgr_(NULL) {
    walk_count_ = AgentRouteWalker::kInvalidWalkCount;
    vrf_walk_ref_ = agent_->vrf_table()->AllocWalker(
                            boost::bind(&AgentRouteWalker::VrfWalkNotify,
                                        this, _1, _2),
                            boost::bind(&AgentRouteWalker::VrfWalkDoneInternal,
                                        this, _2));
    delete_walk_ref_ = agent_->vrf_table()->AllocWalker(
                       boost::bind(&AgentRouteWalker::Deregister, this,
                                   _1, _2),
                       boost::bind(&AgentRouteWalker::DeregisterDone,
                                    this));
    refcount_ = 0;
}

AgentRouteWalker::~AgentRouteWalker() {
    assert(vrf_walk_ref_.get() == NULL);
    assert(delete_walk_ref_.get() == NULL);
}

RouteWalkerDBState *
AgentRouteWalker::LocateRouteWalkerDBState(VrfEntry *vrf) {
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        mgr_->vrf_listener_id()));
    if (vrf->IsDeleted())
        return state;

    if (!state)
        state = mgr_->CreateState(vrf);

    return state;
}

DBTable::DBTableWalkRef
AgentRouteWalker::LocateRouteTableWalkRef(const VrfEntry *vrf,
                                          RouteWalkerDBState *state,
                                          AgentRouteTable *table) {
    assert(state != NULL);
    Agent::RouteTableType table_type = table->GetTableType();
    AgentRouteWalkerPtr walker_ptr = AgentRouteWalkerPtr(this);

    // Search for walker and if found then check if table type has a reference
    // allocated or not. If allocated return the same.
    RouteWalkerDBState::AgentRouteWalkerRefMapIter it =
        state->walker_ref_map_.find(walker_ptr);
    DBTable::DBTableWalkRef route_table_walk_ref = DBTable::DBTableWalkRef();

    // Walker was not found in map, add it
    if (it == state->walker_ref_map_.end()) {
        RouteWalkerDBState::RouteWalkRef route_walk_ref;
        std::pair<RouteWalkerDBState::AgentRouteWalkerRefMapIter, bool> ret;
        ret = state->walker_ref_map_.insert(std::make_pair(walker_ptr, route_walk_ref));
        if (ret.second == false) {
            AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                               "LocateRouteTableWalkRef walker add failed.",
                               "", "");
            return DBTable::DBTableWalkRef();
        }
        it = ret.first;
    }

    // table type had no reference allocated, so allocate it.
    if (it->second[table_type] == DBTable::DBTableWalkRef()) {
        it->second[table_type] = table->AllocWalker(
                   boost::bind(&AgentRouteWalker::RouteWalkNotify,
                               this, _1, _2),
                   boost::bind(&AgentRouteWalker::RouteWalkDoneInternal,
                               this, _2, walker_ptr));
    }
    return it->second[table_type];
}

/*
 * Starts walk for all VRF.
 */
void AgentRouteWalker::StartVrfWalk() {
    mgr_->ValidateAgentRouteWalker(this);
    if (vrf_walk_ref_->in_progress() == false)
        IncrementWalkCount();
    agent_->vrf_table()->WalkAgain(vrf_walk_ref_);
    //TODO trace
    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_, "StartVrfWalk",
                       "", "");
}

DBTable::DBTableWalkRef
AgentRouteWalker::AllocateRouteTableReferences(AgentRouteTable *table) {
    VrfEntry *vrf = table->vrf_entry();
    RouteWalkerDBState *state = LocateRouteWalkerDBState(vrf);
    return LocateRouteTableWalkRef(vrf, state, table);
}

void AgentRouteWalker::WalkTable(AgentRouteTable *table,
                            DBTable::DBTableWalkRef &route_table_walk_ref) {
    if (route_table_walk_ref->in_progress() == false) {
        IncrementWalkCount();
        IncrementRouteWalkCount(table->vrf_entry());
    }
    table->WalkAgain(route_table_walk_ref);
}

/*
 * Starts route walk for given VRF.
 */
void AgentRouteWalker::StartRouteWalk(VrfEntry *vrf) {
    mgr_->ValidateAgentRouteWalker(this);
    AgentRouteTable *table = NULL;

    //Start the walk for every route table
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        table = static_cast<AgentRouteTable *>
            (vrf->GetRouteTable(table_type));
        if (!table) continue;
        DBTable::DBTableWalkRef route_table_walk_ref =
            AllocateRouteTableReferences(table);
        WalkTable(table, route_table_walk_ref);
    }
}

/*
 * VRF entry notification handler
 */
bool AgentRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);

    // TODO - VRF deletion need not be ignored, as it may be a way to
    // unsubscribe. Needs to be fixed with controller walks.
    //
    // In case of controller-peer going down. We want to send 'unsubscribe' of
    // VRF to BGP. Assume sequence where XMPP goes down and VRF is deleted. If
    // XMPP walk starts before VRF deletion, we unsubscribe to the VRF
    // notification at end of walk. If we skip deleted VRF in walk, we will not
    // send 'unsubscribe' to BGP
    //
    if (vrf->IsDeleted()) {
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                           "VrfWalkNotify: Vrf deleted, no route walk.",
                           (vrf != NULL) ? vrf->GetName() : "Unknown",
                           "NA");
        return true;
    }

    StartRouteWalk(vrf);
    return true;
}

void AgentRouteWalker::VrfWalkDoneInternal(DBTableBase *part) {
    VrfWalkDone(part);
    DecrementWalkCount();
    Callback(NULL);
}

void AgentRouteWalker::VrfWalkDone(DBTableBase *part) {
}

/*
 * Route entry notification handler
 */
bool AgentRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    return true;
}

void AgentRouteWalker::RouteWalkDone(DBTableBase *part) {
}

void AgentRouteWalker::RouteWalkDoneInternal(DBTableBase *part,
                                             AgentRouteWalkerPtr ptr) {
    RouteWalkDone(part);

    AgentRouteTable *table = static_cast<AgentRouteTable *>(part);
    DecrementWalkCount();
    DecrementRouteWalkCount(table->vrf_entry());
    uint32_t vrf_id = table->vrf_id();

    VrfEntry *vrf = agent_->vrf_table()->
        FindVrfFromIdIncludingDeletedVrf(vrf_id);
    // If there is no vrf entry for table, that signifies that
    // routes have gone and table is empty. Since routes have gone
    // state from vncontroller on routes have been removed and so would
    // have happened on vrf entry as well.
    Callback(vrf);
}

void AgentRouteWalker::DecrementWalkCount() {
    assert(walk_count_ != AgentRouteWalker::kInvalidWalkCount);
    walk_count_.fetch_and_decrement();
}

void AgentRouteWalker::DecrementRouteWalkCount(const VrfEntry *vrf) {
    VrfRouteWalkCountMap::iterator it = route_walk_count_.find(vrf);
    if (it != route_walk_count_.end()) {
        it->second.fetch_and_decrement();
        if (it->second == AgentRouteWalker::kInvalidWalkCount)
            route_walk_count_.erase(vrf);
    }
}

void AgentRouteWalker::IncrementRouteWalkCount(const VrfEntry *vrf) {
    route_walk_count_[vrf].fetch_and_increment();
}

void AgentRouteWalker::Callback(VrfEntry *vrf) {
    if (vrf && AreAllRouteWalksDone(vrf)) {
        //Deletes the state on VRF
        OnRouteTableWalkCompleteForVrf(vrf);
    }
    if (AreAllWalksDone()) {
        //To be executed in callback where surity is there
        //that all walks are done.
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                           "All Walks are done",
                           (vrf != NULL) ? vrf->GetName() : "Unknown",
                           "NA");
        if (walk_done_cb_.empty() == false) {
            walk_done_cb_();
        }
    }
}

bool AgentRouteWalker::IsRouteTableWalkCompleted(RouteWalkerDBState *state) {
    RouteWalkerDBState::AgentRouteWalkerRefMapConstIter it =
        state->walker_ref_map_.find(AgentRouteWalkerPtr(this));
    if (it == state->walker_ref_map_.end())
        return true;

    for (uint8_t table_type = Agent::ROUTE_TABLE_START;
         table_type < Agent::ROUTE_TABLE_MAX; table_type++) {
        if (it->second[table_type] != DBTable::DBTableWalkRef())
            return false;
    }
    return true;
}

/*
 * Check if all route table walk have been reset for this VRF
 */
void AgentRouteWalker::OnRouteTableWalkCompleteForVrf(VrfEntry *vrf) {
    RouteWalkerDBState *state = LocateRouteWalkerDBState(vrf);
    if (!state) {
        return;
    }

    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                       "All route walks are done",
                       (vrf != NULL) ? vrf->GetName() : "Unknown",
                       "NA");

    if (route_walk_done_for_vrf_cb_.empty())
        return;

    route_walk_done_for_vrf_cb_(vrf);
}

bool AgentRouteWalker::AreAllWalksDone() const {
    return (walk_count_ == AgentRouteWalker::kInvalidWalkCount);
}

bool AgentRouteWalker::AreAllRouteWalksDone(const VrfEntry *vrf) const {
    VrfRouteWalkCountMap::const_iterator it = route_walk_count_.find(vrf);
    return (it == route_walk_count_.end());
}

/* Callback set, his is called when all walks are done i.e. VRF + route */
void AgentRouteWalker::WalkDoneCallback(WalkDone cb) {
    walk_done_cb_ = cb;
}

/*
 * Callback is registered to notify walk complete of all route tables for
 * a VRF.
 */
void AgentRouteWalker::RouteWalkDoneForVrfCallback(RouteWalkDoneCb cb) {
    route_walk_done_for_vrf_cb_ = cb;
}

void AgentRouteWalker::ReleaseVrfWalkReference() {
    agent_->vrf_table()->WalkAgain(delete_walk_ref_);
    if (vrf_walk_ref_ != NULL) {
        agent_->vrf_table()->ReleaseWalker(vrf_walk_ref());
    }
    vrf_walk_ref_.reset();
}

bool AgentRouteWalker::Deregister(DBTablePartBase *partition,
                                  DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    DBTable::ListenerId vrf_listener_id = mgr_->vrf_listener_id();
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        vrf_listener_id));
    AgentRouteWalkerPtr walker_ptr = AgentRouteWalkerPtr(this);

    if (state == NULL)
        return true;

    if (vrf_walk_ref_.get() != NULL) {
        RouteWalkerDBState::AgentRouteWalkerRefMapIter it =
            state->walker_ref_map_.find(AgentRouteWalkerPtr(this));
        if (it != state->walker_ref_map_.end())
            RemoveWalkReferencesInRoutetable(it, state);
        return true;
    }

    //If walk reference is provided as NULL, its delete of walk manager and
    //agent shutdown. So delete all walk_refs.
    for (RouteWalkerDBState::AgentRouteWalkerRefMapIter it2 =
         state->walker_ref_map_.begin();
         it2 != state->walker_ref_map_.end(); it2++) {
        RemoveWalkReferencesInRoutetable(it2, state);
    }
    vrf->ClearState(partition->parent(), vrf_listener_id);
    delete state;
    return true;
}

void
AgentRouteWalker::DeregisterDone(AgentRouteWalkerPtr walker) {
    const Agent *agent = walker.get()->agent();
    if (walker.get()->vrf_walk_ref().get() != NULL) {
        walker.get()->agent()->vrf_table()->ReleaseWalker(walker.get()->
                                                          vrf_walk_ref());
    }
    if (walker.get()->delete_walk_ref().get() != NULL) {
        walker.get()->agent()->vrf_table()->ReleaseWalker(walker.get()->
                                                          delete_walk_ref());
    }

    if (agent->oper_db()->agent_route_walk_manager()) {
        walker.get()->mgr()->RemoveWalker(walker);
        walker.get()->mgr()->TryUnregister();
    }
}

void intrusive_ptr_add_ref(AgentRouteWalker *w) {
    w->refcount_.fetch_and_increment();
}

void intrusive_ptr_release(AgentRouteWalker *w) {
    if (w->refcount_.fetch_and_decrement() == 1) {
        delete w;
    }
}
