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

    virtual string GetTableName() const {return "Layer2AgentRouteTable";}
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::LAYER2;
    }

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static void AddRemoteVmRouteReq(const Peer *peer, const string &vm_vrf,
                                    struct ether_addr &mac,
                                    const Ip4Address &vm_addr,uint8_t plen,
                                    AgentRouteData *data);
    void AddLocalVmRouteReq(const Peer *peer,
                            const string &vrf_name,
                            struct ether_addr &mac,
                            const Ip4Address &vm_ip,
                            uint32_t plen,
                            LocalVmRoute *data);
    void AddLocalVmRouteReq(const Peer *peer,
                            const uuid &intf_uuid,
                            const string &vn_name,
                            const string &vrf_name,
                            uint32_t mpls_label,
                            uint32_t vxlan_id,
                            struct ether_addr &mac,
                            const Ip4Address &vm_ip,
                            uint32_t plen);
    static void AddLocalVmRoute(const Peer *peer,
                                const uuid &intf_uuid,
                                const string &vn_name, 
                                const string &vrf_name,
                                uint32_t mpls_label,
                                uint32_t vxlan_id,
                                struct ether_addr &mac,
                                const Ip4Address &vm_ip,
                                uint32_t plen); 
    static void AddLayer2BroadcastRoute(const string &vrf_name,
                                        const string &vn_name,
                                        int vxlan_id,
                                        ComponentNHKeyList
                                        &component_nh_key_list);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const struct ether_addr &mac,
                          AgentRouteData *data);
    static void Delete(const Peer *peer, const string &vrf_name,
                       const struct ether_addr &mac);
    static void DeleteBroadcastReq(const string &vrf_name);
private:
    DBTableWalker::WalkId walkid_;
    DISALLOW_COPY_AND_ASSIGN(Layer2AgentRouteTable);
};

class Layer2RouteEntry : public AgentRoute {
public:
    Layer2RouteEntry(VrfEntry *vrf, const struct ether_addr &mac,
                     const Ip4Address &vm_ip, uint32_t plen,
                     Peer::Type type, bool is_multicast) : 
        AgentRoute(vrf, is_multicast), mac_(mac){ 
        if (type != Peer::BGP_PEER) {
            vm_ip_ = vm_ip;
            plen_ = plen;
        } else {
            //TODO Add the IP prefix sent by BGP peer to add IP route 
        }
    }
    virtual ~Layer2RouteEntry() { }

    virtual int CompareTo(const Route &rhs) const;
    virtual string ToString() const;
    virtual void UpdateDependantRoutes() { }
    virtual void UpdateNH() { }
    virtual KeyPtr GetDBRequestKey() const;
    virtual void SetKey(const DBRequestKey *key);
    virtual const string GetAddressString() const {
        //For multicast use the same tree as of 255.255.255.255
        if (is_multicast()) 
            return "255.255.255.255";
        return ToString();
    }
    virtual Agent::RouteTableType GetTableType() const {
        return Agent::LAYER2;
    }
    virtual bool DBEntrySandesh(Sandesh *sresp, bool stale) const;

    const struct ether_addr &GetAddress() const {return mac_;}
    const Ip4Address &GetVmIpAddress() const {return vm_ip_;}
    const uint32_t GetVmIpPlen() const {return plen_;}

private:
    struct ether_addr mac_;
    Ip4Address vm_ip_;
    uint32_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteEntry);
};

class Layer2RouteKey : public AgentRouteKey {
public:
    Layer2RouteKey(const Peer *peer, const string &vrf_name, 
                   const struct ether_addr &mac) :
        AgentRouteKey(peer, vrf_name), dmac_(mac) {
    }
    Layer2RouteKey(const Peer *peer, const string &vrf_name, 
                   const struct ether_addr &mac, const Ip4Address &vm_ip,
                   uint32_t plen) :
        AgentRouteKey(peer, vrf_name), dmac_(mac), vm_ip_(vm_ip), plen_(plen) {
    }
    Layer2RouteKey(const Peer *peer, const string &vrf_name) : 
        AgentRouteKey(peer, vrf_name) { 
            dmac_ = *ether_aton("FF:FF:FF:FF:FF:FF");
    }
    virtual ~Layer2RouteKey() { }

    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
    virtual Agent::RouteTableType GetRouteTableType() { return Agent::LAYER2; }
    virtual string ToString() const;
    const struct ether_addr &GetMac() const { return dmac_;}

private:
    struct ether_addr dmac_;
    Ip4Address vm_ip_;
    uint32_t plen_;
    DISALLOW_COPY_AND_ASSIGN(Layer2RouteKey);
};

#endif // vnsw_layer2_route_hpp
