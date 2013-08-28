/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_handler_hpp
#define vnsw_agent_pkt_handler_hpp

#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>

#include <tbb/atomic.h>
#include <boost/array.hpp>
#include <boost/circular_buffer.hpp>

#include <cmn/agent_cmn.h>
#include <filter/acl.h>
#include <oper/mirror_table.h>
#include "tap_itf.h"
#include "vr_defs.h"
#define ALL_ONES_IP_ADDR "255.255.255.255"
#define GW_IP_ADDR       "169.254.1.1"

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DNS_SERVER_PORT 53
#define MDNS_PORT 5353

#define IPv4_ALEN           4
#define MIN_ETH_PKT_LEN    64
#define IPC_HDR_LEN         (sizeof(ethhdr) + sizeof(struct agent_hdr))
#define IP_PROTOCOL        0x800  
#define UDP_PROTOCOL       0x11
#define TCP_PROTOCOL       0x6
#define VLAN_PROTOCOL      0x8100       
static const unsigned char agent_vrrp_mac[] = {0x00, 0x01, 0x00, 0x5E, 0x00, 0x00};

struct IpcMsg {
    IpcMsg(uint16_t command): cmd(command) {};
    ~IpcMsg() {};

    uint16_t cmd;
};

struct GreHdr {
    GreHdr() : flags(), protocol() {};
    ~GreHdr() {};

    uint16_t flags;
    uint16_t protocol;
};

struct MplsHdr {
    MplsHdr() : hdr() {};
    ~MplsHdr() {};

    uint32_t hdr;
};

struct PktType {
    enum Type {
        INVALID,
        ARP,
        IPV4,
        UDP,
        TCP,
        ICMP,
        NON_IPV4,
        MESSAGE
    };
};

struct AgentHdr {
    AgentHdr() : ifindex(-1), vrf(-1), cmd(-1), cmd_param(-1) {};
    ~AgentHdr() {};

    // Fields from agent_hdr
    uint16_t            ifindex;
    uint32_t            vrf;
    uint16_t            cmd;
    uint32_t            cmd_param;
};

struct TunnelInfo {
    TunnelInfo() : 
        type(TunnelType::INVALID), label(-1), ip_saddr(), ip_daddr() {};
    ~TunnelInfo() {};

    TunnelType          type;
    uint32_t            label;
    uint32_t            ip_saddr;
    uint32_t            ip_daddr;
};

// Info from the parsed packet
struct PktInfo {
    uint8_t             *pkt;
    uint16_t            len;

    uint8_t             *data;
    IpcMsg              *ipc;

    PktType::Type       type;
    AgentHdr            agent_hdr;
    uint16_t            ether_type;
    // Fields extracted for processing in agent
    uint32_t            vrf;
    uint32_t            ip_saddr;
    uint32_t            ip_daddr;
    uint8_t             ip_proto;
    uint32_t            sport;
    uint32_t            dport;

    TunnelInfo          tunnel;

    // Pointer to different headers in user packet
    struct ethhdr       *eth;
    struct ether_arp    *arp;
    struct iphdr        *ip;
    union {
        struct tcphdr   *tcp;
        struct udphdr   *udp;
        struct icmphdr  *icmp;
    } transp;

    PktInfo(): 
        pkt(), len(), data(), ipc(), type(PktType::INVALID), agent_hdr(),
        ether_type(-1), ip_saddr(), ip_daddr(), ip_proto(), sport(), dport(),
        tunnel(), eth(), arp(), ip() {
        transp.tcp = 0;
    }

    PktInfo(uint8_t *msg, std::size_t msg_size) : 
        pkt(msg), len(msg_size), data(), ipc(), type(PktType::INVALID),
        agent_hdr(), ether_type(-1), ip_saddr(), ip_daddr(), ip_proto(),
        sport(), dport(), tunnel(), eth(), arp(), ip() {
        transp.tcp = 0;
    }

    PktInfo(IpcMsg *msg) :
        pkt(), len(), data(), ipc(msg), type(PktType::MESSAGE), agent_hdr(),
        ether_type(-1), ip_saddr(), ip_daddr(), ip_proto(), sport(), dport(),
        tunnel(), eth(), arp(), ip() {
        transp.tcp = 0;
    }

    virtual ~PktInfo() {
        if (pkt) delete [] pkt;
    }

    const AgentHdr &GetAgentHdr() const {return agent_hdr;};

    void UpdateHeaderPtr() {
        eth = (struct ethhdr *)(pkt + IPC_HDR_LEN);
        ip = (struct iphdr *)(eth + 1);
        transp.tcp = (struct tcphdr *)(ip + 1);
    }

    std::size_t hash() const {
        std::size_t seed = 0;
        boost::hash_combine(seed, ip_saddr);
        boost::hash_combine(seed, ip_daddr);
        boost::hash_combine(seed, ip_proto);
        boost::hash_combine(seed, sport);
        boost::hash_combine(seed, dport);
        boost::hash_combine(seed, vrf);
        return seed;
    }
};

class PktTrace {
public:
    static const std::size_t pkt_trace_size = 512;  // number of bytes stored
    static const std::size_t pkt_num_buf = 16;     // number of buffers stored
    enum Direction {
        In,
        Out
    };
    struct Pkt {
        Direction dir;
        std::size_t len;
        uint8_t pkt[pkt_trace_size];

        Pkt(Direction d, std::size_t l, uint8_t *msg) : dir(d), len(l) {
            memset(pkt, 0, pkt_trace_size);
            memcpy(pkt, msg, std::min(l, pkt_trace_size));
        }
    };

    typedef boost::function<void(PktTrace::Pkt &)> Cb;

    PktTrace() : pkt_trace_(pkt_num_buf) {};
    virtual ~PktTrace() { pkt_trace_.clear(); };

    void AddPktTrace(Direction dir, std::size_t len, uint8_t *msg) {
        Pkt pkt(dir, len, msg);
        tbb::mutex::scoped_lock lock(mutex_);
        pkt_trace_.push_back(pkt);
    }

    void Clear() { pkt_trace_.clear(); }

    void Iterate(Cb cb) {
        if (cb) {
            tbb::mutex::scoped_lock lock(mutex_);
            for (boost::circular_buffer<Pkt>::iterator it = pkt_trace_.begin();
                 it != pkt_trace_.end(); ++it)
                cb(*it);
        }
    }

private:
    tbb::mutex mutex_;
    boost::circular_buffer<Pkt> pkt_trace_;
};

class PktHandler {
public:
    typedef boost::function<bool(PktInfo *)> RcvQueueFunc;
    typedef boost::function<void(PktTrace::Pkt &)> PktTraceCallback;

    enum ModuleName {
        INVALID,
        FLOW,
        ARP,
        DHCP,
        DNS,
        ICMP,
        DIAG,
        MAX_MODULES
    };

    struct PktStats {
        uint32_t total_rcvd;
        uint32_t flow_rcvd;
        uint32_t arp_rcvd;
        uint32_t dhcp_rcvd;
        uint32_t dns_rcvd;
        uint32_t icmp_rcvd;
        uint32_t diag_rcvd;
        uint32_t dropped;
        tbb::atomic<uint32_t> total_sent;
        uint32_t dhcp_sent;
        uint32_t arp_sent;
        uint32_t dns_sent;
        uint32_t icmp_sent;
        uint32_t diag_sent;
        void Reset() {
            total_rcvd = dhcp_rcvd = arp_rcvd = dns_rcvd = flow_rcvd = dropped =
            dhcp_sent = arp_sent = dns_sent = icmp_rcvd = icmp_sent = 0;
            total_sent = 0;
        }
        PktStats() { Reset(); }
        void PktRcvd(ModuleName mod);
        void PktSent(ModuleName mod);
    };

    static void Init (DB *db, const std::string &if_name,
                      boost::asio::io_service &io, bool run_with_vrouter) {
        assert(instance_ == NULL);
        instance_ = new PktHandler(db, if_name, io, run_with_vrouter);
    };

    static void Shutdown() {
        delete instance_;
        instance_ = NULL;
    }

    virtual ~PktHandler() {
        delete tap_;
        tap_ = NULL;
    };

    static void CreateHostInterface(std::string &if_name);

    static PktHandler *GetPktHandler() {
        return instance_;
    };

    void Register(ModuleName type, RcvQueueFunc cb) {
        enqueue_cb_.at(type) = cb;
    };

    void Unregister(ModuleName type) {
        enqueue_cb_.at(type) = NULL;
    };

    const unsigned char *GetMacAddr() {
        return tap_->MacAddr();
    };
    const TapInterface *GetTapInterface() {
        return tap_;
    }

    void Send(uint8_t *msg, std::size_t len, ModuleName mod) {
        stats_.PktSent(mod);
        pkt_trace_.at(mod).AddPktTrace(PktTrace::Out, len, msg);
        tap_->AsyncWrite(msg, len);
    };

    // identify pkt type and send to the registered handler
    void HandleRcvPkt(uint8_t*, std::size_t);  
    void SendMessage(ModuleName mod, IpcMsg *msg); 

    bool IsGwPacket(const Interface *intf, PktInfo *pkt_info);

    PktStats GetStats() { return stats_; }
    uint32_t GetModuleStats(ModuleName mod);
    void ClearStats() { stats_.Reset(); }
    void PktTraceIterate(ModuleName mod, PktTraceCallback cb) {
        if (cb) {
            PktTrace &pkt(pkt_trace_.at(mod));
            pkt.Iterate(cb);
        }
    }
    void PktTraceClear(ModuleName mod) { pkt_trace_.at(mod).Clear(); }

private:
    friend bool ::CallPktParse(PktInfo *pkt_info, uint8_t *ptr, int len);

    PktHandler(DB *, const std::string &, boost::asio::io_service &, bool);

    // update the routes in each VRF, when it is created / deleted
    void VrfUpdate(DBTablePartBase *, DBEntryBase *);

    uint8_t *ParseAgentHdr(PktInfo *pkt_info);
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

    DB *db_;
    TapInterface *tap_;
    static PktHandler *instance_;

    DISALLOW_COPY_AND_ASSIGN(PktHandler);
};

#endif
