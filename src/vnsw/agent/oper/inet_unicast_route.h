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
    virtual Agent::RouteTableType GetRouteTableType() {
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

protected:
    IpAddress dip_;
    uint8_t plen_;
private:
    DISALLOW_COPY_AND_ASSIGN(InetUnicastRouteKey);
};
class InetMplsUnicastRouteKey : public InetUnicastRouteKey {
public:
    InetMplsUnicastRouteKey(const Peer *peer, const string &vrf_name,
                        const IpAddress &dip, uint8_t plen) :
        InetUnicastRouteKey(peer, vrf_name, dip, plen) { }
    virtual ~InetMplsUnicastRouteKey() { }

    virtual Agent::RouteTableType GetRouteTableType() {
        if (dip_.is_v4()) {
            return Agent::INET4_MPLS;
        }
        return Agent::INVALID;
    }
    virtual AgentRouteKey *Clone() const;

private:
    DISALLOW_COPY_AND_ASSIGN(InetMplsUnicastRouteKey);
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
    virtual const std::string GetSourceAddressString() const {
        return "0.0.0.0";
    }
    virtual Agent::RouteTableType GetTableType() const;
    virtual bool ReComputePathDeletion(AgentPath *path);
    virtual bool ReComputePathAdd(AgentPath *path);
    const IpAddress &addr() const { return addr_; }
    void set_addr(IpAddress addr) { addr_ = addr; };

    uint8_t plen() const { return plen_; }
    void set_plen(int plen) { plen_ = plen; }

    //Key for patricia node lookup
    class Rtkey {
      public:
          static std::size_t BitLength(const InetUnicastRouteEntry *key) {
              return key->plen();
          }
          static char ByteValue(
                  const InetUnicastRouteEntry *key, std::size_t i) {
              if (key->addr().is_v4()) {
                  Ip4Address::bytes_type addr_bytes;
                  addr_bytes = key->addr().to_v4().to_bytes();
                  char res = static_cast<char>(addr_bytes[i]);
                  return res;
              } else {
                  Ip6Address::bytes_type addr_bytes;
                  addr_bytes = key->addr().to_v6().to_bytes();
                  volatile char res = static_cast<char>(addr_bytes[i]);
                  return res;
              }
          }
    };

    bool DBEntrySandesh(Sandesh *sresp, IpAddress addr, uint8_t plen, bool stale) const;
    bool IsHostRoute() const;
    bool IpamSubnetRouteAvailable() const;
    InetUnicastRouteEntry *GetIpamSuperNetRoute() const;

    bool UpdateIpamHostFlags(bool ipam_host_route);
    bool UpdateRouteFlags(bool ipam_subnet_route, bool ipam_host_route,
                          bool proxy_arp);

    bool ipam_subnet_route() const {return ipam_subnet_route_;}
    bool ipam_host_route() const { return ipam_host_route_; }
    bool proxy_arp() const {return proxy_arp_;}

protected:
    friend class InetUnicastAgentRouteTable;

    IpAddress addr_;
    uint8_t plen_;
    Patricia::Node rtnode_;
    // Flag set if route exactly matches a subnet in IPAM
    // ARP packets hitting this route must be flooded even if its ECMP route
    bool ipam_subnet_route_;
    // Flag set if this is host route and falls in an IPAM subnet
    // ARP packets hitting this route are flooded if MAC stitching is missing
    bool ipam_host_route_;
    // Specifies if ARP must be force proxied for this route
    bool proxy_arp_;
private:
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
        if (type_ == Agent::INET4_MPLS) {
            return "Inet4MplsAgentRouteTable";
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
    static void DeleteMplsRouteReq(const Peer *peer, const string &vrf_name,
                          const IpAddress &addr, uint8_t plen,
                          AgentRouteData *data);
    static void Delete(const Peer *peer, const string &vrf_name,
                       const IpAddress &addr, uint8_t plen);
    void Delete(const Peer *peer, const string &vrf_name,
                const IpAddress &addr, uint8_t plen,
                AgentRouteData *data);
    static void AddHostRoute(const string &vrf_name,
                             const IpAddress &addr, uint8_t plen,
                             const std::string &dest_vn_name,
                             bool policy);
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const IpAddress &addr, uint8_t plen,
                            LocalVmRoute *data);
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const IpAddress &addr, uint8_t plen,
                            const boost::uuids::uuid &intf_uuid,
                            const VnListType &vn_list,
                            uint32_t label,
                            const SecurityGroupList &sg_list,
                            const TagList &tag_list,
                            const CommunityList &communities,
                            bool force_policy,
                            const PathPreference &path_preference,
                            const IpAddress &subnet_service_ip,
                            const EcmpLoadBalance &ecmp_load_balance,
                            bool is_local,
                            bool is_health_check_service,
                            bool native_encap,
                            const std::string &intf_name = "");
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                const IpAddress &addr, uint8_t plen,
                const boost::uuids::uuid &intf_uuid,
                const VnListType &vn_list,
                uint32_t label,
                const SecurityGroupList &sg_list,
                const TagList &tag_list,
                const CommunityList &communities,
                bool force_policy,
                const PathPreference &path_preference,
                const IpAddress &subnet_service_ip,
                const EcmpLoadBalance &ecmp_load_balance,
                bool is_local, bool is_health_check_service,
                const std::string &intf_name,
                bool native_encap,
                const std::string &intf_route_type = VmInterface::kInterface);
    void ResyncRoute(const Peer *peer, const string &vrf,
                     const IpAddress &addr, uint8_t plen);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    const IpAddress &vm_addr,uint8_t plen,
                                    AgentRouteData *data);
    void AddVlanNHRouteReq(const Peer *peer, const string &vm_vrf,
                           const IpAddress &addr, uint8_t plen,
                           VlanNhRoute *data);
    void AddVlanNHRouteReq(const Peer *peer, const string &vm_vrf,
                           const IpAddress &addr, uint8_t plen,
                           const boost::uuids::uuid &intf_uuid, uint16_t tag,
                           uint32_t label, const VnListType &dest_vn_list,
                           const SecurityGroupList &sg_list_,
                           const TagList &tag_list,
                           const PathPreference &path_preference);
    static void AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                               const IpAddress &addr, uint8_t plen,
                               const boost::uuids::uuid &intf_uuid, uint16_t tag,
                               uint32_t label, const VnListType &dest_vn_list,
                               const SecurityGroupList &sg_list_,
                               const TagList &tag_list,
                               const PathPreference &path_preference);
    InetUnicastRouteEntry *FindResolveRoute(const Ip4Address &ip);
    static InetUnicastRouteEntry *FindResolveRoute(const string &vrf_name,
                                                   const Ip4Address &ip);
    static bool ShouldAddArp(const Ip4Address &ip);
    static void CheckAndAddArpReq(const string &vrf_name, const Ip4Address &ip,
                                  const Interface *intf,
                                  const VnListType &vn_list,
                                  const SecurityGroupList &sg,
                                  const TagList &tag);
    static void CheckAndAddArpRoute(const string &route_vrf_name,
                                    const Ip4Address &ip,
                                    const MacAddress &mac,
                                    const Interface *intf,
                                    bool resolved,
                                    const VnListType &vn_list,
                                    const SecurityGroupList &sg,
                                    const TagList &tag);
    static void AddArpReq(const string &route_vrf_name,
                          const Ip4Address &ip,
                          const string &nh_vrf_name,
                          const Interface *intf,
                          bool policy,
                          const VnListType &dest_vn_list,
                          const SecurityGroupList &sg_list,
                          const TagList &tag_list);
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
                         const SecurityGroupList &sg_list,
                         const TagList &tag_list);
    static void AddResolveRoute(const Peer *peer,
                                const string &vrf_name, const Ip4Address &ip,
                                const uint8_t plen,
                                const InterfaceKey &intf_key,
                                const uint32_t label, bool policy,
                                const std::string &vn_name,
                                const SecurityGroupList &sg_list,
                                const TagList &tag_list);
    void AddInetInterfaceRouteReq(const Peer *peer, const string &vm_vrf,
                                  const Ip4Address &addr, uint8_t plen,
                                  InetInterfaceRoute *data);
    void AddInetInterfaceRouteReq(const Peer *peer, const string &vm_vrf,
                                  const Ip4Address &addr, uint8_t plen,
                                  const string &interface,
                                  uint32_t label,
                                  const VnListType &vn_list);

    static void AddVHostRecvRoute(const Peer *peer, const string &vrf,
                                  const InterfaceKey &interface,
                                  const IpAddress &addr, uint8_t plen,
                                  const string &vn_name, bool policy,
                                  bool native_encap,
                                  bool ipam_host_route = true);
    static void AddVHostRecvRouteReq(const Peer *peer, const string &vrf,
                                     const InterfaceKey &interface,
                                     const IpAddress &addr, uint8_t plen,
                                     const string &vn_name, bool policy,
                                     bool native_encap);
    static void AddVHostMplsRecvRouteReq(const Peer *peer, const string &vrf,
                                     const InterfaceKey &interface,
                                     const IpAddress &addr, uint8_t plen,
                                     const string &vn_name, bool policy,
                                     bool native_encap);
    static void AddVHostSubnetRecvRoute(const Peer *peer, const string &vrf,
                                        const InterfaceKey &interface,
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
                                const VnListType &vn_name, uint32_t label,
                                const SecurityGroupList &sg_list,
                                const TagList &tag_list,
                                const CommunityList &communities,
                                bool native_encap);
    static void AddGatewayRouteReq(const Peer *peer,
                                   const string &vrf_name,
                                   const Ip4Address &dst_addr,uint8_t plen,
                                   const Ip4Address &gw_ip,
                                   const VnListType &vn_name, uint32_t label,
                                   const SecurityGroupList &sg_list,
                                   const TagList &tag_list,
                                   const CommunityList &communities,
                                   bool native_encap);
    static void AddLocalMplsRouteReq(const Peer *peer,
                                   const string &vrf_name,
                                   const Ip4Address &dst_addr,uint8_t plen,
                                   const Ip4Address &gw_ip,
                                   const VnListType &vn_name, uint32_t label,
                                   const SecurityGroupList &sg_list,
                                   const TagList &tag_list,
                                   const CommunityList &communities,
                                   bool native_encap);
    static void AddMplsRouteInternal(const Peer *peer,
                                    DBRequest *req, const string &vrf_name,
                                    const IpAddress &dst_addr, uint8_t plen,
                                    const IpAddress &gw_ip,
                                    const VnListType &vn_name, uint32_t label,
                                    const SecurityGroupList &sg_list,
                                    const TagList &tag_list,
                                    const CommunityList &communities,
                                    bool native_encap);
    static void AddMplsRouteReq(const Peer *peer,
                                   const string &vrf_name,
                                   const IpAddress &dst_addr,uint8_t plen,
                                   AgentRouteData *data);
    void AddIpamSubnetRoute(const string &vm_vrf, const IpAddress &addr,
                            uint8_t plen, const std::string &vn_name);
    void AddVrouterSubnetRoute(const IpAddress &dst_addr, uint8_t plen);
    void AddVhostMplsRoute(const IpAddress &vhost_addr, const Peer *peer);
    void AddInterfaceRouteReq(Agent *agent, const Peer *peer,
                              const string &vrf_name,
                              const Ip4Address &ip, uint8_t plen,
                              const Interface *intrface,
                              const std::string &vn_name);
    void AddClonedLocalPathReq(const Peer *peer, const string &vm_vrf,
                               const IpAddress &addr,
                               uint8_t plen, ClonedLocalPath *data);
    bool ResyncSubnetRoutes(const InetUnicastRouteEntry *rt, bool val);
    uint8_t GetHostPlen(const IpAddress &ip_addr) const;
    void AddEvpnRoute(const AgentRoute *evpn_entry);
    void DeleteEvpnRoute(const AgentRoute *rt);
    void TraverseHostRoutesInSubnet(InetUnicastRouteEntry *rt,
                                    const Peer *peer);
    IpAddress GetSubnetAddress(const IpAddress &addr,
                               uint16_t plen) const;
    InetUnicastRouteEntry *GetSuperNetRoute(const IpAddress &addr);
    void AddEvpnRoutingRoute(const IpAddress &ip_addr,
                             uint8_t plen,
                             const VrfEntry *vrf,
                             const Peer *peer,
                             const SecurityGroupList &sg_list,
                             const CommunityList &communities,
                             const PathPreference &path_preference,
                             const EcmpLoadBalance &ecmp_load_balance,
                             const TagList &tag_list,
                             DBRequest &nh_req,
                             uint32_t vxlan_id,
                             const VnListType& vn_list);

private:
    Agent::RouteTableType type_;
    InetRouteTree tree_;
    Patricia::Node rtnode_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(InetUnicastAgentRouteTable);
};

#endif // vnsw_inet_unicast_route_hpp
