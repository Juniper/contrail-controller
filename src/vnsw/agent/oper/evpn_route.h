/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_evpn_route_hpp
#define vnsw_evpn_route_hpp

//////////////////////////////////////////////////////////////////
//  EVPN
/////////////////////////////////////////////////////////////////
class EvpnAgentRouteTable : public AgentRouteTable {
public:
    EvpnAgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name) {
    }
    virtual ~EvpnAgentRouteTable() { }

    virtual std::string GetTableName() const {return "EvpnAgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::EVPN;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void AddLocalVmRouteReq(const Peer *peer,
                            const std::string &vrf_name,
                            const MacAddress &mac,
                            const IpAddress &ip_addr,
                            uint32_t ethernet_tag,
                            LocalVmRoute *data);
    void AddLocalVmRoute(const Peer *peer, const std::string &vrf_name,
                         const MacAddress &mac, const VmInterface *intf,
                         const IpAddress &ip, uint32_t label,
                         const std::string &vn_name,
                         const SecurityGroupList &sg_id_list,
                         const PathPreference &path_pref);
    void DelLocalVmRoute(const Peer *peer, const std::string &vrf_name,
                         const MacAddress &mac, const VmInterface *intf,
                         const IpAddress &ip);
    static void AddRemoteVmRouteReq(const Peer *peer,
                                    const std::string &vrf_name,
                                    const MacAddress &mac,
                                    const IpAddress &ip_addr,
                                    uint32_t ethernet_tag,
                                    AgentRouteData *data);
    static void AddEvpnBroadcastRoute(const Peer *peer,
                                        const std::string &vrf_name,
                                        const std::string &vn_name,
                                        uint32_t label,
                                        int vxlan_id,
                                        uint32_t ethernet_tag,
                                        uint32_t tunnel_type,
                                        Composite::Type type,
                                        ComponentNHKeyList
                                        &component_nh_key_list);
    static void AddEvpnReceiveRoute(const Peer *peer,
                                      const std::string &vrf_name,
                                      const MacAddress &mac,
                                      const IpAddress &ip_addr,
                                      const std::string &vn_name,
                                      const std::string &interface,
                                      bool policy);
    void AddEvpnReceiveRouteReq(const Peer *peer, const std::string &vrf_name,
                                  uint32_t vxlan_id, const MacAddress &mac,
                                  const std::string &vn_name);
    void AddEvpnReceiveRoute(const Peer *peer, const std::string &vrf_name,
                               uint32_t vxlan_id, const MacAddress &mac,
                               const std::string &vn_name);
    static void DeleteReq(const Peer *peer, const std::string &vrf_name,
                          const MacAddress &mac, const IpAddress &ip_addr,
                          uint32_t ethernet_tag);
    static void Delete(const Peer *peer, const std::string &vrf_name,
                       const MacAddress &mac, const IpAddress &ip_addr,
                       uint32_t ethernet_tag);
    static void DeleteBroadcastReq(const Peer *peer,
                                   const std::string &vrf_name,
                                   uint32_t ethernet_tag);
    static EvpnRouteEntry *FindRoute(const Agent *agent,
                                       const std::string &vrf_name,
                                       const MacAddress &mac,
                                       const IpAddress &ip_addr);

private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(EvpnAgentRouteTable);
};

class EvpnRouteEntry : public AgentRoute {
public:
    EvpnRouteEntry(VrfEntry *vrf, const MacAddress &mac,
                     const IpAddress &ip_addr,
                     Peer::Type type, bool is_multicast) :
        AgentRoute(vrf, is_multicast), mac_(mac), ip_addr_(ip_addr) {
    }
    virtual ~EvpnRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual void UpdateDependantRoutes() { }
    virtual void UpdateNH() { }
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual const std::string GetAddressString() const {
        //For multicast use the same tree as of 255.255.255.255
        if (is_multicast())
            return "255.255.255.255";
        return ToString();
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::EVPN;
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual uint32_t GetActiveLabel() const;
    virtual bool ReComputePathDeletion(AgentPath *path);
    virtual bool ReComputePathAdd(AgentPath *path);
    virtual void DeletePath(const AgentRouteKey *key, bool force_delete);
    virtual AgentPath *FindPathUsingKey(const AgentRouteKey *key);

    const MacAddress &GetAddress() const {return mac_;}
    const MacAddress &mac() const {return mac_;}
    const IpAddress &ip_addr() const {return ip_addr_;}
    const uint32_t GetVmIpPlen() const;

private:
    bool ReComputeMulticastPaths(AgentPath *path, bool del);

    MacAddress mac_;
    IpAddress ip_addr_;
    DISALLOW_COPY_AND_ASSIGN(EvpnRouteEntry);
};

class EvpnRouteKey : public AgentRouteKey {
public:
    EvpnRouteKey(const Peer *peer, const std::string &vrf_name,
                   const MacAddress &mac, const IpAddress &ip_addr,
                   uint32_t ethernet_tag) :
        AgentRouteKey(peer, vrf_name), dmac_(mac), ip_addr_(ip_addr),
        ethernet_tag_(ethernet_tag) {
    }

    virtual ~EvpnRouteKey() { }

    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    virtual Agent::RouteTableType GetRouteTableType() { return Agent::EVPN; }
    virtual std::string ToString() const;
    virtual EvpnRouteKey *Clone() const;
    const MacAddress &GetMac() const { return dmac_;}
    const IpAddress &ip_addr() const { return ip_addr_;}
    uint32_t ethernet_tag() const {return ethernet_tag_;}

private:
    MacAddress dmac_;
    IpAddress ip_addr_;
    //ethernet_tag is the segment identifier for VXLAN. In control node its used
    //as a key however for forwarding only MAC is used as a key.
    //To handle this ethernet_tag is sent as part of key for all remote routes
    //which in turn is used to create a seperate path for same peer and
    //mac.
    uint32_t ethernet_tag_;
    DISALLOW_COPY_AND_ASSIGN(EvpnRouteKey);
};

#endif // vnsw_evpn_route_hpp
