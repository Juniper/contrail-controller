/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_handler_hpp
#define vnsw_agent_pkt_handler_hpp

#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include <tbb/atomic.h>
#include <boost/array.hpp>

#include <net/address.h>
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

#define IPv4_ALEN           4
#define MIN_ETH_PKT_LEN    64
#define IPC_HDR_LEN        (sizeof(struct ether_header) + sizeof(struct agent_hdr))
#define IP_PROTOCOL        ETHERTYPE_IP
#define VLAN_PROTOCOL      0x8100

struct agent_hdr;
class PacketBuffer;

struct InterTaskMsg {
    InterTaskMsg(uint16_t command): cmd(command) {}
    ~InterTaskMsg() {}

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
        MESSAGE
    };
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
        TRAP_ECMP_RESOLVE = AGENT_TRAP_ECMP_RESOLVE,
        TRAP_SOURCE_MISMATCH = AGENT_TRAP_SOURCE_MISMATCH,
        TRAP_HANDLE_DF = AGENT_TRAP_HANDLE_DF,
        INVALID = MAX_AGENT_HDR_COMMANDS
    };

    AgentHdr() :
        ifindex(-1), vrf(-1), cmd(-1), cmd_param(-1), nh(-1), flow_index(-1),
        mtu(0) {}

    AgentHdr(uint16_t ifindex_p, uint16_t vrf_p, uint16_t cmd_p) :
        ifindex(ifindex_p), vrf(vrf_p), cmd(cmd_p), cmd_param(-1), nh(-1),
        flow_index(-1), mtu(0) {}

    ~AgentHdr() {}

    // Fields from agent_hdr
    uint16_t            ifindex;
    uint32_t            vrf;
    uint16_t            cmd;
    uint32_t            cmd_param;
    uint16_t            nh;
    uint32_t            flow_index;
    uint16_t            mtu;
};

// Tunnel header decoded from the GRE encapsulated packet on fabric
// Supports only IPv4 addresses since only IPv4 is supported on fabric
struct TunnelInfo {
    TunnelInfo() : 
        type(TunnelType::INVALID), label(-1), ip_saddr(), ip_daddr() {}
    ~TunnelInfo() {}

    TunnelType          type;
    uint32_t            label;
    uint32_t            ip_saddr;
    uint32_t            ip_daddr;
};

// Info from the parsed packet
struct PktInfo {
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
    IpAddress           ip_saddr;
    IpAddress           ip_daddr;
    uint8_t             ip_proto;
    uint32_t            sport;
    uint32_t            dport;

    bool                tcp_ack;
    TunnelInfo          tunnel;

    // Pointer to different headers in user packet
    struct ether_header *eth;
    struct ether_arp    *arp;
    struct ip           *ip;
    struct ip6_hdr      *ip6;
    union {
        struct tcphdr   *tcp;
        struct udphdr   *udp;
        struct icmp     *icmp;
        struct icmp6_hdr *icmp6;
    } transp;

    PktInfo(Agent *agent, uint32_t buff_len, uint32_t module, uint32_t mdata);
    PktInfo(const PacketBufferPtr &buff);
    PktInfo(InterTaskMsg *msg);
    virtual ~PktInfo();

    const AgentHdr &GetAgentHdr() const;
    void UpdateHeaderPtr();
    std::size_t hash() const;

    PacketBuffer *packet_buffer() const { return packet_buffer_.get(); }
    PacketBufferPtr packet_buffer_ptr() const { return packet_buffer_; }
    void AllocPacketBuffer(Agent *agent, uint32_t module, uint16_t len,
                           uint32_t mdata);
    void set_len(uint32_t len);

private:
    PacketBufferPtr     packet_buffer_;
};

// Receive packets from the pkt0 (tap) interface, parse and send the packet to
// appropriate protocol task. Also, protocol tasks can send packets to pkt0
// or to other tasks.
class PktHandler {
public:
    typedef boost::function<bool(boost::shared_ptr<PktInfo>)> RcvQueueFunc;
    typedef boost::function<void(PktTrace::Pkt &)> PktTraceCallback;

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
        RX_PACKET,
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

    PktHandler(Agent *, PktModule *pkt_module);
    virtual ~PktHandler();

    void Register(PktModuleName type, RcvQueueFunc cb);

    void Send(const AgentHdr &hdr, const PacketBufferPtr &buff);

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

    uint32_t EncapHeaderLen() const;
    Agent *agent() const { return agent_; }
    PktModule *pkt_module() const { return pkt_module_; }
private:
    friend bool ::CallPktParse(PktInfo *pkt_info, uint8_t *ptr, int len);

    uint8_t *ParseIpPacket(PktInfo *pkt_info, PktType::Type &pkt_type,
                           uint8_t *ptr);
    uint8_t *ParseUserPkt(PktInfo *pkt_info, Interface *intf,
                          PktType::Type &pkt_type, uint8_t *pkt);
    void SetOuterIp(PktInfo *pkt_info, uint8_t *pkt);
    int ParseMPLSoGRE(PktInfo *pkt_info, uint8_t *pkt);
    int ParseMPLSoUDP(PktInfo *pkt_info, uint8_t *pkt);
    bool IsDHCPPacket(PktInfo *pkt_info);

    // handlers for each module type
    boost::array<RcvQueueFunc, MAX_MODULES> enqueue_cb_;

    PktStats stats_;
    boost::array<PktTrace, MAX_MODULES> pkt_trace_;

    Agent *agent_;
    PktModule *pkt_module_;

    DISALLOW_COPY_AND_ASSIGN(PktHandler);
};

#endif
