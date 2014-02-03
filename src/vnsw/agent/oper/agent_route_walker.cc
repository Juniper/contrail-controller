/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "agent_route_walker.h"
#include "oper/route_common.h"
#include "oper/vrf.h"
#include "oper/mirror_table.h"
#include "oper/agent_sandesh.h"

using namespace std;

AgentRouteWalker::AgentRouteWalker(WalkType type) : walk_type_(type), 
    vrf_walkid_(DBTableWalker::kInvalidWalkerId) {
        for (uint32_t rt_table_type = 0; 
             rt_table_type < AgentRouteTableAPIS::MAX; rt_table_type++) {
            route_walkid_[rt_table_type] = DBTableWalker::kInvalidWalkerId;
        }
}

void AgentRouteWalker::CancelVrfWalk() {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, 0,
                  "VRF table walk cancelled ", 0);
        walker->WalkCancel(vrf_walkid_);
        vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
    }
}

void AgentRouteWalker::CancelRouteWalk() {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();

    //Cancel Route table walks
    for (uint8_t table_type = 0; table_type < AgentRouteTableAPIS::MAX; 
         table_type++) {
        if (route_walkid_[table_type] != DBTableWalker::kInvalidWalkerId) {
            AGENT_LOG(AgentRouteWalkerLog, 
                      route_walkid_[table_type], 0,
                      "route table walk cancelled ", table_type);
            walker->WalkCancel(route_walkid_[table_type]);
            route_walkid_[table_type] = DBTableWalker::kInvalidWalkerId;
        }
    }
}

void AgentRouteWalker::StartVrfWalk()
{
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    vrf_walkid_ = DBTableWalker::kInvalidWalkerId;

    vrf_walkid_ = walker->WalkTable(Agent::GetInstance()->GetVrfTable(), NULL,
                                    boost::bind(&AgentRouteWalker::VrfWalkNotify, 
                                                this, _1, _2),
                                    boost::bind(&AgentRouteWalker::VrfWalkDone, 
                                                this, _1));
    if (vrf_walkid_ != DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
                  "VRF table walk started - ", 0);
    }
}

void AgentRouteWalker::StartRouteWalk(const VrfEntry *vrf) {
    DBTableWalker *walker = Agent::GetInstance()->GetDB()->GetWalker();
    DBTableWalker::WalkId walkid;

    AgentRouteTable *table = NULL;

    for (uint8_t table_type = 0; table_type < AgentRouteTableAPIS::MAX; 
         table_type++) {
        table = static_cast<AgentRouteTable *>
            (vrf->GetRouteTable(table_type));
        walkid = walker->WalkTable(table, NULL, 
                             boost::bind(&AgentRouteWalker::RouteWalkNotify, 
                                         this, _1, _2),
                             boost::bind(&AgentRouteWalker::RouteWalkDone, 
                                         this, _1));
        AGENT_LOG(AgentRouteWalkerLog, walkid, walk_type_,
                  "Route table walk started", table_type);
        route_walkid_[table_type] = walkid;
    }
}

bool AgentRouteWalker::VrfWalkNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
    VrfEntry *vrf = static_cast<VrfEntry *>(e);
    std::stringstream str;

    if (vrf->IsDeleted()) {
        str << "Ignore VRF as it is deleted; vrf name - " << vrf->GetName();
        AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_, 
                  str.str().c_str(), 0);
        return true;
    }

    str << "Starting route walk for vrf name - " << vrf->GetName();
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_, 
              str.str().c_str(), 0);
    StartRouteWalk(vrf);
    return true;
}

void AgentRouteWalker::VrfWalkDone(DBTableBase *part) {
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
              "VRF table walk done- ", 0);
    vrf_walkid_ = DBTableWalker::kInvalidWalkerId;
}

bool AgentRouteWalker::RouteWalkNotify(DBTablePartBase *partition,
                                       DBEntryBase *e) {
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
              "Ignore Route notifications from this walk.", 0);
    return true;
}

void AgentRouteWalker::RouteWalkDone(DBTableBase *part) {
    AgentRouteTable *table = static_cast<AgentRouteTable *>(part);
    uint8_t table_type = table->GetTableType();
    if (route_walkid_[table_type] != 
        DBTableWalker::kInvalidWalkerId) {
        AGENT_LOG(AgentRouteWalkerLog, route_walkid_[table_type], walk_type_,
                  "Route table walk done", table_type);
        route_walkid_[table_type] = DBTableWalker::kInvalidWalkerId;
    }
}

void AgentRouteWalker::RestartAgentRouteWalk() {
    AGENT_LOG(AgentRouteWalkerLog, vrf_walkid_, walk_type_,
              "Restart agent route walk.", 0);
    CancelVrfWalk();
    CancelRouteWalk();
    StartVrfWalk();
}
