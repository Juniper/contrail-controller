/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_pkt_gen_h
#define vnsw_agent_test_pkt_gen_h

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
//.de.byte.breaker
#if defined(__linux__)
#include <netinet/ether.h>
#include <linux/if_ether.h>
#endif
#include <netinet/ip_icmp.h>

#include <pkt/pkt_handler.h>
#include <vr_interface.h>

#define TCP_PAYLOAD_SIZE     64
#define UDP_PAYLOAD_SIZE     64

#define ARPOP_REQUEST   1 
#define ARPOP_REPLY     2

#define ARPHRD_ETHER    1

struct icmp_packet {
#if defined(__linux__)
    struct ethhdr eth;
    struct iphdr  ip;
    struct icmphdr icmp;
#elif defined(__FreeBSD__)
    struct ether_header eth;
    struct ip  ip;
    struct icmp icmp;
#else
#error "Unsupported platform"
#endif
} __attribute__((packed));

struct tcp_packet {
#if defined(__linux__)
    struct ethhdr eth;
    struct iphdr  ip;
#elif defined(__FreeBSD__)
    struct ether_headr eth;
    struct ip  ip;
#else
#error "Unsupported platform"
#endif
    struct tcphdr tcp;
    char   payload[TCP_PAYLOAD_SIZE];
}__attribute__((packed));

struct udp_packet {
#if defined(__linux__)
    struct ethhdr eth;
    struct iphdr  ip;
#elif defined(__FreeBSD__)
    struct ether_header eth;
    struct ip  ip;
#else
#error "Unsupported platform"
#endif
    struct udphdr udp;
    uint8_t payload[];
}__attribute__((packed));

class IpUtils {
public:
    static uint16_t IPChecksum(uint16_t *data, uint32_t len) {

        /* data is the one for which we are calculating checksum and its size
           len in bytes */

        register uint32_t sum=0;
        uint16_t ans;
        register uint16_t *temp=data;
        register uint32_t odd = len;

        /* adding 16 bits every time to sum */
        while( odd > 1) {
                sum += *temp++;
                odd -= 2;
                if (sum & 0x80000000)
                    sum = (sum & 0xFFFF) + (sum >> 16);
        }

        /* if length is odd the last 8 bits will also get added here */
        if( odd )
                sum += *(uint8_t *)temp;

        while (sum >> 16)
            sum = (sum & 0xFFFF) + (sum >> 16);
        ans=~sum; /* truncating to 16 bits */
        return ans;
    }

    static void AddEthHdr(char *buff, int &len, const char *dmac, 
                         const char *smac, uint16_t proto) {
    }

    static void IpInit(struct iphdr *ip, uint8_t proto, uint16_t len, uint32_t sip, uint32_t dip) {
#if defined(__linux__)
        ip->saddr = htonl(sip);
        ip->daddr = htonl(dip);
        ip->version = 4;
        ip->ihl = 5;
        ip->tos = 0;
        ip->id = htons(10); //taking a random value
        ip->tot_len = htons(len);
        ip->frag_off = 0;
        ip->ttl = 255;
        ip->protocol = proto;
        ip->check = htons(IPChecksum((uint16_t *)ip, sizeof(struct iphdr)));
#elif defined(__FreeBSD__)
        ip->ip_src = htonl(sip);
        ip->ip_dst = htonl(dip);
        ip->ip_v = 4;
        ip->ip_hl = 5;
        ip->ip_tos = 0;
        ip->ip_id = htons(10); //taking a random value
        ip->ip_len = htons(len);
        ip->ip_off = 0;
        ip->ip_ttl = 255;
        ip->ip_p = proto;
        ip->ip_sum = htons(IPChecksum((uint16_t *)ip, sizeof(struct ip)));
#else
#error "Unsupported platform"
#endif
    }

    static void EthInit(ethhdr *eth, unsigned short proto) {
#if defined(__linux__)
        eth->h_proto = proto;
        eth->h_dest[5] = 5;
        eth->h_source[5] = 4;
#elif defined(__FreeBSD__)
        eth->ether_type = proto;
        eth->ether_dhost[5] = 5;
        eth->ether_shost[5] = 4;
#else
#error "Unsupported platform"
#endif
    }
    static void EthInit(ethhdr *eth, uint8_t *smac, uint8_t *dmac, unsigned short proto) {
#if defined(__linux__)
        eth->ether_type = htons(proto);
        memcpy(eth->ether_dhost, dmac, 6);
        memcpy(eth->ether_shost, smac, 6);
#elif defined(__FreeBSD__)
#else
#error "Unsupported platform"
#endif
    }
};

class TcpPacket {
public:
    TcpPacket(uint16_t sp, uint16_t dp, uint32_t sip, uint32_t dip) {
        uint16_t len;
        Init(sp, dp);
        len = sizeof(pkt.ip) + sizeof(pkt.tcp) + sizeof(pkt.payload);
        IpUtils::IpInit(&pkt.ip, IPPROTO_TCP, len, sip, dip);
        IpUtils::EthInit(&pkt.eth, ETH_P_IP);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.tcp.source = htons(sport);
        pkt.tcp.dest = htons(dport);
        pkt.tcp.seq = htonl(1);
        pkt.tcp.ack_seq = htonl(0);

        pkt.tcp.doff = 5;
        pkt.tcp.fin = 0;
        pkt.tcp.syn = 0;
        pkt.tcp.rst = 0;
        pkt.tcp.psh= 0;
        pkt.tcp.ack = 0;
        pkt.tcp.urg = 0;
        pkt.tcp.res1 = 0;
        pkt.tcp.res2 = 0;

        pkt.tcp.window = htons(0);
        pkt.tcp.check = htons(0);
        pkt.tcp.urg_ptr = htons(0);
    }
    struct tcp_packet pkt;
};

class UdpPacket {
public:
    UdpPacket(uint16_t sp, uint16_t dp, uint32_t sip, uint32_t dip) {
        uint16_t len;
        Init(sp, dp);
        len = sizeof(pkt.ip) + sizeof(pkt.udp) + sizeof(pkt.payload);
        IpUtils::IpInit(&pkt.ip, IPPROTO_UDP, len, sip, dip);
        IpUtils::EthInit(&pkt.eth, ETH_P_IP);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.udp.source = htons(sport);
        pkt.udp.dest = htons(dport);
        pkt.udp.len = htons(sizeof(pkt.payload));
        pkt.udp.check = htons(0); //ignoring checksum for now.
        
    }
    struct udp_packet pkt;
};

class ArpPacket {
public:
    ArpPacket(uint16_t arpop, uint8_t smac, uint32_t sip, uint32_t tip) {
        memset(&pkt, 0, sizeof(struct ether_arp));
        pkt.ea_hdr.ar_hrd = ARPHRD_ETHER;
        pkt.ea_hdr.ar_pro = ETH_P_IP;
        pkt.ea_hdr.ar_hln = 6;
        pkt.ea_hdr.ar_pln = 4;
        pkt.ea_hdr.ar_op = arpop;
        pkt.arp_sha[5] = smac;
        sip = ntohl(sip);
        memcpy(pkt.arp_spa, &sip, 4);
        tip = ntohl(tip);
        memcpy(pkt.arp_tpa, &tip, 4);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    struct ether_arp pkt;
};

class IcmpPacket {
public:        
    IcmpPacket(uint8_t *smac, uint8_t *dmac, uint32_t sip, uint32_t dip) {
        uint16_t len;
        len = sizeof(pkt.ip) + sizeof(pkt.icmp);
        IpUtils::IpInit(&(pkt.ip), IPPROTO_ICMP, len, sip, dip);
        IpUtils::EthInit(&(pkt.eth), smac, dmac, ETH_P_IP);
        pkt.icmp.type = ICMP_ECHO;
        pkt.icmp.code = 0;
        pkt.icmp.checksum = IpUtils::IPChecksum((uint16_t *)&pkt.icmp, sizeof(icmp_packet));
        pkt.icmp.un.echo.id = 0;
        pkt.icmp.un.echo.sequence = 0;
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    icmp_packet pkt;
};

class PktGen {
public:
    const static int kMaxPktLen=1024;
    PktGen() : len(0) { memset(buff, 0, kMaxPktLen);};
    virtual ~PktGen() {};

    void AddEthHdr(const char *dmac, const char *smac, uint16_t proto) {
        struct ethhdr *eth = (struct ethhdr *)(buff + len);

        memcpy(eth->h_dest, ether_aton(dmac), sizeof(ether_addr));
        memcpy(eth->h_source, ether_aton(smac), sizeof(ether_addr));
        eth->h_proto = htons(proto);
        len += sizeof(ethhdr);
    };

#if defined(__linux__)
    void AddIpHdr(const char *sip, const char *dip, uint16_t proto) {
        struct iphdr *ip = (struct iphdr *)(buff + len);

        ip->ihl = 5;
        ip->version = 4;
        ip->tot_len = 100;
        ip->saddr = inet_addr(sip);
        ip->daddr = inet_addr(dip);
        ip->protocol = proto;
        len += sizeof(iphdr);
    };
#elif defined(__FreeBSD__)
    void AddIpHdr(const char *sip, const char *dip, uint16_t proto) {
        struct ip *ip = (struct ip *)(buff + len);

        ip->ip_hl = 5;
        ip->ip_v = 4;
        ip->ip_len = 100;
        ip->ip_shost.s_addr = inet_addr(sip);
        ip->ip_dhost.s_addr = inet_addr(dip);
        ip->ip_p = proto;
        len += sizeof(ip);
    };
#else
#error "Unsupported platform"
#endif

    void AddUdpHdr(uint16_t sport, uint16_t dport, int plen) {
        struct udphdr *udp = (struct udphdr *)(buff + len);
        udp->dest = htons(dport);
        udp->source = htons(sport);
        len += sizeof(udphdr) + len;
    };

    void AddTcpHdr(uint16_t sport, uint16_t dport, int plen) {
        struct tcphdr *udp = (struct tcphdr *)(buff + len);
        udp->dest = htons(dport);
        udp->source = htons(sport);
        len += sizeof(tcphdr) + len;
    };

    void AddIcmpHdr() {
        struct icmphdr *icmp = (struct icmphdr *)(buff + len);
        icmp->type = 0;
        icmp->un.echo.id = 0;
        len += sizeof(icmphdr) + len;
    };

    void AddGreHdr(uint16_t proto) {
        GreHdr *gre = (GreHdr *)(buff + len);
        gre->flags = 0;
        gre->protocol = htons(proto);
        len += sizeof(GreHdr);
    }

    void AddGreHdr() {
        return AddGreHdr(VR_GRE_PROTO_MPLS);
    };

    void AddMplsHdr(uint32_t label, bool bottom) {
        MplsHdr *mpls = (MplsHdr *)(buff + len);

        mpls->hdr = label << 12;
        if (bottom) {
            mpls->hdr |= 0x100;
        }
        mpls->hdr = htonl(mpls->hdr);

        len += sizeof(MplsHdr);
    };

    void AddAgentHdr(int if_id, int cmd, int param = 0, int vrf = -1) {
        agent_hdr *hdr= (agent_hdr *)(buff + len);
        if (vrf == -1) {
            Interface *intf = InterfaceTable::GetInstance()->FindInterface(if_id);
            if (intf && intf->vrf()) {
                vrf = intf->vrf()->vrf_id();
            }
        }

        hdr->hdr_ifindex = htons(if_id);
        hdr->hdr_cmd = htons(cmd);
        hdr->hdr_cmd_param = htonl(param);
        hdr->hdr_vrf = htons(vrf);
        len += sizeof(*hdr);
    };

    char *GetBuff() {return buff;};
    int GetBuffLen() {return len;};
    void Reset() {len = 0;};
private:
    char buff[kMaxPktLen];
    int len;
};

#endif
