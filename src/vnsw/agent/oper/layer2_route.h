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
        AgentRouteTable(db, name) { };
    virtual ~Layer2AgentRouteTable() { };

    virtual string GetTableName() const {return "Layer2AgentRouteTable";};
    virtual AgentRouteTableAPIS::TableType GetTableType() const {
        return AgentRouteTableAPIS::LAYER2;};

    static void RouteResyncReq(const string &vrf_name, 
                               const struct ether_addr &mac);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static Layer2RouteEntry *FindRoute(const string &vrf_name, 
                                       const struct ether_addr &mac);
    static void AddRemoteVmRouteReq(const Peer *peer,
                                    const string &vrf_name,
                                    TunnelType::TypeBmap bmap,
                                    const Ip4Address &server_ip,
                                    uint32_t label,
                                    struct ether_addr &mac,
                                    const Ip4Address &vm_ip, uint32_t plen);
    static void AddLocalVmRouteReq(const Peer *peer,
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
                                        const Ip4Address &dip,
                                        const Ip4Address &sip,
                                        int vxlan_id);
    static void DeleteReq(const Peer *peer, const string &vrf_name,
                          const struct ether_addr &mac);
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
        //For multicast use the same tree as of 255.255.255.255
        if (IsMulticast()) 
            return "255.255.255.255";
        return ToString();
    };
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

    virtual AgentRoute *AllocRouteEntry(VrfEntry *vrf, bool is_multicast) const;
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
        RouteData(op , false), dest_addr_(dest_addr),
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


#endif // vnsw_layer2_route_hpp
