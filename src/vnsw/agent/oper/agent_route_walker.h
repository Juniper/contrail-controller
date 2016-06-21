/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_walker_hpp
#define vnsw_agent_route_walker_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <sandesh/sandesh_trace.h>

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
 * Multiple objects of this class can have separate parallel walks.
 * There is no more walk cancellations to start a new walk.
 * Same walk reference can be used to restart walks with different context.
 * Each AgentRouteWalker instance will have its own walker reference on vrf
 * table. Walk on this table will result in more walk references created for
 * route tables. These references are stored in DB State keyed with Vrf walk
 * reference.
 *
 * DB state is created by agent route walk manager and is unique for a vrf
 * entry. Each instance of AgentRouteWalker will insert its vrf_walk_ref in this
 * state and maintain route table references with walk tracker in same.
 *
 */

#define AGENT_DBWALK_TRACE_BUF "AgentDBwalkTrace"
extern SandeshTraceBufferPtr AgentDBwalkTraceBuf;

#define AGENT_DBWALK_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(AgentDBwalkTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0);

struct RouteWalkerDBState : DBState {
    typedef std::vector<DBTable::DBTableWalkRef> RouteTableWalkRefList;
    typedef std::map<DBTable::DBTableWalkRef,
            RouteTableWalkRefList> VrfWalkRefMap;

    RouteWalkerDBState();
    VrfWalkRefMap vrf_walk_ref_map_;
};

class AgentRouteWalkerManager {
public:
    AgentRouteWalkerManager(Agent *agent);
    virtual ~AgentRouteWalkerManager();
    void RemoveWalkReferencesInVrf(VrfEntry *vrf);
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    DBTable::ListenerId vrf_listener_id() const {
        return vrf_listener_id_;
    }
    //Walk to release all references.
    static bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e,
                              DBTable::DBTableWalkRef vrf_walk_ref,
                              const Agent *agent,
                              DBTable::ListenerId vrf_listener_id);
    static void VrfWalkDone(DBTable::DBTableWalkRef walker_ref,
                            DBTableBase *part,
                            DBTable::DBTableWalkRef vrf_walk_ref,
                            DBTable::ListenerId vrf_listener_id);

private:
    DBTable::ListenerId vrf_listener_id_;
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalkerManager);
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

    AgentRouteWalker(Agent *agent, WalkType type,
                     const std::string &name);
    virtual ~AgentRouteWalker();

    void StartVrfWalk();

    //Route table walk for specified VRF
    void StartRouteWalk(VrfEntry *vrf);

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

    virtual void VrfWalkDone(DBTableBase *part);
    virtual void RouteWalkDone(DBTableBase *part);

    void WalkDoneCallback(WalkDone cb);
    void RouteWalkDoneForVrfCallback(RouteWalkDoneCb cb);
    int walk_count() const {return walk_count_;}
    bool IsWalkCompleted() const {return (walk_count_ == kInvalidWalkCount);}
    bool AreAllWalksDone() const;
    Agent *agent() const {return agent_;}
    void set_walkable_route_tables(uint32_t walkable_route_tables) {
        walkable_route_tables_ = walkable_route_tables;
    }
    uint32_t walkable_route_tables() const {return walkable_route_tables_;}
    DBTable::DBTableWalkRef GetRouteTableWalkRef(const VrfEntry *vrf,
                                                 RouteWalkerDBState *state,
                                                 AgentRouteTable *table);
    RouteWalkerDBState *GetRouteWalkerDBState(const VrfEntry *vrf);
    bool IsRouteTableWalkCompleted(RouteWalkerDBState *state) const;

private:

    void Callback(VrfEntry *vrf);
    void OnRouteTableWalkCompleteForVrf(VrfEntry *vrf);
    void DecrementWalkCount();
    void IncrementWalkCount() {walk_count_.fetch_and_increment();}

    Agent *agent_;
    std::string name_;
    AgentRouteWalker::WalkType walk_type_;    
    tbb::atomic<int> walk_count_;
    WalkDone walk_done_cb_;
    RouteWalkDoneCb route_walk_done_for_vrf_cb_;
    uint32_t walkable_route_tables_;
    DBTable::DBTableWalkRef vrf_walk_ref_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalker);
};

#endif
