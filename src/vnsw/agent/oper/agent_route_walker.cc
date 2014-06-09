/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_route_walker.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

using namespace std;

AgentRouteWalker::AgentRouteWalker(Agent *agent, WalkType type) :
    agent_(agent), walk_type_(type),
    vrf_walkid_(DBTableWalker::kInvalidWalkerId), walk_done_cb_(),
    route_walk_done_for_vrf_cb_() {
    walk_count_ = AgentRouteWalker::kInvalidWalkCount;
    for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
         table_type++) {
        route_walkid_[table_type].clear();
    }
}

/*
 * Cancels VRF walk. Does not stop route walks if issued for vrf
 */
void AgentRouteWalker::CancelVrfWalk() {
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, 0,
                  "VRF table walk cancelled ", "", 0);
        walker->WalkCancel(vrf_walkid_);
        vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
        DecrementWalkCount();
    }
}

/*
 * Cancels route walks started for given VRF
 */
void AgentRouteWalker::CancelRouteWalk(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    uint32_t vrf_id = vrf->vrf_id();

    //Cancel Route table walks
    for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
         table_type++) {
        VrfRouteWalkerIdMapIterator iter = 
            route_walkid_[table_type].find(vrf_id);
        if (iter != route_walkid_[table_type].end()) {
            AGENT_LOG(AgentRouteWalkerLog, iter->second, 0,
                      "route table walk cancelled for vrf", 
                      vrf->GetName(), table_type);
            walker->WalkCancel(iter->second);
            route_walkid_[table_type].erase(iter);
            DecrementWalkCount();
        }
    }
}

/*
 * Startes a new walk for all VRF.
 * Cancels any old walk of VRF.
 */
void AgentRouteWalker::StartVrfWalk()
{
    DBTableWalker *walker = agent_->db()->GetWalker();

    //Cancel the VRF walk if started previously
    CancelVrfWalk();

    //New walk start for VRF
    vrf_walkid_ = walker->WalkTable(agent_->vrf_table(), NULL,
                                    boost::bind(&AgentRouteWalker::VrfWalkNotify, 
                                                this, _1, _2),
                                    boost::bind(&AgentRouteWalker::VrfWalkDone, 
                                                this, _1));
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        IncrementWalkCount();
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
                  "VRF table walk started ", "", 0);
    }
}

/*
 * Starts route walk for given VRF.
 * Cancels any old route walks started for given VRF
 */
void AgentRouteWalker::StartRouteWalk(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    DBTableWalker::WalkId walkid;
    uint32_t vrf_id = vrf->vrf_id();
    AgentRouteTable *table = NULL;

    //Cancel any walk started previously for this VRF
    CancelRouteWalk(vrf);

    //Start the walk for every route table
    for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
         table_type++) {
        table = static_cast<AgentRouteTable *>
            (vrf->GetRouteTable(table_type));
        walkid = walker->WalkTable(table, NULL, 
                             boost::bind(&AgentRouteWalker::RouteWalkNotify, 
                                         this, _1, _2),
                             boost::bind(&AgentRouteWalker::RouteWalkDone, 
                                         this, _1));
        if (walkid != DBTableWalker::kInvalidWalkerId) {
            IncrementWalkCount();
            route_walkid_[table_type][vrf_id] = walkid;
            AGENT_LOG(AgentRouteWalkerLog, walkid, walk_type_,
                      "Route table walk started for vrf ", vrf->GetName(), 
                      table_type);
        }
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
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_, 
                  "Ignore VRF as it is deleted ", vrf->GetName(), 0);
        return true;
    }

    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_, 
              "Starting route walk for vrf ", vrf->GetName(), 0);
    StartRouteWalk(vrf);
    return true;
}

void AgentRouteWalker::VrfWalkDone(DBTableBase *part) {
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
              "VRF table walk done ", "",  0);
    vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
    DecrementWalkCount();
    OnWalkComplete();
}

/*
 * Route entry notification handler
 */
bool AgentRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
              "Ignore Route notifications from this walk.", "", 0);
    return true;
}

void AgentRouteWalker::RouteWalkDone(DBTableBase *part) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(part);
    uint32_t vrf_id = table->vrf_id();
    uint8_t table_type = table->GetTableType();

    VrfRouteWalkerIdMapIterator iter = route_walkid_[table_type].find(vrf_id);
    if (iter != route_walkid_[table_type].end()) {
        AGENT_LOG(AgentRouteWalkerLog, iter->second, 
                  walk_type_, "Route table walk done for route ", 
                  table->GetTableName(), table_type);
        route_walkid_[table_type].erase(vrf_id);
        DecrementWalkCount();

        // vrf entry can be null as table wud have released the reference
        // via lifetime actor
        VrfEntry *vrf = table->vrf_entry();
        // If there is no vrf entry for table, that signifies that 
        // routes have gone and table is empty. Since routes have gone
        // state from vncontroller on routes have been removed and so would
        // have happened on vrf entry as well.
        if (vrf != NULL) {
            Callback(vrf);
        }
    }
}

void AgentRouteWalker::DecrementWalkCount() {
    if (walk_count_ != AgentRouteWalker::kInvalidWalkCount) {
        walk_count_--;
    }
}

void AgentRouteWalker::Callback(VrfEntry *vrf) {
    OnRouteTableWalkCompleteForVrf(vrf);
    OnWalkComplete();
}

/*
 * Check if all route table walk have been reset for this VRF
 */
void AgentRouteWalker::OnRouteTableWalkCompleteForVrf(VrfEntry *vrf) {
    if (!route_walk_done_for_vrf_cb_)
        return;

    for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
         table_type++) {
        VrfRouteWalkerIdMapIterator iter = 
            route_walkid_[table_type].find(vrf->vrf_id());
        if (iter != route_walkid_[table_type].end()) {
            return;
        }
    }
    route_walk_done_for_vrf_cb_(vrf);
}

/*
 * Check if all walks are over.
 */
void AgentRouteWalker::OnWalkComplete() {
    bool walk_done = false;
    if (vrf_walkid_ == DBTableWalker::kInvalidWalkerId) {
        walk_done = true;
        for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
             table_type++) {
            if (route_walkid_[table_type].size() != 0) {
                //Route walk pending
                walk_done = false;
                break;
            }
        }
    }
    if (walk_done && walk_count_) {
        assert(0);
    }
    if (walk_done && !walk_count_ && walk_done_cb_) {
        walk_done_cb_();
    }
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
