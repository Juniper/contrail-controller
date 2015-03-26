/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_bridge_route_hpp
#define vnsw_bridge_route_hpp

//////////////////////////////////////////////////////////////////
//  BRIDGE
/////////////////////////////////////////////////////////////////
class BridgeAgentRouteTable : public AgentRouteTable {
public:
    BridgeAgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name) {
    }
    virtual ~BridgeAgentRouteTable() { }

    virtual std::string GetTableName() const {return "BridgeAgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::BRIDGE;
    }
    virtual AgentSandesh *GetAgentSandesh(const std::string &context);

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void AddMacVmBindingRoute(const Peer *peer,
                      const std::string &vrf_name,
                      const MacAddress &mac,
                      const VmInterface *vm_intf);
    void AddBridgeRoute(const AgentRoute *rt);
    static void AddBridgeBroadcastRoute(const Peer *peer,
                                        const std::string &vrf_name,
                                        const std::string &vn_name,
                                        uint32_t label,
                                        int vxlan_id,
                                        uint32_t ethernet_tag,
                                        uint32_t tunnel_type,
                                        Composite::Type type,
                                        ComponentNHKeyList
                                        &component_nh_key_list);
    static void AddBridgeReceiveRoute(const Peer *peer,
                                      const std::string &vrf_name,
                                      const MacAddress &mac,
                                      const std::string &vn_name,
                                      const std::string &interface,
                                      bool policy);
    void AddBridgeReceiveRouteReq(const Peer *peer, const std::string &vrf_name,
                                  uint32_t vxlan_id, const MacAddress &mac,
                                  const std::string &vn_name);
    void AddBridgeReceiveRoute(const Peer *peer, const std::string &vrf_name,
                               uint32_t vxlan_id, const MacAddress &mac,
                               const std::string &vn_name);
    static void DeleteReq(const Peer *peer, const std::string &vrf_name,
                          const MacAddress &mac, uint32_t ethernet_tag);
    static void Delete(const Peer *peer, const std::string &vrf_name,
                       const MacAddress &mac, uint32_t ethernet_tag);
    static void DeleteBroadcastReq(const Peer *peer,
                                   const std::string &vrf_name,
                                   uint32_t ethernet_tag);
    void DeleteBridgeRoute(const AgentRoute *rt);
    void DeleteMacVmBindingRoute(const Peer *peer,
                         const std::string &vrf_name,
                         const MacAddress &mac,
                         const VmInterface *vm_intf);
    const VmInterface *FindVmFromDhcpBinding(const MacAddress &mac);
    BridgeRouteEntry *FindRoute(const MacAddress &mac);

private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(BridgeAgentRouteTable);
};

class BridgeRouteEntry : public AgentRoute {
public:
    BridgeRouteEntry(VrfEntry *vrf, const MacAddress &mac,
                     Peer::Type type, bool is_multicast) :
        AgentRoute(vrf, is_multicast), mac_(mac) {
    }
    virtual ~BridgeRouteEntry() { }

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
        return Agent::BRIDGE;
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual uint32_t GetActiveLabel() const;
    virtual bool ReComputePathDeletion(AgentPath *path);
    virtual bool ReComputePathAdd(AgentPath *path);
    virtual AgentPath *FindPathUsingKeyData(const AgentRouteKey *key,
                                            const AgentRouteData *data) const;
    virtual void DeletePathUsingKeyData(const AgentRouteKey *key,
                                        const AgentRouteData *data,
                                        bool force_delete);

    const MacAddress &mac() const {return mac_;}
    const AgentPath *FindMacVmBindingPath() const;

private:
    bool ReComputeMulticastPaths(AgentPath *path, bool del);
    AgentPath *FindEvpnPathUsingKeyData(const AgentRouteKey *key,
                                        const AgentRouteData *data) const;
    AgentPath *FindMulticastPathUsingKeyData(const AgentRouteKey *key,
                                             const AgentRouteData *data) const;

    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteEntry);
};

class BridgeRouteKey : public AgentRouteKey {
public:
    BridgeRouteKey(const Peer *peer, const std::string &vrf_name,
                   const MacAddress &mac, uint32_t ethernet_tag = 0) :
        AgentRouteKey(peer, vrf_name), dmac_(mac), ethernet_tag_(ethernet_tag) {
    }

    virtual ~BridgeRouteKey() { }

    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    virtual Agent::RouteTableType GetRouteTableType() { return Agent::BRIDGE; }
    virtual std::string ToString() const;
    virtual BridgeRouteKey *Clone() const;
    const MacAddress &GetMac() const { return dmac_;}
    uint32_t ethernet_tag() const {return ethernet_tag_;}

private:
    MacAddress dmac_;
    //TODO retained only for multicast route. Once multicast route shift to
    //evpn table this will go off.
    uint32_t ethernet_tag_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteKey);
};

#endif // vnsw_bridge_route_hpp
