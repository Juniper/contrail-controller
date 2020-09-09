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
struct InterfaceKey;
class PhysicalInterface;
class Peer;
class EvpnPeer;
class EcmpLoadBalance;
class TsnElector;

class PathPreference {
public:
    enum Preference {
        INVALID = 0,
        HA_STALE = 1,
        LOW = 100,
        HIGH = 200
    };
    PathPreference(): sequence_(0), preference_(LOW),
        wait_for_traffic_(true), ecmp_(false), static_preference_(false),
        dependent_ip_(Ip4Address(0)) {}
    PathPreference(uint32_t sequence, uint32_t preference,
        bool wait_for_traffic, bool ecmp): sequence_(sequence),
        preference_(preference), wait_for_traffic_(wait_for_traffic),
        ecmp_(ecmp), static_preference_(false), dependent_ip_(Ip4Address(0)) {}
    uint32_t sequence() const { return sequence_;}
    uint32_t preference() const { return preference_;}
    bool wait_for_traffic() const {
        return wait_for_traffic_;
    }
    bool ecmp() const {
        return ecmp_;
    }

    bool is_ecmp() const {
        if ((preference_ == HIGH && sequence_ == 0)) {
            return true;
        }

        if (static_preference_) {
            return false;
        }

        if (ecmp_ == true) {
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
    void set_preference(uint32_t preference) {
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

    bool operator==(const PathPreference &rhs) const {
        if (preference_ == rhs.preference_ &&
            sequence_ == rhs.sequence_) {
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
    uint32_t preference_;
    bool wait_for_traffic_;
    bool ecmp_;
    bool static_preference_;
    IpAddress dependent_ip_;
    std::string vrf_;
};

//Route data to change preference and sequence number of path
struct PathPreferenceData : public AgentRouteData {
    PathPreferenceData(const PathPreference &path_preference):
        AgentRouteData(ROUTE_PREFERENCE_CHANGE, false, 0),
        path_preference_(path_preference) { }
    virtual std::string ToString() const {
        return "";
    }
    virtual bool AddChangePathExtended(Agent*, AgentPath*, const AgentRoute*);
    PathPreference path_preference_;
};

class AgentPathEcmpComponent {
public:
    AgentPathEcmpComponent(IpAddress addr, uint32_t label, AgentRoute *rt);
    virtual ~AgentPathEcmpComponent() { }
    bool Unresolved() {return unresolved;}

    bool operator == (const AgentPathEcmpComponent &rhs) const {
        if (addr_ == rhs.addr_ && label_ == rhs.label_) {
            return true;
        }

        return false;
    }
    IpAddress GetGwIpAddr() { return addr_;}
    uint32_t GetLabel() {return label_;}
    void UpdateDependentRoute(AgentRoute *rt) {
        if (rt) {
            dependent_rt_.reset(rt);
        } else {
            dependent_rt_.clear();
        }
    }

    void SetUnresolved(bool flag) {
        unresolved = flag;
    }

private:
    IpAddress addr_;
    uint32_t label_;
    DependencyRef<AgentRoute, AgentRoute> dependent_rt_;
    bool unresolved;
    DISALLOW_COPY_AND_ASSIGN(AgentPathEcmpComponent);
};

typedef boost::shared_ptr<AgentPathEcmpComponent> AgentPathEcmpComponentPtr;
typedef std::vector<AgentPathEcmpComponentPtr> AgentPathEcmpComponentPtrList;
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
    void ImportPrevActiveNH(Agent *agent, NextHop *nh);
    virtual bool PostChangeNH(Agent *agent, NextHop *nh);

    const SecurityGroupList &sg_list() const {return sg_list_;}
    const TagList &tag_list() const {return tag_list_;}
    const CommunityList &communities() const {return communities_;}
    const std::string &dest_vn_name() const {
        assert(dest_vn_list_.size() <= 1);
        if (dest_vn_list_.size())
            return *dest_vn_list_.begin();
        else
            return Agent::NullString();
    }
    const VnListType &dest_vn_list() const {return dest_vn_list_;}
    const std::string &origin_vn() const {return origin_vn_;}
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
    void set_origin_vn(const std::string &origin_vn) {origin_vn_ = origin_vn;}
    void set_unresolved(bool unresolved) {unresolved_ = unresolved;};
    void set_gw_ip(const IpAddress &addr) {gw_ip_ = addr;}
    void set_force_policy(bool force_policy) {force_policy_ = force_policy;}
    void set_vrf_name(const std::string &vrf_name) {vrf_name_ = vrf_name;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    void set_tunnel_type(TunnelType::Type type) {tunnel_type_ = type;}
    void set_sg_list(const SecurityGroupList &sg) {sg_list_ = sg;}
    void set_tag_list(const TagList &tag) {tag_list_ = tag;}
    void set_communities(const CommunityList &communities) {communities_ = communities;}
    void clear_sg_list() { sg_list_.clear(); }
    void clear_tag_list() { tag_list_.clear(); }
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

    void set_copy_local_path(bool copy_local_path) {
        copy_local_path_ = copy_local_path;
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
    bool ResolveGwNextHops(Agent *agent, const AgentRoute *sync_route);
    bool RebakeAllTunnelNHinCompositeNH(const AgentRoute *sync_route);
    virtual std::string ToString() const { return "AgentPath"; }
    void SetSandeshData(PathSandeshData &data) const;
    uint32_t preference() const { return path_preference_.preference();}
    uint32_t sequence() const { return path_preference_.sequence();}
    const PathPreference& path_preference() const { return path_preference_;}
    PathPreference& path_preference_non_const() { return path_preference_;}
    void set_path_preference(const PathPreference &rp) { path_preference_ = rp;}
    void set_composite_nh_key(CompositeNHKey *key) {
        composite_nh_key_.reset(key);
    }
    CompositeNHKey* composite_nh_key() {
        return composite_nh_key_.get();
    }
    bool ReorderCompositeNH(Agent *agent, CompositeNHKey *nh,
                            bool &comp_nh_policy,
                            const AgentPath *local_path);
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

    bool is_health_check_service() const {
        return is_health_check_service_;
    }

    void set_is_health_check_service(bool val) {
        is_health_check_service_ = val;
    }

    bool etree_leaf() const {
        return etree_leaf_;
    }

    void set_etree_leaf(bool leaf) {
        etree_leaf_ = leaf;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

    void set_layer2_control_word(bool layer2_control_word) {
        layer2_control_word_ = layer2_control_word;
    }

    void set_native_vrf_id(uint32_t vrf_id) {
        native_vrf_id_ = vrf_id;
    }

    uint32_t native_vrf_id() const {
        return native_vrf_id_;
    }

    void UpdateEcmpHashFields(const Agent *agent,
                              const EcmpLoadBalance &ecmp_load_balance,
                              DBRequest &nh_req);
    uint64_t peer_sequence_number() const {return peer_sequence_number_;}
    void set_peer_sequence_number(uint64_t sequence_number) {
        peer_sequence_number_ = sequence_number;
    }
    bool ResyncControlWord(const AgentRoute *rt);
    bool inactive() const {return inactive_;}
    void set_inactive(bool inactive) {
        inactive_ = inactive;
    }

    void ResetEcmpHashFields();
    void CopyLocalPath(CompositeNHKey *composite_nh_key,
                       const AgentPath *local_path);
    AgentRoute *GetParentRoute() {return parent_rt_;}
    void ResetEcmpMemberList(AgentPathEcmpComponentPtrList list) {
        ecmp_member_list_ = list;
    }
    AgentRouteTable *GetDependentTable() const {return dependent_table_;}
    void SetDependentTable(AgentRouteTable *table) {
        dependent_table_ = table;
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
    // Origin VN name to be populated in flows
    std::string origin_vn_;

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
    TagList tag_list_;
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
    // if the path is marked service health check set vrouter to do proxy arp
    // TODO(prabhjot) need to find more genric solution for marking proxy arp
    // to move all the logic of identifing proxy arp in one place
    bool is_health_check_service_;
    // These Ecmp fields will hold the corresponding composite nh ecmp fields reference
    // if the path's ecmp load balance field is not set
    EcmpHashFields ecmp_hash_fields_;
    uint64_t peer_sequence_number_;
    //Is the path an etree leaf or root
    bool etree_leaf_;
    //Should control word of 4 bytes be inserted while egressing the
    //packet, which could be used by receiving router to determine
    //if its a l2 packet or l3 packet.
    bool layer2_control_word_;
    bool inactive_;
    bool copy_local_path_;
    //Valid for routes exported in ip-fabric:__default__ VRF
    //Indicates the VRF from which routes was originated
    uint32_t  native_vrf_id_;
    AgentRoute *parent_rt_;
    AgentPathEcmpComponentPtrList ecmp_member_list_;
    AgentRouteTable *dependent_table_;
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
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
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

    virtual bool CanDeletePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) const;
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
                 const std::string &vn_name, const SecurityGroupList &sg_list,
                 const TagList &tag_list) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        intf_key_(key->Clone()), policy_(policy),
        label_(label), dest_vn_name_(vn_name), path_sg_list_(sg_list),
        path_tag_list_(tag_list) {}
    virtual ~ResolveRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "Resolve";}
private:
    boost::scoped_ptr<const InterfaceKey> intf_key_;
    bool policy_;
    uint32_t label_;
    const std::string dest_vn_name_;
    const SecurityGroupList path_sg_list_;
    const TagList path_tag_list_;
    DISALLOW_COPY_AND_ASSIGN(ResolveRoute);
};

class LocalVmRoute : public AgentRouteData {
public:
    LocalVmRoute(const VmInterfaceKey &intf, uint32_t mpls_label,
                 uint32_t vxlan_id, bool force_policy, const VnListType &vn_list,
                 uint8_t flags, const SecurityGroupList &sg_list,
                 const TagList &tag_list,
                 const CommunityList &communities,
                 const PathPreference &path_preference,
                 const IpAddress &subnet_service_ip,
                 const EcmpLoadBalance &ecmp_load_balance, bool is_local,
                 bool is_health_check_service, uint64_t sequence_number,
                 bool etree_leaf, bool native_encap,
                 const std::string &intf_route_type = ""):
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, sequence_number),
        intf_(intf), mpls_label_(mpls_label),
        vxlan_id_(vxlan_id), force_policy_(force_policy),
        dest_vn_list_(vn_list), proxy_arp_(false), sync_route_(false),
        flags_(flags), sg_list_(sg_list), tag_list_(tag_list),
        communities_(communities),
        tunnel_bmap_(TunnelType::MplsType()),
        path_preference_(path_preference),
        subnet_service_ip_(subnet_service_ip),
        ecmp_load_balance_(ecmp_load_balance), is_local_(is_local),
        is_health_check_service_(is_health_check_service),
        etree_leaf_(etree_leaf), native_encap_(native_encap),
        intf_route_type_(intf_route_type), native_vrf_id_(VrfEntry::kInvalidIndex) {
    }
    virtual ~LocalVmRoute() { }
    void DisableProxyArp() {proxy_arp_ = false;}
    virtual std::string ToString() const {return "local VM";}
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual bool UpdateRoute(AgentRoute *rt);
    const CommunityList &communities() const {return communities_;}
    const SecurityGroupList &sg_list() const {return sg_list_;}
    const TagList &tag_list() const {return tag_list_;}
    void set_tunnel_bmap(TunnelType::TypeBmap bmap) {tunnel_bmap_ = bmap;}
    const PathPreference& path_preference() const { return path_preference_;}
    void set_path_preference(const PathPreference &path_preference) {
        path_preference_ = path_preference;
    }
    uint32_t vxlan_id() const {return vxlan_id_;}
    uint32_t tunnel_bmap() const {return tunnel_bmap_;}
    bool proxy_arp() const {return proxy_arp_;}
    bool etree_leaf() const { return etree_leaf_;}
    const std::string &intf_route_type() const { return intf_route_type_; }
    void set_native_vrf_id(uint32_t vrf_id) {
        native_vrf_id_ = vrf_id;
    }
    uint32_t native_vrf_id() const {
        return native_vrf_id_;
    }
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
    TagList tag_list_;
    CommunityList communities_;
    TunnelType::TypeBmap tunnel_bmap_;
    PathPreference path_preference_;
    IpAddress subnet_service_ip_;
    EcmpLoadBalance ecmp_load_balance_;
    bool is_local_;
    bool is_health_check_service_;
    bool etree_leaf_;
    bool native_encap_;
    std::string intf_route_type_;
    uint32_t  native_vrf_id_;
    DISALLOW_COPY_AND_ASSIGN(LocalVmRoute);
};

class PBBRoute : public AgentRouteData {
public:
    PBBRoute(const VrfKey &vrf_key, const MacAddress &mac, uint32_t isid,
             const VnListType &vn_list, const SecurityGroupList &sg_list,
             const TagList &tag_list):
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        vrf_key_(vrf_key), dmac_(mac), isid_(isid),
        dest_vn_list_(vn_list), sg_list_(sg_list), tag_list_(tag_list) {
    }

    virtual ~PBBRoute() { }
    virtual std::string ToString() const {return "PBB route";}
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    const SecurityGroupList &sg_list() const {return sg_list_;}
    const TagList &tag_list() const {return tag_list_;}

private:
    VrfKey vrf_key_;
    MacAddress dmac_;
    uint32_t isid_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    PathPreference path_preference_;
    DISALLOW_COPY_AND_ASSIGN(PBBRoute);
};

class InetInterfaceRoute : public AgentRouteData {
public:
    InetInterfaceRoute(const InetInterfaceKey &intf, uint32_t label,
                       int tunnel_bmap, const VnListType &dest_vn_list,
                       uint64_t sequence_number):
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, sequence_number),
        intf_(intf), label_(label), tunnel_bmap_(tunnel_bmap),
        dest_vn_list_(dest_vn_list) {
    }
    virtual ~InetInterfaceRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
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
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        intf_(intf), dest_vn_name_(dest_vn_name),
        proxy_arp_(false), policy_(false) {
    }
    virtual ~HostRoute() { }
    void set_proxy_arp() {proxy_arp_ = true;}
    void set_policy(bool policy) {
        policy_ = policy;
    }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "host";}
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    PacketInterfaceKey intf_;
    std::string dest_vn_name_;
    bool proxy_arp_;
    bool policy_;
    DISALLOW_COPY_AND_ASSIGN(HostRoute);
};

class L2ReceiveRoute : public AgentRouteData {
public:
    L2ReceiveRoute(const std::string &dest_vn_name, uint32_t vxlan_id,
                   uint32_t mpls_label, const PathPreference &pref,
                   uint64_t sequence_number) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, sequence_number),
        dest_vn_name_(dest_vn_name), vxlan_id_(vxlan_id),
        mpls_label_(mpls_label), path_preference_(pref) {
    }
    virtual ~L2ReceiveRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
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
                const TagList &tag_list,
                const PathPreference &path_preference, uint64_t sequence_number) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, sequence_number),
        intf_(intf), tag_(tag), label_(label),
        dest_vn_list_(dest_vn_list), sg_list_(sg_list), tag_list_(tag_list),
        path_preference_(path_preference), tunnel_bmap_(TunnelType::MplsType()) {
    }
    virtual ~VlanNhRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "vlannh";}

private:
    VmInterfaceKey intf_;
    uint16_t tag_;
    uint32_t label_;
    VnListType dest_vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    PathPreference path_preference_;
    TunnelType::TypeBmap tunnel_bmap_;
    DISALLOW_COPY_AND_ASSIGN(VlanNhRoute);
};

class MulticastRoutePath : public AgentPath {
public:
    MulticastRoutePath(const Peer *peer);
    virtual ~MulticastRoutePath() { }

    virtual bool PostChangeNH(Agent *agent, NextHop *nh);
    void set_original_nh(NextHopRef nh) {
        original_nh_ = nh;
    }
    NextHopRef original_nh() const {
        return original_nh_;
    }
    NextHop *UpdateNH(Agent *agent, CompositeNH *cnh,
                      const TsnElector *te);
    void UpdateLabels(uint32_t evpn_label, uint32_t fabric_label) {
        evpn_label_ = evpn_label;
        fabric_label_ = fabric_label;
    }

private:
    NextHopRef original_nh_;
    uint32_t evpn_label_;
    uint32_t fabric_label_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoutePath);
};

class MulticastRoute : public AgentRouteData {
public:
    MulticastRoute(const string &vn_name, uint32_t label, int vxlan_id,
                   uint32_t tunnel_type, DBRequest &nh_req,
                   COMPOSITETYPE comp_nh_type, uint64_t sequence_number,
                   bool ha_stale = false):
    AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, true, sequence_number),
    vn_name_(vn_name), label_(label), vxlan_id_(vxlan_id),
    tunnel_type_(tunnel_type), comp_nh_type_(comp_nh_type),
    ha_stale_(ha_stale) {
        composite_nh_req_.Swap(&nh_req);
    }
    virtual ~MulticastRoute() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
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
                                   NextHop *nh,
                                   const AgentRoute *rt,
                                   bool ha_stale = false);
    virtual bool CanDeletePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) const;
private:
    string vn_name_;
    uint32_t label_;
    uint32_t vxlan_id_;
    uint32_t tunnel_type_;
    DBRequest composite_nh_req_;
    COMPOSITETYPE comp_nh_type_;
    bool ha_stale_;
    DISALLOW_COPY_AND_ASSIGN(MulticastRoute);
};

class IpamSubnetRoute : public AgentRouteData {
public:
    IpamSubnetRoute(DBRequest &nh_req, const std::string &dest_vn_name);
    virtual ~IpamSubnetRoute() {}
    virtual string ToString() const {return "subnet route";}
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    DBRequest nh_req_;
    std::string dest_vn_name_;
    DISALLOW_COPY_AND_ASSIGN(IpamSubnetRoute);
};

class ReceiveRoute : public AgentRouteData {
public:
    ReceiveRoute(const InterfaceKey &intf_key, uint32_t label,
                 uint32_t tunnel_bmap, bool policy, const std::string &vn) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        label_(label), tunnel_bmap_(tunnel_bmap),
        policy_(policy), proxy_arp_(false), ipam_host_route_(false), vn_(vn), sg_list_(), tag_list_() {
        intf_.reset(intf_key.Clone());
    }
    virtual ~ReceiveRoute() { }

    void SetProxyArp(bool proxy_arp) { proxy_arp_ = proxy_arp; }
    void SetIpamHostRoute(bool ipam_host_route) { ipam_host_route_ = ipam_host_route; }

    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "receive";}
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    boost::scoped_ptr<InterfaceKey> intf_;
    uint32_t label_;
    int tunnel_bmap_;
    bool policy_;
    bool proxy_arp_;
    bool ipam_host_route_;
    std::string vn_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    DISALLOW_COPY_AND_ASSIGN(ReceiveRoute);
};

class Inet4UnicastArpRoute : public AgentRouteData {
public:
    Inet4UnicastArpRoute(const std::string &vrf_name,
                         const Ip4Address &addr, bool policy,
                         const VnListType &vn_list, const SecurityGroupList &sg,
                         const TagList &tag) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        vrf_name_(vrf_name), addr_(addr), policy_(policy),
        vn_list_(vn_list), sg_list_(sg), tag_list_(tag) {
    }
    virtual ~Inet4UnicastArpRoute() { }

    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "arp";}
private:
    std::string vrf_name_;
    Ip4Address addr_;
    bool policy_;
    VnListType vn_list_;
    SecurityGroupList sg_list_;
    TagList tag_list_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastArpRoute);
};

class Inet4UnicastGatewayRoute : public AgentRouteData {
public:
    Inet4UnicastGatewayRoute(const IpAddress &gw_ip,
                             const std::string &vrf_name,
                             const VnListType &vn_list,
                             uint32_t label, const SecurityGroupList &sg,
                             const TagList &tag,
                             const CommunityList &communities,
                             bool native_encap):
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        gw_ip_(gw_ip), vrf_name_(vrf_name), vn_list_(vn_list),
        mpls_label_(label), sg_list_(sg), tag_list_(tag), communities_(communities),
        native_encap_(native_encap) {
    }
    virtual ~Inet4UnicastGatewayRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "gateway";}

private:
    IpAddress gw_ip_;
    std::string vrf_name_;
    VnListType vn_list_;
    uint32_t mpls_label_;
    const SecurityGroupList sg_list_;
    const TagList tag_list_;
    const CommunityList communities_;
    bool native_encap_;
    DISALLOW_COPY_AND_ASSIGN(Inet4UnicastGatewayRoute);
};

class DropRoute : public AgentRouteData {
public:
    DropRoute(const string &vn_name) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        vn_(vn_name) { }
    virtual ~DropRoute() { }
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "drop";}
private:
    std::string vn_;
    DISALLOW_COPY_AND_ASSIGN(DropRoute);
};

class Inet4UnicastInterfaceRoute : public AgentRouteData {
public:
    Inet4UnicastInterfaceRoute(const PhysicalInterface *intrface,
                               const std::string &vn_name);
    virtual ~Inet4UnicastInterfaceRoute() { }

    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
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
    MacVmBindingPathData(const VmInterface *vm_intf, bool flood_dhcp) :
        AgentRouteData(AgentRouteData::ADD_DEL_CHANGE, false, 0),
        vm_intf_(vm_intf), flood_dhcp_(flood_dhcp) { }
    virtual ~MacVmBindingPathData() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "MacVmBindingPathData";}

private:
    const VmInterface *vm_intf_;
    bool flood_dhcp_;
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
    InetEvpnRoutePath(const Peer  *peer,
            const MacAddress &mac,
            const std::string &parent,
            AgentRoute *rt);
    virtual ~InetEvpnRoutePath() { }
    virtual std::string ToString() const { return "InetEvpnRoutePath"; }
    virtual const AgentPath *UsablePath() const;
    virtual const NextHop *ComputeNextHop(Agent *agent) const;
    //Syncs path parameters. Parent route is used for setting dependant rt.
    virtual bool Sync(AgentRoute *sync_route);
    bool SyncDependantRoute(const AgentRoute *sync_route);
    virtual bool IsLess(const AgentPath &rhs) const;

    const MacAddress &MacAddr() const {return mac_;}
    void SetMacAddr(const MacAddress &mac) {mac_ = mac;}
    const std::string &Parent() const {return parent_;}
    void SetParent(const std::string &parent) {parent_ = parent;}


private:
    MacAddress mac_;
    std::string parent_;
    DISALLOW_COPY_AND_ASSIGN(InetEvpnRoutePath);
};

class InetEvpnRouteData : public AgentRouteData {
public:
    InetEvpnRouteData(const EvpnRouteEntry *evpn_rt);
    virtual ~InetEvpnRouteData() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "Derived Inet route from Evpn";}
    const MacAddress &MacAddr() const {return mac_;}
    const std::string &parent() const {return parent_;}
    virtual bool CanDeletePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) const;

private:
    const MacAddress mac_;
    std::string parent_;
    DISALLOW_COPY_AND_ASSIGN(InetEvpnRouteData);
};

// EvpnRoutingData
// Used to do vxlan routing, will be used for following-
// 1) Adding inet route to L2 VRF inet table.
// 2) Adding evpn route(type 5) in routing vrf
// 3) Adding inet route to routing vrf inet table.
class EvpnRoutingData : public AgentRouteData {
public:
    EvpnRoutingData(DBRequest &nh_req,
                    const SecurityGroupList &sg_list,
                    const CommunityList &communities,
                    const PathPreference &path_preference,
                    const EcmpLoadBalance &ecmp_load_balance,
                    const TagList &tag_list,
                    VrfEntryConstRef vrf_entry,
                    uint32_t vxlan_id,
                    const VnListType& vn_list,
                    const std::string& origin_vn = "");
    virtual ~EvpnRoutingData() { }
    virtual AgentPath *CreateAgentPath(const Peer *peer, AgentRoute *rt) const;
    virtual bool AddChangePathExtended(Agent *agent, AgentPath *path,
                                       const AgentRoute *rt);
    virtual std::string ToString() const {return "Evpn routing data";}
    //Checks if path is evpnrouting path and deletes leak route before path gets
    //destroyed.
    virtual bool CanDeletePath(Agent *agent, AgentPath *path,
                               const AgentRoute *rt) const;
    virtual bool UpdateRoute(AgentRoute *rt);

private:
    DBRequest nh_req_;
    const SecurityGroupList sg_list_;
    const CommunityList communities_;
    const PathPreference path_preference_;
    const EcmpLoadBalance ecmp_load_balance_;
    const TagList tag_list_;
    VrfEntryConstRef routing_vrf_;
    uint32_t vxlan_id_;
    const VnListType dest_vn_list_;
    const std::string origin_vn_;
    DISALLOW_COPY_AND_ASSIGN(EvpnRoutingData);
};

class EvpnRoutingPath : public AgentPath {
public:
    EvpnRoutingPath(const Peer *peer, AgentRoute *rt,
                    VrfEntryConstRef routing_vrf);
    virtual ~EvpnRoutingPath();

    const VrfEntry *routing_vrf() const;
    void set_routing_vrf(const VrfEntry *vrf);
    void DeleteEvpnType5Route(Agent *agent, const AgentRoute *rt) const;

private:
    VrfEntryConstRef routing_vrf_;
    uint32_t l3_vrf_vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(EvpnRoutingPath);
};

#endif // vnsw_agent_path_hpp
