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
#include <cmn/agent_cmn.h>
#include <route/route.h>
#include <route/table.h>

#include <oper/interface.h>
#include <oper/nexthop.h>
#include <oper/peer.h>
#include <oper/mpls.h>
#include <oper/agent_types.h>
#include <controller/controller_peer.h>
#include <sandesh/sandesh_trace.h>
#include <oper/route_types.h>

//Route entry in route table related classes

class RouteEntry : public Route {
public:
    enum Trace {
        ADD,
        DELETE,
        ADD_PATH,
        DELETE_PATH,
        CHANGE_PATH,
    };

    //TODO Remove false in is_multicast and let caller specify
    RouteEntry(VrfEntry *vrf) : Route(), vrf_(vrf), is_multicast_(false) { };
    virtual ~RouteEntry() { };

    //TODO Rename iterator as gw_route-ITERATOR
    typedef DependencyList<RouteEntry, RouteEntry>::iterator iterator;
    typedef DependencyList<RouteEntry, RouteEntry>::const_iterator
        const_iterator;
    typedef DependencyList<NextHop, RouteEntry>::iterator tunnel_nh_iterator;
    typedef DependencyList<NextHop, RouteEntry>::const_iterator
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
    bool IsMulticast() const {return is_multicast_;};;
    //TODO Move this to key and non changeable
    void SetMulticast(bool is_mcast) {is_multicast_ = is_mcast;};
    //TODO remove setvrf
    void SetVrf(VrfEntryRef vrf) { vrf_ = vrf; };
    uint32_t GetVrfId() const;
    const VrfEntry *GetVrfEntry() const {return vrf_.get();};
    bool Sync(void);
    const AgentPath *GetActivePath() const;
    const NextHop *GetActiveNextHop() const; 
    const Peer *GetActivePeer() const;
    AgentPath *FindPath(const Peer *peer) const;
    bool HasUnresolvedPath();
    uint32_t GetMplsLabel() const; 
    const string &GetDestVnName() const;
    //TODO rename this canunsubscribe
    bool CanBeDeleted() const;

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
    DEPENDENCY_LIST(RouteEntry, RouteEntry, dependant_routes_);
    DEPENDENCY_LIST(NextHop, RouteEntry, tunnel_nh_list_);
    DISALLOW_COPY_AND_ASSIGN(RouteEntry);
};

class RouteComparator {
public:
    bool operator() (const RouteEntry *rt1, const RouteEntry *rt2) {
        return rt1->IsLess(*rt2);
    }
};

class NHComparator {
public:
    bool operator() (const NextHop *nh1, const NextHop *nh2) {
        return nh1->IsLess(*nh2);
    }
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

class AgentRouteTable : public RouteTable {
public:
    typedef set<const RouteEntry *, RouteComparator> UnresolvedRouteTree;
    typedef set<const NextHop *, NHComparator> UnresolvedNHTree;
    typedef set<const RouteEntry *, RouteComparator>::const_iterator 
        const_rt_iterator;
    typedef set<const NextHop *, NHComparator>::const_iterator const_nh_iterator;

    AgentRouteTable(DB *db, const std::string &name);
    virtual ~AgentRouteTable();

    //TODO reorganize the functions below
    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual void ProcessDelete(RouteEntry *rt) { };
    virtual void ProcessAdd(RouteEntry *rt) { };
    virtual void RouteTableWalkerNotify(VrfEntry *vrf, AgentXmppChannel *, 
                                        DBState *, bool associate,
                                        bool unicast_walk, bool multicast_walk);
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

    void AddUnresolvedRoute(const RouteEntry *rt);
    void RemoveUnresolvedRoute(const RouteEntry *rt);
    void EvaluateUnresolvedRoutes(void);
    void AddUnresolvedNH(const NextHop *);
    void RemoveUnresolvedNH(const NextHop *);
    void EvaluateUnresolvedNH(void);
    RouteEntry *FindActiveEntry(const RouteKey *key);
    NextHop *FindNextHop(NextHopKey *key) const;
    bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry);

    const_rt_iterator unresolved_route_begin() { 
        return unresolved_rt_tree_.begin(); };
    const_rt_iterator unresolved_route_end() { 
        return unresolved_rt_tree_.end(); };
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
    bool DelWalkerCb(DBTablePartBase *part, DBEntryBase *entry);
    void DeleteRouteDone(DBTableBase *base, RouteTableWalkerState *state);
    void DeleteAllRoutes();
    void MayResumeDelete(bool is_empty);

    static bool PathSelection(const Path &path1, const Path &path2);

private:
    class DeleteActor;
    void Input(DBTablePartition *part, DBClient *client, DBRequest *req);
    void DeleteRoute(DBTablePartBase *part, RouteEntry *rt, 
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

// Path info for every route entry

class AgentPath : public Path {
public:
    AgentPath(const Peer *peer, RouteEntry *rt) : 
        Path(), peer_(peer), nh_(NULL), label_(MplsTable::kInvalidLabel),
        dest_vn_name_(""), unresolved_(true), sync_(false), vrf_name_(""), 
        dependant_rt_(rt), proxy_arp_(false), force_policy_(false),
        tunnel_bmap_(0), interfacenh_flags_(0) { };
    virtual ~AgentPath() { };

    const Peer *GetPeer() const {return peer_;};
    const NextHop *GetNextHop() const; 
    uint32_t GetLabel() const {return label_;};
    int GetTunnelBmap() const {return tunnel_bmap_;};
    TunnelType::Type GetTunnelType() const {
        return TunnelType::ComputeType(tunnel_bmap_);
    };
    const string &GetDestVnName() const {return dest_vn_name_;};
    const Ip4Address& GetGatewayIp() const {return gw_ip_;};
    const string &GetVrfName() const {return vrf_name_;};
    bool GetProxyArp() const {return proxy_arp_;};
    bool GetForcePolicy() const {return force_policy_;};
    const bool IsUnresolved() const {return unresolved_;};
    uint8_t GetInterfaceNHFlags() const {return interfacenh_flags_;};
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;};  

    void SetLabel(uint32_t label) {label_ = label;};
    void SetDestVnName(const string &dest_vn) {dest_vn_name_ = dest_vn;};
    void SetUnresolved(bool unresolved) {unresolved_ = unresolved;};
    void SetGatewayIp(const Ip4Address &addr) {gw_ip_ = addr;};
    void SetProxyArp(bool proxy_arp) {proxy_arp_ = proxy_arp;};
    void SetForcePolicy(bool force_policy) {force_policy_ = force_policy;};
    void SetVrfName(const string &vrf_name) {vrf_name_ = vrf_name;};
    void SetTunnelBmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;};
    void SetInterfaceNHFlags(uint8_t flags) {interfacenh_flags_ = flags;};
    void SetSecurityGroupList(SecurityGroupList &sg) {sg_list_ = sg;};

    void ClearSecurityGroupList() { sg_list_.clear(); }
    void ResetDependantRoute(RouteEntry *rt) {dependant_rt_.reset(rt);};

    bool ChangeNH(NextHop *nh);
    bool Sync(RouteEntry *sync_route); //vm_path sync
    void SyncRoute(bool sync) {sync_ = sync;};
    bool RouteNeedsSync() {return sync_;};
    virtual std::string ToString() const { return "AgentPath"; };

private:
    const Peer *peer_;
    NextHopRef nh_;
    uint32_t label_;
    string dest_vn_name_;
    // Points to gateway route, if this path is part of
    // indirect route
    bool unresolved_;
    bool sync_;
    string vrf_name_;
    Ip4Address gw_ip_;
    DependencyRef<RouteEntry, RouteEntry> dependant_rt_;
    bool proxy_arp_;
    bool force_policy_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint8_t interfacenh_flags_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

// Route Key related classes 

class RouteKey : public AgentKey {
public:
    RouteKey(const Peer *peer, const string &vrf_name) : AgentKey(), 
    peer_(peer), vrf_name_(vrf_name) { };
    virtual ~RouteKey() { };

    virtual AgentRouteTable *GetRouteTableFromVrf(VrfEntry *vrf) = 0; 
    virtual AgentRouteTableAPIS::TableType GetRouteTableType() = 0;
    virtual RouteEntry *AllocRouteEntry(VrfEntry *vrf) const = 0;

    const string &GetVrfName() const { return vrf_name_; };
    const Peer *GetPeer() const { return peer_; };
    virtual string ToString() const = 0;
private:
    const Peer *peer_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(RouteKey);
};

//Route data related classes

class RouteData : public AgentData {
public:
    enum Op {
        CHANGE,
        RESYNC,
    };
    RouteData(Op op) : op_(op), is_mcast_(false) { };
    RouteData(Op op, bool is_mcast) : op_(op), is_mcast_(is_mcast) { };
    virtual ~RouteData() { };
    virtual string ToString() const = 0;

    virtual bool AddChangePath(AgentPath *path) = 0;

    bool IsMulticast() const {return is_mcast_;};
    void SetMulticast(bool is_mcast) {is_mcast_ = is_mcast;};
    Op GetOp() const { return op_; };

private:
    Op op_;
    bool is_mcast_;
    DISALLOW_COPY_AND_ASSIGN(RouteData);
};

class ResolveRoute : public RouteData {
public:
    ResolveRoute(Op op  = RouteData::CHANGE) : RouteData(op) { };
    virtual ~ResolveRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "Resolve";};;
private:
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public RouteData {
public:
    LocalVmRoute(const VmPortInterfaceKey &intf, uint32_t label,
                 int tunnel_bmap, bool force_policy, const string &vn_name,
                 uint8_t flags, const SecurityGroupList &sg_list,
                 Op op = RouteData::CHANGE) :
        RouteData(op), intf_(intf), label_(label), tunnel_bmap_(tunnel_bmap),
        force_policy_(force_policy), dest_vn_name_(vn_name),
        proxy_arp_(true), sync_route_(false), 
        flags_(flags), sg_list_(sg_list) { };
    virtual ~LocalVmRoute() { };
    void DisableProxyArp() {proxy_arp_ = false;};
    virtual string ToString() const {return "local VM";};;
    virtual bool AddChangePath(AgentPath *path);
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;}; 

private:
    VmPortInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool force_policy_;
    string dest_vn_name_;
    bool proxy_arp_;
    bool sync_route_;
    uint8_t flags_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class RemoteVmRoute : public RouteData {
public:
    RemoteVmRoute(const string &vrf_name, const Ip4Address &addr,
                  uint32_t label, const string &dest_vn_name,
                  int bmap, const SecurityGroupList &sg_list,
                  Op op = RouteData::CHANGE) :
        RouteData(op), server_vrf_(vrf_name),
        server_ip_(addr), tunnel_bmap_(bmap), 
        label_(label), dest_vn_name_(dest_vn_name), sg_list_(sg_list) { };
    virtual ~RemoteVmRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "remote VM";};;
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;}; 

private:
    string server_vrf_;
    Ip4Address server_ip_;
    int tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(RemoteVmRoute);
};

class HostRoute : public RouteData {
public:
    HostRoute(const HostInterfaceKey &intf, const string &dest_vn_name,
              Op op  = RouteData::CHANGE) : 
        RouteData(op), intf_(intf),
        dest_vn_name_(dest_vn_name), proxy_arp_(false) { };
    virtual ~HostRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "host";};;

private:
    HostInterfaceKey intf_;
    string dest_vn_name_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class VlanNhRoute : public RouteData {
public:
    VlanNhRoute(const VmPortInterfaceKey &intf, uint16_t tag, uint32_t label,
                const string &dest_vn_name, const SecurityGroupList &sg_list,
                Op op  = RouteData::CHANGE) :
        RouteData(op), intf_(intf),
        tag_(tag), label_(label), dest_vn_name_(dest_vn_name), 
        sg_list_(sg_list) { };
    virtual ~VlanNhRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "vlannh";};;

private:
    VmPortInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoute : public RouteData {
public:
    MulticastRoute(const Ip4Address &src_addr, 
                   const Ip4Address &grp_addr,
                   const string &vn_name, const string &vrf_name,
                   COMPOSITETYPE type,
                   Op op  = RouteData::CHANGE) :
        RouteData(op, true), 
        src_addr_(src_addr), grp_addr_(grp_addr),
        vn_name_(vn_name), vrf_name_(vrf_name),
        comp_type_(type) { };
    virtual ~MulticastRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "multicast";};;

private:
    Ip4Address src_addr_;
    Ip4Address grp_addr_;
    string vn_name_;
    string vrf_name_;
    COMPOSITETYPE comp_type_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class ReceiveRoute : public RouteData {
public:
    ReceiveRoute(const VirtualHostInterfaceKey &intf, uint32_t label,
                 uint32_t tunnel_bmap, bool policy, const string &vn,
                 Op op  = RouteData::CHANGE) : 
        RouteData(op), intf_(intf), label_(label), tunnel_bmap_(tunnel_bmap),
        policy_(policy), proxy_arp_(false), vn_(vn), sg_list_() {};
    virtual ~ReceiveRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "receive";};;

private:
    VirtualHostInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool policy_;
    bool proxy_arp_;
    string vn_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveRoute);
};

class Inet4UnicastEcmpRoute : public RouteData {
public:
    Inet4UnicastEcmpRoute(const Ip4Address &dest_addr, 
                          const string &vn_name, 
                          uint32_t label, bool local_ecmp_nh, 
                          const string &vrf_name,
                          Op op  = RouteData::CHANGE) : 
        RouteData(op), dest_addr_(dest_addr),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name) {
        };
    virtual ~Inet4UnicastEcmpRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "inet4 ecmp";};;

private:
    Ip4Address dest_addr_;
    string vn_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastEcmpRoute);
};


class Inet4UnicastArpRoute : public RouteData {
public:
    Inet4UnicastArpRoute(const string &vrf_name, 
                         const Ip4Address &addr,
                         Op op  = RouteData::CHANGE) :
        RouteData(op), vrf_name_(vrf_name), addr_(addr) { };
    virtual ~Inet4UnicastArpRoute() { };

    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "arp";};;
private:
    string vrf_name_;
    Ip4Address addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public RouteData {
public:
    Inet4UnicastGatewayRoute(const Ip4Address &gw_ip, 
                             const string &vrf_name,
                             Op op  = RouteData::CHANGE) :
        RouteData(op), gw_ip_(gw_ip), vrf_name_(vrf_name) { };
    virtual ~Inet4UnicastGatewayRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "gateway";};;

private:
    Ip4Address gw_ip_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public RouteData {
public:
    DropRoute(Op op  = RouteData::CHANGE) :
        RouteData(op) { };
    virtual ~DropRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "drop";};;
private:
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};

//////////////////////////////////////////////////////////////////
//  UNICAST INET4
/////////////////////////////////////////////////////////////////
//TODO Remove this, flatten out
class Inet4AgentRouteTable : public AgentRouteTable {
public:
    enum Type {
        UNICAST,
        MULTICAST,
    };
    Inet4AgentRouteTable(Type type, DB *db, const std::string &name) : 
        AgentRouteTable(db, name), type_(type) { };
    virtual ~Inet4AgentRouteTable() { };
    virtual void ProcessDelete(RouteEntry *rt) { };
    virtual void ProcessAdd(RouteEntry *rt) { };
    virtual string GetTableName() const {return "Inet4AgentRouteTable";};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::INET4_UNICAST;};
    Type GetInetRouteType() { return type_; };

private:
    Type type_;
    DISALLOW_COPY_AND_ASSIGN(Inet4AgentRouteTable);
};

class Inet4UnicastRouteEntry : public RouteEntry {
public:
    Inet4UnicastRouteEntry(VrfEntry *vrf, const Ip4Address &addr) : 
        RouteEntry(vrf), addr_(addr) { 
            plen_ = 32; 
        };
    Inet4UnicastRouteEntry(VrfEntry *vrf, 
                           const Ip4Address &addr, uint8_t plen) : 
        RouteEntry(vrf), addr_(addr), plen_(plen) { 
            addr_ = boost::asio::ip::address_v4(addr.to_ulong() & 
                                                (plen ? (0xFFFFFFFF << (32 - plen)) : 0));
        };
    virtual ~Inet4UnicastRouteEntry() { };

    virtual int CompareTo(const Route &rhs) const;
    virtual string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual void RouteResyncReq() const;
    virtual bool DBEntrySandesh(Sandesh *sresp) const;
    virtual const string GetAddressString() const {return addr_.to_string();};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::INET4_UNICAST;};

    bool DBEntrySandesh(Sandesh *sresp, 
                        Ip4Address addr,
                        uint8_t plen) const;

    const Ip4Address &GetIpAddress() const {return addr_;};
    int GetPlen() const {return plen_;};
    void SetAddr(Ip4Address addr) { addr_ = addr; };
    void SetPlen(int plen) { plen_ = plen; };
    //Key for patricia node lookup 
    class Rtkey {
      public:
          static std::size_t Length(RouteEntry *key) {
              Inet4UnicastRouteEntry *uckey =
                  static_cast<Inet4UnicastRouteEntry *>(key);
              return uckey->GetPlen();
          }
          static char ByteValue(RouteEntry *key, std::size_t i) {
              Inet4UnicastRouteEntry *uckey =
                  static_cast<Inet4UnicastRouteEntry *>(key);
              const Ip4Address::bytes_type &addr_bytes = 
                  uckey->GetIpAddress().to_bytes();
              return static_cast<char>(addr_bytes[i]);
          }
    };

private:
    friend class Inet4UnicastAgentRouteTable;

    Ip4Address addr_;
    uint8_t plen_;
    Patricia::Node rtnode_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastRouteEntry);
};

class Inet4UnicastAgentRouteTable : public Inet4AgentRouteTable {
public:
    typedef Patricia::Tree<Inet4UnicastRouteEntry, &Inet4UnicastRouteEntry::rtnode_, 
            Inet4UnicastRouteEntry::Rtkey> Inet4RouteTree;

    Inet4UnicastAgentRouteTable(DB *db, const std::string &name) :
        Inet4AgentRouteTable(Inet4AgentRouteTable::UNICAST, db, name), 
        walkid_(DBTableWalker::kInvalidWalkerId) { };
    virtual ~Inet4UnicastAgentRouteTable() { };

    Inet4UnicastRouteEntry *FindLPM(const Ip4Address &ip);
    virtual string GetTableName() const {return "Inet4UnicastAgentRouteTable";};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::INET4_UNICAST;};
    virtual void ProcessAdd(RouteEntry *rt) { 
        tree_.Insert(static_cast<Inet4UnicastRouteEntry *>(rt));
    };
    virtual void ProcessDelete(RouteEntry *rt) { 
        tree_.Remove(static_cast<Inet4UnicastRouteEntry *>(rt));
    };
    Inet4UnicastRouteEntry *FindRoute(const Ip4Address &ip) { 
        return FindLPM(ip); };

    Inet4UnicastRouteEntry *FindResolveRoute(const Ip4Address &ip);
    static Inet4UnicastRouteEntry *FindResolveRoute(const string &vrf_name, 
                                                    const Ip4Address &ip);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static Inet4UnicastRouteEntry *FindRoute(const string &vrf_name, 
                                             const Ip4Address &ip);
    static void RouteResyncReq(const string &vrf_name, 
                               const Ip4Address &ip, uint8_t plen);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const Ip4Address &addr, uint8_t plen);
    static void AddHostRoute(const string &vrf_name,
                             const Ip4Address &addr, uint8_t plen,
                             const std::string &dest_vn_name);
    static void AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                               const Ip4Address &addr, uint8_t plen,
                               const uuid &intf_uuid, uint16_t tag,
                               uint32_t label, const string &dest_vn_name,
                               const SecurityGroupList &sg_list_);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid,
                                const string &vn_name,
                                uint32_t label, bool force_policy); 
    static void AddSubnetBroadcastRoute(const Peer *peer, 
                                        const string &vrf_name,
                                        const Ip4Address &src_addr, 
                                        const Ip4Address &grp_addr,
                                        const string &vn_name); 
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf, 
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label, 
                                const SecurityGroupList &sg_list_);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label);
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr,uint8_t plen,
                                 const Ip4Address &server_ip,
                                 TunnelType::TypeBmap bmap, uint32_t label,
                                 const string &dest_vn_name); 
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr, uint8_t plen,
                                 const Ip4Address &server_ip, 
                                 TunnelType::TypeBmap bmap,uint32_t label,
                                 const string &dest_vn_name,
                                 const SecurityGroupList &sg_list_);
    static void AddRemoteVmRoute(const Peer *peer, const string &vm_vrf,
                                 const Ip4Address &vm_addr,uint8_t plen,
                                 std::vector<ComponentNHData> comp_nh_list,
                                 uint32_t label,
                                 const string &dest_vn_name, 
                                 bool local_ecmp_nh);
    static void AddArpReq(const string &vrf_name, const Ip4Address &ip); 
    static void ArpRoute(DBRequest::DBOperation op, 
                         const Ip4Address &ip, 
                         const struct ether_addr &mac,
                         const string &vrf_name, 
                         const Interface &intf,
                         bool resolved,
                         const uint8_t plen);
    static void AddResolveRoute(const string &vrf_name, 
                                const Ip4Address &ip, 
                                const uint8_t plen); 
    static void AddVHostInterfaceRoute(const Peer *peer, const string &vm_vrf,
                                       const Ip4Address &addr, uint8_t plen,
                                       const string &interface, uint32_t label,
                                       const string &vn_name);
    static void AddVHostRecvRoute(const Peer *peer,
                                  const string &vm_vrf,
                                  const string &interface_name,
                                  const Ip4Address &addr,
                                  uint8_t plen,
                                  const string &vn,
                                  bool policy);
    static void AddVHostRecvRoute(const string &vm_vrf,
                                  const string &interface_name,
                                  const Ip4Address &addr,
                                  bool policy);
    static void AddVHostRecvRoute(const string &vm_vrf,
                                  const string &interface_name,
                                  const Ip4Address &addr,
                                  const string &vn,
                                  bool policy);
    static void AddVHostSubnetRecvRoute(const string &vm_vrf, 
                                        const string &interface_name,
                                        const Ip4Address &addr, uint8_t plen, 
                                        bool policy);
    static void AddDropRoute(const string &vm_vrf,
                             const Ip4Address &addr, uint8_t plen);
    static void DelVHostSubnetRecvRoute(const string &vm_vrf, 
                                        const Ip4Address &addr, uint8_t plen);
    static void AddGatewayRoute(const Peer *peer, const string &vrf_name,
                                const Ip4Address &dst_addr,uint8_t plen,
                                const Ip4Address &gw_ip);

private:
    Inet4RouteTree tree_;
    Patricia::Node rtnode_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastAgentRouteTable);
};

class Inet4UnicastRouteKey : public RouteKey {
public:
    Inet4UnicastRouteKey(const Peer *peer, const string &vrf_name, 
                         const Ip4Address &dip, 
                         uint8_t plen) : RouteKey(peer, vrf_name), dip_(dip), 
                         plen_(plen) { };
    virtual ~Inet4UnicastRouteKey() { };
    //Called from oute creation in input of route table
    virtual RouteEntry *AllocRouteEntry(VrfEntry *vrf) const;
    //Enqueue add/chg/delete for route
    virtual AgentRouteTable *GetRouteTableFromVrf(VrfEntry *vrf) { 
        return (static_cast<AgentRouteTable *>(vrf->
                         GetRouteTable(AgentRouteTableAPIS::INET4_UNICAST)));
    };
    virtual AgentRouteTableAPIS::TableType GetRouteTableType() {
       return AgentRouteTableAPIS::INET4_UNICAST;
    }; 
    virtual string ToString() const { return ("Inet4UnicastRouteKey"); };

    const Ip4Address &GetAddress() const {return dip_;};
    const uint8_t &GetPlen() const {return plen_;};

private:
    Ip4Address dip_;
    uint8_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastRouteKey);
};

//////////////////////////////////////////////////////////////////
//  MULTICAST INET4
/////////////////////////////////////////////////////////////////
class Inet4MulticastAgentRouteTable : public Inet4AgentRouteTable {
public:
    Inet4MulticastAgentRouteTable(DB *db, const std::string &name) :
        Inet4AgentRouteTable(Inet4AgentRouteTable::MULTICAST, db, name),
        walkid_(DBTableWalker::kInvalidWalkerId) { };
    virtual ~Inet4MulticastAgentRouteTable() { };
    virtual bool DelExplicitRoute(DBTablePartBase *part, DBEntryBase *entry) { 
        return true; };
    //Nexthop will be stored in path as lcoalvmpeer peer so that it falls in line
    //Override virtual routines for no action w.r.t. multicast
    virtual string GetTableName() const {return "Inet4MulticastAgentRouteTable";};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::INET4_MULTICAST;};

    void McRtRouteNotifyDone(DBTableBase *base, DBState *);
    void AddVHostRecvRoute(const string &vm_vrf,
                           const string &interface_name,
                           const Ip4Address &addr,
                           bool policy);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static Inet4MulticastRouteEntry *FindRoute(const string &vrf_name, 
                                               const Ip4Address &ip, 
                                               const Ip4Address &dip);
    static void RouteResyncReq(const string &vrf_name, 
                               const Ip4Address &sip, 
                               const Ip4Address &dip);
    static void AddMulticastRoute(const string &vrf_name, 
                                  const string &vn_name,
                                  const Ip4Address &src_addr,
                                  const Ip4Address &grp_addr); 
    static void DeleteMulticastRoute(const string &vrf_name, 
                                     const Ip4Address &src_addr,
                                     const Ip4Address &grp_addr); 

private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastAgentRouteTable);
};

class Inet4MulticastRouteEntry : public RouteEntry {
public:
    Inet4MulticastRouteEntry(VrfEntry *vrf, const Ip4Address &dst, 
                             const Ip4Address &src) :
        RouteEntry(vrf), dst_addr_(dst), src_addr_(src) { }; 
    virtual ~Inet4MulticastRouteEntry() { };

    virtual int CompareTo(const Route &rhs) const;
    virtual string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual void RouteResyncReq() const;
    virtual const string GetAddressString() const {
        return dst_addr_.to_string();};
    virtual bool DBEntrySandesh(Sandesh *sresp) const;
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::INET4_MULTICAST;};;

    void SetDstIpAddress(const Ip4Address &dst) {dst_addr_ = dst;};
    void SetSrcIpAddress(const Ip4Address &src) {src_addr_ = src;};
    const Ip4Address &GetSrcIpAddress() const {return src_addr_;};
    const Ip4Address &GetDstIpAddress() const {return dst_addr_;};

private:
    Ip4Address dst_addr_;
    Ip4Address src_addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastRouteEntry);
};

class Inet4MulticastRouteKey : public RouteKey {
public:
    Inet4MulticastRouteKey(const string &vrf_name,const Ip4Address &dip, 
                           const Ip4Address &sip) :
                         RouteKey(Agent::GetInstance()->GetLocalVmPeer(), 
                                  vrf_name), dip_(dip), 
                         sip_(sip) { };
    Inet4MulticastRouteKey(const string &vrf_name, const Ip4Address &dip) : 
        RouteKey(Agent::GetInstance()->GetLocalVmPeer(), vrf_name), dip_(dip) { 
            boost::system::error_code ec;
            sip_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
    };
    Inet4MulticastRouteKey(const string &vrf_name) : 
        RouteKey(Agent::GetInstance()->GetLocalVmPeer(), vrf_name) { 
            boost::system::error_code ec;
            dip_ =  IpAddress::from_string("255.255.255.255", ec).to_v4();
            sip_ =  IpAddress::from_string("0.0.0.0", ec).to_v4();
    };
    virtual ~Inet4MulticastRouteKey() { };
    virtual RouteEntry *AllocRouteEntry(VrfEntry *vrf) const;
    //Enqueue add/chg/delete for route
    virtual AgentRouteTable *GetRouteTableFromVrf(VrfEntry *vrf) { 
        return (static_cast<Inet4MulticastAgentRouteTable *>
                (vrf->GetRouteTable(AgentRouteTableAPIS::INET4_MULTICAST)));
    };
    virtual AgentRouteTableAPIS::TableType GetRouteTableType() {
       return AgentRouteTableAPIS::INET4_MULTICAST;
    }; 
    virtual string ToString() const { return ("Inet4MulticastRouteKey"); };

    const Ip4Address &GetDstIpAddress() const {return dip_;};
    const Ip4Address &GetSrcIpAddress() const {return sip_;};

private:
    Ip4Address dip_;
    Ip4Address sip_;
    DISALLOW_COPY_AND_ASSIGN(Inet4MulticastRouteKey);
};

//////////////////////////////////////////////////////////////////
//  LAYER2
/////////////////////////////////////////////////////////////////
class Layer2AgentRouteTable : public AgentRouteTable {
public:
    Layer2AgentRouteTable(DB *db, const std::string &name) : 
        AgentRouteTable(db, name) { };
    virtual ~Layer2AgentRouteTable() { };

    virtual string GetTableName() const {return "Layer2AgentRouteTable";};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::LAYER2;};
    virtual void RouteTableWalkerNotify(VrfEntry *vrf, AgentXmppChannel *xmpp, 
                                        DBState *state, bool associate,
                                        bool unicast_walk, bool multicast_walk) {
        //Dont support multicast walk
        AgentRouteTable::RouteTableWalkerNotify(vrf, xmpp, state, associate, 
                                                unicast_walk, false);
    };

    static void RouteResyncReq(const string &vrf_name, 
                               const struct ether_addr &mac);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static Layer2RouteEntry *FindRoute(const string &vrf_name, 
                                       const struct ether_addr &mac);
    static void AddRemoteVmRoute(const Peer *peer,
                                 const string &vrf_name,
                                 TunnelType::TypeBmap bmap,
                                 const Ip4Address &server_ip,
                                 uint32_t label,
                                 struct ether_addr &mac,
                                 const Ip4Address &vm_ip, uint32_t plen);
    static void AddLocalVmRoute(const Peer *peer,
                                const uuid &intf_uuid,
                                const string &vn_name, 
                                const string &vrf_name,
                                uint32_t label, int bmap, 
                                struct ether_addr &mac,
                                const Ip4Address &vm_ip,
                                uint32_t plen); 
    static void AddLayer2BroadcastRoute(const string &vrf_name,
                                        const string &vn_name,
                                        const Ip4Address &dip,
                                        const Ip4Address &sip);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          struct ether_addr &mac);
    static void DeleteBroadcastReq(const string &vrf_name);
private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Layer2AgentRouteTable);
};

class Layer2RouteEntry : public RouteEntry {
public:
    Layer2RouteEntry(VrfEntry *vrf, const struct ether_addr &mac,
                     const Ip4Address &vm_ip, uint32_t plen,
                     Peer::Type type) : RouteEntry(vrf), mac_(mac){ 
        if (type != Peer::BGP_PEER) {
            vm_ip_ = vm_ip;
            plen_ = plen;
        } else {
            //TODO Add the IP prefix sent by BGP peer to add IP route 
        }
    };
    Layer2RouteEntry(VrfEntry *vrf, const struct ether_addr &mac):
        RouteEntry(vrf), mac_(mac), plen_(0) { 
            boost::system::error_code ec;
            vm_ip_ = IpAddress::from_string("0.0.0.0", ec).to_v4();
        };
    virtual ~Layer2RouteEntry() { };

    virtual int CompareTo(const Route &rhs) const;
    virtual string ToString() const;
    virtual void UpdateDependantRoutes() { };
    virtual void UpdateNH() { };
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual void RouteResyncReq() const;
    virtual const string GetAddressString() const {
        return ToString();};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::LAYER2;};;
    virtual bool DBEntrySandesh(Sandesh *sresp) const;

    const struct ether_addr &GetAddress() const {return mac_;};
    const Ip4Address &GetVmIpAddress() const {return vm_ip_;};
    const uint32_t GetVmIpPlen() const {return plen_;};

private:
    struct ether_addr mac_;
    Ip4Address vm_ip_;
    uint32_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteEntry);
};

class Layer2RouteKey : public RouteKey {
public:
    Layer2RouteKey(const Peer *peer, const string &vrf_name, 
                   const struct ether_addr &mac) :
        RouteKey(peer, vrf_name), dmac_(mac) { };
    Layer2RouteKey(const Peer *peer, const string &vrf_name, 
                   const struct ether_addr &mac, const Ip4Address &vm_ip,
                   uint32_t plen) :
        RouteKey(peer, vrf_name), dmac_(mac), vm_ip_(vm_ip), plen_(plen) { };
    Layer2RouteKey(const Peer *peer, const string &vrf_name) : 
        RouteKey(peer, vrf_name) { 
            dmac_ = *ether_aton("FF:FF:FF:FF:FF:FF");
        };
    virtual ~Layer2RouteKey() { };

    virtual RouteEntry *AllocRouteEntry(VrfEntry *vrf) const;
    //Enqueue add/chg/delete for route
    virtual AgentRouteTable *GetRouteTableFromVrf(VrfEntry *vrf) { 
        return (static_cast<AgentRouteTable *>(vrf->
                                               GetRouteTable(AgentRouteTableAPIS::LAYER2)));
    };
    virtual AgentRouteTableAPIS::TableType GetRouteTableType() {
        return AgentRouteTableAPIS::LAYER2;
    }; 
    virtual string ToString() const { return ("Layer2RouteKey"); };
    const struct ether_addr &GetMac() const { return dmac_;};

private:
    struct ether_addr dmac_;
    Ip4Address vm_ip_;
    uint32_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteKey);
};

class Layer2EcmpRoute : public RouteData {
public:
    Layer2EcmpRoute(const struct ether_addr &dest_addr, 
                    const string &vn_name, 
                    const string &vrf_name, uint32_t label, 
                    bool local_ecmp_nh,
                    Op op  = RouteData::CHANGE) : 
        RouteData(op), dest_addr_(dest_addr),
        vn_name_(vn_name), vrf_name_(vrf_name),
        label_(label), local_ecmp_nh_(local_ecmp_nh) {
        };
    virtual ~Layer2EcmpRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "layer2ecmp";};;

private:
    const struct ether_addr dest_addr_;
    string vn_name_;
    string vrf_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
    DISALLOW_COPY_AND_ASSIGN(Layer2EcmpRoute);
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
