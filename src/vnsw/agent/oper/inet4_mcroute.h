/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inet4_mc_route_hpp
#define vnsw_agent_inet4_mc_route_hpp

#include <net/address.h>
#include <base/lifetime.h>
#include <base/patricia.h>
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <route/table.h>

#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/vm_path.h>
#include <oper/peer.h>
#include <oper/agent_types.h>
#include <oper/inet4_route.h>


#define IS_BCAST_MCAST(grp)    ((grp.to_ulong() == 0xFFFFFFFF) || \
                               ((grp.to_ulong() & 0xF0000000) == 0xE0000000))

struct Inet4McRouteKey : public Inet4RouteKey {
    Inet4McRouteKey(const string &vrf_name,
            const Ip4Address &src, const Ip4Address &grp) :
        Inet4RouteKey(vrf_name,  grp), src_(src)
        {};
    Inet4McRouteKey(const string &vrf_name,
            const Ip4Address &grp, uint8_t plen) :
        Inet4RouteKey(vrf_name,  grp, plen) 
        {
            boost::system::error_code ec;
            src_ = IpAddress::from_string("0.0.0.0", ec).to_v4();
        };
     virtual ~Inet4McRouteKey() { };
    Ip4Address src_;
};

// Route for multicast
struct Inet4McastRoute : Inet4RouteData {
    Inet4McastRoute(const Ip4Address src_addr,
                        const Ip4Address grp_addr) :
        Inet4RouteData(Inet4RouteData::MCAST_ROUTE), grp_addr_(grp_addr),
        src_addr_(src_addr) { };
    virtual ~Inet4McastRoute() { };

    Ip4Address grp_addr_;
    Ip4Address src_addr_;
};

// Route for Host operating system
struct Inet4McReceiveRoute : Inet4RouteData {
    Inet4McReceiveRoute(const VirtualHostInterfaceKey &intf, bool policy) :
        Inet4RouteData(Inet4RouteData::RECEIVE_ROUTE), intf_(intf),
        policy_(policy ? true : false) { };
    virtual ~Inet4McReceiveRoute() { };

    VirtualHostInterfaceKey intf_;
    bool policy_;
};

class Inet4McRoute : public Inet4Route {
public:
    Inet4McRoute(VrfEntry *vrf, const Ip4Address &src, const Ip4Address &grp);
    virtual ~Inet4McRoute() { };
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual int CompareTo(const Route &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual std::string ToString() const;
    virtual Ip4Address GetSrcIpAddress() const;
    const virtual NextHop *GetActiveNextHop() const;
    static bool Inet4McNotifyRouteEntryWalk(AgentXmppChannel *, DBState *state, bool associate,
                                            DBTablePartBase *part, DBEntryBase *entry);
    virtual bool DBEntrySandesh(Sandesh *sresp) const;
private:
    friend class Inet4McRouteTable;
    Ip4Address src_;
    NextHopRef nh_;
    DISALLOW_COPY_AND_ASSIGN(Inet4McRoute);
};

class Inet4McRouteTable : public Inet4RouteTable {
public:
    Inet4McRouteTable(DB *db, const std::string &name);
    virtual ~Inet4McRouteTable();
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    // Createmulticast route and NH 
    static void AddV4MulticastRoute(const string &vrf_name,
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);

    static void DeleteV4MulticastRoute(const string &vrf_name, 
                                    const Ip4Address &src_addr,
                                    const Ip4Address &grp_addr);

    virtual bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry);
    //Find route for given vrf and (s,g)
    static void AddVHostRecvRoute(const string &vrf_name,const Ip4Address &ip,
                                  bool policy);
    static Inet4McRoute* FindRoute(const string &vrf_name,
                                 const Ip4Address &src, const Ip4Address &grp);
    static void DeleteReq(const string &vrf_name, const Ip4Address &addr);
    Inet4Route *FindRoute(const Ip4Address &src, const Ip4Address &grp) { 
        return FindExact(src, grp); 
    };

    virtual Inet4Route *FindRoute(const Ip4Address &grp) { 
        Ip4Address src(0);
        return FindExact(src, grp);
    };
    NextHop *GetMcNextHop(Inet4McRouteKey *key, Inet4RouteData *data);

    void Inet4McRouteTableWalkerNotify(VrfEntry *vrf, AgentXmppChannel *, 
                                       DBState *, bool associate);
    void Inet4McRouteNotifyDone(DBTableBase *base, DBState *state);
 
    void DeleteRoute(DBTablePartBase *part, Inet4McRoute *entry,
                     const Peer *peer);
    bool DelPeerRoutes(DBTablePartBase *part, DBEntryBase *entry, Peer *peer);
private:
    Inet4Route *FindExact(const Ip4Address &src, const Ip4Address &grp);
    NextHop *GetMcNextHop(Inet4McastRoute *key, Inet4RouteData *data);
    DBTableWalker::WalkId walkid_;
};


#endif //vnsw_agent_inet4_mc_route_hpp
