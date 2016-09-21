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

RouteWalkerDBState::RouteWalkerDBState() : vrf_walk_ref_map_() {
}

static void
RemoveWalkReferencesInRoutetable(RouteWalkerDBState::RouteTableWalkRefList &list) {
    for (RouteWalkerDBState::RouteTableWalkRefList::iterator it =
         list.begin(); it != list.end(); it++) {
        DBTable::DBTableWalkRef ref = (*it);
        AgentRouteTable *route_table =
            static_cast<AgentRouteTable *>(ref.get()->table());
        route_table->ReleaseWalker(ref);
    }
    list.clear();
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

void AgentRouteWalkerManager::RemoveWalkReferencesInVrf(VrfEntry *vrf) {
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        vrf_listener_id_));
    for (RouteWalkerDBState::VrfWalkRefMap::iterator it =
         state->vrf_walk_ref_map_.begin();
         it != state->vrf_walk_ref_map_.end(); it++) {
        //Iterate through all route walk references.
        RouteWalkerDBState::RouteTableWalkRefList rt_table_walk_ref_list =
            it->second;
        RemoveWalkReferencesInRoutetable(it->second);
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
        RemoveWalkReferencesInVrf(vrf);
        vrf->ClearState(partition->parent(), vrf_listener_id_);
        delete state;
        return;
    }

    if (!state) {
        CreateState(vrf);
    }
}

RouteWalkerDBState *AgentRouteWalkerManager::CreateState(VrfEntry *vrf) {
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        vrf_listener_id_));
    assert(state == NULL);
    state = new RouteWalkerDBState();
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

void AgentRouteWalkerManager::RegisterAgentRouteWalker(AgentRouteWalker *walker) {
    assert(agent_->test_mode() == true);
    walk_ref_list_.push_back(walker);
}

AgentRouteWalker::AgentRouteWalker(WalkType walk_type,
                                   const std::string &name,
                                   AgentRouteWalkerManager *mgr) :
    agent_(mgr->agent()), name_(name), walk_type_(walk_type),
    walk_done_cb_(), route_walk_done_for_vrf_cb_(), mgr_(mgr) {
    walk_count_ = AgentRouteWalker::kInvalidWalkCount;
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        walkable_route_tables_ |= (1 << table_type);
    }
    vrf_walk_ref_ = agent_->vrf_table()->AllocWalker(
                            boost::bind(&AgentRouteWalker::VrfWalkNotify,
                                        this, _1, _2),
                            boost::bind(&AgentRouteWalker::VrfWalkDone,
                                        this, _2));
    delete_walk_ref_ = mgr->agent()->vrf_table()->AllocWalker(
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
    RouteWalkerDBState::VrfWalkRefMap::iterator it =
        state->vrf_walk_ref_map_.find(vrf_walk_ref_);
    if (it != state->vrf_walk_ref_map_.end()) {
        RouteWalkerDBState::RouteTableWalkRefList rt_table_walk_ref_list =
            it->second;
        for(RouteWalkerDBState::RouteTableWalkRefList::iterator it2 =
            rt_table_walk_ref_list.begin(); it2 != rt_table_walk_ref_list.end();
            it2++) {
            if ((*it2)->table() == table) {
                return *it2;
            }
        }
    }

    //Allocate walk_ref for this table
    DBTable::DBTableWalkRef rt_table_walk_ref = table->AllocWalker(
                               boost::bind(&AgentRouteWalker::RouteWalkNotify,
                                           this, _1, _2),
                               boost::bind(&AgentRouteWalker::RouteWalkDone,
                                           this, _2));

    if (it == state->vrf_walk_ref_map_.end()) {
        //Vrf walk ref is getting added in map for first time.
        RouteWalkerDBState::RouteTableWalkRefList route_table_walk_ref_list;
        route_table_walk_ref_list.push_back(rt_table_walk_ref);
        state->vrf_walk_ref_map_[vrf_walk_ref_] =
            route_table_walk_ref_list;
    } else {
        //Vrf walk ref was present, route table ref is getting added.
        it->second.push_back(rt_table_walk_ref);
    }
    return rt_table_walk_ref;
}

/*
 * Starts walk for all VRF.
 */
void AgentRouteWalker::StartVrfWalk() {
    mgr_->ValidateAgentRouteWalker(this);
    IncrementWalkCount();
    agent_->vrf_table()->WalkAgain(vrf_walk_ref_);
    //TODO trace
    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_, "StartVrfWalk",
                       walk_type_, "", "");
}

DBTable::DBTableWalkRef
AgentRouteWalker::AllocateRouteTableReferences(AgentRouteTable *table) {
    VrfEntry *vrf = table->vrf_entry();
    RouteWalkerDBState *state = LocateRouteWalkerDBState(vrf);
    return LocateRouteTableWalkRef(vrf, state, table);
}

void AgentRouteWalker::WalkTable(AgentRouteTable *table,
                            DBTable::DBTableWalkRef &route_table_walk_ref) {
    if (!(walkable_route_tables_ & (1 << table->GetTableType())))
        return;
    IncrementWalkCount();
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
                           "VrfWalkNotify: Vrf deleted, no route walk.", walk_type_,
                           (vrf != NULL) ? vrf->GetName() : "Unknown",
                           "NA");
        return true;
    }

    StartRouteWalk(vrf);
    return true;
}

void AgentRouteWalker::VrfWalkDone(DBTableBase *part) {
    DecrementWalkCount();
    Callback(NULL);
}

/*
 * Route entry notification handler
 */
bool AgentRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    return true;
}

void AgentRouteWalker::RouteWalkDone(DBTableBase *part) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(part);
    DecrementWalkCount();
    uint32_t vrf_id = table->vrf_id();

    VrfEntry *vrf = agent_->vrf_table()->
        FindVrfFromIdIncludingDeletedVrf(vrf_id);
    // If there is no vrf entry for table, that signifies that
    // routes have gone and table is empty. Since routes have gone
    // state from vncontroller on routes have been removed and so would
    // have happened on vrf entry as well.
    if (vrf != NULL) {
        Callback(vrf);
    }
}

void AgentRouteWalker::DecrementWalkCount() {
    assert(walk_count_ != AgentRouteWalker::kInvalidWalkCount);
    walk_count_.fetch_and_decrement();
}

void AgentRouteWalker::Callback(VrfEntry *vrf) {
    if (vrf) {
        //Deletes the state on VRF
        OnRouteTableWalkCompleteForVrf(vrf);
    }
    if (AreAllWalksDone()) {
        //To be executed in callback where surity is there
        //that all walks are done.
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                           "All Walks are done", walk_type_,
                           (vrf != NULL) ? vrf->GetName() : "Unknown",
                           "NA");
        if (walk_done_cb_.empty() == false) {
            walk_done_cb_();
        }
    }
}

bool AgentRouteWalker::IsRouteTableWalkCompleted(RouteWalkerDBState *state) const {
    RouteWalkerDBState::VrfWalkRefMap::const_iterator ref_list =
        state->vrf_walk_ref_map_.find(vrf_walk_ref_);;
    if (ref_list == state->vrf_walk_ref_map_.end())
        return true;

    for (RouteWalkerDBState::RouteTableWalkRefList::const_iterator it =
         (ref_list->second).begin();
         it != (ref_list->second).end(); it++) {
        if ((*it)->done() == false)
            return false;
    }
    return true;
}

/*
 * Check if all route table walk have been reset for this VRF
 */
void AgentRouteWalker::OnRouteTableWalkCompleteForVrf(VrfEntry *vrf) {
    RouteWalkerDBState *state = LocateRouteWalkerDBState(vrf);
    if (!state)
        return;

    if (IsRouteTableWalkCompleted(state) == false)
        return;

    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                       "All route walks are done", walk_type_,
                       (vrf != NULL) ? vrf->GetName() : "Unknown",
                       "NA");

    if (route_walk_done_for_vrf_cb_.empty())
        return;

    route_walk_done_for_vrf_cb_(vrf);
}

bool AgentRouteWalker::AreAllWalksDone() const {
    return (walk_count_ == AgentRouteWalker::kInvalidWalkCount);
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
}

bool AgentRouteWalker::Deregister(DBTablePartBase *partition,
                                  DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    DBTable::ListenerId vrf_listener_id = mgr_->vrf_listener_id();
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        vrf_listener_id));
    if (state == NULL)
        return true;

    if (vrf_walk_ref_.get() != NULL) {
        RouteWalkerDBState::VrfWalkRefMap::iterator it =
            state->vrf_walk_ref_map_.find(vrf_walk_ref_);
        if (it != state->vrf_walk_ref_map_.end())
            RemoveWalkReferencesInRoutetable(it->second);
        return true;
    }

    //If walk reference is provided as NULL, its delete of walk manager and
    //agent shutdown. So delete all walk_refs.
    for (RouteWalkerDBState::VrfWalkRefMap::iterator it2 =
         state->vrf_walk_ref_map_.begin();
         it2 != state->vrf_walk_ref_map_.end(); it2++) {
        RemoveWalkReferencesInRoutetable(it2->second);
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
    //delete walker;
}

void intrusive_ptr_add_ref(AgentRouteWalker *w) {
    w->refcount_.fetch_and_increment();
}

void intrusive_ptr_release(AgentRouteWalker *w) {
    if (w->refcount_.fetch_and_decrement() == 1) {
        delete w;
    }
}
