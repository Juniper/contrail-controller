/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inet4_route_hpp
#define vnsw_agent_inet4_route_hpp

#include <net/address.h>
#include <base/lifetime.h>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <route/table.h>

#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/vm_path.h>
#include <oper/peer.h>
#include <oper/agent_types.h>
#include <controller/controller_peer.h>
#include <sandesh/sandesh_trace.h>

class VrfExport;
class TunnelNH;
class LifetimeActor;
class LifetimeManager;
class RouteTableWalkerState;

struct Inet4RouteKey : public AgentKey {
    Inet4RouteKey(const string &vrf_name,
                  const Ip4Address &addr, uint8_t plen) : 
        AgentKey(), vrf_name_(vrf_name), addr_(addr), plen_(plen)
         { };
    Inet4RouteKey(const string &vrf_name,
                  const Ip4Address &addr) : 
        AgentKey(), vrf_name_(vrf_name), addr_(addr)
         { plen_ = 32; };
    virtual ~Inet4RouteKey() { };

    string vrf_name_;
    Ip4Address addr_;
    uint8_t plen_;
};

struct Inet4RouteData : public AgentData {
    enum Type {
        LOCAL_VM,       // Route to VM on local server
        REMOTE_VM,      // Route to VM on remote server
        VLAN_NH,        // Route pointing to VLAN NH
        HOST,           // Route to trap packets to AGENT on local server
        ARP_ROUTE,      // Physical subnet routes
        GATEWAY_ROUTE,  // Routes reachable via directly connected route 
        RESOLVE_ROUTE,  // Subnet match route, used to trigger ARP resolve
        RECEIVE_ROUTE,  // Routes for packets to Host OS
        SBCAST_ROUTE,   // Routes for packets to subnet broadcast
        MCAST_ROUTE,    // Routes for multicast streams
        ECMP_ROUTE,     // Route pointing to ECMP nh
        DROP_ROUTE      // Discard route
    };

    enum Op {
        CHANGE,
        RESYNC,
    };

    Inet4RouteData(Type type) : AgentData(), type_(type), op_(CHANGE) { };
    Inet4RouteData(Op op): op_(op) { };
    virtual ~Inet4RouteData() { };

    Type type_;
    Op op_;
};

class Inet4Route : public Route {
public:
    enum Trace {
        ADD,
        DELETE,
        ADD_PATH,
        DELETE_PATH,
        CHANGE_PATH,
    };
        
    enum RtType {
        INET4_UCAST,
        INET4_SBCAST,
        INET4_MCAST
    };

    Inet4Route(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen, RtType type);
    Inet4Route(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen);
    Inet4Route(VrfEntry *vrf, const Ip4Address &addr, RtType type);
    virtual ~Inet4Route() { };

    virtual bool IsLess(const DBEntry &rhs) const = 0;
    virtual int CompareTo(const Route &rhs) const = 0;

    virtual KeyPtr GetDBRequestKey() const = 0;
    virtual void SetKey(const DBRequestKey *key)  = 0;
    virtual std::string ToString() const;
    void SetVrf(VrfEntryRef vrf) ;
    void SetAddr(Ip4Address addr) ;
    void SetPlen(uint8_t plen);

    uint32_t GetVrfId() const;
    const VrfEntry *GetVrfEntry() const {return vrf_.get();};
    const Ip4Address &GetIpAddress() const {return addr_;};
    int GetPlen() const {return plen_;};
    virtual Ip4Address GetSrcIpAddress() const; 
    const virtual NextHop *GetActiveNextHop() const = 0;
    virtual bool DBEntrySandesh(Sandesh *sresp) const = 0;
    void FillTrace(RouteInfo &route, Trace event, const AgentPath *path);

    bool IsMcast() const {
        if (rt_type_ == INET4_MCAST) {
            return true;
        }
        return false;
    }

    bool IsSbcast() const {
        if (rt_type_ == INET4_SBCAST) {
            return true;
        }
        return false;
    }

    bool IsUcast() const {
        if (rt_type_ == INET4_UCAST) {
            return true;
        }
        return false;
    }

private:
    friend class Inet4RouteTable;

    VrfEntryRef vrf_;
    Ip4Address addr_;
    uint8_t plen_;
    uint32_t rt_type_;
    DISALLOW_COPY_AND_ASSIGN(Inet4Route);
};

class RouteComparator {
public:
    bool operator() (const Inet4Route *rt1, const Inet4Route *rt2) {
        return rt1->IsLess(*rt2);
    }
};

class NHComparator {
public:
    bool operator() (const NextHop *nh1, const NextHop *nh2) {
        return nh1->IsLess(*nh2);
    }
};

class Inet4RouteTable : public RouteTable {
public:
    Inet4RouteTable(DB *db, const std::string &name);
    virtual ~Inet4RouteTable();

    // Input function overrides DBTable::Input
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req) = 0;

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const = 0;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};


    NextHop *FindNextHop(NextHopKey *k) const;
    VrfEntry *FindVrfEntry(const string &vrf_name) const;
    void SetVrfEntry(VrfEntryRef vrf) ;
    void SetVrfDeleteRef(LifetimeActor *delete_ref) ;

    LifetimeActor *deleter(); 
    void ManagedDelete();
    bool DelWalkerCb(DBTablePartBase *part, DBEntryBase *entry);
    virtual bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry) = 0;
    void DeleteRouteDone(DBTableBase *base, RouteTableWalkerState *state);
    void DeleteAllRoutes();
    void MayResumeDelete(bool is_empty);
    virtual Inet4Route *FindRoute(const Ip4Address &ip) = 0;
    string GetVrfName() { return vrf_entry_->GetName();};
    virtual Inet4Route *FindActiveEntry(const Inet4Route *key);
    virtual Inet4Route *FindActiveEntry(const Inet4RouteKey *key);
private:
    class DeleteActor;
    void Inet4RouteNotifyDone(DBTableBase *base, Inet4Route *route);
    DB *db_;
    VrfEntryRef vrf_entry_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<Inet4RouteTable> vrf_delete_ref_;
    DISALLOW_COPY_AND_ASSIGN(Inet4RouteTable);
};

class RouteTableWalkerState {
public:
    RouteTableWalkerState(LifetimeActor *actor): rt_delete_ref_(this, actor) {
    }
    ~RouteTableWalkerState() {
        rt_delete_ref_.Reset(NULL);
    }
    void ManagedDelete() { };
private:
    LifetimeRef<RouteTableWalkerState> rt_delete_ref_;
};

#define AGENT_DBWALK_TRACE_BUF "AgentDBwalkTrace"

extern SandeshTraceBufferPtr AgentDBwalkTraceBuf;

#define AGENT_DBWALK_TRACE(obj, ...) do {                                  \
    obj::TraceMsg(AgentDBwalkTraceBuf, __FILE__, __LINE__, ##__VA_ARGS__); \
} while (0);

#endif // vnsw_agent_inet4_route_hpp
