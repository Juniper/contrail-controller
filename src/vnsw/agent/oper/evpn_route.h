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
    virtual void UpdateDerivedRoutes(AgentRoute *entry,
                                     const AgentPath *path,
                                     bool active_path_changed);
    virtual void PreRouteDelete(AgentRoute *entry);
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    //Add routines
    void AddOvsPeerMulticastRouteReq(const Peer* peer,
                                     uint32_t vxlan_id,
                                     const std::string &vn_name,
                                     Ip4Address vtep,
                                     Ip4Address tor_ip);
    void AddOvsPeerMulticastRoute(const Peer* peer,
                                  uint32_t vxlan_id,
                                  const std::string &vn_name,
                                  Ip4Address vtep,
                                  Ip4Address tor_ip);
    void AddReceiveRouteReq(const Peer *peer, const std::string &vrf_name,
                            uint32_t label, const MacAddress &mac,
                            const IpAddress &ip_addr, uint32_t ethernet_tag,
                            const std::string &vn_name,
                            const PathPreference &pref);
    void AddReceiveRoute(const Peer *peer, const std::string &vrf_name,
                         uint32_t label, const MacAddress &mac,
                         const IpAddress &ip_addr, uint32_t ethernet_tag,
                         const std::string &vn_name,
                         const PathPreference &pref);
    void AddControllerReceiveRouteReq(const Peer *peer,
                         const std::string &vrf_name,
                         uint32_t label, const MacAddress &mac,
                         const IpAddress &ip_addr, uint32_t ethernet_tag,
                         const std::string &vn_name,
                         const PathPreference &pref);
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
                         const PathPreference &path_pref,
                         uint32_t ethernet_tag);
    static void ResyncVmRoute(const Peer *peer,
                              const string &vrf_name,
                              const MacAddress &mac,
                              const IpAddress &ip_addr,
                              uint32_t ethernet_tag,
                              AgentRouteData *data);
    static void AddRemoteVmRouteReq(const Peer *peer,
                                    const std::string &vrf_name,
                                    const MacAddress &mac,
                                    const IpAddress &ip_addr,
                                    uint32_t ethernet_tag,
                                    AgentRouteData *data);
    static void AddRemoteVmRoute(const Peer *peer,
                                 const std::string &vrf_name,
                                 const MacAddress &mac,
                                 const IpAddress &ip_addr,
                                 uint32_t ethernet_tag,
                                 AgentRouteData *data);

    //Delete routines
    void DeleteOvsPeerMulticastRouteReq(const Peer *peer,
                                        uint32_t vxlan_id,
                                        const Ip4Address &tor_ip);
    void DeleteOvsPeerMulticastRoute(const Peer *peer,
                                     uint32_t vxlan_id,
                                     const Ip4Address &tor_ip);
    void DelLocalVmRoute(const Peer *peer, const std::string &vrf_name,
                         const MacAddress &mac, const VmInterface *intf,
                         const IpAddress &ip, uint32_t ethernet_tag);
    static void DeleteReq(const Peer *peer, const std::string &vrf_name,
                          const MacAddress &mac, const IpAddress &ip_addr,
                          uint32_t ethernet_tag, AgentRouteData *data);
    static void Delete(const Peer *peer, const std::string &vrf_name,
                       const MacAddress &mac, const IpAddress &ip_addr,
                       uint32_t ethernet_tag);
    EvpnRouteEntry *FindRoute(const MacAddress &mac, const IpAddress &ip_addr,
                              uint32_t ethernet_tag);
    static EvpnRouteEntry *FindRoute(const Agent *agent,
                                       const std::string &vrf_name,
                                       const MacAddress &mac,
                                       const IpAddress &ip_addr,
                                       uint32_t ethernet_tag);

private:
    DISALLOW_COPY_AND_ASSIGN(EvpnAgentRouteTable);
    void AddOvsPeerMulticastRouteInternal(const Peer* peer,
                                          uint32_t vxlan_id,
                                          const std::string &vn_name,
                                          Ip4Address vtep,
                                          Ip4Address tor_ip,
                                          bool enqueue);
    void DeleteOvsPeerMulticastRouteInternal(const Peer *peer,
                                             uint32_t vxlan_id,
                                             const Ip4Address &tor_ip,
                                             bool enqueue);

};

class EvpnRouteEntry : public AgentRoute {
public:
    EvpnRouteEntry(VrfEntry *vrf,
                   const MacAddress &mac,
                   const IpAddress &ip_addr,
                   uint32_t ethernet_tag,
                   bool is_multicast);
    virtual ~EvpnRouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual std::string ToString() const;
    virtual void UpdateDependantRoutes() { }
    virtual void UpdateNH() { }
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual const std::string GetAddressString() const {
        return mac_.ToString();
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::EVPN;
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual uint32_t GetActiveLabel() const;

    const MacAddress &mac() const {return mac_;}
    const IpAddress &ip_addr() const {return ip_addr_;}
    const uint32_t GetVmIpPlen() const;
    uint32_t ethernet_tag() const {return ethernet_tag_;}
    void set_publish_to_bridge_route_table(bool publish_to_bridge_route_table) {
        publish_to_bridge_route_table_ = publish_to_bridge_route_table;
    }
    bool publish_to_bridge_route_table() const {
        if (is_multicast())
            return false;
        return publish_to_bridge_route_table_;
    }
    void set_publish_to_inet_route_table(bool publish_to_inet_route_table) {
        publish_to_inet_route_table_ = publish_to_inet_route_table;
    }
    bool publish_to_inet_route_table() const {
        if (is_multicast())
            return false;
        return publish_to_inet_route_table_;
    }
    const AgentPath *FindOvsPath() const;

private:

    MacAddress mac_;
    IpAddress ip_addr_;
    uint32_t ethernet_tag_;
    bool publish_to_inet_route_table_;
    bool publish_to_bridge_route_table_;
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
