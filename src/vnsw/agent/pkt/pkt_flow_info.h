/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __agent_pkt_flow_info_h_
#define __agent_pkt_flow_info_h_

class VrfEntry;
class Interface;
class Inet4UnicastRouteEntry;
class VnEntry;
class VmEntry;
class FlowTable;
class FlowEntry;
struct PktInfo;
struct MatchPolicy;

struct PktControlInfo {
    PktControlInfo() : 
        vrf_(NULL), intf_(NULL), rt_(NULL), vn_(NULL), vm_(NULL), 
        vlan_nh_(false), vlan_tag_(0) { }
    virtual ~PktControlInfo() { }

    const VrfEntry *vrf_;
    const Interface *intf_;
    const Inet4UnicastRouteEntry *rt_;
    const VnEntry *vn_;
    const VmEntry *vm_;
    bool  vlan_nh_;
    uint16_t vlan_tag_;
};

class PktFlowInfo {
public:
    static const int kLinkLocalInvalidFd = -1;

    PktFlowInfo(boost::shared_ptr<PktInfo> info, FlowTable *ftable): 
        pkt(info), flow_table(ftable), source_vn(NULL), dest_vn(NULL),
        flow_source_vrf(-1), flow_dest_vrf(-1), source_sg_id_l(NULL),
        dest_sg_id_l(NULL), nat_done(false), nat_ip_saddr(0),
        nat_ip_daddr(0), nat_sport(0), nat_dport(0), nat_vrf(0),
        nat_dest_vrf(0), dest_vrf(0), acl(NULL), ingress(false),
        short_flow(false), local_flow(false), linklocal_flow(false),
        tcp_ack(false), linklocal_bind_local_port(false),
        linklocal_src_port_fd(kLinkLocalInvalidFd),
        ecmp(false), in_component_nh_idx(-1), out_component_nh_idx(-1),
        trap_rev_flow(false), source_plen(0), dest_plen(0) {
    }

    static bool ComputeDirection(const Interface *intf);
    void LinkLocalServiceFromVm(const PktInfo *pkt, PktControlInfo *in,
                                PktControlInfo *out);
    void LinkLocalServiceFromHost(const PktInfo *pkt, PktControlInfo *in,
                                  PktControlInfo *out);
    void LinkLocalServiceTranslate(const PktInfo *pkt, PktControlInfo *in,
                                   PktControlInfo *out);
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
    void SetEcmpFlowInfo(const PktInfo *pkt, const PktControlInfo *in,
                         const PktControlInfo *out);
    static bool GetIngressNwPolicyAclList(const Interface *intf,
                                          const VnEntry *vn,
                                          MatchPolicy *m_policy);
    void RewritePktInfo(uint32_t index);
    void VrfTranslate(const PktInfo *pkt, PktControlInfo *ctrl,
                      PktControlInfo *rev_flow);
    uint32_t LinkLocalBindPort(const VmEntry *vm, uint8_t proto);

public:
    boost::shared_ptr<PktInfo> pkt;
    FlowTable *flow_table;

    const std::string   *source_vn;
    const std::string   *dest_vn;
    uint32_t            flow_source_vrf;
    uint32_t            flow_dest_vrf;
    const SecurityGroupList *source_sg_id_l;
    const SecurityGroupList *dest_sg_id_l;

    // NAT addresses
    bool                nat_done;
    uint32_t            nat_ip_saddr;
    uint32_t            nat_ip_daddr;
    uint32_t            nat_sport;
    uint32_t            nat_dport;
    // VRF for matching the NAT flow
    uint16_t            nat_vrf;
    // Modified VRF for the NAT flow
    // After flow processing, packet is assigned this VRF
    uint16_t            nat_dest_vrf;

    // Modified VRF for the forward flow
    // After flow processing, packet is assigned this VRF
    uint16_t            dest_vrf;

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
    uint32_t            in_component_nh_idx;
    uint32_t            out_component_nh_idx;
    bool                trap_rev_flow;
    uint8_t             source_plen;
    uint8_t             dest_plen;
};

#endif // __agent_pkt_flow_info_h_
