/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_handler_hpp
#define vnsw_agent_pkt_handler_hpp

#include <net/if.h>
#include <sys/socket.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/ip_icmp.h>

#include <tbb/atomic.h>
#include <boost/array.hpp>

#include <net/address.h>
#include <net/mac_address.h>
#include <oper/mirror_table.h>
#include <oper/nexthop.h>
#include <pkt/pkt_trace.h>
#include <pkt/packet_buffer.h>

#include "vr_defs.h"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCPV6_SERVER_PORT 547
#define DHCPV6_CLIENT_PORT 546
#define DNS_SERVER_PORT 53
#define VXLAN_UDP_DEST_PORT 4789
#define MPLS_OVER_UDP_DEST_PORT      51234
#define IANA_MPLS_OVER_UDP_DEST_PORT 6635

#define IPv4_ALEN           4
#define ARP_TX_BUFF_LEN     128
#define IPC_HDR_LEN        (sizeof(struct ether_header) + sizeof(struct agent_hdr))
#define VLAN_PROTOCOL      0x8100
#define DEFAULT_IP_TTL     64
//Ideally VM is one hop away but traffic gets routed so use 2.
#define BGP_SERVICE_TTL_REV_FLOW 2
#define BGP_SERVICE_TTL_FWD_FLOW 255
#define DEFAULT_IP_ID      0
#define VLAN_HDR_LEN       4

#define ICMP_UNREACH_HDR_LEN 8

#define ETHERTYPE_QINQ    0x88A8
#define ETHERTYPE_PBB     0x88E7
#define PBB_HEADER_LEN    4

struct PktInfo;
struct agent_hdr;
class PacketBuffer;
class Proto;
class EcmpLoadBalance;
typedef boost::shared_ptr<PktInfo> PktInfoPtr;

struct InterTaskMsg {
    InterTaskMsg(uint16_t command): cmd(command) {}
    virtual ~InterTaskMsg() {}

    uint16_t cmd;
};

struct GreHdr {
    GreHdr() : flags(), protocol() {}
    ~GreHdr() {}

    uint16_t flags;
    uint16_t protocol;
};

struct MplsHdr {
    MplsHdr() : hdr() {}
    ~MplsHdr() {}

    uint32_t hdr;
};

struct VxlanHdr {
    VxlanHdr() : reserved(0), vxlan_id(0) { }
    VxlanHdr(uint32_t id) : reserved(0), vxlan_id((id) << 8) { }
    ~VxlanHdr() { }

    uint32_t reserved;
    uint32_t vxlan_id;
};

struct PktType {
    enum Type {
        INVALID,
        ARP,
        IP,
        UDP,
        TCP,
        ICMP,
        ICMPV6,
        NON_IP,
        MESSAGE,
        SCTP
    };
};

struct sctphdr {
	 u_int16_t th_sport;
         u_int16_t th_dport;
         u_int32_t vtag;
};

struct AgentHdr {
    // Packet commands between agent and vrouter. The values must be in-sync 
    // with vrouter/include/vr_defs.h
    enum PktCommand {
        TX_SWITCH = AGENT_CMD_SWITCH,
        TX_ROUTE = AGENT_CMD_ROUTE,
        TRAP_ARP = AGENT_TRAP_ARP,
        TRAP_L2_PROTOCOL = AGENT_TRAP_L2_PROTOCOLS,
        TRAP_NEXTHOP = AGENT_TRAP_NEXTHOP,
        TRAP_RESOLVE = AGENT_TRAP_RESOLVE,
        TRAP_FLOW_MISS = AGENT_TRAP_FLOW_MISS,
        TRAP_L3_PROTOCOLS = AGENT_TRAP_L3_PROTOCOLS,
        TRAP_DIAG = AGENT_TRAP_DIAG,
        TRAP_UNUSED_1,
        TRAP_SOURCE_MISMATCH = AGENT_TRAP_SOURCE_MISMATCH,
        TRAP_HANDLE_DF = AGENT_TRAP_HANDLE_DF,
        TRAP_TOR_CONTROL_PKT = AGENT_TRAP_TOR_CONTROL_PKT,
        TRAP_ZERO_TTL = AGENT_TRAP_ZERO_TTL,
        TRAP_ICMP_ERROR = AGENT_TRAP_ICMP_ERROR,
        TRAP_FLOW_ACTION_HOLD = AGENT_TRAP_FLOW_ACTION_HOLD,
        TRAP_ROUTER_ALERT = AGENT_TRAP_ROUTER_ALERT,
        TRAP_MAC_LEARN = AGENT_TRAP_MAC_LEARN,
        TRAP_MAC_MOVE = AGENT_TRAP_MAC_MOVE,
        INVALID = MAX_AGENT_HDR_COMMANDS
    };

    enum PktCommandParams {
        PACKET_CMD_PARAM_CTRL = CMD_PARAM_PACKET_CTRL,
        PACKET_CMD_PARAM_DIAG = CMD_PARAM_1_DIAG,
        MAX_PACKET_CMD_PARAM  = MAX_CMD_PARAMS,
    };

    AgentHdr() :
        ifindex(-1), vrf(-1), cmd(-1), cmd_param(-1), cmd_param_1(-1),
        cmd_param_2(0), cmd_param_3(0), cmd_param_4(0), cmd_param_5(0),
        nh(-1), flow_index(-1), mtu(0) {}

    AgentHdr(uint32_t ifindex_p, uint32_t vrf_p, uint16_t cmd_p) :
        ifindex(ifindex_p), vrf(vrf_p), cmd(cmd_p), cmd_param(-1),
        cmd_param_1(-1), cmd_param_2(0), cmd_param_3(0), cmd_param_4(0),
        cmd_param_5(0), nh(-1), flow_index(-1), mtu(0) {}

    AgentHdr(uint32_t ifindex_p, uint32_t vrf_p, uint16_t cmd_p,
             uint32_t param1, uint32_t param2) :
        ifindex(ifindex_p), vrf(vrf_p), cmd(cmd_p), cmd_param(param1),
        cmd_param_1(param2), cmd_param_2(0), cmd_param_3(0), cmd_param_4(0),
        cmd_param_5(0), nh(-1), flow_index(-1), mtu(0) {}

    ~AgentHdr() {}

    // Fields from agent_hdr
    uint32_t            ifindex;
    uint32_t            vrf;
    uint16_t            cmd;
    uint32_t            cmd_param;
    uint32_t            cmd_param_1;
    uint32_t            cmd_param_2;
    uint32_t            cmd_param_3;
    uint32_t            cmd_param_4;
    uint8_t             cmd_param_5;
    uint32_t            nh;
    uint32_t            flow_index;
    uint16_t            mtu;
};

// Tunnel header decoded from the MPLSoGRE/MPLSoUDP encapsulated packet on
// fabric. Supports only IPv4 addresses since only IPv4 is supported on fabric
struct TunnelInfo {
    TunnelInfo() : type(TunnelType::INVALID) { Reset(); }
    ~TunnelInfo() {}

    void Reset() {
        type = TunnelType::INVALID;
        label = -1;
        vxlan_id = -1;
        src_port = 0;
        ip_saddr = 0;
        ip_daddr = 0;
        eth = NULL;
        ip = NULL;
    }

    TunnelType          type;
    uint32_t            label;      // Valid only for MPLSoGRE and MPLSoUDP
    uint32_t            vxlan_id;   // Valid only for VXLAN
    uint16_t            src_port;   // Valid only for VXLAN and MPLSoUDP
    uint32_t            ip_saddr;
    uint32_t            ip_daddr;
    struct ether_header *eth;
    struct ip           *ip;
};

// Receive packets from the pkt0 (tap) interface, parse and send the packet to
// appropriate protocol task. Also, protocol tasks can send packets to pkt0
// or to other tasks.
class PktHandler {
public:
    typedef boost::function<bool(boost::shared_ptr<PktInfo>)> RcvQueueFunc;
    typedef boost::function<void(PktTrace::Pkt &)> PktTraceCallback;

    static const uint32_t kMulticastControlWord = 0;
    static const uint32_t kMulticastControlWordSize = 4;
    enum PktModuleName {
        INVALID,
        FLOW,
        ARP,
        DHCP,
        DHCPV6,
        DNS,
        ICMP,
        ICMPV6,
        DIAG,
        ICMP_ERROR,
        ICMPV6_ERROR,
        RX_PACKET,
        MAC_LEARNING,
        MAX_MODULES
    };

    struct PktStats {
        uint32_t sent[MAX_MODULES];
        uint32_t received[MAX_MODULES];
        uint32_t q_threshold_exceeded[MAX_MODULES];
        uint32_t dropped;
        void Reset() {
            for (int i = 0; i < MAX_MODULES; ++i) {
                sent[i] = received[i] = q_threshold_exceeded[i] = 0;
            }
            dropped = 0;
        }
        PktStats() { Reset(); }
        void PktRcvd(PktModuleName mod);
        void PktSent(PktModuleName mod);
        void PktQThresholdExceeded(PktModuleName mod);
    };

    struct PacketBufferEnqueueItem {
        const AgentHdr hdr;
        const PacketBufferPtr buff;

        PacketBufferEnqueueItem(const AgentHdr &h, const PacketBufferPtr &b)
            : hdr(h), buff(b) {}
    };
    typedef WorkQueue<boost::shared_ptr<PacketBufferEnqueueItem> >
        PktHandlerQueue;

    PktHandler(Agent *, PktModule *pkt_module);
    virtual ~PktHandler();

    void Register(PktModuleName type, RcvQueueFunc cb);
    void Register(PktModuleName type, Proto *proto);

    void Send(const AgentHdr &hdr, const PacketBufferPtr &buff);

    PktModuleName ParsePacket(const AgentHdr &hdr, PktInfo *pkt_info,
                              uint8_t *pkt);
    int ParseUserPkt(PktInfo *pkt_info, Interface *intf,
                     PktType::Type &pkt_type, uint8_t *pkt);
    bool ProcessPacket(boost::shared_ptr<PacketBufferEnqueueItem> item);
// identify pkt type and send to the registered handler
    void HandleRcvPkt(const AgentHdr &hdr, const PacketBufferPtr &buff);
    void SendMessage(PktModuleName mod, InterTaskMsg *msg); 

    bool IsGwPacket(const Interface *intf, const IpAddress &dst_ip);

    const PktStats &GetStats() const { return stats_; }
    void ClearStats() { stats_.Reset(); }
    void PktTraceIterate(PktModuleName mod, PktTraceCallback cb);
    void PktTraceClear(PktModuleName mod) { pkt_trace_.at(mod).Clear(); }
    void PktTraceBuffers(PktModuleName mod, uint32_t buffers) {
        pkt_trace_.at(mod).set_num_buffers(buffers);
    }
    uint32_t PktTraceBuffers(PktModuleName mod) const {
        return pkt_trace_.at(mod).num_buffers();
    }
    uint32_t PktTraceSize(PktModuleName mod) const {
        return pkt_trace_.at(mod).pkt_trace_size();
    }
    void AddPktTrace(PktModuleName module, PktTrace::Direction dir,
                     const PktInfo *pkt);

    uint32_t EncapHeaderLen() const;
    Agent *agent() const { return agent_; }
    PktModule *pkt_module() const { return pkt_module_; }
    void Enqueue(PktModuleName module, boost::shared_ptr<PktInfo> pkt_info);
    bool IsFlowPacket(PktInfo *pkt_info);
    void CalculatePort(PktInfo *pkt_info);
    const PktHandlerQueue *work_queue() const { return &work_queue_; }

private:
    void PktModuleEnqueue(PktModuleName mod, const AgentHdr &hdr,
                          boost::shared_ptr<PktInfo> pkt_info, uint8_t *pkt);
    int ParseEthernetHeader(PktInfo *pkt_info, uint8_t *pkt);
    int ParseMplsHdr(PktInfo *pkt_info, uint8_t *pkt);
    int ParseIpPacket(PktInfo *pkt_info, PktType::Type &pkt_type,
                      uint8_t *ptr);

    int ParseMPLSoGRE(PktInfo *pkt_info, uint8_t *pkt);
    int ParseMPLSoUDP(PktInfo *pkt_info, uint8_t *pkt);
    int ParseControlWord(PktInfo *pkt_info, uint8_t *pkt,
                         const MplsLabel *mpls);
    int ParseUDPTunnels(PktInfo *pkt_info, uint8_t *pkt);
    int ParseVxlan(PktInfo *pkt_info, uint8_t *pkt);
    int ParseUdp(PktInfo *pkt_info, uint8_t *pkt);
    bool ComputeForwardingMode(PktInfo *pkt_info, const Interface *intf) const;

    void SetOuterIp(PktInfo *pkt_info, uint8_t *pkt);
    void SetOuterMac(PktInfo *pkt_info);
    bool IgnoreFragmentedPacket(PktInfo *pkt_info);
    bool IsDHCPPacket(PktInfo *pkt_info);
    bool IsValidInterface(uint32_t ifindex, Interface **interface);
    bool IsToRDevice(uint32_t vrf_id, const IpAddress &ip);
    bool IsManagedTORPacket(Interface *intf, PktInfo *pkt_info,
                            PktType::Type &pkt_type, uint8_t *pkt);
    bool IsFlowPacket(const AgentHdr &agent_hdr);
    bool IsDiagPacket(PktInfo *pkt_info);

    boost::array<Proto *, MAX_MODULES> proto_list_;

    PktStats stats_;
    boost::array<PktTrace, MAX_MODULES> pkt_trace_;
    DBTableBase::ListenerId iid_;

    Agent *agent_;
    PktModule *pkt_module_;
    PktHandlerQueue work_queue_;
    DISALLOW_COPY_AND_ASSIGN(PktHandler);
};

// Info from the parsed packet
struct PktInfo {
    PktHandler::PktModuleName module;
    uint8_t             *pkt;
    uint16_t            len;
    uint16_t            max_pkt_len;

    uint8_t             *data;
    InterTaskMsg        *ipc;

    Address::Family     family;
    PktType::Type       type;
    AgentHdr            agent_hdr;
    uint16_t            ether_type;
    // Fields extracted for processing in agent
    uint32_t            vrf;
    MacAddress          smac;
    MacAddress          dmac;
    IpAddress           ip_saddr;
    IpAddress           ip_daddr;
    uint8_t             ip_proto;
    uint32_t            sport;
    uint32_t            dport;
    uint32_t            ttl;

    MacAddress          b_smac;
    MacAddress          b_dmac;
    uint32_t            i_sid;

    bool                tcp_ack;
    TunnelInfo          tunnel;
    bool                l3_label;
    bool                multicast_label;

    // Pointer to different headers in user packet
    struct ether_header *eth;
    uint32_t            *pbb_header;
    struct ether_arp    *arp;
    struct ip           *ip;
    struct ip6_hdr      *ip6;
    union {
        struct tcphdr   *tcp;
        struct udphdr   *udp;
        struct icmp     *icmp;
        struct icmp6_hdr *icmp6;
        struct sctphdr *sctp;
    } transp;

    PktInfo(Agent *agent, uint32_t buff_len, PktHandler::PktModuleName module,
            uint32_t mdata);
    PktInfo(const PacketBufferPtr &buff);
    PktInfo(const PacketBufferPtr &buff, const AgentHdr &hdr);
    PktInfo(PktHandler::PktModuleName module, InterTaskMsg *msg);
    virtual ~PktInfo();

    const AgentHdr &GetAgentHdr() const;
    void UpdateHeaderPtr();
    std::size_t hash(const EcmpLoadBalance &ecmp_has_fields_to_use) const;

    PacketBuffer *packet_buffer() const { return packet_buffer_.get(); }
    PacketBufferPtr packet_buffer_ptr() const { return packet_buffer_; }
    void AllocPacketBuffer(Agent *agent, uint32_t module, uint16_t len,
                           uint32_t mdata);
    void set_len(uint32_t len);
    void reset_packet_buffer();

    uint32_t GetUdpPayloadLength() const;

private:
    PacketBufferPtr     packet_buffer_;
};

#endif
