/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __agent_pkt_flow_info_h_
#define __agent_pkt_flow_info_h_

class VrfEntry;
class Interface;
class VnEntry;
class VmEntry;
class FlowTable;
class FlowEntry;
class AgentRoute;
struct PktInfo;
struct MatchPolicy;

typedef map<int, int> FlowRouteRefMap;

struct PktControlInfo {
    PktControlInfo() : 
        vrf_(NULL), intf_(NULL), rt_(NULL), vn_(NULL), vm_(NULL), 
        vlan_nh_(false), vlan_tag_(0) { }
    virtual ~PktControlInfo() { }

    const VrfEntry *vrf_;
    const Interface *intf_;
    const AgentRoute *rt_;
    const VnEntry *vn_;
    const VmEntry *vm_;
    bool  vlan_nh_;
    uint16_t vlan_tag_;
    // The NH-ID field used as key in the flow
    uint32_t nh_;
};

class PktFlowInfo {
public:
    static const int kLinkLocalInvalidFd = -1;
    static const Ip4Address kDefaultIpv4;
    static const Ip6Address kDefaultIpv6;
    static const int kBgpRouterServiceInvalidFd = -1;

    PktFlowInfo(Agent *a, boost::shared_ptr<PktInfo> info, FlowTable *ftable) :
        l3_flow(false), family(info->family), pkt(info),
        flow_table(ftable), agent(a),
        flow_source_vrf(-1), flow_dest_vrf(-1), nat_done(false),
        nat_ip_saddr(), nat_ip_daddr(), nat_sport(0), nat_dport(0), nat_vrf(0),
        nat_dest_vrf(0), dest_vrf(0), acl(NULL), ingress(false),
        short_flow(false), local_flow(false), linklocal_flow(false),
        tcp_ack(false), linklocal_bind_local_port(false),
        linklocal_src_port_fd(kLinkLocalInvalidFd),
        ecmp(false), out_component_nh_idx(-1),
        fip_snat(false), fip_dnat(false), snat_fip(),
        short_flow_reason(0), peer_vrouter(), tunnel_type(TunnelType::INVALID),
        flood_unknown_unicast(false), bgp_router_service_flow(false),
        alias_ip_flow(false), ttl(0) {
    }

    static bool ComputeDirection(const Interface *intf);
    void CheckLinkLocal(const PktInfo *pkt);
    void LinkLocalServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out);
    void LinkLocalServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                                  PktControlInfo *out);
    void LinkLocalServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                                   PktControlInfo *out);
    void BgpRouterServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out);
    void BgpRouterServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                                   PktControlInfo *out);
    void ProcessHealthCheckFatFlow(const VmInterface *vmi, const PktInfo *pkt,
                                   PktControlInfo *in, PktControlInfo *out);
    void FloatingIpSNat(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void FloatingIpDNat(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void IngressProcess(const PktInfo *pkt, PktControlInfo *in,
                        PktControlInfo *out);
    void EgressProcess(const PktInfo *pkt, PktControlInfo *in,
                       PktControlInfo *out);
    void Add(const PktInfo *pkt, PktControlInfo *in,
             PktControlInfo *out);
    bool Process(const PktInfo *pkt, PktControlInfo *in, PktControlInfo *out);
    bool ValidateConfig(const PktInfo *pkt, PktControlInfo *in);
    static bool GetIngressNwPolicyAclList(const Interface *intf,
                                          const VnEntry *vn,
                                          MatchPolicy *m_policy);
    bool VrfTranslate(const PktInfo *pkt, PktControlInfo *ctrl,
                      PktControlInfo *rev_flow, const IpAddress &src_ip,
                      bool nat_flow);
    void UpdateFipStatsInfo(FlowEntry *flow, FlowEntry *rflow, const PktInfo *p,
                            const PktControlInfo *in, const PktControlInfo *o);
    const NextHop *TunnelToNexthop(const PktInfo *pkt);
    void ChangeVrf(const PktInfo *pkt, PktControlInfo *info,
                   const VrfEntry *vrf);
    bool UnknownUnicastFlow(const PktInfo *p,
                            const PktControlInfo *in_info,
                            const PktControlInfo *out_info);
    void GenerateTrafficSeen(const PktInfo *pkt, const PktControlInfo *in);
    void ApplyFlowLimits(const PktControlInfo *in, const PktControlInfo *out);
    void LinkLocalPortBind(const PktInfo *pkt, const PktControlInfo *in,
                           FlowEntry *flow);
    bool IngressRouteAllowNatLookup(const AgentRoute *in_rt,
                                    const AgentRoute *out_rt,
                                    uint32_t sport,
                                    uint32_t dport,
                                    const Interface *intf);
    bool EgressRouteAllowNatLookup(const AgentRoute *in_rt,
                                   const AgentRoute *out_rt,
                                   uint32_t sport,
                                   uint32_t dport,
                                   const Interface *intf);
public:
    void UpdateRoute(const AgentRoute **rt, const VrfEntry *vrf,
                     const IpAddress &addr, const MacAddress &mac,
                     FlowRouteRefMap &ref_map);
    uint8_t RouteToPrefixLen(const AgentRoute *route);
    void CalculatePort(const PktInfo *p, const Interface *intf);
    void SetPktInfo(boost::shared_ptr<PktInfo> info);
    bool RouteAllowNatLookupCommon(const AgentRoute *rt,
                                   uint32_t sport,
                                   uint32_t dport,
                                   const Interface *intf);
    bool IsBgpRouterServiceRoute(const AgentRoute *in_rt,
                                 const AgentRoute *out_rt,
                                 const Interface *intf,
                                 uint32_t sport,
                                 uint32_t dport);
    void UpdateEvictedFlowStats(const PktInfo *pkt);
    bool                l3_flow;
    Address::Family     family;
    boost::shared_ptr<PktInfo> pkt;
    FlowTable           *flow_table;
    Agent               *agent;

    uint32_t            flow_source_vrf;
    uint32_t            flow_dest_vrf;
    // map for references to the routes which were ignored due to more specific
    // route this will be used to trigger flow re-compute to use more specific
    // on route add. key for the map is vrf and data is prefix length
    FlowRouteRefMap     flow_source_plen_map;
    FlowRouteRefMap     flow_dest_plen_map;

    // NAT addresses
    bool                nat_done;
    IpAddress           nat_ip_saddr;
    IpAddress           nat_ip_daddr;
    uint32_t            nat_sport;
    uint32_t            nat_dport;
    // VRF for matching the NAT flow
    uint32_t            nat_vrf;
    // Modified VRF for the NAT flow
    // After flow processing, packet is assigned this VRF
    uint32_t            nat_dest_vrf;

    // Modified VRF for the forward flow
    // After flow processing, packet is assigned this VRF
    uint32_t            dest_vrf;

    // Intermediate fields used in creating flows
    const AclDBEntry    *acl;

    // Ingress flow or egress flow
    bool                ingress;
    bool                short_flow;
    bool                local_flow;
    bool                linklocal_flow;
    bool                tcp_ack;
    bool                linklocal_bind_local_port;
    int                 linklocal_src_port_fd;

    bool                ecmp;
    uint32_t            out_component_nh_idx;

    // Following fields are required for FIP stats accounting
    bool                fip_snat;
    bool                fip_dnat;
    IpAddress           snat_fip;
    uint16_t            short_flow_reason;
    std::string         peer_vrouter;
    TunnelType          tunnel_type;

    // flow entry obtained from flow IPC, which requires recomputation.
    FlowEntry           *flow_entry;
    bool                 flood_unknown_unicast;

    //BGP router service info
    bool                 bgp_router_service_flow;

    // Alias IP flow
    bool                 alias_ip_flow;
    //TTL of nat'd flow especially bgp-service flows
    uint8_t              ttl;
};

#endif // __agent_pkt_flow_info_h_
