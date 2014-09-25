/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_inet6_unicast_route_hpp
#define vnsw_inet6_unicast_route_hpp

//////////////////////////////////////////////////////////////////
//  UNICAST INET6
/////////////////////////////////////////////////////////////////
class Inet6UnicastRouteKey : public AgentRouteKey {
public:
    Inet6UnicastRouteKey(const Peer *peer, const string &vrf_name, 
                         const Ip6Address &dip, uint8_t plen) : 
        AgentRouteKey(peer, vrf_name), dip_(dip), plen_(plen) { }
    virtual ~Inet6UnicastRouteKey() { }

    //Called from oute creation in input of route table
    AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    Agent::RouteTableType GetRouteTableType() {
       return Agent::INET6_UNICAST;
    }
    virtual string ToString() const { return ("Inet6UnicastRouteKey"); }

    virtual AgentRouteKey *Clone() const;
    const Ip6Address &addr() const {return dip_;}
    uint8_t plen() const {return plen_;}

private:
    Ip6Address dip_;
    uint8_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Inet6UnicastRouteKey);
};

class Inet6UnicastRouteEntry : public AgentRoute {
public:
    Inet6UnicastRouteEntry(VrfEntry *vrf, const Ip6Address &addr, 
                           uint8_t plen, bool is_multicast);
    virtual ~Inet6UnicastRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual const std::string GetAddressString() const {
        return addr_.to_string();
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET6_UNICAST;
    }

    const Ip6Address &addr() const { return addr_; }
    void set_addr(Ip6Address addr) { addr_ = addr; };

    uint8_t plen() const { return plen_; }
    void set_plen(int plen) { plen_ = plen; }

    //Key for patricia node lookup 
    class Rtkey {
      public:
          static std::size_t Length(const AgentRoute *key) {
              const Inet6UnicastRouteEntry *uckey =
                  static_cast<const Inet6UnicastRouteEntry *>(key);
              return uckey->plen();
          }
          static char ByteValue(const AgentRoute *key, std::size_t i) {
              const Inet6UnicastRouteEntry *uckey =
                  static_cast<const Inet6UnicastRouteEntry *>(key);
              const Ip6Address::bytes_type &addr_bytes = 
                  uckey->addr().to_bytes();
              return static_cast<char>(addr_bytes[i]);
          }
    };
    bool DBEntrySandesh(Sandesh *sresp, Ip6Address addr, uint8_t plen, 
                        bool stale) const;

private:
    friend class Inet6UnicastAgentRouteTable;

    Ip6Address addr_;
    uint8_t plen_;
    Patricia::Node rtnode_;
    DISALLOW_COPY_AND_ASSIGN(Inet6UnicastRouteEntry);
};

class Inet6UnicastAgentRouteTable : public AgentRouteTable {
public:
    typedef Patricia::Tree<Inet6UnicastRouteEntry,
                           &Inet6UnicastRouteEntry::rtnode_, 
                           Inet6UnicastRouteEntry::Rtkey> Inet6RouteTree;

    Inet6UnicastAgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name), walkid_(DBTableWalker::kInvalidWalkerId) {
    }
    virtual ~Inet6UnicastAgentRouteTable() { }

    Inet6UnicastRouteEntry *FindLPM(const Ip6Address &ip);
    Inet6UnicastRouteEntry *FindLPM(const Inet6UnicastRouteEntry &rt_key);
    virtual string GetTableName() const {return "Inet6UnicastAgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::INET6_UNICAST;
    }
    virtual void ProcessAdd(AgentRoute *rt) { 
        tree_.Insert(static_cast<Inet6UnicastRouteEntry *>(rt));
    }
    virtual void ProcessDelete(AgentRoute *rt) { 
        tree_.Remove(static_cast<Inet6UnicastRouteEntry *>(rt));
    }
    Inet6UnicastRouteEntry *FindRoute(const Ip6Address &ip) { 
        return FindLPM(ip);
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void ReEvaluatePaths(const string &vrf_name, 
                               const Ip6Address &ip, uint8_t plen);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const Ip6Address &addr, uint8_t plen);
    static void Delete(const Peer *peer, const string &vrf_name,
                       const Ip6Address &addr, uint8_t plen);
    static void AddHostRoute(const string &vrf_name,
                             const Ip6Address &addr, uint8_t plen,
                             const std::string &dest_vn_name);
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const Ip6Address &addr, uint8_t plen,
                            LocalVmRoute *data);
    //TODO: change subnet_gw_ip to Ip6Address
    void AddLocalVmRouteReq(const Peer *peer, const string &vm_vrf,
                            const Ip6Address &addr, uint8_t plen,
                            const uuid &intf_uuid, const string &vn_name,
                            uint32_t label, 
                            const SecurityGroupList &sg_list,
                            bool force_policy, 
                            const PathPreference &path_preference,
                            const Ip6Address &subnet_gw_ip);
    static void AddLocalVmRoute(const Peer *peer, const string &vm_vrf,
                                const Ip6Address &addr, uint8_t plen,
                                const uuid &intf_uuid, const string &vn_name,
                                uint32_t label, 
                                const SecurityGroupList &sg_list,
                                bool force_policy,
                                const PathPreference &path_preference,
                                const Ip6Address &subnet_gw_ip);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    const Ip6Address &vm_addr,uint8_t plen,
                                    AgentRouteData *data);
    static void AddSubnetRoute(const string &vm_vrf, const Ip6Address &addr,
                               uint8_t plen, const string &vn_name,
                               uint32_t vxlan_id);
private:
    Inet6RouteTree tree_;
    Patricia::Node rtnode_;
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Inet6UnicastAgentRouteTable);
};

#endif // vnsw_inet6_unicast_route_hpp
