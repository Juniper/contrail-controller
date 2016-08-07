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
class InterfaceKey;
class PhysicalInterface;
class Peer;
class EvpnPeer;
class EcmpLoadBalance;

class PathPreference {
public:
    enum Preference {
        HA_STALE = 1,
        LOW = 100,
        HIGH = 200
    };
    PathPreference(): sequence_(0), preference_(LOW),
        wait_for_traffic_(true), ecmp_(false), static_preference_(false),
        dependent_ip_(Ip4Address(0)) {}
    PathPreference(uint32_t sequence, Preference preference,
        bool wait_for_traffic, bool ecmp): sequence_(sequence),
        preference_(preference), wait_for_traffic_(wait_for_traffic),
        ecmp_(ecmp), static_preference_(false), dependent_ip_(Ip4Address(0)) {}
    uint32_t sequence() const { return sequence_;}
    Preference preference() const { return preference_;}
    bool wait_for_traffic() const {
        return wait_for_traffic_;
    }
    bool ecmp() const {
        return ecmp_;
    }

    bool is_ecmp() const {
        if (ecmp_ == true || (preference_ == HIGH && sequence_ == 0)) {
            return true;
        }
        return false;
    }

    bool static_preference() const {
        return static_preference_;
    }

    const IpAddress& dependent_ip() const {
        return dependent_ip_;
    }

    const std::string& vrf() const {
        return vrf_;
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

    void set_static_preference(bool static_pref) {
        static_preference_ = static_pref;
    }

    void set_dependent_ip(const IpAddress &ip) {
        dependent_ip_ = ip;
    }

    void set_vrf(const std::string &vrf) {
        vrf_ = vrf;
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

    //Check if any configuration values have changed
    //ecmp flag and static preference are updated from
    //configuration, if static preference flag is set,
    //then preference also would be picked from configuration
    bool ConfigChanged(const PathPreference &rhs) const {
        bool ret = false;
        if (ecmp_ != rhs.ecmp_) {
            ret = true;
        } else if (static_preference_ != rhs.static_preference_) {
            ret = true;
        } else if (static_preference_ && preference_ != rhs.preference_) {
            ret = true;
        } else if (dependent_ip_ != rhs.dependent_ip_) {
            ret = true;
        }
        return ret;
    }

    bool IsDependentRt(void) const {
        if (dependent_ip_.is_v4()) {
            if (dependent_ip_ != Ip4Address(0)) {
                return true;
            }
        } else if (dependent_ip_.is_v6()) {
            if (!dependent_ip_.is_unspecified()) {
                return true;
            }
        }
        return false;
    }


private:
    uint32_t sequence_;
    Preference preference_;
    bool wait_for_traffic_;
    bool ecmp_;
    bool static_preference_;
    IpAddress dependent_ip_;
    std::string vrf_;
};

//Route data to change preference and sequence number of path
struct PathPreferenceData : public AgentRouteData {
    PathPreferenceData(const PathPreference &path_preference):
        AgentRouteData(ROUTE_PREFERENCE_CHANGE, false),
        path_preference_(path_preference) { }
    virtual std::string ToString() const {
        return "";
    }
    virtual bool AddChangePath(Agent*, AgentPath*, const AgentRoute*);
    PathPreference path_preference_;
};

// A common class for all different type of paths
class AgentPath : public Path {
public:
    AgentPath(const Peer *peer, AgentRoute *rt);
    virtual ~AgentPath();

    virtual const NextHop *ComputeNextHop(Agent *agent) const;
    virtual bool IsLess(const AgentPath &right) const;
    //UsablePath
    //This happens when a route is dependant on other route to get the path and
    //in these cases active path of other route will be usable path.
    //If there is no dependant route then it returns self.
    virtual const AgentPath *UsablePath() const;
    //Syncs path parameters. Parent route is also used to pick params.
    virtual bool Sync(AgentRoute *sync_route);

    const SecurityGroupList &sg_list() const {return sg_list_;}
    const CommunityList &communities() const {return communities_;}
    const std::string &dest_vn_name() const {
        assert(dest_vn_list_.size() <= 1);
        if (dest_vn_list_.size())
            return *dest_vn_list_.begin();
        else
            return Agent::NullString();
    }
    const VnListType &dest_vn_list() const {return dest_vn_list_;}
    void GetDestinationVnList(std::vector<std::string> *vn_list) const;
    uint32_t GetActiveLabel() const;
    NextHop *nexthop() const;
    const Peer *peer() const {return peer_.get();}
    uint32_t label() const {return label_;}
    uint32_t vxlan_id() const {return vxlan_id_;}
    TunnelType::Type tunnel_type() const {return tunnel_type_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}
    const IpAddress& gw_ip() const {return gw_ip_;}
    const std::string &vrf_name() const {return vrf_name_;}
    bool force_policy() const {return force_policy_;}
    const bool unresolved() const {return unresolved_;}
    const Ip4Address& tunnel_dest() const {return tunnel_dest_;}
    bool is_subnet_discard() const {return is_subnet_discard_;}
    const IpAddress subnet_service_ip() const { return subnet_service_ip_;}

    TunnelType::Type GetTunnelType() const {
        return TunnelType::ComputeType(tunnel_bmap_);
    }

    void set_nexthop(NextHop *nh);
    void set_vxlan_id(uint32_t vxlan_id) {vxlan_id_ = vxlan_id;}
    void set_label(uint32_t label) {label_ = label;}
    void set_dest_vn_list(const VnListType &dest_vn_list) {dest_vn_list_ = dest_vn_list;}
    void set_unresolved(bool unresolved) {unresolved_ = unresolved;};
    void set_gw_ip(const IpAddress &addr) {gw_ip_ = addr;}
    void set_force_policy(bool force_policy) {force_policy_ = force_policy;}
    void set_vrf_name(const std::string &vrf_name) {vrf_name_ = vrf_name;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    void set_tunnel_type(TunnelType::Type type) {tunnel_type_ = type;}
    void set_sg_list(const SecurityGroupList &sg) {sg_list_ = sg;}
    void set_communities(const CommunityList &communities) {communities_ = communities;}
    void clear_sg_list() { sg_list_.clear(); }
    void clear_communities() { communities_.clear(); }
    void set_tunnel_dest(const Ip4Address &tunnel_dest) {
        tunnel_dest_ = tunnel_dest;
    }
    void set_is_subnet_discard(bool discard) {
        is_subnet_discard_= discard;
    }
    void set_subnet_service_ip(const IpAddress &ip) {
        subnet_service_ip_ = ip;
    }
    void set_local_ecmp_mpls_label(MplsLabel *mpls);
    bool dest_vn_match(const std::string &vn) const;
    const MplsLabel* local_ecmp_mpls_label() const;
    void ClearDependantRoute() {dependant_rt_.clear();}
    void ResetDependantRoute(AgentRoute *rt) {dependant_rt_.reset(rt);}
    const AgentRoute *dependant_rt() const {return dependant_rt_.get();}
    bool ChangeNH(Agent *agent, NextHop *nh);
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
    void set_composite_nh_key(CompositeNHKey *key) {
        composite_nh_key_.reset(key);
    }
    CompositeNHKey* composite_nh_key() {
        return composite_nh_key_.get();
    }
    bool ReorderCompositeNH(Agent *agent, CompositeNHKey *nh,
                            bool &comp_nh_policy);
    bool ChangeCompositeNH(Agent *agent, CompositeNHKey *nh);
    // Get nexthop-ip address to be used for path
    const Ip4Address *NexthopIp(Agent *agent) const;

    MacAddress arp_mac() const {return arp_mac_;}
    void set_arp_mac(const MacAddress &mac) {
        arp_mac_ = mac;
    }
    const Interface* arp_interface() const {return arp_interface_.get();}
    void set_arp_interface(const Interface *intf) {
        arp_interface_ = intf;
    }

    bool arp_valid() const { return arp_valid_;}
    void set_arp_valid(bool valid) { arp_valid_ = valid;}

    bool ecmp_suppressed() const { return ecmp_suppressed_;}
    void set_ecmp_suppressed(bool suppresed) { ecmp_suppressed_ = suppresed;}

    bool CopyArpData();
    const IpAddress& GetFixedIp() const {
        return path_preference_.dependent_ip();
    }
    const EcmpLoadBalance &ecmp_load_balance() const {
        return ecmp_load_balance_;
    }
    void set_ecmp_load_balance(const EcmpLoadBalance &ecmp_load_balance) {
        ecmp_load_balance_ = ecmp_load_balance;
    }

    bool is_local() const {
        return is_local_;
    }

    void set_is_local(bool is_local) {
        is_local_ = is_local;
    }

private:
    PeerConstPtr peer_;
    // Nexthop for route. Not used for gateway routes
    NextHopRef nh_;
    // MPLS Label sent by control-node
    uint32_t label_;
    // VXLAN-ID sent by control-node
    uint32_t vxlan_id_;
    // destination vn-name used in policy lookups
    VnListType dest_vn_list_;

    // sync_ flag says that any change in this path sholud result in re-sync
    // of all paths in the route. This can be used in cases where some
    // properties are inherited from one path to other
    bool sync_;

    // When force_policy_ is not set,
    //     Use nexthop with policy if policy enabled on interface
    //     Use nexthop without policy if policy is disabled on interface
    // When force_policy_ is set
    //     Use nexthop with policy irrespective of interface configuration
    bool force_policy_;
    SecurityGroupList sg_list_;
    CommunityList communities_;

    // tunnel destination address
    Ip4Address tunnel_dest_;
    // tunnel_bmap_ sent by control-node
    TunnelType::TypeBmap tunnel_bmap_;
    // tunnel-type computed for the path
    TunnelType::Type tunnel_type_;

    // VRF for gw_ip_ in gateway route
    std::string vrf_name_;
    // gateway for the route
    IpAddress gw_ip_;
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
    IpAddress subnet_service_ip_;
    //Mac address of ARP NH, used to notify change
    //to routes dependent on mac change, or ageout of ARP
    MacAddress arp_mac_;
    //Interface on which ARP would be resolved
    InterfaceConstRef arp_interface_;
    bool arp_valid_;
    // set true if path was supposed to be ecmp, however ecmp was suppressed
    // by taking only one of the nexthop from the path
    bool ecmp_suppressed_;
    EcmpLoadBalance ecmp_load_balance_;
    // if the path is marked local do no export it to BGP and do not
    // program it to vrouter
    bool is_local_;
    DISALLOW_COPY_AND_ASSIGN(AgentPath);
};

/*
 * EvpnDerivedPath
 *
 * Used by bridge routes. This path is derived from AgentPath as
 * common route code expects type AgentPath. Also EvpnDerivedPath
 * keeps reference path from Evpn route.
 * evpn_peer_ref is unique peer generated for each evpn route.
 * ip_addr is the IP taken from parent evpn route.
 * parent is for debugging to know which evpn route added the path.
 */
class EvpnDerivedPath : public AgentPath {
public:
    EvpnDerivedPath(const EvpnPeer *evpn_peer,
             const IpAddress &ip_addr,
             uint32_t ethernet_tag,
             const std::string &parent);
    virtual ~EvpnDerivedPath() { }

    virtual const NextHop *ComputeNextHop(Agent *agent) const;
    virtual bool IsLess(const AgentPath &right) const;

    //Data get/set
    const IpAddress &ip_addr() const {return ip_addr_;}
    void set_ip_addr(const IpAddress &ip_addr) {ip_addr_ = ip_addr;}
    const std::string &parent() const {return parent_;}
    void set_parent(const std::string &parent) {parent_ = parent;}
    uint32_t ethernet_tag() const {return ethernet_tag_;}
    void set_ethernet_tag(uint32_t ethernet_tag) {ethernet_tag_ = ethernet_tag;}

private:
    //Key parameters for comparision
    IpAddress ip_addr_;
    uint32_t ethernet_tag_;
    //Data
    std::string parent_;
    DISALLOW_COPY_AND_ASSIGN(EvpnDerivedPath);
};

/*
 * EvpnDerivedPathData
 * Route data used to transfer information from Evpn route for bridge rute
 * creation.
 * path_parameters_changed - Telss if some parameters changed in reference path.
 * Currently its always true as any change in path attributes results in
 * notification of evpn route and thats when bridge route is also updated.
 */
class EvpnDerivedPathData : public AgentRouteData {
public:
    EvpnDerivedPathData(const EvpnRouteEntry *evpn_rt);
    virtual ~EvpnDerivedPathData() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "EvpnDerivedPathData";}
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;

    EvpnPeer *evpn_peer() const;
    const IpAddress &ip_addr() const {return ip_addr_;}
    const std::string &parent() const {return parent_;}
    void set_parent(const std::string &parent) {parent_ = parent;}
    void set_ethernet_tag(uint32_t ethernet_tag) {ethernet_tag_ = ethernet_tag;}
    uint32_t ethernet_tag() const {return ethernet_tag_;}
    const AgentPath *reference_path() const {return reference_path_;}

private:
    void CopyData(const AgentPath *path);

    uint32_t ethernet_tag_;
    IpAddress ip_addr_;
    std::string parent_;
    //reference_path holds good if route request is inline i.e. via Process
    //and not via Enqueue.
    const AgentPath *reference_path_;
    bool ecmp_suppressed_;
    DISALLOW_COPY_AND_ASSIGN(EvpnDerivedPathData);
};

class ResolveRoute : public AgentRouteData {
public:
    ResolveRoute(const InterfaceKey *key, bool policy, const uint32_t label,
                 const std::string &vn_name, const SecurityGroupList &sg_list) :
        AgentRouteData(false), intf_key_(key->Clone()), policy_(policy),
        label_(label), dest_vn_name_(vn_name), path_sg_list_(sg_list) {}
    virtual ~ResolveRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "Resolve";}
private:
    boost::scoped_ptr<const InterfaceKey> intf_key_;
    bool policy_;
    uint32_t label_;
    const std::string dest_vn_name_;
    const SecurityGroupList path_sg_list_;
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public AgentRouteData {
public:
    LocalVmRoute(const VmInterfaceKey &intf, uint32_t mpls_label,
                 uint32_t vxlan_id, bool force_policy, const VnListType &vn_list,
                 uint8_t flags, const SecurityGroupList &sg_list,
                 const CommunityList &communities,
                 const PathPreference &path_preference,
                 const IpAddress &subnet_service_ip,
                 const EcmpLoadBalance &ecmp_load_balance, bool is_local) :
        AgentRouteData(false), intf_(intf), mpls_label_(mpls_label),
        vxlan_id_(vxlan_id), force_policy_(force_policy),
        dest_vn_list_(vn_list), proxy_arp_(false), sync_route_(false),
        flags_(flags), sg_list_(sg_list), communities_(communities),
        tunnel_bmap_(TunnelType::MplsType()),
        path_preference_(path_preference),
        subnet_service_ip_(subnet_service_ip),
        ecmp_load_balance_(ecmp_load_balance), is_local_(is_local) {
    }
    virtual ~LocalVmRoute() { }
    void DisableProxyArp() {proxy_arp_ = false;}
    virtual std::string ToString() const {return "local VM";}
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    const CommunityList &communities() const {return communities_;}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    const PathPreference& path_preference() const { return path_preference_;}
    void set_path_preference(const PathPreference &path_preference) {
        path_preference_ = path_preference;
    }
    uint32_t vxlan_id() const {return vxlan_id_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}
    bool proxy_arp() const {return proxy_arp_;}

private:
    VmInterfaceKey intf_;
    uint32_t mpls_label_;
    uint32_t vxlan_id_;
    bool force_policy_;
    VnListType dest_vn_list_;
    bool proxy_arp_;
    bool sync_route_;
    uint8_t flags_;
    SecurityGroupList sg_list_;
    CommunityList communities_;
    TunnelType::TypeBmap tunnel_bmap_;
    PathPreference path_preference_;
    IpAddress subnet_service_ip_;
    EcmpLoadBalance ecmp_load_balance_;
    bool is_local_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class InetInterfaceRoute : public AgentRouteData {
public:
    InetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                       int tunnel_bmap, const VnListType &dest_vn_list):
        AgentRouteData(false), intf_(intf), label_(label),
        tunnel_bmap_(tunnel_bmap), dest_vn_list_(dest_vn_list) {
    }
    virtual ~InetInterfaceRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "host";}
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    InetInterfaceKey intf_;
    uint32_t label_;
    int tunnel_bmap_;
    VnListType dest_vn_list_;
    DISALLOW_COPY_AND_ASSIGN(InetInterfaceRoute);
};

class HostRoute : public AgentRouteData {
public:
    HostRoute(const PacketInterfaceKey &intf, const std::string &dest_vn_name):
        AgentRouteData(false), intf_(intf), dest_vn_name_(dest_vn_name),
        proxy_arp_(false), relaxed_policy_(false) {
    }
    virtual ~HostRoute() { }
    void set_proxy_arp() {proxy_arp_ = true;}
    void set_relaxed_policy(bool relaxed_policy) {
        relaxed_policy_ = relaxed_policy;
    }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "host";}
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    PacketInterfaceKey intf_;
    std::string dest_vn_name_;
    bool proxy_arp_;
    bool relaxed_policy_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class L2ReceiveRoute : public AgentRouteData {
public:
    L2ReceiveRoute(const std::string &dest_vn_name, uint32_t vxlan_id,
                   uint32_t mpls_label, const PathPreference &pref) :
        AgentRouteData(false), dest_vn_name_(dest_vn_name),
        vxlan_id_(vxlan_id), mpls_label_(mpls_label), path_preference_(pref) {
    }
    virtual ~L2ReceiveRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "l2-receive";}
    virtual bool UpdateRoute(AgentRoute *rt) {return false;}

private:
    std::string dest_vn_name_;
    uint32_t vxlan_id_;
    uint32_t mpls_label_;
    const PathPreference path_preference_;
    DISALLOW_COPY_AND_ASSIGN(L2ReceiveRoute);
};

class VlanNhRoute : public AgentRouteData {
public:
    VlanNhRoute(const VmInterfaceKey &intf, uint16_t tag, uint32_t label,
                const VnListType &dest_vn_list, const SecurityGroupList &sg_list,
                const PathPreference &path_preference):
        AgentRouteData(false), intf_(intf), tag_(tag), label_(label),
        dest_vn_list_(dest_vn_list), sg_list_(sg_list),
        path_preference_(path_preference), tunnel_bmap_(TunnelType::MplsType()) {
    }
    virtual ~VlanNhRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "vlannh";}

private:
    VmInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoute : public AgentRouteData {
public:
    MulticastRoute(const string &vn_name, uint32_t label, int vxlan_id,
                   uint32_t tunnel_type, DBRequest &nh_req,
                   COMPOSITETYPE comp_nh_type):
    AgentRouteData(true), vn_name_(vn_name), label_(label), vxlan_id_(vxlan_id), 
    tunnel_type_(tunnel_type), comp_nh_type_(comp_nh_type) {
        composite_nh_req_.Swap(&nh_req);
    }
    virtual ~MulticastRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "multicast";}
    uint32_t vxlan_id() const {return vxlan_id_;}
    COMPOSITETYPE comp_nh_type() const {return comp_nh_type_;}
    static bool CopyPathParameters(Agent *agent,
                                   AgentPath *path,
                                   const std::string &dest_vn_name,
                                   bool unresolved,
                                   uint32_t vxlan_id,
                                   uint32_t label,
                                   uint32_t tunnel_type,
                                   NextHop *nh);

private:
    string vn_name_;
    uint32_t label_;
    uint32_t vxlan_id_;
    uint32_t tunnel_type_;
    DBRequest composite_nh_req_;
    COMPOSITETYPE comp_nh_type_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class IpamSubnetRoute : public AgentRouteData {
public:
    IpamSubnetRoute(DBRequest &nh_req, const std::string &dest_vn_name);
    virtual ~IpamSubnetRoute() {}
    virtual string ToString() const {return "subnet route";}
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    DBRequest nh_req_;
    std::string dest_vn_name_;
    DISALLOW_COPY_AND_ASSIGN(IpamSubnetRoute);
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
    void set_proxy_arp() {proxy_arp_ = true;}
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "receive";}
    virtual bool UpdateRoute(AgentRoute *rt);

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
                         const Ip4Address &addr, bool policy,
                         const VnListType &vn_list, const SecurityGroupList &sg) :
        AgentRouteData(false), vrf_name_(vrf_name), addr_(addr), 
        policy_(policy), vn_list_(vn_list), sg_list_(sg) {
    }
    virtual ~Inet4UnicastArpRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "arp";}
private:
    std::string vrf_name_;
    Ip4Address addr_;
    bool policy_;
    VnListType vn_list_;
    SecurityGroupList sg_list_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public AgentRouteData {
public:
    Inet4UnicastGatewayRoute(const IpAddress &gw_ip,
                             const std::string &vrf_name,
                             const std::string &vn_name,
                             uint32_t label, const SecurityGroupList &sg,
                             const CommunityList &communities) :
        AgentRouteData(false), gw_ip_(gw_ip), vrf_name_(vrf_name),
        vn_name_(vn_name), mpls_label_(label), sg_list_(sg), communities_(communities) {
    }
    virtual ~Inet4UnicastGatewayRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "gateway";}

private:
    IpAddress gw_ip_;
    std::string vrf_name_;
    std::string vn_name_;
    uint32_t mpls_label_;
    const SecurityGroupList sg_list_;
    const CommunityList communities_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public AgentRouteData {
public:
    DropRoute(const string &vn_name) :
        AgentRouteData(false), vn_(vn_name) { }
    virtual ~DropRoute() { }
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "drop";}
private:
    std::string vn_;
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};

class Inet4UnicastInterfaceRoute : public AgentRouteData {
public:
    Inet4UnicastInterfaceRoute(const PhysicalInterface *interface,
                               const std::string &vn_name);
    virtual ~Inet4UnicastInterfaceRoute() { }

    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "Interface";}

private:
    std::auto_ptr<InterfaceKey> interface_key_;
    std::string vn_name_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastInterfaceRoute);
};

//MacVmBindingPath is used to store VM interface from which
//this route was added. This helps in retrieving the
//VM using MAC in a VRF. Also it stores the flood dhcp
//flag which is used to decide if DHCP request are to
//be answered by agent or external DHCP server.
class MacVmBindingPath : public AgentPath {
public:
    MacVmBindingPath(const Peer *peer);
    virtual ~MacVmBindingPath() { }

    virtual const NextHop *ComputeNextHop(Agent *agent) const;
    virtual bool IsLess(const AgentPath &right) const;

    //Data get/set
    const VmInterface *vm_interface() const {
        return dynamic_cast<const VmInterface *>(vm_interface_.get());
    }
    void set_vm_interface(const VmInterface *vm_interface) {
        vm_interface_ = vm_interface;
    }
    virtual bool flood_dhcp() const {return flood_dhcp_;}
    void set_flood_dhcp(bool flood_dhcp) {flood_dhcp_ = flood_dhcp;}

private:
    //Key parameters for comparision
    InterfaceConstRef vm_interface_;
    // should vrouter flood the DHCP request coming from this source route
    bool flood_dhcp_;
    DISALLOW_COPY_AND_ASSIGN(MacVmBindingPath);
};

//MacVmBindingPathData is expected to be used only in
//inline calls as it is carrying interface pointer.
//In case request is required key will have to be
//provided.
class MacVmBindingPathData : public AgentRouteData {
public:
    MacVmBindingPathData(const VmInterface *vm_intf) :
        AgentRouteData(false), vm_intf_(vm_intf) { }
    virtual ~MacVmBindingPathData() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "MacVmBindingPathData";}

private:
    const VmInterface *vm_intf_;
    DISALLOW_COPY_AND_ASSIGN(MacVmBindingPathData);
};

/*
 * InetEvpnRoutePath/InetEvpnRouteData
 *
 * InetEvpnRoute is derived from evpn route.
 * Installation of evpn route initiates addition of inet route as well.
 * This is done inline and request contains parent evpn route.
 * Nexthop derivation: NH is not picked from EVPN route for this path.
 * LPM search is done on the inet route prefix and whatever is the supernet
 * route, NH is picked from there. In case host route is available the path from
 * same takes higher precedence than InetEvpnRoute path.
 */
class InetEvpnRoutePath : public AgentPath {
public:
    InetEvpnRoutePath(const Peer *peer, AgentRoute *rt);
    virtual ~InetEvpnRoutePath() { }
    virtual std::string ToString() const { return "InetEvpnRoutePath"; }
    virtual const AgentPath *UsablePath() const;
    virtual const NextHop *ComputeNextHop(Agent *agent) const;
    //Syncs path parameters. Parent route is used for setting dependant rt.
    virtual bool Sync(AgentRoute *sync_route);
    bool SyncDependantRoute(const AgentRoute *sync_route);

private:
    DISALLOW_COPY_AND_ASSIGN(InetEvpnRoutePath);
};

class InetEvpnRouteData : public AgentRouteData {
public:
    InetEvpnRouteData() : AgentRouteData(false) {
    }
    virtual ~InetEvpnRouteData() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt);
    virtual std::string ToString() const {return "Derived Inet route from Evpn";}

private:
    DISALLOW_COPY_AND_ASSIGN(InetEvpnRouteData);
};
#endif // vnsw_agent_path_hpp
