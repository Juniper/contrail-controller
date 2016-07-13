/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_hpp
#define vnsw_agent_route_hpp

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/address.h>

#include <base/lifetime.h>
#include <base/patricia.h>
#include <base/task_annotations.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <agent_types.h>
#include <route/route.h>
#include <route/table.h>

#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/agent_types.h>
#include <oper/multicast.h>
#include <controller/controller_peer.h>
#include <sandesh/sandesh_trace.h>

class AgentRoute;
class AgentPath;
class Peer;

struct AgentRouteKey : public AgentKey {
    AgentRouteKey(const Peer *peer, const std::string &vrf_name) : 
        AgentKey(), peer_(peer), vrf_name_(vrf_name) { }
    virtual ~AgentRouteKey() { }

    virtual Agent::RouteTableType GetRouteTableType() = 0;
    virtual std::string ToString() const = 0;
    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf,
                                        bool is_multicast) const = 0;
    virtual AgentRouteKey *Clone() const = 0;

    const std::string &vrf_name() const { return vrf_name_; }
    const Peer *peer() const { return peer_.get(); }
    void set_peer(const Peer *peer) {peer_ = peer;}

    PeerConstPtr peer_;
    std::string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteKey);
};

struct AgentRouteData : public AgentData {
    enum Type {
        ADD_DEL_CHANGE,
        ROUTE_PREFERENCE_CHANGE,
        IPAM_SUBNET,
    };
    AgentRouteData(bool is_multicast) : type_(ADD_DEL_CHANGE),
    is_multicast_(is_multicast) { }
    AgentRouteData(Type type, bool is_multicast):
        type_(type), is_multicast_(is_multicast) { }
    virtual ~AgentRouteData() { }

    virtual std::string ToString() const = 0;
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) = 0;
    virtual bool UpdateRoute(AgentRoute *rt) {return false;}

    bool is_multicast() const {return is_multicast_;}

    Type type_;
    bool is_multicast_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteData);
};

struct RouteComparator {
    bool operator() (const AgentRoute *rt1, const AgentRoute *rt2);
};

struct NHComparator {
    bool operator() (const NextHop *nh1, const NextHop *nh2);
};

struct RouteTableWalkerState {
    RouteTableWalkerState(LifetimeActor *actor) : rt_delete_ref_(this, actor) {
    }

    ~RouteTableWalkerState() {
        rt_delete_ref_.Reset(NULL);
    }
    void ManagedDelete() { }

    LifetimeRef<RouteTableWalkerState> rt_delete_ref_;
};

// Agent implements multiple route tables - inet4-unicast, inet4-multicast, 
// bridge. This base class contains common code for all route tables
class AgentRouteTable : public RouteTable {
public:
    static const int kPartitionCount = 1;
    typedef std::set<const AgentRoute *, RouteComparator> UnresolvedRouteTree;
    typedef std::set<const NextHop *, NHComparator> UnresolvedNHTree;

    AgentRouteTable(DB *db, const std::string &name);
    virtual ~AgentRouteTable();

    virtual int PartitionCount() const { return kPartitionCount; }
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;}
    virtual size_t Hash(const DBRequestKey *key) const {return 0;}

    virtual Agent::RouteTableType GetTableType() const = 0;
    virtual std::string GetTableName() const = 0;

    virtual void ProcessDelete(AgentRoute *rt) { }
    virtual void ProcessAdd(AgentRoute *rt) { }
    //Entry notification
    virtual void NotifyEntry(AgentRoute *entry);
    //Can be used for operations related to updation of route.
    virtual void UpdateDerivedRoutes(AgentRoute *entry,
                                     const AgentPath *path,
                                     bool active_path_changed) { }
    //Can be used for operations resulting from deletion of route.
    virtual void PreRouteDelete(AgentRoute *entry) { }

    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context) {
        return AgentSandeshPtr();
    }
    virtual SandeshTraceBufferPtr GetOperDBTraceBuf() const {return OperDBTraceBuf;}
    // Unresolved route tree accessors
    UnresolvedRouteTree::const_iterator unresolved_route_begin() const {
        return unresolved_rt_tree_.begin();
    }
    UnresolvedRouteTree::const_iterator unresolved_route_end() const {
        return unresolved_rt_tree_.end();
    }
    int unresolved_route_size() const { return unresolved_rt_tree_.size(); }

    // Unresolved NH tree accessors
    void AddUnresolvedNH(const NextHop *);
    void RemoveUnresolvedNH(const NextHop *);
    void EvaluateUnresolvedNH(void);
    UnresolvedNHTree::const_iterator unresolved_nh_begin() const {
        return unresolved_nh_tree_.begin();
    }
    UnresolvedNHTree::const_iterator unresolved_nh_end() const {
        return unresolved_nh_tree_.end();
    }

    Agent *agent() const { return agent_; }
    const std::string &vrf_name() const;
    uint32_t vrf_id() const {return vrf_id_;}
    VrfEntry *vrf_entry() const;
    AgentRoute *FindActiveEntry(const AgentRouteKey *key);
    AgentRoute *FindActiveEntryNoLock(const AgentRouteKey *key);
    AgentRoute *FindActiveEntry(const AgentRoute *key);
    AgentRoute *FindActiveEntryNoLock(const AgentRoute *key);

    // Set VRF for the route-table
    void SetVrf(VrfEntry * vrf);

    // Helper functions to delete routes
    bool DeleteAllBgpPath(DBTablePartBase *part, DBEntryBase *entry);
    bool DelExplicitRouteWalkerCb(DBTablePartBase *part, DBEntryBase *entry);

    // Lifetime actor routines
    LifetimeActor *deleter();
    void ManagedDelete();
    virtual void RetryDelete();

    // Process DBRequest inline
    void Process(DBRequest &req);

    // Path comparator
    static bool PathSelection(const Path &path1, const Path &path2);
    static const std::string &GetSuffix(Agent::RouteTableType type);
    void DeletePathFromPeer(DBTablePartBase *part, AgentRoute *rt,
                            AgentPath *path);
                            //const Peer *peer);
    //Stale path handling
    void SquashStalePaths(AgentRoute *rt, const AgentPath *path);
    void EvaluateUnresolvedRoutes(void);

private:
    class DeleteActor;
    void AddUnresolvedRoute(const AgentRoute *rt);
    void RemoveUnresolvedRoute(const AgentRoute *rt);
    void DeleteRouteDone(DBTableBase *base, RouteTableWalkerState *state);

    void Input(DBTablePartition *part, DBClient *client, DBRequest *req);

    Agent *agent_;
    UnresolvedRouteTree unresolved_rt_tree_;
    UnresolvedNHTree unresolved_nh_tree_;
    VrfEntryRef vrf_entry_;
    // VRF is stored to identify which VRF this table belonged to
    // in case lifetimeactor has reset the vrf_. 
    uint32_t vrf_id_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<AgentRouteTable> vrf_delete_ref_;
    SandeshTraceBufferPtr OperDBTraceBuf;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteTable);
};

#define OPER_TRACE_ROUTE(obj, ...)\
do {\
   Oper##obj::TraceMsg(GetOperDBTraceBuf(), __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#define OPER_TRACE_ROUTE_ENTRY(obj, table, ...)\
do {\
   Oper##obj::TraceMsg(table->GetOperDBTraceBuf(),\
                       __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

// Base class for all Route entries in agent
class AgentRoute : public Route {
public:
    enum Trace {
        ADD,
        DELETE,
        ADD_PATH,
        DELETE_PATH,
        CHANGE_PATH,
        STALE_PATH,
    };

    typedef DependencyList<AgentRoute, AgentRoute> RouteDependencyList;
    typedef DependencyList<NextHop, AgentRoute> TunnelNhDependencyList;

    AgentRoute(VrfEntry *vrf, bool is_multicast) :
        Route(), vrf_(vrf), is_multicast_(is_multicast) { }
    virtual ~AgentRoute() { }

    // Virtual functions from base DBEntry
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const = 0;
    virtual void SetKey(const DBRequestKey *key) = 0;

    // Virtual functions defined by AgentRoute
    virtual int CompareTo(const Route &rhs) const = 0;
    virtual Agent::RouteTableType GetTableType() const = 0;
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const = 0;
    virtual std::string ToString() const = 0;
    virtual const std::string GetAddressString() const = 0;
    virtual bool ReComputePathDeletion(AgentPath *path) {return false;}
    virtual bool ReComputePathAdd(AgentPath *path) {return false;}
    virtual uint32_t GetActiveLabel() const;
    virtual AgentPath *FindPathUsingKeyData(const AgentRouteKey *key,
                                            const AgentRouteData *data) const;
    virtual AgentPath *FindPath(const Peer *peer) const;
    virtual void DeletePathUsingKeyData(const AgentRouteKey *key,
                                        const AgentRouteData *data,
                                        bool force_delete);
    virtual bool RecomputeRoutePath(Agent *agent,
                             DBTablePartition *part,
                             AgentPath *path,
                             AgentRouteData *data) {return false;}

    // Accessor functions
    bool is_multicast() const {return is_multicast_;}
    VrfEntry *vrf() const {return vrf_;}
    uint32_t vrf_id() const;

    AgentPath *FindLocalVmPortPath() const;
    AgentPath *FindStalePath() const;
    const AgentPath *GetActivePath() const;
    const NextHop *GetActiveNextHop() const; 
    const std::string &dest_vn_name() const;
    bool IsRPFInvalid() const;

    void EnqueueRouteResync() const;
    void ResyncTunnelNextHop();
    bool HasUnresolvedPath();
    bool Sync(void);

    //TODO Move dependantroutes and nh  to inet4
    void UpdateDependantRoutes();// analogous to updategatewayroutes
    bool IsDependantRouteEmpty() { return dependant_routes_.empty(); }
    bool IsTunnelNHListEmpty() { return tunnel_nh_list_.empty(); }

    void FillTrace(RouteInfo &route, Trace event, const AgentPath *path);
    bool WaitForTraffic() const;
    virtual uint8_t plen() const { return 0; }

protected:
    void SetVrf(VrfEntry *vrf) { vrf_ = vrf; }
    void RemovePathInternal(AgentPath *path);
    void RemovePath(AgentPath *path);
    void InsertPath(const AgentPath *path);
    void DeletePathInternal(AgentPath *path);

private:
    friend class AgentRouteTable;
    bool ProcessPath(Agent *agent,
                     DBTablePartition *part,
                     AgentPath *path,
                     AgentRouteData *data);

    VrfEntry *vrf_;
    // Unicast table can contain routes for few multicast address 
    // (ex. subnet multicast). Flag to specify if this is multicast route
    bool is_multicast_;
    DEPENDENCY_LIST(AgentRoute, AgentRoute, dependant_routes_);
    DEPENDENCY_LIST(NextHop, AgentRoute, tunnel_nh_list_);
    DISALLOW_COPY_AND_ASSIGN(AgentRoute);
};

#define AGENT_DBWALK_TRACE_BUF "AgentDBwalkTrace"

extern SandeshTraceBufferPtr AgentDBwalkTraceBuf;

#define AGENT_DBWALK_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(AgentDBwalkTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0);

#define GETPEERNAME(peer) (peer)? peer->GetName() : ""
#define AGENT_ROUTE_LOG(table, msg, route, vrf, peer_info)\
do {\
    AgentRouteLog::TraceMsg(table->GetOperDBTraceBuf(), __FILE__, __LINE__,\
                            msg, route, vrf, peer_info);\
} while(false);\

#endif
