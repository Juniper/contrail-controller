/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
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

RouteWalkerDBState::RouteWalkerDBState() {
    vrf_walk_ref_map_.clear();
}

static void ReleaseVrfWalkReference(const Agent *agent,
                                    DBTable::DBTableWalkRef vrf_walk_ref,
                                    DBTable::ListenerId vrf_listener_id) {
    DBTable::DBTableWalkRef ref = agent->vrf_table()->AllocWalker(
                        boost::bind(&AgentRouteWalkerManager::VrfWalkNotify,
                                    _1, _2, vrf_walk_ref, agent,
                                    vrf_listener_id),
                        boost::bind(&AgentRouteWalkerManager::VrfWalkDone,
                                    _1, _2, vrf_walk_ref, vrf_listener_id));
    agent->vrf_table()->WalkAgain(ref);
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

AgentRouteWalkerManager::AgentRouteWalkerManager(Agent *agent) : agent_(agent) {
    vrf_listener_id_ = agent->vrf_table()->Register(
                       boost::bind(&AgentRouteWalkerManager::VrfNotify, this,
                                   _1, _2));
}

AgentRouteWalkerManager::~AgentRouteWalkerManager() {
    //Needed to release vrf_listener_id.
    ReleaseVrfWalkReference(agent_, NULL, vrf_listener_id_);
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
        state = new RouteWalkerDBState();
        vrf->SetState(partition->parent(), vrf_listener_id_, state);
    }
}

bool AgentRouteWalkerManager::VrfWalkNotify(DBTablePartBase *partition,
                                     DBEntryBase *e,
                                     DBTable::DBTableWalkRef walk_ref,
                                     const Agent *agent,
                                     DBTable::ListenerId vrf_listener_id) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                                                        vrf_listener_id));
    if (state == NULL)
        return true;

    if (walk_ref.get() != NULL) {
        RouteWalkerDBState::VrfWalkRefMap::iterator it =
            state->vrf_walk_ref_map_.find(walk_ref);
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
AgentRouteWalkerManager::VrfWalkDone(DBTable::DBTableWalkRef walker_ref,
                                DBTableBase *partition,
                                DBTable::DBTableWalkRef vrf_walk_ref,
                                DBTable::ListenerId vrf_listener_id) {
    if (vrf_walk_ref.get() != NULL) {
        walker_ref->table()->ReleaseWalker(vrf_walk_ref);
    } else {
        walker_ref->table()->Unregister(vrf_listener_id);
    }
    walker_ref->table()->ReleaseWalker(walker_ref);
}

AgentRouteWalker::AgentRouteWalker(Agent *agent, WalkType type,
                                   const std::string &name) :
    agent_(agent), name_(name), walk_type_(type), walk_done_cb_(),
    route_walk_done_for_vrf_cb_() {
    walk_count_ = AgentRouteWalker::kInvalidWalkCount;
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        walkable_route_tables_ |= (1 << table_type);
    }
    vrf_walk_ref_ = NULL;
}

AgentRouteWalker::~AgentRouteWalker() {
    if (vrf_walk_ref_.get() != NULL) {
        if (agent_->oper_db()->agent_route_walk_manager()) {
            ReleaseVrfWalkReference(agent_, vrf_walk_ref_,
              agent_->oper_db()->agent_route_walk_manager()->vrf_listener_id());
        }
        agent_->vrf_table()->ReleaseWalker(vrf_walk_ref_);
    }
}

RouteWalkerDBState *
AgentRouteWalker::GetRouteWalkerDBState(const VrfEntry *vrf) {
    RouteWalkerDBState *state =
        static_cast<RouteWalkerDBState *>(vrf->GetState(vrf->get_table(),
                         agent_->oper_db()->agent_route_walk_manager()->
                         vrf_listener_id()));
    return state;
}

DBTable::DBTableWalkRef
AgentRouteWalker::GetRouteTableWalkRef(const VrfEntry *vrf,
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
    DBTable::DBTableWalkRef rt_table_ref = table->AllocWalker(
                               boost::bind(&AgentRouteWalker::RouteWalkNotify,
                                           this, _1, _2),
                               boost::bind(&AgentRouteWalker::RouteWalkDone,
                                           this, _2));

    if (it == state->vrf_walk_ref_map_.end()) {
        RouteWalkerDBState::RouteTableWalkRefList route_table_walk_ref_list;
        route_table_walk_ref_list.push_back(rt_table_ref);
        state->vrf_walk_ref_map_[vrf_walk_ref_] =
            route_table_walk_ref_list;
    } else {
        it->second.push_back(rt_table_ref);
    }
    return rt_table_ref;
}

/*
 * Starts walk for all VRF.
 */
void AgentRouteWalker::StartVrfWalk() {
    if (vrf_walk_ref_.get() == NULL) {
        vrf_walk_ref_ = agent_->vrf_table()->AllocWalker(
                                boost::bind(&AgentRouteWalker::VrfWalkNotify,
                                            this, _1, _2),
                                boost::bind(&AgentRouteWalker::VrfWalkDone,
                                            this, _2));
    }
    agent_->vrf_table()->WalkAgain(vrf_walk_ref_);
    IncrementWalkCount();
    //TODO trace
    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_, "StartVrfWalk",
                       walk_type_, "", "");
}

/*
 * Starts route walk for given VRF.
 */
void AgentRouteWalker::StartRouteWalk(VrfEntry *vrf) {
    AgentRouteTable *table = NULL;

    //Start the walk for every route table
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        if (!(walkable_route_tables_ & (1 << table_type)))
            continue;
        table = static_cast<AgentRouteTable *>
            (vrf->GetRouteTable(table_type));
        if (table == NULL) {
            AGENT_DBWALK_TRACE(AgentRouteWalkerTrace, name_,
                               "StartRouteWalk: table skipped", walk_type_,
                               (vrf != NULL) ? vrf->GetName() : "Unknown",
                               vrf->GetTableTypeString(table_type));
            continue;
        }
        RouteWalkerDBState *state = GetRouteWalkerDBState(vrf);
        DBTable::DBTableWalkRef route_table_walk_ref =
            GetRouteTableWalkRef(vrf, state, table);
        table->WalkAgain(route_table_walk_ref);
        IncrementWalkCount();
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
    if (walk_count_ != AgentRouteWalker::kInvalidWalkCount) {
        walk_count_.fetch_and_decrement();
    }
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
    RouteWalkerDBState *state = GetRouteWalkerDBState(vrf);
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
