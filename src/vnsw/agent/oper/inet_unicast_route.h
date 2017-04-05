/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_inet_unicast_route_hpp
#define vnsw_inet_unicast_route_hpp

class VlanNhRoute;
class LocalVmRoute;
class InetInterfaceRoute;
class ClonedLocalPath;
class EcmpLoadBalance;

//////////////////////////////////////////////////////////////////
//  UNICAST INET
/////////////////////////////////////////////////////////////////
class InetUnicastRouteKey : public AgentRouteKey {
public:
    InetUnicastRouteKey(const Peer *peer, const string &vrf_name,
                        const IpAddress &dip, uint8_t plen) :
        AgentRouteKey(peer, vrf_name), dip_(dip), plen_(plen) { }
    virtual ~InetUnicastRouteKey() { }

    //Called from oute creation in input of route table
    AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    Agent::RouteTableType GetRouteTableType() {
        if (dip_.is_v4()) {
            return Agent::INET4_UNICAST;
        }
        if (dip_.is_v6()) {
            return Agent::INET6_UNICAST;
        }
        return Agent::INVALID;
    }
    virtual string ToString() const;
    virtual AgentRouteKey *Clone() const;

    const IpAddress &addr() const {return dip_;}
    uint8_t plen() const {return plen_;}

private:
    IpAddress dip_;
    uint8_t plen_;
    DISALLOW_COPY_AND_ASSIGN(InetUnicastRouteKey);
};

class InetUnicastRouteEntry : public AgentRoute {
public:
    InetUnicastRouteEntry(VrfEntry *vrf, const IpAddress &addr,
                           uint8_t plen, bool is_multicast);
    virtual ~InetUnicastRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual const std::string GetAddressString() const {
        return addr_.to_string();
    }
    virtual Agent::RouteTableType GetTableType() const {
        if (addr_.is_v4()) {
            return Agent::INET4_UNICAST;
        }
        if (addr_.is_v6()) {
            return Agent::INET6_UNICAST;
        }
        return Agent::INVALID;
    }
    virtual bool ReComputePathDeletion(AgentPath *path);
    virtual bool ReComputePathAdd(AgentPath *path);
    virtual bool EcmpAddPath(AgentPath *path);
    virtual bool EcmpDeletePath(AgentPath *path);
    void AppendEcmpPath(Agent *agent, AgentPath *path);
    void DeleteComponentNH(Agent *agent, AgentPath *path);
    bool UpdateComponentNH(Agent *agent, AgentPath *ecmp_path, AgentPath *path);

    AgentPath *AllocateEcmpPath(Agent *agent, const AgentPath *path1,
                                const AgentPath *path2);
    static bool ModifyEcmpPath(const IpAddress &dest_addr,
                               uint8_t plen, const VnListType &vn_name,
                               uint32_t label, bool local_ecmp_nh,
                               const string &vrf_name,
                               SecurityGroupList sg_list,
                               const CommunityList &communities,
                               const PathPreference &path_preference,
                               TunnelType::TypeBmap tunnel_bmap,
                               const EcmpLoadBalance &ecmp_ecmp_load_balance,
                               DBRequest &nh_req,
                               Agent* agent,
                               AgentPath *path, const string &route_str,
                               bool alloc_label);
    static bool SyncEcmpPath(AgentPath *path, SecurityGroupList sg_list,
                             const CommunityList &communities,
                             const PathPreference &path_preference,
                             TunnelType::TypeBmap tunnel_bmap,
                             const EcmpLoadBalance &ecmp_ecmp_load_balance);

    const IpAddress &addr() const { return addr_; }
    void set_addr(IpAddress addr) { addr_ = addr; };

    uint8_t plen() const { return plen_; }
    void set_plen(int plen) { plen_ = plen; }

    //Key for patricia node lookup 
    class Rtkey {
      public:
          static std::size_t BitLength(const AgentRoute *key) {
              const InetUnicastRouteEntry *uckey =
                  static_cast<const InetUnicastRouteEntry *>(key);
              return uckey->plen();
          }
          static char ByteValue(const AgentRoute *key, std::size_t i) {
              const InetUnicastRouteEntry *uckey =
                  static_cast<const InetUnicastRouteEntry *>(key);
              if (uckey->addr().is_v4()) {
                  Ip4Address::bytes_type addr_bytes;
                  addr_bytes = uckey->addr().to_v4().to_bytes();
                  return static_cast<char>(addr_bytes[i]);
              } else {
                  Ip6Address::bytes_type addr_bytes;
                  addr_bytes = uckey->addr().to_v6().to_bytes();
                  return static_cast<char>(addr_bytes[i]);
              }
          }
    };
    bool DBEntrySandesh(Sandesh *sresp, IpAddress addr, uint8_t plen, bool stale) const;
    const NextHop* GetLocalNextHop() const;
    bool IsHostRoute() const;
    bool IpamSubnetRouteAvailable() const;
    InetUnicastRouteEntry *GetIpamSuperNetRoute() const;
    bool ipam_subnet_route() const {return ipam_subnet_route_;}
    void set_ipam_subnet_route(bool ipam_subnet_route) {
        ipam_subnet_route_ = ipam_subnet_route;}
    bool proxy_arp() const {return proxy_arp_;}
    void set_proxy_arp(bool proxy_arp) {
        proxy_arp_ = proxy_arp;}

private:
    friend class InetUnicastAgentRouteTable;

    IpAddress addr_;
    uint8_t plen_;
    Patricia::Node rtnode_;
    bool ipam_subnet_route_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(InetUnicastRouteEntry);
};

class InetUnicastAgentRouteTable : public AgentRouteTable {
public:
    typedef Patricia::Tree<InetUnicastRouteEntry,
                           &InetUnicastRouteEntry::rtnode_,
                           InetUnicastRouteEntry::Rtkey> InetRouteTree;

    InetUnicastAgentRouteTable(DB *db, const std::string &name);
    virtual ~InetUnicastAgentRouteTable() { }

    InetUnicastRouteEntry *FindLPM(const IpAddress &ip);
    InetUnicastRouteEntry *FindLPM(const InetUnicastRouteEntry &rt_key);
    virtual string GetTableName() const {
        if (type_ == Agent::INET4_UNICAST) {
            return "Inet4UnicastAgentRouteTable";
        }
        if (type_ == Agent::INET6_UNICAST) {
            return "Inet6UnicastAgentRouteTable";
        }
        return "Unknown";
    }
    virtual Agent::RouteTableType GetTableType() const {
        return type_;
    }
    virtual void ProcessAdd(AgentRoute *rt) { 
        tree_.Insert(static_cast<InetUnicastRouteEntry *>(rt));
    }
    virtual void ProcessDelete(AgentRoute *rt) { 
        tree_.Remove(static_cast<InetUnicastRouteEntry *>(rt));
    }
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);
    InetUnicastRouteEntry *FindRouteUsingKey(InetUnicastRouteEntry &key) {
        return FindLPM(key);
    }
    InetUnicastRouteEntry *FindRoute(const IpAddress &ip) {
        return FindLPM(ip);
    }

    const InetUnicastRouteEntry *GetNext(const InetUnicastRouteEntry *rt) {
        return static_cast<const InetUnicastRouteEntry *>(tree_.FindNext(rt));
    }

    InetUnicastRouteEntry *GetNextNonConst(const InetUnicastRouteEntry *rt) {
        return static_cast<InetUnicastRouteEntry *>(tree_.FindNext(rt));
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const IpAddress &addr, uint8_t plen,
                          AgentRouteData *data);
    static void Delete(const Peer *peer, const string &vrf_name,
                       const IpAddress &addr, uint8_t plen);
    static void AddHostRoute(const string &vrf_name,
                             const IpAddress &addr, uint8_t plen,
                             const std::string &dest_vn_name,
                             bool relaxed_policy);
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const IpAddress &addr, uint8_t plen,
                            LocalVmRoute *data);
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const IpAddress &addr, uint8_t plen,
                            const uuid &intf_uuid,
                            const VnListType &vn_list,
                            uint32_t label,
                            const SecurityGroupList &sg_list,
                            const CommunityList &communities,
                            bool force_policy,
                            const PathPreference &path_preference,
                            const IpAddress &subnet_service_ip,
                            const EcmpLoadBalance &ecmp_load_balance,
                            bool is_local,
                            bool is_health_check_service);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const IpAddress &addr, uint8_t plen,
                                const uuid &intf_uuid,
                                const VnListType &vn_list,
                                uint32_t label,
                                const SecurityGroupList &sg_list,
                                const CommunityList &communities,
                                bool force_policy,
                                const PathPreference &path_preference,
                                const IpAddress &subnet_service_ip,
                                const EcmpLoadBalance &ecmp_load_balance,
                                bool is_local, bool is_health_check_service);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    const IpAddress &vm_addr,uint8_t plen,
                                    AgentRouteData *data);
    void AddVlanNHRouteReq(const Peer *peer, const string &vm_vrf,
                           const IpAddress &addr, uint8_t plen,
                           VlanNhRoute *data);
    void AddVlanNHRouteReq(const Peer *peer, const string &vm_vrf,
                           const IpAddress &addr, uint8_t plen,
                           const uuid &intf_uuid, uint16_t tag,
                           uint32_t label, const VnListType &dest_vn_list,
                           const SecurityGroupList &sg_list_,
                           const PathPreference &path_preference);
    static void AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                               const IpAddress &addr, uint8_t plen,
                               const uuid &intf_uuid, uint16_t tag,
                               uint32_t label, const VnListType &dest_vn_list,
                               const SecurityGroupList &sg_list_,
                               const PathPreference &path_preference);
    InetUnicastRouteEntry *FindResolveRoute(const Ip4Address &ip);
    static InetUnicastRouteEntry *FindResolveRoute(const string &vrf_name, 
                                                   const Ip4Address &ip);
    static void CheckAndAddArpReq(const string &vrf_name, const Ip4Address &ip,
                                  const Interface *intf,
                                  const VnListType &vn_list,
                                  const SecurityGroupList &sg);
    static void AddArpReq(const string &route_vrf_name,
                          const Ip4Address &ip,
                          const string &nh_vrf_name,
                          const Interface *intf,
                          bool policy,
                          const VnListType &dest_vn_list,
                          const SecurityGroupList &sg_list);
    static void ArpRoute(DBRequest::DBOperation op,
                         const string &route_vrf_name,
                         const Ip4Address &ip,
                         const MacAddress &mac,
                         const string &nh_vrf_name,
                         const Interface &intf,
                         bool resolved,
                         const uint8_t plen,
                         bool policy,
                         const VnListType &dest_vn_list,
                         const SecurityGroupList &sg_list);
    static void AddResolveRoute(const Peer *peer,
                                const string &vrf_name, const Ip4Address &ip,
                                const uint8_t plen,
                                const InterfaceKey &intf_key,
                                const uint32_t label, bool policy,
                                const std::string &vn_name,
                                const SecurityGroupList &sg_list);
    void AddInetInterfaceRouteReq(const Peer *peer, const string &vm_vrf,
                                  const Ip4Address &addr, uint8_t plen,
                                  InetInterfaceRoute *data);
    void AddInetInterfaceRouteReq(const Peer *peer, const string &vm_vrf,
                                  const Ip4Address &addr, uint8_t plen,
                                  const string &interface,
                                  uint32_t label,
                                  const VnListType &vn_list);
    static void AddVHostRecvRoute(const Peer *peer, const string &vrf,
                                  const string &interface,
                                  const IpAddress &addr, uint8_t plen,
                                  const string &vn_name, bool policy);
    static void AddVHostRecvRouteReq(const Peer *peer, const string &vrf,
                                     const string &interface,
                                     const IpAddress &addr, uint8_t plen,
                                     const string &vn_name, bool policy);
    static void AddVHostSubnetRecvRoute(const Peer *peer, const string &vrf,
                                        const string &interface,
                                        const Ip4Address &addr, uint8_t plen,
                                        const std::string &vn_name,
                                        bool policy);
    static void DelVHostSubnetRecvRoute(const string &vm_vrf,
                                        const Ip4Address &addr, uint8_t plen);
    static void AddDropRoute(const string &vm_vrf,
                             const Ip4Address &addr, uint8_t plen,
                             const string &vn_name);
    static void AddGatewayRoute(const Peer *peer,
                                const string &vrf_name,
                                const Ip4Address &dst_addr,uint8_t plen,
                                const Ip4Address &gw_ip,
                                const std::string &vn_name, uint32_t label,
                                const SecurityGroupList &sg_list,
                                const CommunityList &communities);
    static void AddGatewayRouteReq(const Peer *peer,
                                   const string &vrf_name,
                                   const Ip4Address &dst_addr,uint8_t plen,
                                   const Ip4Address &gw_ip,
                                   const std::string &vn_name, uint32_t label,
                                   const SecurityGroupList &sg_list,
                                   const CommunityList &communities);
    void AddIpamSubnetRoute(const string &vm_vrf, const IpAddress &addr,
                            uint8_t plen, const std::string &vn_name);
    void AddInterfaceRouteReq(Agent *agent, const Peer *peer,
                              const string &vrf_name,
                              const Ip4Address &ip, uint8_t plen,
                              const Interface *interface,
                              const std::string &vn_name);
    void AddClonedLocalPathReq(const Peer *peer, const string &vm_vrf,
                               const IpAddress &addr,
                               uint8_t plen, ClonedLocalPath *data);
    bool ResyncSubnetRoutes(const InetUnicastRouteEntry *rt,
                            bool add_change);
    uint8_t GetHostPlen(const IpAddress &ip_addr) const;
    void AddEvpnRoute(const AgentRoute *evpn_entry);
    void DeleteEvpnRoute(const AgentRoute *rt);
    void TraverseHostRoutesInSubnet(InetUnicastRouteEntry *rt,
                                    const Peer *peer);
    IpAddress GetSubnetAddress(const IpAddress &addr,
                               uint16_t plen) const;
    InetUnicastRouteEntry *GetSuperNetRoute(const IpAddress &addr);

private:
    Agent::RouteTableType type_;
    InetRouteTree tree_;
    Patricia::Node rtnode_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(InetUnicastAgentRouteTable);
};

#endif // vnsw_inet_unicast_route_hpp
