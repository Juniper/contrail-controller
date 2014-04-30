/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_walker_hpp
#define vnsw_agent_route_walker_hpp

#include "cmn/agent_cmn.h"


/**
 * The infrastructure is to support and manage VRF walks along with
 * corresponding route walks. The walkids are internally managed.
 * Following type of walks can be issued:
 * 1) ALL VRF walk - Use API StartVrfWalk()
 * 2) Specific VRF walk - Use StartRouteWalk(vrf_entry) API
 * 3) Specialized walk - Issue StartVrfWalk() and override VrfWalkNotify() to
 *    select a set of VRF to walk on. By default VrfWalkNotify starts walks on
 *    all route table.
 *    RouteWalkNotify() - This should be overriden to listen and act on route
 *    entry notifications. By default it ignores the request.
 *    WalkType - On receiving route notification this can be used to filter the
 *    routes. Say a MULTICAST walk is issued the route notifications for all
 *    unicast entries can be ignored by checking for walktype.
 * 4) Only VRF walk - Use API StartVRFWalk and override VrfWalkNotify() to not
 *    start route table walk. In this way only VRF entries can be traversed 
 *    without route walks issued.
 *    
 * Cancellation of walks happen when a new walk is started and there was an old
 * walk started using same object. Multiple objects of this class can have
 * separate parallel walks. 
 * Cancellation of VRF walk only cancels VRF walk and not the corresponding
 * route walk started by VRF.
 * Cancellation of route walk can be done by usig CancelRouteWalk with vrf as
 * argument.
 * TODO - Do route cancellation for route walks when vrf walk is cancelled.
 *
 */

class AgentRouteWalker {
public:
    static const int kInvalidWalkCount = 0;
    typedef boost::function<void()> WalkDone;
    typedef boost::function<void(VrfEntry *)> RouteWalkDoneCb;
    enum WalkType {
        UNICAST,
        MULTICAST,
        ALL,
    };

    typedef std::map<uint32_t, DBTableWalker::WalkId> VrfRouteWalkerIdMap;
    typedef std::map<uint32_t, DBTableWalker::WalkId>::iterator VrfRouteWalkerIdMapIterator;

    AgentRouteWalker(Agent *agent, WalkType type);
    virtual ~AgentRouteWalker() { }

    void StartVrfWalk();
    void CancelVrfWalk();

    //Route table walk for specified VRF
    void StartRouteWalk(const VrfEntry *vrf);
    void CancelRouteWalk(const VrfEntry *vrf);

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

    virtual void VrfWalkDone(DBTableBase *part);
    virtual void RouteWalkDone(DBTableBase *part);

    void WalkDoneCallback(WalkDone cb);
    void RouteWalkDoneForVrfCallback(RouteWalkDoneCb cb);
    int walk_count() const {return walk_count_;}
    bool IsWalkCompleted() const {return (walk_count_ == 0);}
    Agent *agent() const {return agent_;}

private:
    void Callback(VrfEntry *vrf);
    void OnWalkComplete();
    void OnRouteTableWalkCompleteForVrf(VrfEntry *vrf);
    void DecrementWalkCount();
    void IncrementWalkCount() {walk_count_++;}

    Agent *agent_;
    AgentRouteWalker::WalkType walk_type_;    
    tbb::atomic<int> walk_count_;
    DBTableWalker::WalkId vrf_walkid_;
    VrfRouteWalkerIdMap route_walkid_[Agent::ROUTE_TABLE_MAX];
    WalkDone walk_done_cb_;
    RouteWalkDoneCb route_walk_done_for_vrf_cb_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalker);
};

#endif
