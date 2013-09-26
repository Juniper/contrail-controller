/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inet4_ucroute_hpp
#define vnsw_agent_inet4_ucroute_hpp

#include <net/address.h>
#include <base/dependency.h>
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


struct Inet4UcRouteKey : public Inet4RouteKey {
    Inet4UcRouteKey(const Peer *peer, const string &vrf_name,
            const Ip4Address &addr, uint8_t plen) :
        Inet4RouteKey(vrf_name, addr, plen), peer_(peer)
        {};
     virtual ~Inet4UcRouteKey() { };
     const Peer *peer_;
};

// Route for VM on a remote server
struct Inet4UcRemoteVmRoute : Inet4RouteData {
    Inet4UcRemoteVmRoute(const string &vrf_name, const Ip4Address &addr,
                         uint32_t label, const string &dest_vn_name,
                         TunnelType::TypeBmap bmap) :
        Inet4RouteData(Inet4RouteData::REMOTE_VM), server_vrf_(vrf_name),
        server_ip_(addr), tunnel_bmap_(bmap), label_(label),
        dest_vn_name_(dest_vn_name), sg_list_() { };
    virtual ~Inet4UcRemoteVmRoute() { };

    string server_vrf_;
    Ip4Address server_ip_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
};

// Route for VM on local server
struct Inet4UcLocalVmRoute : Inet4RouteData {
    Inet4UcLocalVmRoute(const VmPortInterfaceKey &intf, uint32_t label,
                        bool force_policy, const string &vn_name) :
        Inet4RouteData(Inet4RouteData::LOCAL_VM), intf_(intf),
        label_(label), force_policy_(force_policy), dest_vn_name_(vn_name),
        proxy_arp_(true), sg_list_() { };
    virtual ~Inet4UcLocalVmRoute() { };
    void DisableProxyArp() {proxy_arp_ = false;};

    VmPortInterfaceKey intf_;
    uint32_t label_;
    bool force_policy_;
    string dest_vn_name_;
    bool proxy_arp_;
    SecurityGroupList sg_list_;
};

// Route for receiving on local Agent
struct Inet4UcHostRoute : Inet4RouteData {
    Inet4UcHostRoute(const HostInterfaceKey &intf, const string &dest_vn_name) : 
        Inet4RouteData(Inet4RouteData::HOST), intf_(intf),
        dest_vn_name_(dest_vn_name), proxy_arp_(false) { };
    virtual ~Inet4UcHostRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};

    HostInterfaceKey intf_;
    string dest_vn_name_;
    bool proxy_arp_;
};

// Route for VLAN NH
struct Inet4UcVlanNhRoute : Inet4RouteData {
    Inet4UcVlanNhRoute(const VmPortInterfaceKey &intf, uint16_t tag,
                       uint32_t label, const string &dest_vn_name,
                       const SecurityGroupList &sg_list) :
        Inet4RouteData(Inet4RouteData::VLAN_NH), intf_(intf),
        tag_(tag), label_(label), dest_vn_name_(dest_vn_name), 
        sg_list_(sg_list) { };
    virtual ~Inet4UcVlanNhRoute() { };

    VmPortInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
};

//Route for subnet broadcast
struct Inet4UcSbcastRoute : Inet4RouteData {
    Inet4UcSbcastRoute(const Ip4Address &src_addr, const Ip4Address &grp_addr,
                       const string &vn_name) :
        Inet4RouteData(Inet4RouteData::SBCAST_ROUTE), 
        src_addr_(src_addr), grp_addr_(grp_addr),
        vn_name_(vn_name) { };
    virtual ~Inet4UcSbcastRoute() { };
    Ip4Address src_addr_;
    Ip4Address grp_addr_;
    string vn_name_;
};

//Route for subnet broadcast
struct Inet4UcEcmpRoute : Inet4RouteData {
    Inet4UcEcmpRoute(const Ip4Address &dest_addr, const string &vn_name, 
                     uint32_t label, bool local_ecmp_nh) : 
        Inet4RouteData(Inet4RouteData::ECMP_ROUTE), dest_addr_(dest_addr),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh) {
    };
    virtual ~Inet4UcEcmpRoute() { };

    Ip4Address dest_addr_;
    string vn_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
};


// Route for Host operating system
struct Inet4UcReceiveRoute : Inet4RouteData {
    Inet4UcReceiveRoute(const VirtualHostInterfaceKey &intf, bool policy,
                        const string &vn) : 
        Inet4RouteData(Inet4RouteData::RECEIVE_ROUTE), intf_(intf),
        policy_(policy ? true : false), proxy_arp_(false), vn_(vn),
        sg_list_() {};
    virtual ~Inet4UcReceiveRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};

    VirtualHostInterfaceKey intf_;
    bool policy_;
    bool proxy_arp_;
    string vn_;
    SecurityGroupList sg_list_;
};

// Data for indirect routes
struct Inet4UcGatewayRoute : Inet4RouteData {
    Inet4UcGatewayRoute(const Ip4Address gw_ip) :
        Inet4RouteData(Inet4RouteData:: GATEWAY_ROUTE), gw_ip_(gw_ip) { };
    virtual ~Inet4UcGatewayRoute() { };

    Ip4Address gw_ip_;
};

// Data for drop route
struct Inet4UcDropRoute : Inet4RouteData {
    Inet4UcDropRoute() :
        Inet4RouteData(Inet4RouteData::DROP_ROUTE) { };
    virtual ~Inet4UcDropRoute() { };
};

class Inet4UcRoute : public Inet4Route {
public:
    typedef DependencyList<Inet4UcRoute, Inet4UcRoute>::iterator iterator;
    typedef DependencyList<Inet4UcRoute, Inet4UcRoute>::const_iterator
        const_iterator;
    typedef DependencyList<NextHop, Inet4UcRoute>::iterator tunnel_nh_iterator;
    typedef DependencyList<NextHop, Inet4UcRoute>::const_iterator
        const_tunnel_nh_iterator;

    Inet4UcRoute(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen, RtType type);
    Inet4UcRoute(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen);
    virtual ~Inet4UcRoute() { };
    virtual bool IsLess(const DBEntry &rhs) const;
    virtual int CompareTo(const Route &rhs) const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    uint32_t GetMplsLabel() const { 
        return GetActivePath()->GetLabel();
    };

    const string &GetDestVnName() const { 
        return GetActivePath()->GetDestVnName();
    };

    const AgentPath *GetActivePath() const;
    const virtual NextHop *GetActiveNextHop() const;
    const Peer *GetActivePeer() const; 

    AgentPath *FindPath(const Peer *peer) const;

    static bool Inet4UcNotifyRouteEntryWalk(AgentXmppChannel *, DBState *state,
                                            bool subnet_only, bool associate,
                                            DBTablePartBase *part, 
                                            DBEntryBase *entry);
    iterator begin() { return gw_routes_.begin(); }
    iterator end() { return gw_routes_.end(); }
    const_iterator begin() const { return gw_routes_.begin(); }
    const_iterator end() const { return gw_routes_.end(); }
    bool gw_route_empty() { return gw_routes_.empty(); }
    bool HasUnresolvedPath(void);
    bool Sync(void);
    bool tunnel_nh_list_empty() { return tunnel_nh_list_.empty(); }
    void UpdateNH(void);
    void UpdateGatewayRoutes(void);
    virtual bool DBEntrySandesh(Sandesh *sresp) const;
    bool DBEntrySandesh(Sandesh *sresp, Ip4Address addr, uint8_t plen) const;

    class Rtkey {
    public:
        static std::size_t Length(Inet4Route *key) {
            return key->GetPlen();
        }
        static char ByteValue(Inet4Route *key, std::size_t i) {
            const Ip4Address::bytes_type &addr_bytes = key->GetIpAddress().to_bytes();
            return static_cast<char>(addr_bytes[i]);
        }
    };
private:
    friend class Inet4UcRouteTable;

    void InsertPath(const AgentPath *path);
    void RemovePath(const Peer *peer);
    Patricia::Node rtnode_;
    //LifetimeRef<Inet4Route> table_delete_ref_;
    // List of all gateway routes dependent on this route
    DEPENDENCY_LIST(Inet4UcRoute, Inet4UcRoute, gw_routes_);
    DEPENDENCY_LIST(NextHop, Inet4UcRoute, tunnel_nh_list_);
    DISALLOW_COPY_AND_ASSIGN(Inet4UcRoute);
};


class Inet4UcRouteTable : public Inet4RouteTable {
public:
    typedef set<const Inet4UcRoute *, RouteComparator> UnresolvedRouteTree;
    typedef set<const NextHop *, NHComparator> UnresolvedNHTree;
    typedef set<const Inet4UcRoute *, RouteComparator>::const_iterator const_rt_iterator;
    typedef set<const NextHop *, NHComparator> ::const_iterator const_nh_iterator;


    typedef Patricia::Tree<Inet4UcRoute, &Inet4UcRoute::rtnode_, Inet4UcRoute::Rtkey> RouteTree;
    Inet4UcRouteTable(DB *db, const std::string &name);
    virtual ~Inet4UcRouteTable();
    virtual void Input(DBTablePartition *root, DBClient *client,
                       DBRequest *req);
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    static bool PathSelection(const Path &path1, const Path &path2);
    void DeleteRoute(DBTablePartBase *part, Inet4UcRoute *entry,
                     const Peer *peer);
    bool DelPeerRoutes(DBTablePartBase *part, DBEntryBase *entry, Peer *peer);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static Inet4UcRouteTable *GetInstance() {return uc_route_table_;};
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                                const Ip4Address &addr, uint8_t plen);
    // Create Route to trap packets to agent
    static void AddHostRoute(const string &vrf_name, const Ip4Address &addr,
                             uint8_t plen, const std::string &dest_vn_name);
    // Create Route for a local VM
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label, bool force_policy);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf, 
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label,
                                const SecurityGroupList &sg_list_);
    // Create route pointing to VLAN NH
    static void AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                               const Ip4Address &addr, uint8_t plen,
                               const uuid &intf_uuid, uint16_t tag,
                               uint32_t label, const string &dest_vn_name,
                               const SecurityGroupList &sg_list_);

    // Create NH and Route for a Remote VM
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr, uint8_t plen,
                                 const Ip4Address &server_ip,
                                 TunnelType::TypeBmap bmap, uint32_t label,
                                 const string &dest_vn_name);
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr, uint8_t plen,
                                 const Ip4Address &server_ip, 
                                 TunnelType::TypeBmap bmap,uint32_t label,
                                 const string &dest_vn_name,
                                 const SecurityGroupList &sg_list_);

    //Create Composite NH and Route for a VM
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr, uint8_t plen,
                                 std::vector<ComponentNHData> comp_nh_list, 
                                 uint32_t label,
                                 const string &dest_vn_name, bool local_ecmp_nh);

    // Createmulticast route and NH 
    static void AddSubnetBroadcastRoute(const Peer *peer, const string &vrf_name, 
                                const Ip4Address &src_addr, 
                                const Ip4Address &grp_addr, 
                                const string &vn_name);

    // Create or delete NH and Route for a directly connected host 
    static void ArpRoute(DBRequest::DBOperation op, 
                         const Ip4Address &ip, const struct ether_addr &mac,
                         const string &vrf_name, const Interface &intf,
                         bool resolved, const uint8_t plen);

    static void AddArpReq(const string &vrf_name, const Ip4Address &ip);

    static void AddResolveRoute(const string &vrf_name, const Ip4Address &ip,
                                const uint8_t plen);

    static void AddVHostRecvRoute(const Peer *peer, const string &vrf_name,
                                  const string &interface_name,
                                  const Ip4Address &ip, uint8_t plen,
                                  const string &vn, bool policy);
    static void AddVHostRecvRoute(const string &vrf_name,
                                  const string &interface_name,
                                  const Ip4Address &ip,
                                  bool policy);
    static void AddVHostSubnetRecvRoute(const string &vrf_name,
                                        const string &interface_name,
                                        const Ip4Address &ip,
                                        uint8_t plen, bool policy);
    static void AddDropRoute(const string &vrf_name, const Ip4Address &ip,
                             uint8_t plen);
    static void DelVHostSubnetRecvRoute(const string &vrf_name,
                                        const Ip4Address &ip, uint8_t plen);
    static void AddVHostBcastRecvRoute(const string &vm_vrf);

    static void AddGatewayRoute(const Peer *peer, const string &vm_vrf, 
                                const Ip4Address &ip, uint8_t plen, 
                                const Ip4Address &gw_ip);
    static void RouteResyncReq(const string &vrf, const Ip4Address &ip, 
                               uint8_t plen);
    // Find a matching route for the IP address
    Inet4UcRoute *FindLPM(const Ip4Address &ip);
    static Inet4UcRoute *FindRoute(const string &vrf_name, const Ip4Address &ip);
    Inet4UcRoute *FindResolveRoute(const Ip4Address &ip);
    static Inet4UcRoute *FindResolveRoute(const string &vrf_name, const Ip4Address &ip);
    void AddUnresolvedRoute(const Inet4UcRoute *rt);
    void RemoveUnresolvedRoute(const Inet4UcRoute *rt);
    void EvaluateUnresolvedRoutes(void);
    void AddUnresolvedNH(const NextHop *);
    void RemoveUnresolvedNH(const NextHop *);
    void EvaluateUnresolvedNH(void);
    virtual bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry);
    void Inet4UcRouteTableWalkerNotify(VrfEntry *vrf, AgentXmppChannel *, DBState *,
                                       bool subnet_only, bool associate);

    void Inet4UcRouteNotifyDone(DBTableBase *base, DBState *);
    const_rt_iterator unresolved_route_begin() {
       return unresolved_rt_tree_.begin(); 
    };
    const_rt_iterator unresolved_route_end() {
        return unresolved_rt_tree_.end();
    };
    const_nh_iterator unresolved_nh_begin() {
        return unresolved_nh_tree_.begin();
    };
    const_nh_iterator unresolved_nh_end() {
        return unresolved_nh_tree_.end();
    };

    virtual Inet4Route *FindRoute(const Ip4Address &ip) { return FindLPM(ip); };
private:
    static Inet4UcRouteTable *uc_route_table_;
    UnresolvedRouteTree unresolved_rt_tree_;
    UnresolvedNHTree unresolved_nh_tree_;
    Patricia::Node rtnode_;
    RouteTree tree_; //LPM tree for unicast
    DBTableWalker::WalkId walkid_;
};

#define AGENT_ROUTE_LOG(oper, route, vrf, peer)\
do {\
    AgentRouteLog::Send("Agent", SandeshLevel::SYS_INFO, __FILE__, __LINE__,\
                   oper, route, vrf, (peer)? peer->GetName():" ");\
} while(false);\

#endif //vnsw_agent_inet4_ucroute_hpp
