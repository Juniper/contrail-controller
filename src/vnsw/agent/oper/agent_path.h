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
        dest_vn_name_(""), unresolved_(true), sync_(false), vrf_name_(""), 
        dependant_rt_(rt), proxy_arp_(false), force_policy_(false),
        tunnel_bmap_(0), interfacenh_flags_(0) { };
    virtual ~AgentPath() { }; 

    const Peer *GetPeer() const {return peer_;};
    const NextHop *GetNextHop() const; 
    uint32_t GetLabel() const {return label_;};
    int GetTunnelBmap() const {return tunnel_bmap_;};
    TunnelType::Type GetTunnelType() const {
        return TunnelType::ComputeType(tunnel_bmap_);
    };
    const string &GetDestVnName() const {return dest_vn_name_;};
    const Ip4Address& GetGatewayIp() const {return gw_ip_;};
    const string &GetVrfName() const {return vrf_name_;};
    bool GetProxyArp() const {return proxy_arp_;};
    bool GetForcePolicy() const {return force_policy_;};
    const bool IsUnresolved() const {return unresolved_;};
    uint8_t GetInterfaceNHFlags() const {return interfacenh_flags_;};
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;};  

    void SetLabel(uint32_t label) {label_ = label;};
    void SetDestVnName(const string &dest_vn) {dest_vn_name_ = dest_vn;};
    void SetUnresolved(bool unresolved) {unresolved_ = unresolved;};
    void SetGatewayIp(const Ip4Address &addr) {gw_ip_ = addr;};
    void SetProxyArp(bool proxy_arp) {proxy_arp_ = proxy_arp;};
    void SetForcePolicy(bool force_policy) {force_policy_ = force_policy;};
    void SetVrfName(const string &vrf_name) {vrf_name_ = vrf_name;};
    void SetTunnelBmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;};
    void SetInterfaceNHFlags(uint8_t flags) {interfacenh_flags_ = flags;};
    void SetSecurityGroupList(SecurityGroupList &sg) {sg_list_ = sg;};

    void ClearSecurityGroupList() { sg_list_.clear(); }
    void ResetDependantRoute(AgentRoute *rt) {dependant_rt_.reset(rt);};

    bool ChangeNH(NextHop *nh);
    bool Sync(AgentRoute *sync_route); //vm_path sync
    void SyncRoute(bool sync) {sync_ = sync;};
    bool RouteNeedsSync() {return sync_;};
    virtual std::string ToString() const { return "AgentPath"; };

private:
    const Peer *peer_;
    NextHopRef nh_;
    uint32_t label_;
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
    TunnelType::TypeBmap tunnel_bmap_;
    uint8_t interfacenh_flags_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

class ResolveRoute : public RouteData {
public:
    ResolveRoute(Op op  = RouteData::CHANGE) : RouteData(op, false) { };
    virtual ~ResolveRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "Resolve";};;
private:
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public RouteData {
public:
    LocalVmRoute(const VmInterfaceKey &intf, uint32_t label,
                 int tunnel_bmap, bool force_policy, const string &vn_name,
                 uint8_t flags, const SecurityGroupList &sg_list,
                 Op op = RouteData::CHANGE) :
        RouteData(op, false), intf_(intf), 
        label_(label), tunnel_bmap_(tunnel_bmap),
        force_policy_(force_policy), dest_vn_name_(vn_name),
        proxy_arp_(true), sync_route_(false), 
        flags_(flags), sg_list_(sg_list) { };
    virtual ~LocalVmRoute() { };
    void DisableProxyArp() {proxy_arp_ = false;};
    virtual string ToString() const {return "local VM";};;
    virtual bool AddChangePath(AgentPath *path);
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;}; 

private:
    VmInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool force_policy_;
    string dest_vn_name_;
    bool proxy_arp_;
    bool sync_route_;
    uint8_t flags_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class RemoteVmRoute : public RouteData {
public:
    RemoteVmRoute(const string &vrf_name, const Ip4Address &addr,
                  uint32_t label, const string &dest_vn_name,
                  int bmap, const SecurityGroupList &sg_list,
                  DBRequest &req, Op op = RouteData::CHANGE):
        RouteData(op, false), server_vrf_(vrf_name),
        server_ip_(addr), tunnel_bmap_(bmap), 
        label_(label), dest_vn_name_(dest_vn_name), sg_list_(sg_list)
        {nh_req_.Swap(&req);}
    virtual ~RemoteVmRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "remote VM";};;
    const SecurityGroupList &GetSecurityGroupList() const {return sg_list_;}; 

private:
    string server_vrf_;
    Ip4Address server_ip_;
    int tunnel_bmap_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DBRequest nh_req_;
    DISALLOW_COPY_AND_ASSIGN(RemoteVmRoute);
};

class InetInterfaceRoute : public RouteData {
public:
    InetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                       int tunnel_bmap, const string &dest_vn_name,
                       Op op = RouteData::CHANGE) : 
        RouteData(op,false), intf_(intf), label_(label), 
        tunnel_bmap_(tunnel_bmap), dest_vn_name_(dest_vn_name) { };
    virtual ~InetInterfaceRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "host";};;

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    string dest_vn_name_;
    DISALLOW_COPY_AND_ASSIGN(InetInterfaceRoute);
};

class HostRoute : public RouteData {
public:
    HostRoute(const PacketInterfaceKey &intf, const string &dest_vn_name,
              Op op  = RouteData::CHANGE) : 
        RouteData(op, false), intf_(intf),
        dest_vn_name_(dest_vn_name), proxy_arp_(false) { };
    virtual ~HostRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "host";};;

private:
    PacketInterfaceKey intf_;
    string dest_vn_name_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class VlanNhRoute : public RouteData {
public:
    VlanNhRoute(const VmInterfaceKey &intf, uint16_t tag, uint32_t label,
                const string &dest_vn_name, const SecurityGroupList &sg_list,
                Op op  = RouteData::CHANGE) :
        RouteData(op, false), intf_(intf),
        tag_(tag), label_(label), dest_vn_name_(dest_vn_name), 
        sg_list_(sg_list) { };
    virtual ~VlanNhRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "vlannh";};;

private:
    VmInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    string dest_vn_name_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoute : public RouteData {
public:
    MulticastRoute(const Ip4Address &src_addr, 
                   const Ip4Address &grp_addr,
                   const string &vn_name, 
                   const string &vrf_name,
                   int vxlan_id,
                   COMPOSITETYPE type, Op op  = RouteData::CHANGE) :
        RouteData(op, true), 
        src_addr_(src_addr), grp_addr_(grp_addr),
        vn_name_(vn_name), vrf_name_(vrf_name), vxlan_id_(vxlan_id),
        comp_type_(type) { };
    virtual ~MulticastRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "multicast";};;

private:
    Ip4Address src_addr_;
    Ip4Address grp_addr_;
    string vn_name_;
    string vrf_name_;
    int vxlan_id_;
    COMPOSITETYPE comp_type_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class ReceiveRoute : public RouteData {
public:
    ReceiveRoute(const InetInterfaceKey &intf, uint32_t label,
                 uint32_t tunnel_bmap, bool policy, const string &vn,
                 Op op  = RouteData::CHANGE) : 
        RouteData(op, false), intf_(intf), 
        label_(label), tunnel_bmap_(tunnel_bmap),
        policy_(policy), proxy_arp_(false), vn_(vn), sg_list_() {};
    virtual ~ReceiveRoute() { };
    void EnableProxyArp() {proxy_arp_ = true;};
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "receive";};;

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

class Inet4UnicastEcmpRoute : public RouteData {
public:
    Inet4UnicastEcmpRoute(const Ip4Address &dest_addr, uint8_t plen,
                          const string &vn_name, 
                          uint32_t label, bool local_ecmp_nh, 
                          const string &vrf_name, SecurityGroupList sg_list,
                          DBRequest &nh_req, Op op  = RouteData::CHANGE) :
        RouteData(op, false), dest_addr_(dest_addr), plen_(plen),
        vn_name_(vn_name), label_(label), local_ecmp_nh_(local_ecmp_nh),
        vrf_name_(vrf_name), sg_list_(sg_list) {
            nh_req_.Swap(&nh_req);
        };
    virtual ~Inet4UnicastEcmpRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "inet4 ecmp";};;

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


class Inet4UnicastArpRoute : public RouteData {
public:
    Inet4UnicastArpRoute(const string &vrf_name, 
                         const Ip4Address &addr,
                         Op op  = RouteData::CHANGE) :
        RouteData(op, false), vrf_name_(vrf_name), addr_(addr) { };
    virtual ~Inet4UnicastArpRoute() { };

    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "arp";};;
private:
    string vrf_name_;
    Ip4Address addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public RouteData {
public:
    Inet4UnicastGatewayRoute(const Ip4Address &gw_ip, 
                             const string &vrf_name,
                             Op op  = RouteData::CHANGE) :
        RouteData(op, false), gw_ip_(gw_ip), 
        vrf_name_(vrf_name) { };
    virtual ~Inet4UnicastGatewayRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "gateway";};;

private:
    Ip4Address gw_ip_;
    string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public RouteData {
public:
    DropRoute(Op op  = RouteData::CHANGE) :
        RouteData(op, false) { };
    virtual ~DropRoute() { };
    virtual bool AddChangePath(AgentPath *path);
    virtual string ToString() const {return "drop";};;
private:
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};

#endif // vnsw_agent_path_hpp
