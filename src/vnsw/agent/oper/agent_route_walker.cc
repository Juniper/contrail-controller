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
    vrf_walkid_(DBTableWalker::kInvalidWalkerId) {
    for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
         table_type++) {
        route_walkid_[table_type].clear();
    }
}

/*
 * Cancels VRF walk. Does not stop route walks if issued for vrf
 */
void AgentRouteWalker::CancelVrfWalk() {
    DBTableWalker *walker = agent_->GetDB()->GetWalker();
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, 0,
                  "VRF table walk cancelled ", "", 0);
        walker->WalkCancel(vrf_walkid_);
        vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
    }
}

/*
 * Cancels route walks started for given VRF
 */
void AgentRouteWalker::CancelRouteWalk(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->GetDB()->GetWalker();
    uint32_t vrf_id = vrf->GetVrfId();

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
        }
    }
}

/*
 * Startes a new walk for all VRF.
 * Cancels any old walk of VRF.
 */
void AgentRouteWalker::StartVrfWalk()
{
    DBTableWalker *walker = agent_->GetDB()->GetWalker();

    //Cancel the VRF walk if started previously
    CancelVrfWalk();

    //New walk start for VRF
    vrf_walkid_ = walker->WalkTable(agent_->GetVrfTable(), NULL,
                                    boost::bind(&AgentRouteWalker::VrfWalkNotify, 
                                                this, _1, _2),
                                    boost::bind(&AgentRouteWalker::VrfWalkDone, 
                                                this, _1));
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
                  "VRF table walk started ", "", 0);
    }
}

/*
 * Starts route walk for given VRF.
 * Cancels any old route walks started for given VRF
 */
void AgentRouteWalker::StartRouteWalk(const VrfEntry *vrf) {
    DBTableWalker *walker = agent_->GetDB()->GetWalker();
    DBTableWalker::WalkId walkid;
    uint32_t vrf_id = vrf->GetVrfId();
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
        AGENT_LOG(AgentRouteWalkerLog, walkid, walk_type_,
                  "Route table walk started for vrf ", vrf->GetName(), 
                  table_type);
        route_walkid_[table_type][vrf_id] = walkid;
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
    uint32_t vrf_id = table->GetVrfId();
    uint8_t table_type = table->GetTableType();

    VrfRouteWalkerIdMapIterator iter = route_walkid_[table_type].find(vrf_id);
    if (iter != route_walkid_[table_type].end()) {
        AGENT_LOG(AgentRouteWalkerLog, iter->second, 
                  walk_type_, "Route table walk done for vrf ", 
                  table->GetVrfName(), table_type);
        route_walkid_[table_type].erase(vrf_id);
    }
}

bool AgentRouteWalker::IsWalkCompleted() {
    if (vrf_walkid_ == DBTableWalker::kInvalidWalkerId) {
        for (uint8_t table_type = 0; table_type < Agent::ROUTE_TABLE_MAX; 
             table_type++) {
            if (route_walkid_[table_type].size() != 0) {
                //Route walk pending
                return false;
            }
        }
        return true;
    }
    //VRF walk not over
    return false;
}
