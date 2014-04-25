/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_inet4_unicast_route_hpp
#define vnsw_inet4_unicast_route_hpp

//////////////////////////////////////////////////////////////////
//  UNICAST INET4
/////////////////////////////////////////////////////////////////
class Inet4UnicastRouteKey : public AgentRouteKey {
public:
    Inet4UnicastRouteKey(const Peer *peer, const string &vrf_name, 
                         const Ip4Address &dip, uint8_t plen) : 
        AgentRouteKey(peer, vrf_name), dip_(dip), plen_(plen) { }
    virtual ~Inet4UnicastRouteKey() { }

    //Called from oute creation in input of route table
    AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    Agent::RouteTableType GetRouteTableType() {
       return Agent::INET4_UNICAST;
    }
    virtual string ToString() const { return ("Inet4UnicastRouteKey"); }

    const Ip4Address &addr() const {return dip_;}
    const uint8_t &plen() const {return plen_;}

private:
    Ip4Address dip_;
    uint8_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastRouteKey);
};

class Inet4UnicastRouteEntry : public AgentRoute {
public:
    Inet4UnicastRouteEntry(VrfEntry *vrf, const Ip4Address &addr, 
                           uint8_t plen, bool is_multicast);
    virtual ~Inet4UnicastRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual const std::string GetAddressString() const {
        return addr_.to_string();
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET4_UNICAST;
    }
    virtual bool EcmpAddPath(AgentPath *path);
    virtual bool EcmpDeletePath(AgentPath *path);

    AgentPath *AllocateEcmpPath(Agent *agent, const AgentPath *path1,
                                const AgentPath *path2);

    const Ip4Address &addr() const { return addr_; }
    void set_addr(Ip4Address addr) { addr_ = addr; };

    int plen() const { return plen_; }
    void set_plen(int plen) { plen_ = plen; }

    //Key for patricia node lookup 
    class Rtkey {
      public:
          static std::size_t Length(const AgentRoute *key) {
              const Inet4UnicastRouteEntry *uckey =
                  static_cast<const Inet4UnicastRouteEntry *>(key);
              return uckey->plen();
          }
          static char ByteValue(const AgentRoute *key, std::size_t i) {
              const Inet4UnicastRouteEntry *uckey =
                  static_cast<const Inet4UnicastRouteEntry *>(key);
              const Ip4Address::bytes_type &addr_bytes = 
                  uckey->addr().to_bytes();
              return static_cast<char>(addr_bytes[i]);
          }
    };
    bool DBEntrySandesh(Sandesh *sresp, Ip4Address addr, uint8_t plen, bool stale) const;

private:
    friend class Inet4UnicastAgentRouteTable;

    Ip4Address addr_;
    uint8_t plen_;
    Patricia::Node rtnode_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastRouteEntry);
};

class Inet4UnicastAgentRouteTable : public AgentRouteTable {
public:
    typedef Patricia::Tree<Inet4UnicastRouteEntry,
                           &Inet4UnicastRouteEntry::rtnode_, 
                           Inet4UnicastRouteEntry::Rtkey> Inet4RouteTree;

    Inet4UnicastAgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {
    }
    virtual ~Inet4UnicastAgentRouteTable() { }

    Inet4UnicastRouteEntry *FindLPM(const Ip4Address &ip);
    Inet4UnicastRouteEntry *FindLPM(const Inet4UnicastRouteEntry &rt_key);
    virtual string GetTableName() const {return "Inet4UnicastAgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET4_UNICAST;
    }
    virtual void ProcessAdd(AgentRoute *rt) { 
        tree_.Insert(static_cast<Inet4UnicastRouteEntry *>(rt));
    }
    virtual void ProcessDelete(AgentRoute *rt) { 
        tree_.Remove(static_cast<Inet4UnicastRouteEntry *>(rt));
    }
    Inet4UnicastRouteEntry *FindRoute(const Ip4Address &ip) { 
        return FindLPM(ip);
    }

    Inet4UnicastRouteEntry *FindResolveRoute(const Ip4Address &ip);
    static Inet4UnicastRouteEntry *FindResolveRoute(const string &vrf_name, 
                                                    const Ip4Address &ip);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void ReEvaluatePaths(const string &vrf_name, 
                               const Ip4Address &ip, uint8_t plen);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const Ip4Address &addr, uint8_t plen);
    static void Delete(const Peer *peer, const string &vrf_name,
                       const Ip4Address &addr, uint8_t plen);
    static void AddHostRoute(const string &vrf_name,
                             const Ip4Address &addr, uint8_t plen,
                             const std::string &dest_vn_name);
    static void AddVlanNHRouteReq(const Peer *peer, const string &vm_vrf,
                                  const Ip4Address &addr, uint8_t plen,
                                  const uuid &intf_uuid, uint16_t tag,
                                  uint32_t label, const string &dest_vn_name,
                                  const SecurityGroupList &sg_list_);
    static void AddVlanNHRoute(const Peer *peer, const string &vm_vrf,
                               const Ip4Address &addr, uint8_t plen,
                               const uuid &intf_uuid, uint16_t tag,
                               uint32_t label, const string &dest_vn_name,
                               const SecurityGroupList &sg_list_);
    static void AddSubnetBroadcastRoute(const Peer *peer, 
                                        const string &vrf_name,
                                        const Ip4Address &src_addr, 
                                        const Ip4Address &grp_addr,
                                        const string &vn_name);
    static void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                                   const Ip4Address &addr, uint8_t plen,
                                   const uuid &intf_uuid, const string &vn_name,
                                   uint32_t label, 
                                   const SecurityGroupList &sg_list,
                                   bool force_policy);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip4Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label, 
                                const SecurityGroupList &sg_list,
                                bool force_policy);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    const Ip4Address &vm_addr, uint8_t plen,
                                    const Ip4Address &server_ip,
                                    TunnelType::TypeBmap bmap,uint32_t label,
                                    const string &dest_vn_name,
                                    const SecurityGroupList &sg_list_);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    const Ip4Address &vm_addr,uint8_t plen,
                                    std::vector<ComponentNHData> comp_nh_list,
                                    uint32_t label,
                                    const string &dest_vn_name,
                                    const SecurityGroupList &sg_list_);
    static void CheckAndAddArpReq(const string &vrf_name, const Ip4Address &ip);
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
    static void AddInetInterfaceRoute(const Peer *peer, const string &vm_vrf,
                                      const Ip4Address &addr, uint8_t plen,
                                      const string &interface, uint32_t label,
                                      const string &vn_name);
    static void AddVHostRecvRoute(const Peer *peer, const string &vrf,
                                  const string &interface,
                                  const Ip4Address &addr, uint8_t plen,
                                  const string &vn_name, bool policy);
    static void AddVHostRecvRouteReq(const Peer *peer, const string &vrf,
                                     const string &interface,
                                     const Ip4Address &addr, uint8_t plen,
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
                             const string &vn_name, bool is_subnet_discard);
    static void AddGatewayRoute(const string &vrf_name,
                                const Ip4Address &dst_addr,uint8_t plen,
                                const Ip4Address &gw_ip,
                                const std::string &vn_name);
    static void AddGatewayRouteReq(const string &vrf_name,
                                   const Ip4Address &dst_addr,uint8_t plen,
                                   const Ip4Address &gw_ip,
                                   const std::string &vn_name);

private:
    Inet4RouteTree tree_;
    Patricia::Node rtnode_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastAgentRouteTable);
};

#endif // vnsw_inet4_unicast_route_hpp
