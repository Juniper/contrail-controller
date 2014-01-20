/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_route_hpp
#define vnsw_agent_route_hpp

#include <sys/types.h>
#include <sys/socket.h>
#include <net/ethernet.h>
#include <net/address.h>
#include <netinet/ether.h>
#include <base/lifetime.h>
#include <base/patricia.h>
#include <base/task_annotations.h>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <route/table.h>

#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/mpls.h>
#include <oper/agent_types.h>
#include <oper/multicast.h>
#include <controller/controller_peer.h>
#include <sandesh/sandesh_trace.h>
#include <oper/route_types.h>

//Route entry in route table related classes

class AgentRoute : public Route {
public:
    enum Trace {
        ADD,
        DELETE,
        ADD_PATH,
        DELETE_PATH,
        CHANGE_PATH,
    };

    AgentRoute(VrfEntry *vrf, bool is_multicast) : Route(), vrf_(vrf), 
        is_multicast_(is_multicast) { };
    virtual ~AgentRoute() { };

    //TODO Rename iterator as gw_route-ITERATOR
    typedef DependencyList<AgentRoute, AgentRoute>::iterator iterator;
    typedef DependencyList<AgentRoute, AgentRoute>::const_iterator
        const_iterator;
    typedef DependencyList<NextHop, AgentRoute>::iterator tunnel_nh_iterator;
    typedef DependencyList<NextHop, AgentRoute>::const_iterator
        const_tunnel_nh_iterator;

    virtual int CompareTo(const Route &rhs) const = 0;
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual KeyPtr GetDBRequestKey() const = 0;
    virtual void SetKey(const DBRequestKey *key) = 0;
    virtual string ToString() const = 0;
    virtual bool DBEntrySandesh(Sandesh *sresp) const = 0;
    //TODO coment why routeresyncreq - rename to reevaluatepath
    virtual void RouteResyncReq() const = 0;
    virtual const string GetAddressString() const = 0;
    virtual AgentRouteTableAPIS::TableType GetTableType() const = 0;

    void FillTrace(RouteInfo &route, Trace event, const AgentPath *path);

    //TODO Move dependantroutes and nh  to inet4
    void UpdateDependantRoutes();// analogous to updategatewayroutes
    void UpdateNH();
    bool IsMulticast() const {return is_multicast_;};
    //TODO remove setvrf
    void SetVrf(VrfEntryRef vrf) { vrf_ = vrf; };
    uint32_t GetVrfId() const;
    VrfEntry *GetVrfEntry() const {return vrf_.get();};
    bool Sync(void);
    const AgentPath *GetActivePath() const;
    const NextHop *GetActiveNextHop() const; 
    const Peer *GetActivePeer() const;
    AgentPath *FindPath(const Peer *peer) const;
    bool HasUnresolvedPath();
    uint32_t GetMplsLabel() const; 
    const string &GetDestVnName() const;
    bool CanDissociate() const;

    iterator begin() { return dependant_routes_.begin(); };
    iterator end() { return dependant_routes_.end(); };
    const_iterator begin() const { return dependant_routes_.begin(); };
    const_iterator end() const { return dependant_routes_.end(); };
    bool IsDependantRouteEmpty() { return dependant_routes_.empty(); };
    bool IsTunnelNHListEmpty() { return tunnel_nh_list_.empty(); };

private:
    friend class AgentRouteTable;

    void RemovePath(const Peer *peer);
    void InsertPath(const AgentPath *path);
    bool SyncPath(AgentPath *path);

    VrfEntryRef vrf_;
    bool is_multicast_;
    DEPENDENCY_LIST(AgentRoute, AgentRoute, dependant_routes_);
    DEPENDENCY_LIST(NextHop, AgentRoute, tunnel_nh_list_);
    DISALLOW_COPY_AND_ASSIGN(AgentRoute);
};

class RouteComparator {
public:
    bool operator() (const AgentRoute *rt1, const AgentRoute *rt2) {
        return rt1->IsLess(*rt2);
    }
};

struct NHComparator {
    bool operator() (const NextHop *nh1, const NextHop *nh2) {
        return nh1->IsLess(*nh2);
    }
};

struct RouteTableWalkerState {
    RouteTableWalkerState(LifetimeActor *actor): rt_delete_ref_(this, actor) {
    }
    ~RouteTableWalkerState() {
        rt_delete_ref_.Reset(NULL);
    }
    void ManagedDelete() { };
    LifetimeRef<RouteTableWalkerState> rt_delete_ref_;
};

class AgentRouteTable : public RouteTable {
public:
    typedef set<const AgentRoute *, RouteComparator> UnresolvedRouteTree;
    typedef set<const NextHop *, NHComparator> UnresolvedNHTree;
    typedef set<const AgentRoute *, RouteComparator>::const_iterator 
        const_rt_iterator;
    typedef set<const NextHop *, NHComparator>::const_iterator const_nh_iterator;

    AgentRouteTable(DB *db, const std::string &name);
    virtual ~AgentRouteTable();

    //TODO reorganize the functions below
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual void ProcessDelete(AgentRoute *rt) { };
    virtual void ProcessAdd(AgentRoute *rt) { };
    //Notify walk is done on per peer basis for exporting route
    virtual void RouteTableWalkerNotify(VrfEntry *vrf, AgentXmppChannel *, 
                                        DBState *, bool associate,
                                        bool unicast_walk, bool multicast_walk);
    //Rebake walk is to modify routes
    void RouteTableWalkerRebake(VrfEntry *vrf, bool unicast_walk, 
                                bool multicast_walk);
    bool RebakeRouteEntryWalk(bool unicast_walk, bool multicast_walk,
                              DBTablePartBase *part, 
                              DBEntryBase *entry); 
    void RebakeRouteEntryWalkDone(DBTableBase *part,
                                  bool unicast_walk, 
                                  bool multicast_walk); 
    virtual AgentRouteTableAPIS::TableType GetTableType() const = 0;
    virtual string GetTableName() const = 0;

    //TODO Evaluate pushing walks to controller
    bool NotifyRouteEntryWalk(AgentXmppChannel *, 
                              DBState *state, 
                              bool associate,
                              bool unicast_walk,
                              bool multicast_walk,
                              DBTablePartBase *part,
                              DBEntryBase *entry);
    void UnicastRouteNotifyDone(DBTableBase *base, DBState *, Peer *);
    void MulticastRouteNotifyDone(DBTableBase *base, DBState *, Peer *);

    void AddUnresolvedRoute(const AgentRoute *rt);
    void RemoveUnresolvedRoute(const AgentRoute *rt);
    void EvaluateUnresolvedRoutes(void);
    void AddUnresolvedNH(const NextHop *);
    void RemoveUnresolvedNH(const NextHop *);
    void EvaluateUnresolvedNH(void);
    AgentRoute *FindActiveEntry(const RouteKey *key);
    NextHop *FindNextHop(NextHopKey *key) const;
    bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry);

    const_rt_iterator unresolved_route_begin() { 
        return unresolved_rt_tree_.begin(); };
    const_rt_iterator unresolved_route_end() { 
        return unresolved_rt_tree_.end(); };
    int unresolved_route_size() { return unresolved_rt_tree_.size(); }

    const_nh_iterator unresolved_nh_begin() { 
        return unresolved_nh_tree_.begin(); };
    const_nh_iterator unresolved_nh_end() { 
        return unresolved_nh_tree_.end(); };
    bool DelPeerRoutes(DBTablePartBase *part, DBEntryBase *entry, Peer *peer);

    VrfEntry *FindVrfEntry(const string &vrf_name) const;
    void SetVrfEntry(VrfEntryRef vrf);
    void SetVrfDeleteRef(LifetimeActor *delete_ref);
    string GetVrfName() { return vrf_entry_->GetName();};

    LifetimeActor *deleter();
    void ManagedDelete();
    bool DelExplicitRouteWalkerCb(DBTablePartBase *part, DBEntryBase *entry);
    void DeleteRouteDone(DBTableBase *base, RouteTableWalkerState *state);
    void DeleteAllLocalVmRoutes();
    void DeleteAllPeerRoutes();
    void MayResumeDelete(bool is_empty);

    static bool PathSelection(const Path &path1, const Path &path2);
    void Process(DBRequest &req) {
        CHECK_CONCURRENCY("db::DBTable");
        DBTablePartition *tpart =
            static_cast<DBTablePartition *>(GetTablePartition(req.key.get()));
        tpart->Process(NULL, &req);
    };
private:
    class DeleteActor;
    void Input(DBTablePartition *part, DBClient *client, DBRequest *req);
    void DeleteRoute(DBTablePartBase *part, AgentRoute *rt, 
                     const Peer *peer);
    UnresolvedRouteTree unresolved_rt_tree_;
    UnresolvedNHTree unresolved_nh_tree_;
    DBTableWalker::WalkId walkid_;
    DB *db_;
    VrfEntryRef vrf_entry_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<AgentRouteTable> vrf_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(AgentRouteTable);
};

struct RouteKey : public AgentKey {
    RouteKey(const Peer *peer, const string &vrf_name) : AgentKey(), 
    peer_(peer), vrf_name_(vrf_name) { };
    virtual ~RouteKey() { };

    virtual AgentRouteTable *GetRouteTableFromVrf(VrfEntry *vrf) = 0; 
    virtual AgentRouteTableAPIS::TableType GetRouteTableType() = 0;
    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, 
                                        bool is_multicast) const = 0;
    virtual string ToString() const = 0;

    const string &GetVrfName() const { return vrf_name_; };
    const Peer *GetPeer() const { return peer_; };

    const Peer *peer_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(RouteKey);
};

//Route data related classes

struct RouteData : public AgentData {
public:
    enum Op {
        CHANGE,
        RESYNC,
    };
    RouteData(Op op, bool is_multicast) : op_(op), 
        is_multicast_(is_multicast) { }; 
    virtual ~RouteData() { };
    virtual string ToString() const = 0;
    virtual bool AddChangePath(AgentPath *path) = 0;
    bool IsMulticast() {return is_multicast_;};

    Op GetOp() const { return op_; };

private:
    Op op_;
    bool is_multicast_;
    DISALLOW_COPY_AND_ASSIGN(RouteData);
};

#define AGENT_DBWALK_TRACE_BUF "AgentDBwalkTrace"

extern SandeshTraceBufferPtr AgentDBwalkTraceBuf;

#define AGENT_DBWALK_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(AgentDBwalkTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0);

#define AGENT_ROUTE_LOG(oper, route, vrf, peer)\
do {\
    AgentRouteLog::Send("Agent", SandeshLevel::SYS_INFO, __FILE__, __LINE__,\
                   oper, route, vrf, (peer)? peer->GetName():" ");\
} while(false);\

#endif
