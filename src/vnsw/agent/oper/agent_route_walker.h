/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_walker_hpp
#define vnsw_agent_route_walker_hpp

#include <boost/intrusive_ptr.hpp>
#include <array>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <sandesh/sandesh_trace.h>

/**
 * The infrastructure is to support and manage VRF walks along with
 * corresponding route walks.
 * Following type of walks can be issued:
 * 1) ALL VRF walk - Use API StartVrfWalk()
 * 2) Specific VRF walk - Use StartRouteWalk(vrf_entry) API
 * 3) Specialized walk - Issue StartVrfWalk() and override VrfWalkNotify() to
 *    select a set of VRF to walk on. By default VrfWalkNotify starts walks on
 *    all route table.
 *    RouteWalkNotify() - This should be overriden to listen and act on route
 *    entry notifications. By default it ignores the request.
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
 * How to use the walker?
 *
 * Either walker can be derived or directly instantiated. Then the object needs
 * to be registered with agent route walker manager. Once the scope of walker is
 * over release of this walker should be done via agent routewalk manager.
 *
 * Walk References in walker:
 * Walker maintain two kind of references. First is the walk reference to walk
 * VRF table. This is part of walker itself.
 * Second is a set of references which is used to walk route tables.
 * These references are stored in state created on each vrf entry.
 * State is keyed with walker pointer and has an array of walk references.
 * Route walk references are stored in state because for each vrf there can be
 * different references and (vrf+walker) is the key to identify them.
 *
 * Agent Route Walk Manager
 * ------------------------
 *
 * Manager keeps a track of all walkers under a instrusive pointer list.
 * It has also maintained a listener on agent's vrf table. There is a state
 * maintained on each VRF which contains a map of walker to route table walker
 * references.
 * DB state is created by agent route walk manager and is unique for a vrf
 * entry. Each instance of AgentRouteWalker will insert its vrf_walk_ref in this
 * state and maintain route table references with walk tracker in same.
 *
 * On receiving vrf delete manager can refer to state and invoke release of all
 * walk references.
 *
 */

#define AGENT_DBWALK_TRACE_BUF "AgentDBwalkTrace"
extern SandeshTraceBufferPtr AgentDBwalkTraceBuf;

#define AGENT_DBWALK_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(AgentDBwalkTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (false)

class AgentRouteWalker;
class AgentRouteWalkerManager;
void intrusive_ptr_add_ref(AgentRouteWalker *w);
void intrusive_ptr_release(AgentRouteWalker *w);
typedef boost::intrusive_ptr<AgentRouteWalker> AgentRouteWalkerPtr;

struct RouteWalkerDBState : DBState {
    typedef std::array<DBTable::DBTableWalkRef, Agent::ROUTE_TABLE_MAX>
        RouteWalkRef;
    typedef std::map<AgentRouteWalkerPtr, RouteWalkRef> AgentRouteWalkerRefMap;
    typedef AgentRouteWalkerRefMap::iterator AgentRouteWalkerRefMapIter;
    typedef AgentRouteWalkerRefMap::const_iterator AgentRouteWalkerRefMapConstIter;

    RouteWalkerDBState();
    AgentRouteWalkerRefMap walker_ref_map_;
};

class AgentRouteWalker {
public:
    static const int kInvalidWalkCount = 0;
    typedef boost::function<void()> WalkDone;
    typedef boost::function<void(VrfEntry *)> RouteWalkDoneCb;
    typedef std::map<const VrfEntry *, tbb::atomic<int> > VrfRouteWalkCountMap;

    virtual ~AgentRouteWalker();

    void StartVrfWalk();
    //Route table walk for specified VRF
    void StartRouteWalk(VrfEntry *vrf);

    virtual bool VrfWalkNotify(DBTablePartBase *partition, DBEntryBase *e);
    virtual bool RouteWalkNotify(DBTablePartBase *partition, DBEntryBase *e);

    virtual void VrfWalkDone(DBTableBase *part);
    virtual void RouteWalkDone(DBTableBase *part);

    //Walk done callbacks
    void WalkDoneCallback(WalkDone cb);
    void RouteWalkDoneForVrfCallback(RouteWalkDoneCb cb);

    //Helpers
    int walk_count() const {return walk_count_;}
    bool IsWalkCompleted() const {return (walk_count_ == kInvalidWalkCount);}
    bool AreAllWalksDone() const;
    bool AreAllRouteWalksDone(const VrfEntry *vrf) const;
    bool IsRouteTableWalkCompleted(RouteWalkerDBState *state);
    AgentRouteWalkerManager *mgr() {return mgr_;}
    Agent *agent() const {return agent_;}
    uint32_t refcount() const { return refcount_; }

protected:
    friend class AgentRouteWalkerManager;
    friend void intrusive_ptr_add_ref(AgentRouteWalker *w);
    friend void intrusive_ptr_release(AgentRouteWalker *w);
    AgentRouteWalker(const std::string &name, Agent *agent);
    void set_mgr(AgentRouteWalkerManager *mgr) {mgr_ = mgr;}

private:
    void Callback(VrfEntry *vrf);
    void OnRouteTableWalkCompleteForVrf(VrfEntry *vrf);
    void DecrementWalkCount();
    void DecrementRouteWalkCount(const VrfEntry *vrf);
    void IncrementWalkCount() {walk_count_.fetch_and_increment();}
    void IncrementRouteWalkCount(const VrfEntry *vrf);
    void WalkTable(AgentRouteTable *table,
                   DBTable::DBTableWalkRef &route_table_walk_ref);
    DBTable::DBTableWalkRef AllocateRouteTableReferences(AgentRouteTable *table);
    void VrfWalkDoneInternal(DBTableBase *part);
    void RouteWalkDoneInternal(DBTableBase *part, AgentRouteWalkerPtr ptr);
    DBTable::DBTableWalkRef LocateRouteTableWalkRef(const VrfEntry *vrf,
                                                    RouteWalkerDBState *state,
                                                    AgentRouteTable *table);
    RouteWalkerDBState *LocateRouteWalkerDBState(VrfEntry *vrf);
    DBTable::DBTableWalkRef &vrf_walk_ref() {
        return vrf_walk_ref_;
    }
    DBTable::DBTableWalkRef &delete_walk_ref() {
        return delete_walk_ref_;
    }
    //Walk to release all references.
    void ReleaseVrfWalkReference();
    bool Deregister(DBTablePartBase *partition, DBEntryBase *e);
    static void DeregisterDone(AgentRouteWalkerPtr walker);

    Agent *agent_;
    std::string name_;
    VrfRouteWalkCountMap route_walk_count_;
    tbb::atomic<int> walk_count_;
    WalkDone walk_done_cb_;
    RouteWalkDoneCb route_walk_done_for_vrf_cb_;
    DBTable::DBTableWalkRef vrf_walk_ref_;
    AgentRouteWalkerManager *mgr_;
    DBTable::DBTableWalkRef delete_walk_ref_;
    mutable tbb::atomic<uint32_t> refcount_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalker);
};

class AgentRouteWalkerManager {
public:
    typedef std::set<AgentRouteWalkerPtr> WalkRefList;
    typedef std::set<AgentRouteWalkerPtr>::iterator WalkRefListIter;

    AgentRouteWalkerManager(Agent *agent);
    virtual ~AgentRouteWalkerManager();
    Agent *agent() {return agent_;}

    void RegisterWalker(AgentRouteWalker *walker);
    void ReleaseWalker(AgentRouteWalker *walker);
    void Shutdown();
    void TryUnregister();
    //UT helper
    uint8_t walk_ref_list_size() const {return walk_ref_list_.size();}

protected:
    friend class AgentRouteWalker;
    void VrfNotify(DBTablePartBase *partition, DBEntryBase *e);
    void RemoveWalker(AgentRouteWalkerPtr walker);
    void ValidateAgentRouteWalker(AgentRouteWalkerPtr walker) const;
    RouteWalkerDBState *CreateState(VrfEntry *vrf);
    void RemoveWalkReferencesInVrf(RouteWalkerDBState *state, VrfEntry *vrf);
    DBTable::ListenerId vrf_listener_id() const {
        return vrf_listener_id_;
    }

private:
    DBTable::ListenerId vrf_listener_id_;
    Agent *agent_;
    WalkRefList walk_ref_list_;
    bool marked_for_deletion_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteWalkerManager);
};

#endif
