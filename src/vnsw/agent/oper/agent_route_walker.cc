/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <route/route.h>

#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cmn/agent_db.h>

#include <oper/agent_route_walker.h>
#include <oper/agent_route_encap.h>
#include <oper/vrf.h>
#include <oper/agent_route.h>

using namespace std;

AgentRouteWalker::AgentRouteWalker(Agent *agent, WalkType type) :
    agent_(agent), walk_type_(type),
    vrf_walkid_(DBTableWalker::kInvalidWalkerId), walk_done_cb_(),
    route_walk_done_for_vrf_cb_(),
    work_queue_(TaskScheduler::GetInstance()->
                GetTaskId("Agent::RouteWalker"), 0,
                boost::bind(&AgentRouteWalker::RouteWalker, this, _1)) {
    walk_count_ = AgentRouteWalker::kInvalidWalkCount;
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        route_walkid_[table_type].clear();
    }
}

AgentRouteWalker::~AgentRouteWalker() {
    work_queue_.Shutdown();
}

bool AgentRouteWalker::RouteWalker(boost::shared_ptr<AgentRouteWalkerQueueEntry> data) {
    VrfEntry *vrf = data->vrf_ref_.get();
    switch (data->type_) {
      case AgentRouteWalkerQueueEntry::START_VRF_WALK:
          StartVrfWalkInternal();
          break;
      case AgentRouteWalkerQueueEntry::CANCEL_VRF_WALK:
          CancelVrfWalkInternal();
          break;
      case AgentRouteWalkerQueueEntry::START_ROUTE_WALK:
          StartRouteWalkInternal(vrf);
          break;
      case AgentRouteWalkerQueueEntry::CANCEL_ROUTE_WALK:
          CancelRouteWalkInternal(vrf);
          break;
      case AgentRouteWalkerQueueEntry::DONE_WALK:
          CallbackInternal(vrf);
          break;
      default:
          assert(0);
    }
    return true;
}

/*
 * Cancels VRF walk. Does not stop route walks if issued for vrf
 */
void AgentRouteWalker::CancelVrfWalk() {
    boost::shared_ptr<AgentRouteWalkerQueueEntry> data(new AgentRouteWalkerQueueEntry(NULL,
                                      AgentRouteWalkerQueueEntry::CANCEL_VRF_WALK));
    work_queue_.Enqueue(data);
}

void AgentRouteWalker::CancelVrfWalkInternal() {
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                           "VRF table walk cancelled ",
                           walk_type_, "", vrf_walkid_, 0, "",
                           DBTableWalker::kInvalidWalkerId);
        walker->WalkCancel(vrf_walkid_);
        vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
        DecrementWalkCount();
    }
}

/*
 * Cancels route walks started for given VRF
 */
void AgentRouteWalker::CancelRouteWalk(VrfEntry *vrf) {
    boost::shared_ptr<AgentRouteWalkerQueueEntry> data(new AgentRouteWalkerQueueEntry(vrf,
                                      AgentRouteWalkerQueueEntry::CANCEL_ROUTE_WALK));
    work_queue_.Enqueue(data);
}

void AgentRouteWalker::CancelRouteWalkInternal(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    uint32_t vrf_id = vrf->vrf_id();

    //Cancel Route table walks
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
         table_type++) {
        VrfRouteWalkerIdMapIterator iter = 
            route_walkid_[table_type].find(vrf_id);
        if (iter != route_walkid_[table_type].end()) {
            AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                               "route table walk cancelled", walk_type_,
                               (vrf != NULL) ? vrf->GetName() : "Unknown",
                               vrf_walkid_, table_type, "", iter->second);
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
void AgentRouteWalker::StartVrfWalk() {
    boost::shared_ptr<AgentRouteWalkerQueueEntry> data(new AgentRouteWalkerQueueEntry(NULL,
                                      AgentRouteWalkerQueueEntry::START_VRF_WALK));
    work_queue_.Enqueue(data);
}

void AgentRouteWalker::StartVrfWalkInternal()
{
    DBTableWalker *walker = agent_->db()->GetWalker();

    //Cancel the VRF walk if started previously
    CancelVrfWalkInternal();

    //New walk start for VRF
    vrf_walkid_ = walker->WalkTable(agent_->vrf_table(), NULL,
                                    boost::bind(&AgentRouteWalker::VrfWalkNotify, 
                                                this, _1, _2),
                                    boost::bind(&AgentRouteWalker::VrfWalkDone, 
                                                this, _1));
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        IncrementWalkCount();
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                           "VRF table walk started",
                           walk_type_, "", vrf_walkid_,
                           0, "", DBTableWalker::kInvalidWalkerId);
    }
}

/*
 * Starts route walk for given VRF.
 * Cancels any old route walks started for given VRF
 */
void AgentRouteWalker::StartRouteWalk(VrfEntry *vrf) {
    boost::shared_ptr<AgentRouteWalkerQueueEntry> data(new AgentRouteWalkerQueueEntry(vrf,
                                      AgentRouteWalkerQueueEntry::START_ROUTE_WALK));
    work_queue_.Enqueue(data);
}

void AgentRouteWalker::StartRouteWalkInternal(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    DBTableWalker::WalkId walkid;
    uint32_t vrf_id = vrf->vrf_id();
    AgentRouteTable *table = NULL;

    //Cancel any walk started previously for this VRF
    CancelRouteWalkInternal(vrf);

    //Start the walk for every route table
    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
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
            AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                               "Route table walk started for vrf", walk_type_,
                               (vrf != NULL) ? vrf->GetName() : "Unknown",
                               vrf_walkid_, table_type, "", walkid);
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
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                           "Ignore VRF as it is deleted", walk_type_,
                           (vrf != NULL) ? vrf->GetName() : "Unknown",
                           vrf_walkid_, 0, "", DBTableWalker::kInvalidWalkerId);
        return true;
    }

    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                       "Starting route walk for vrf", walk_type_,
                       (vrf != NULL) ? vrf->GetName() : "Unknown",
                       vrf_walkid_, 0, "", DBTableWalker::kInvalidWalkerId);
    StartRouteWalk(vrf);
    return true;
}

void AgentRouteWalker::VrfWalkDone(DBTableBase *part) {
    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                       "VRF table walk done",
                       walk_type_, "", vrf_walkid_,
                       0, "", DBTableWalker::kInvalidWalkerId);
    vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
    DecrementWalkCount();
    Callback(NULL);
}

/*
 * Route entry notification handler
 */
bool AgentRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    const AgentRoute *route = static_cast<const AgentRoute *>(e);
    AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                       "Ignore Route notifications from this walk",
                       walk_type_, "", vrf_walkid_,
                       (route != NULL) ? route->GetTableType() : 0,
                       "", DBTableWalker::kInvalidWalkerId);
    return true;
}

void AgentRouteWalker::RouteWalkDone(DBTableBase *part) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(part);
    uint32_t vrf_id = table->vrf_id();
    uint8_t table_type = table->GetTableType();

    VrfRouteWalkerIdMapIterator iter = route_walkid_[table_type].find(vrf_id);
    if (iter != route_walkid_[table_type].end()) {
        AGENT_DBWALK_TRACE(AgentRouteWalkerTrace,
                           "Route table walk done for route",
                           walk_type_, "", vrf_walkid_, table_type,
                           (table != NULL) ? table->GetTableName() : "Unknown",
                           iter->second);
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
    boost::shared_ptr<AgentRouteWalkerQueueEntry> data
        (new AgentRouteWalkerQueueEntry(vrf, AgentRouteWalkerQueueEntry::DONE_WALK));
    work_queue_.Enqueue(data);
}

void AgentRouteWalker::CallbackInternal(VrfEntry *vrf) {
    if (vrf) {
        OnRouteTableWalkCompleteForVrf(vrf);
    }
    OnWalkComplete();
}

/*
 * Check if all route table walk have been reset for this VRF
 */
void AgentRouteWalker::OnRouteTableWalkCompleteForVrf(VrfEntry *vrf) {
    if (!route_walk_done_for_vrf_cb_)
        return;

    for (uint8_t table_type = (Agent::INVALID + 1);
         table_type < Agent::ROUTE_TABLE_MAX;
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
        for (uint8_t table_type = (Agent::INVALID + 1);
             table_type < Agent::ROUTE_TABLE_MAX;
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
