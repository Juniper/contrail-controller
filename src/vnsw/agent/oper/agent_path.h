/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_path_hpp
#define vnsw_agent_path_hpp

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <route/path.h>
#include <oper/nexthop.h>
#include <oper/agent_route.h>
#include <oper/mpls.h>
#include <oper/vxlan.h>

//Forward declaration
class AgentXmppChannel;
class Peer;

class PathPreference {
public:
    enum Preference {
        LOW = 100,
        HIGH = 200
    };
    PathPreference(): sequence_(0), preference_(LOW),
        wait_for_traffic_(true), ecmp_(false) {}
    PathPreference(uint32_t sequence, Preference preference,
        bool wait_for_traffic, bool ecmp): sequence_(sequence),
        preference_(preference), wait_for_traffic_(wait_for_traffic),
        ecmp_(ecmp) {}
    uint32_t sequence() const { return sequence_;}
    Preference preference() const { return preference_;}
    bool wait_for_traffic() const {
        return wait_for_traffic_;
    }
    bool ecmp() const {
        return ecmp_;
    }
    void set_sequence(uint32_t sequence) {
        sequence_ = sequence;
    }
    void set_preference(Preference preference) {
        preference_ = preference;
    }
    void set_wait_for_traffic(bool wait_for_traffic) {
        wait_for_traffic_ = wait_for_traffic;
    }
    void set_ecmp(bool ecmp) {
        ecmp_ = ecmp;
    }

    bool operator!=(const PathPreference &rhs) const {
        return (sequence_ != rhs.sequence_ || preference_ != rhs.preference_
                || wait_for_traffic_ != rhs.wait_for_traffic_
                || ecmp_ != rhs.ecmp_);
    }

    bool operator<(const PathPreference &rhs) const {
        if (preference_ < rhs.preference_) {
            return true;
        }

        if (sequence_ <  rhs.sequence_) {
            return true;
        }
        return false;
    }
private:
    uint32_t sequence_;
    Preference preference_;
    bool wait_for_traffic_;
    bool ecmp_;
};

//Route data to change preference and sequence number of path
struct PathPreferenceData : public AgentRouteData {
    PathPreferenceData(const PathPreference &path_preference):
        AgentRouteData(ROUTE_PREFERENCE_CHANGE, false),
        path_preference_(path_preference) { }
    virtual std::string ToString() const {
        return "";
    }
    virtual bool AddChangePath(Agent*, AgentPath*);
    PathPreference path_preference_;
};

// A common class for all different type of paths
class AgentPath : public Path {
public:
    AgentPath(const Peer *peer, AgentRoute *rt);
    virtual ~AgentPath();

    const Peer *peer() const {return peer_;}
    const NextHop *nexthop(Agent *agent) const;
    uint32_t label() const {return label_;}
    uint32_t vxlan_id() const {return vxlan_id_;}
    TunnelType::Type tunnel_type() const {return tunnel_type_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}
    const Ip4Address& gw_ip() const {return gw_ip_;}
    const std::string &vrf_name() const {return vrf_name_;}
    bool proxy_arp() const {return proxy_arp_;}
    bool force_policy() const {return force_policy_;}
    const bool unresolved() const {return unresolved_;}
    const Ip4Address& server_ip() const {return server_ip_;}
    const std::string &dest_vn_name() const {return dest_vn_name_;}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    bool is_subnet_discard() const {return is_subnet_discard_;}
    const IpAddress subnet_gw_ip() const { return subnet_gw_ip_;}

    uint32_t GetActiveLabel() const;
    TunnelType::Type GetTunnelType() const {
        return TunnelType::ComputeType(tunnel_bmap_);
    }

    void set_vxlan_id(uint32_t vxlan_id) {vxlan_id_ = vxlan_id;}
    void set_label(uint32_t label) {label_ = label;}
    void set_dest_vn_name(const std::string &dest_vn) {dest_vn_name_ = dest_vn;}
    void set_unresolved(bool unresolved) {unresolved_ = unresolved;};
    void set_gw_ip(const Ip4Address &addr) {gw_ip_ = addr;}
    void set_proxy_arp(bool proxy_arp) {proxy_arp_ = proxy_arp;}
    void set_force_policy(bool force_policy) {force_policy_ = force_policy;}
    void set_vrf_name(const std::string &vrf_name) {vrf_name_ = vrf_name;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    void set_tunnel_type(TunnelType::Type type) {tunnel_type_ = type;}
    void set_sg_list(SecurityGroupList &sg) {sg_list_ = sg;}
    void clear_sg_list() { sg_list_.clear(); }
    void set_server_ip(const Ip4Address &server_ip) {server_ip_ = server_ip;}
    void set_is_subnet_discard(bool discard) {
        is_subnet_discard_= discard;
    }
    void set_subnet_gw_ip(const IpAddress &ip) {
        subnet_gw_ip_ = ip;
    }
    void set_local_ecmp_mpls_label(MplsLabel *mpls);
    const MplsLabel* local_ecmp_mpls_label() const;
    void ResetDependantRoute(AgentRoute *rt) {dependant_rt_.reset(rt);}
    bool ChangeNH(Agent *agent, NextHop *nh);
    bool Sync(AgentRoute *sync_route); //vm_path sync
    void SyncRoute(bool sync) {sync_ = sync;}
    bool RouteNeedsSync() {return sync_;}
    uint32_t GetTunnelBmap() const;
    bool UpdateNHPolicy(Agent *agent);
    bool UpdateTunnelType(Agent *agent, const AgentRoute *sync_route);
    bool RebakeAllTunnelNHinCompositeNH(const AgentRoute *sync_route);
    virtual std::string ToString() const { return "AgentPath"; }
    void SetSandeshData(PathSandeshData &data) const;
    bool is_stale() const {return is_stale_;}
    void set_is_stale(bool is_stale) {is_stale_ = is_stale;}
    uint32_t preference() const { return path_preference_.preference();}
    uint32_t sequence() const { return path_preference_.sequence();}
    const PathPreference& path_preference() const { return path_preference_;}
    void set_path_preference(const PathPreference &rp) { path_preference_ = rp;}
    bool IsLess(const AgentPath &right) const;
    void set_composite_nh_key(CompositeNHKey *key) {
        composite_nh_key_.reset(key);
    }
    CompositeNHKey* composite_nh_key() {
        return composite_nh_key_.get();
    }
    bool ReorderCompositeNH(Agent *agent, CompositeNHKey *nh);
    bool ChangeCompositeNH(Agent *agent, CompositeNHKey *nh);
private:
    const Peer *peer_;
    // Nexthop for route. Not used for gateway routes
    NextHopRef nh_;
    // MPLS Label sent by control-node
    uint32_t label_;
    // VXLAN-ID sent by control-node
    uint32_t vxlan_id_;
    // destination vn-name used in policy lookups
    std::string dest_vn_name_;
    bool sync_;

    // Proxy-Arp enabled for the route?
    bool proxy_arp_;
    // When force_policy_ is not set,
    //     Use nexthop with policy if policy enabled on interface
    //     Use nexthop without policy if policy is disabled on interface
    // When force_policy_ is set
    //     Use nexthop with policy irrespective of interface configuration
    bool force_policy_;
    SecurityGroupList sg_list_;

    // tunnel destination address
    Ip4Address server_ip_;
    // tunnel_bmap_ sent by control-node
    TunnelType::TypeBmap tunnel_bmap_;
    // tunnel-type computed for the path
    TunnelType::Type tunnel_type_;

    // VRF for gw_ip_ in gateway route
    std::string vrf_name_;
    // gateway for the route
    Ip4Address gw_ip_;
    // gateway route is unresolved if,
    //    - no route present for gw_ip_
    //    - ARP not resolved for gw_ip_
    bool unresolved_;
    // Stale peer info; peer is dead
    bool is_stale_;
    // subnet route with discard nexthop.
    bool is_subnet_discard_;
    // route for the gateway
    DependencyRef<AgentRoute, AgentRoute> dependant_rt_;
    PathPreference path_preference_;
    //Local MPLS label path is dependent on
    DependencyRef<AgentRoute, MplsLabel> local_ecmp_mpls_label_;
    //CompositeNH key for resync
    boost::scoped_ptr<CompositeNHKey> composite_nh_key_;
    //Gateway address of the subnet this route belong to.
    //This IP address gets used in sending arp query to the VM
    //helping in deciding the priority during live migration and
    //allowed address pair
    IpAddress subnet_gw_ip_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

class ResolveRoute : public AgentRouteData {
public:
    ResolveRoute() : AgentRouteData(false) { }
    virtual ~ResolveRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "Resolve";}
private:
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public AgentRouteData {
public:
    LocalVmRoute(const VmInterfaceKey &intf, uint32_t mpls_label,
                 uint32_t vxlan_id, bool force_policy, const string &vn_name,
                 uint8_t flags, const SecurityGroupList &sg_list,
                 const PathPreference &path_preference,
                 const IpAddress &subnet_gw_ip) :
        AgentRouteData(false), intf_(intf), mpls_label_(mpls_label),
        vxlan_id_(vxlan_id), force_policy_(force_policy),
        dest_vn_name_(vn_name), proxy_arp_(true), sync_route_(false),
        flags_(flags), sg_list_(sg_list), tunnel_bmap_(TunnelType::MplsType()),
        path_preference_(path_preference), subnet_gw_ip_(subnet_gw_ip) {
    }
    virtual ~LocalVmRoute() { }
    void DisableProxyArp() {proxy_arp_ = false;}
    virtual std::string ToString() const {return "local VM";}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    const SecurityGroupList &sg_list() const {return sg_list_;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    const PathPreference& path_preference() const { return path_preference_;}
    void set_path_preference(const PathPreference &path_preference) {
        path_preference_ = path_preference;
    }
    uint32_t vxlan_id() const {return vxlan_id_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}

private:
    VmInterfaceKey intf_;
    uint32_t mpls_label_;
    uint32_t vxlan_id_;
    bool force_policy_;
    std::string dest_vn_name_;
    bool proxy_arp_;
    bool sync_route_;
    uint8_t flags_;
    SecurityGroupList sg_list_;
    TunnelType::TypeBmap tunnel_bmap_;
    PathPreference path_preference_;
    IpAddress subnet_gw_ip_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class InetInterfaceRoute : public AgentRouteData {
public:
    InetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                       int tunnel_bmap, const std::string &dest_vn_name) :
        AgentRouteData(false), intf_(intf), label_(label),
        tunnel_bmap_(tunnel_bmap), dest_vn_name_(dest_vn_name) {
    }
    virtual ~InetInterfaceRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "host";}

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    std::string dest_vn_name_;
    DISALLOW_COPY_AND_ASSIGN(InetInterfaceRoute);
};

class HostRoute : public AgentRouteData {
public:
    HostRoute(const PacketInterfaceKey &intf, const std::string &dest_vn_name) :
        AgentRouteData(false), intf_(intf), dest_vn_name_(dest_vn_name),
        proxy_arp_(false) {
    }
    virtual ~HostRoute() { }
    void EnableProxyArp() {proxy_arp_ = true;}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "host";}

private:
    PacketInterfaceKey intf_;
    std::string dest_vn_name_;
    bool proxy_arp_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class VlanNhRoute : public AgentRouteData {
public:
    VlanNhRoute(const VmInterfaceKey &intf, uint16_t tag, uint32_t label,
                const string &dest_vn_name, const SecurityGroupList &sg_list,
                const PathPreference &path_preference):
        AgentRouteData(false), intf_(intf), tag_(tag), label_(label),
        dest_vn_name_(dest_vn_name), sg_list_(sg_list),
        path_preference_(path_preference), tunnel_bmap_(TunnelType::MplsType()) {
    }
    virtual ~VlanNhRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "vlannh";}

private:
    VmInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    std::string dest_vn_name_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoute : public AgentRouteData {
public:
    MulticastRoute(const string &vn_name, uint32_t label,
                   int vxlan_id, DBRequest &nh_req):
    AgentRouteData(true), vn_name_(vn_name),
    label_(label), vxlan_id_(vxlan_id) {
        composite_nh_req_.Swap(&nh_req);
    }
    virtual ~MulticastRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "multicast";}
    static bool CopyPathParameters(Agent *agent,
                                   AgentPath *path,
                                   const std::string &dest_vn_name,
                                   bool unresolved,
                                   uint32_t vxlan_id,
                                   uint32_t label,
                                   uint32_t tunnel_type,
                                   bool is_subnet_discard,
                                   NextHop *nh);

private:
    string vn_name_;
    uint32_t label_;
    uint32_t vxlan_id_;
    DBRequest composite_nh_req_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class SubnetRoute : public MulticastRoute {
public:
    SubnetRoute(const string &vn_name, uint32_t vxlan_id, DBRequest &nh_req);
    virtual ~SubnetRoute() {}
    virtual string ToString() const {return "subnet route";}

private:
    DISALLOW_COPY_AND_ASSIGN(SubnetRoute);
};

class ReceiveRoute : public AgentRouteData {
public:
    ReceiveRoute(const InetInterfaceKey &intf, uint32_t label,
                 uint32_t tunnel_bmap, bool policy, const std::string &vn) :
        AgentRouteData(false), intf_(intf), label_(label),
        tunnel_bmap_(tunnel_bmap), policy_(policy), proxy_arp_(false),
        vn_(vn), sg_list_() {
    }
    virtual ~ReceiveRoute() { }
    void EnableProxyArp() {proxy_arp_ = true;}
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "receive";}

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool policy_;
    bool proxy_arp_;
    std::string vn_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveRoute);
};

class Inet4UnicastArpRoute : public AgentRouteData {
public:
    Inet4UnicastArpRoute(const std::string &vrf_name,
                         const Ip4Address &addr) :
        AgentRouteData(false), vrf_name_(vrf_name), addr_(addr) {
    }
    virtual ~Inet4UnicastArpRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "arp";}
private:
    std::string vrf_name_;
    Ip4Address addr_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public AgentRouteData {
public:
    Inet4UnicastGatewayRoute(const Ip4Address &gw_ip,
                             const std::string &vrf_name) :
        AgentRouteData(false), gw_ip_(gw_ip), vrf_name_(vrf_name) {
    }
    virtual ~Inet4UnicastGatewayRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "gateway";}

private:
    Ip4Address gw_ip_;
    std::string vrf_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public AgentRouteData {
public:
    DropRoute(const string &vn_name) :
        AgentRouteData(false), vn_(vn_name) { }
    virtual ~DropRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path);
    virtual std::string ToString() const {return "drop";}
private:
    std::string vn_;
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};
#endif // vnsw_agent_path_hpp
