/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_layer2_route_hpp
#define vnsw_layer2_route_hpp

//////////////////////////////////////////////////////////////////
//  LAYER2
/////////////////////////////////////////////////////////////////
class Layer2AgentRouteTable : public AgentRouteTable {
public:
    Layer2AgentRouteTable(DB *db, const std::string &name) :
        AgentRouteTable(db, name) {
    }
    virtual ~Layer2AgentRouteTable() { }

    virtual std::string GetTableName() const {return "Layer2AgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::LAYER2;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name);

    void AddEvpnRoute(AgentRoute *rt);
    static void AddLayer2BroadcastRoute(const Peer *peer,
                                        const std::string &vrf_name,
                                        const std::string &vn_name,
                                        uint32_t label,
                                        int vxlan_id,
                                        uint32_t ethernet_tag,
                                        uint32_t tunnel_type,
                                        Composite::Type type,
                                        ComponentNHKeyList
                                        &component_nh_key_list);
    static void AddLayer2ReceiveRoute(const Peer *peer,
                                      const std::string &vrf_name,
                                      const MacAddress &mac,
                                      const std::string &vn_name,
                                      const std::string &interface,
                                      bool policy);
    static void DeleteReq(const Peer *peer, const std::string &vrf_name,
                          const MacAddress &mac, uint32_t ethernet_tag);
    static void Delete(const Peer *peer, const std::string &vrf_name,
                       const MacAddress &mac, uint32_t ethernet_tag);
    static void DeleteBroadcastReq(const Peer *peer,
                                   const std::string &vrf_name,
                                   uint32_t ethernet_tag);
    void DeleteEvpnRoute(AgentRoute *rt);
    static Layer2RouteEntry *FindRoute(const Agent *agent,
                                       const std::string &vrf_name,
                                       const MacAddress &mac);

private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Layer2AgentRouteTable);
};

class Layer2RouteEntry : public AgentRoute {
public:
    Layer2RouteEntry(VrfEntry *vrf, const MacAddress &mac,
                     Peer::Type type, bool is_multicast) :
        AgentRoute(vrf, is_multicast), mac_(mac) {
    }
    virtual ~Layer2RouteEntry() { }

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
        return Agent::LAYER2;
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;
    virtual uint32_t GetActiveLabel() const;
    virtual bool ReComputePathDeletion(AgentPath *path);
    virtual bool ReComputePathAdd(AgentPath *path);
    virtual void DeletePath(const AgentRouteKey *key, bool force_delete);
    virtual AgentPath *FindPathUsingKey(const AgentRouteKey *key);

    const MacAddress &GetAddress() const {return mac_;}

private:
    bool ReComputeMulticastPaths(AgentPath *path, bool del);

    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteEntry);
};

class Layer2RouteKey : public AgentRouteKey {
public:
    Layer2RouteKey(const Peer *peer, const std::string &vrf_name,
                   const MacAddress &mac, uint32_t ethernet_tag = 0) :
        AgentRouteKey(peer, vrf_name), dmac_(mac), ethernet_tag_(ethernet_tag) {
    }

    virtual ~Layer2RouteKey() { }

    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    virtual Agent::RouteTableType GetRouteTableType() { return Agent::LAYER2; }
    virtual std::string ToString() const;
    virtual Layer2RouteKey *Clone() const;
    const MacAddress &GetMac() const { return dmac_;}
    uint32_t ethernet_tag() const {return ethernet_tag_;}

private:
    MacAddress dmac_;
    //ethernet_tag is the segment identifier for VXLAN. In control node its
    //used as a key however for forwarding only MAC is used as a key.
    //To handle this ethernet_tag is sent as part of key for all remote routes
    //which in turn is used to create a seperate path for same peer and
    //mac.
    uint32_t ethernet_tag_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteKey);
};

#endif // vnsw_layer2_route_hpp
