/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_walker_hpp
#define vnsw_agent_route_walker_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

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

struct AgentRouteWalkerQueueEntry {
    enum RequestType {
        START_VRF_WALK,
        CANCEL_VRF_WALK,
        START_ROUTE_WALK,
        CANCEL_ROUTE_WALK,
        DELETE,
        DONE_WALK
    };

    AgentRouteWalkerQueueEntry(VrfEntry *vrf, RequestType type,
                               bool all_walks_done) :
        vrf_ref_(vrf, this), type_(type), all_walks_done_(all_walks_done) { }
    virtual ~AgentRouteWalkerQueueEntry() { }

    VrfEntryRef vrf_ref_;
    RequestType type_;
    bool all_walks_done_;
};

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
    virtual ~AgentRouteWalker();

    void StartVrfWalk();
    void CancelVrfWalk();

    //Route table walk for specified VRF
    void StartRouteWalk(VrfEntry *vrf);
    void CancelRouteWalk(VrfEntry *vrf);

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

    virtual void VrfWalkDone(DBTableBase *part);
    virtual void RouteWalkDone(DBTableBase *part);

    void WalkDoneCallback(WalkDone cb);
    void RouteWalkDoneForVrfCallback(RouteWalkDoneCb cb);
    int queued_walk_done_count() const {return queued_walk_done_count_;}
    bool AllWalkDoneDequeued() const {return (queued_walk_done_count_ ==
                                              kInvalidWalkCount);}
    int queued_walk_count() const {return queued_walk_count_;}
    bool AllWalksDequeued() const {return (queued_walk_count_ ==
                                           kInvalidWalkCount);}
    int walk_count() const {return walk_count_;}
    bool IsWalkCompleted() const {return (walk_count_ == kInvalidWalkCount);}
    //Callback for start of a walk issued from Agent::RouteWalker
    //task context.
    virtual bool RouteWalker(boost::shared_ptr<AgentRouteWalkerQueueEntry> data);
    bool AreAllWalksDone() const;
    Agent *agent() const {return agent_;}
    void set_walkable_route_tables(uint32_t walkable_route_tables) {
        walkable_route_tables_ = walkable_route_tables;
    }
    uint32_t walkable_route_tables() const {return walkable_route_tables_;}
    void Delete();

private:
    void StartVrfWalkInternal();
    void CancelVrfWalkInternal();
    void StartRouteWalkInternal(const VrfEntry * vrf);
    void CancelRouteWalkInternal(const VrfEntry *vrf);
    void DeleteInternal();

    void Callback(VrfEntry *vrf);
    void CallbackInternal(VrfEntry *vrf, bool all_walks_done);
    void OnWalkComplete();
    void OnRouteTableWalkCompleteForVrf(VrfEntry *vrf);
    void DecrementWalkCount();
    void IncrementWalkCount() {walk_count_.fetch_and_increment();}
    void IncrementQueuedWalkCount() {queued_walk_count_.fetch_and_increment();}
    void DecrementQueuedWalkCount();
    void IncrementQueuedWalkDoneCount() {queued_walk_done_count_.fetch_and_increment();}
    void DecrementQueuedWalkDoneCount();

    Agent *agent_;
    AgentRouteWalker::WalkType walk_type_;    
    tbb::atomic<int> queued_walk_count_;
    tbb::atomic<int> queued_walk_done_count_;
    tbb::atomic<int> walk_count_;
    DBTableWalker::WalkId vrf_walkid_;
    VrfRouteWalkerIdMap route_walkid_[Agent::ROUTE_TABLE_MAX];
    WalkDone walk_done_cb_;
    RouteWalkDoneCb route_walk_done_for_vrf_cb_;
    //work queue(Agent::RouteWalker) is used for starting/cancelling
    //walks. This task is in exclusion with dbtable and Controller
    //which makes sure that walk-done and cancel walk do not get executed in
    //parallel. Both these calls modify walkid.
    WorkQueue<boost::shared_ptr<AgentRouteWalkerQueueEntry> > work_queue_;
    uint32_t walkable_route_tables_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalker);
};

class AgentRouteWalkerCleaner {
public:
    typedef std::vector<AgentRouteWalker *> List;
    typedef List::iterator ListIter;

    AgentRouteWalkerCleaner(Agent *agent);
    virtual ~AgentRouteWalkerCleaner();
    void FreeWalker(AgentRouteWalker *walker);

private:
    bool Free();

    std::vector<AgentRouteWalker *> walker_;
    boost::scoped_ptr<TaskTrigger> trigger_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalkerCleaner);
};

#endif
