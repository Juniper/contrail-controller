/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_path_hpp
#define vnsw_agent_path_hpp

// Path info for every route entry
class AgentPath : public Path {
public:
    AgentPath(const Peer *peer, AgentRoute *rt) : 
        Path(), peer_(peer), nh_(NULL), label_(MplsTable::kInvalidLabel),
        vxlan_id_(VxLanTable::kInvalidvxlan_id), dest_vn_name_(""),
        unresolved_(true), sync_(false), vrf_name_(""), dependant_rt_(rt),
        proxy_arp_(false), force_policy_(false), 
        tunnel_bmap_(TunnelType::AllType()),
        tunnel_type_(TunnelType::ComputeType(TunnelType::AllType())), 
        interfacenh_flags_(0), server_ip_(Agent::GetInstance()->GetRouterId()) {
    }
    virtual ~AgentPath() { }

    const Peer *peer() const {return peer_;}
    const NextHop *nexthop(Agent *agent) const; 
    uint32_t label() const {return label_;}
    uint32_t vxlan_id() const {return vxlan_id_;}
    TunnelType::Type tunnel_type() const {return tunnel_type_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}
    const Ip4Address& gw_ip() const {return gw_ip_;}
    const string &vrf_name() const {return vrf_name_;}
    bool proxy_arp() const {return proxy_arp_;}
    bool force_policy() const {return force_policy_;}
    const bool unresolved() const {return unresolved_;}
    const Ip4Address& server_ip() const {return server_ip_;}
    const string &dest_vn_name() const {return dest_vn_name_;}
    uint8_t interface_nh_flags() const {return interfacenh_flags_;}
    const SecurityGroupList &sg_list() const {return sg_list_;}

    uint32_t GetActiveLabel() const;
    TunnelType::Type GetTunnelType() const {
        return TunnelType::ComputeType(tunnel_bmap_);
    }

    void set_vxlan_id(uint32_t vxlan_id) {vxlan_id_ = vxlan_id;}
    void set_label(uint32_t label) {label_ = label;}
    void set_dest_vn_name(const string &dest_vn) {dest_vn_name_ = dest_vn;}
    void set_unresolved(bool unresolved) {unresolved_ = unresolved;};
    void set_gw_ip(const Ip4Address &addr) {gw_ip_ = addr;}
    void set_proxy_arp(bool proxy_arp) {proxy_arp_ = proxy_arp;}
    void set_force_policy(bool force_policy) {force_policy_ = force_policy;}
    void set_vrf_name(const string &vrf_name) {vrf_name_ = vrf_name;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    void set_tunnel_type(TunnelType::Type type) {tunnel_type_ = type;}
    void set_interface_nh_flags(uint8_t flags) {interfacenh_flags_ = flags;}
    void set_sg_list(SecurityGroupList &sg) {sg_list_ = sg;}
    void clear_sg_list() { sg_list_.clear(); }
    void set_server_ip(const Ip4Address &server_ip) {server_ip_ = server_ip;}

    void ResetDependantRoute(AgentRoute *rt) {dependant_rt_.reset(rt);}
    bool ChangeNH(Agent *agent, NextHop *nh);
    bool Sync(AgentRoute *sync_route); //vm_path sync
    void SyncRoute(bool sync) {sync_ = sync;}
    bool RouteNeedsSync() {return sync_;}
    uint32_t GetTunnelBmap() const;
    bool UpdateNHPolicy(Agent *agent);
    bool UpdateTunnelType(Agent *agent, const AgentRoute *sync_route);
    bool RebakeAllTunnelNHinCompositeNH(const AgentRoute *sync_route,
                                        const NextHop *nh);
    virtual std::string ToString() const { return "AgentPath"; }

private:
    const Peer *peer_;
    NextHopRef nh_;
    uint32_t label_;
    uint32_t vxlan_id_;
    string dest_vn_name_;
    // Points to gateway route, if this path is part of
    // indirect route
    bool unresolved_;
    bool sync_;
    string vrf_name_;
    Ip4Address gw_ip_;
    DependencyRef<AgentRoute, AgentRoute> dependant_rt_;
    bool proxy_arp_;
    bool force_policy_;
    //tunnel_bmap_ is used to store the bmap sent in remote route
    // by control node
    TunnelType::TypeBmap tunnel_bmap_;
    //Tunnel type stores the encap type used for route
    TunnelType::Type tunnel_type_;
    uint8_t interfacenh_flags_;
    SecurityGroupList sg_list_;
    Ip4Address server_ip_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

class ResolveRoute : public AgentRouteData {
public:
    ResolveRoute() : AgentRouteData(false) { }
    virtual ~ResolveRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "Resolve";}
private:
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public AgentRouteData {
public:
    LocalVmRoute(const VmInterfaceKey &intf, uint32_t mpls_label, 
                 uint32_t vxlan_id, bool force_policy, const string &vn_name,
                 uint8_t flags, const SecurityGroupList &sg_list) :
        AgentRouteData(false), intf_(intf), mpls_label_(mpls_label),
        vxlan_id_(vxlan_id), force_policy_(force_policy),
        dest_vn_name_(vn_name), proxy_arp_(true), sync_route_(false),
        flags_(flags), sg_list_(sg_list), tunnel_bmap_(TunnelType::MplsType()) {
    }
    virtual ~LocalVmRoute() { }
    void DisableProxyArp() {proxy_arp_ = false;}
    virtual string ToString() const {return "local VM";}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    const SecurityGroupList &sg_list() const {return sg_list_;}
    void tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}

private:
    VmInterfaceKey intf_;
    uint32_t mpls_label_;
    uint32_t vxlan_id_;
    bool force_policy_;
    string dest_vn_name_;
    bool proxy_arp_;
    bool sync_route_;
    uint8_t flags_;
    SecurityGroupList sg_list_;
    TunnelType::TypeBmap tunnel_bmap_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class RemoteVmRoute : public AgentRouteData {
public:
    RemoteVmRoute(const string &vrf_name, const Ip4Address &addr,
                  uint32_t label, const string &dest_vn_name,
                  int bmap, const SecurityGroupList &sg_list,
                  DBRequest &req):
        AgentRouteData(false), server_vrf_(vrf_name), server_ip_(addr),
        tunnel_bmap_(bmap), label_(label), dest_vn_name_(dest_vn_name),
        sg_list_(sg_list) { nh_req_.Swap(&req);
    }
    virtual ~RemoteVmRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "remote VM";}
    const SecurityGroupList &sg_list() const {return sg_list_;}

private:
    string server_vrf_;
    Ip4Address server_ip_;
    TunnelType::TypeBmap tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(RemoteVmRoute);
};

class InetInterfaceRoute : public AgentRouteData {
public:
    InetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                       int tunnel_bmap, const string &dest_vn_name) : 
        AgentRouteData(false), intf_(intf), label_(label), 
        tunnel_bmap_(tunnel_bmap), dest_vn_name_(dest_vn_name) {
    }
    virtual ~InetInterfaceRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "host";}

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    string dest_vn_name_;
    DISALLOW_COPY_AND_ASSIGN(InetInterfaceRoute);
};

class HostRoute : public AgentRouteData {
public:
    HostRoute(const PacketInterfaceKey &intf, const string &dest_vn_name) : 
        AgentRouteData(false), intf_(intf), dest_vn_name_(dest_vn_name),
        proxy_arp_(false) {
    }
    virtual ~HostRoute() { }
    void EnableProxyArp() {proxy_arp_ = true;}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "host";}

private:
    PacketInterfaceKey intf_;
    string dest_vn_name_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class VlanNhRoute : public AgentRouteData {
public:
    VlanNhRoute(const VmInterfaceKey &intf, uint16_t tag, uint32_t label,
                const string &dest_vn_name, const SecurityGroupList &sg_list) :
        AgentRouteData(false), intf_(intf), tag_(tag), label_(label),
        dest_vn_name_(dest_vn_name), sg_list_(sg_list) {
    }
    virtual ~VlanNhRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "vlannh";}

private:
    VmInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoute : public AgentRouteData {
public:
    MulticastRoute(const Ip4Address &src_addr, 
                   const Ip4Address &grp_addr,
                   const string &vn_name, 
                   const string &vrf_name,
                   int vxlan_id,
                   COMPOSITETYPE type) :
        AgentRouteData(true), src_addr_(src_addr), grp_addr_(grp_addr),
        vn_name_(vn_name), vrf_name_(vrf_name), vxlan_id_(vxlan_id),
        comp_type_(type) {
    }
    virtual ~MulticastRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "multicast";}

private:
    Ip4Address src_addr_;
    Ip4Address grp_addr_;
    string vn_name_;
    string vrf_name_;
    int vxlan_id_;
    COMPOSITETYPE comp_type_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class ReceiveRoute : public AgentRouteData {
public:
    ReceiveRoute(const InetInterfaceKey &intf, uint32_t label,
                 uint32_t tunnel_bmap, bool policy, const string &vn) : 
        AgentRouteData(false), intf_(intf), label_(label),
        tunnel_bmap_(tunnel_bmap), policy_(policy), proxy_arp_(false),
        vn_(vn), sg_list_() {
    }
    virtual ~ReceiveRoute() { }
    void EnableProxyArp() {proxy_arp_ = true;}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "receive";}

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool policy_;
    bool proxy_arp_;
    string vn_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveRoute);
};

class Inet4UnicastEcmpRoute : public AgentRouteData {
public:
    Inet4UnicastEcmpRoute(const Ip4Address &dest_addr, uint8_t plen,
                          const string &vn_name, 
                          uint32_t label, bool local_ecmp_nh, 
                          const string &vrf_name, SecurityGroupList sg_list,
                          DBRequest &nh_req) :
        AgentRouteData(false), dest_addr_(dest_addr), plen_(plen),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name), sg_list_(sg_list) {
            nh_req_.Swap(&nh_req);
    }
    virtual ~Inet4UnicastEcmpRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "inet4 ecmp";}

private:
    Ip4Address dest_addr_;
    uint8_t plen_;
    string vn_name_;
    uint32_t label_;
    bool local_ecmp_nh_;
    string vrf_name_;
    SecurityGroupList sg_list_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastEcmpRoute);
};


class Inet4UnicastArpRoute : public AgentRouteData {
public:
    Inet4UnicastArpRoute(const string &vrf_name, 
                         const Ip4Address &addr) :
        AgentRouteData(false), vrf_name_(vrf_name), addr_(addr) {
    }
    virtual ~Inet4UnicastArpRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "arp";}
private:
    string vrf_name_;
    Ip4Address addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public AgentRouteData {
public:
    Inet4UnicastGatewayRoute(const Ip4Address &gw_ip, 
                             const string &vrf_name) :
        AgentRouteData(false), gw_ip_(gw_ip), vrf_name_(vrf_name) {
    }
    virtual ~Inet4UnicastGatewayRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "gateway";}

private:
    Ip4Address gw_ip_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public AgentRouteData {
public:
    DropRoute() : AgentRouteData(false) { }
    virtual ~DropRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual string ToString() const {return "drop";}
private:
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};

#endif // vnsw_agent_path_hpp
