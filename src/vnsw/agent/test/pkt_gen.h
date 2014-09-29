/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_pkt_gen_h
#define vnsw_agent_test_pkt_gen_h

#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ether.h>
#include <linux/if_ether.h>
#include <netinet/ip_icmp.h>

#include <pkt/pkt_handler.h>
#include <vr_interface.h>

#define TCP_PAYLOAD_SIZE     64
#define UDP_PAYLOAD_SIZE     64

#define ARPOP_REQUEST   1
#define ARPOP_REPLY     2

#define ARPHRD_ETHER    1

struct icmp_packet {
    struct ethhdr eth;
    struct iphdr  ip;
    struct icmphdr icmp;
} __attribute__((packed));

struct tcp_packet {
    struct ethhdr eth;
    struct iphdr  ip;
    struct tcphdr tcp;
    char   payload[TCP_PAYLOAD_SIZE];
}__attribute__((packed));

struct udp_packet {
    struct ethhdr eth;
    struct iphdr  ip; 
    struct udphdr udp;
    uint8_t payload[];
}__attribute__((packed));

struct icmp6_packet {
    struct ethhdr eth;
    struct ip6_hdr  ip; 
    struct icmphdr icmp;
} __attribute__((packed));

struct tcp6_packet {
    struct ethhdr eth;
    struct ip6_hdr  ip; 
    struct tcphdr tcp;
    char   payload[TCP_PAYLOAD_SIZE];
}__attribute__((packed));

struct udp6_packet {
    struct ethhdr eth;
    struct ip6_hdr  ip; 
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

    static void IpInit(struct iphdr *ip, uint8_t proto, uint16_t len,
                       uint32_t sip, uint32_t dip) {
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
    }

    static void Ip6Init(struct ip6_hdr *ip, uint8_t proto, uint16_t len,
                        uint32_t sip, uint32_t dip) {
        bzero(ip, sizeof(*ip));
        ip->ip6_src.s6_addr32[0] = htonl(sip);
        ip->ip6_dst.s6_addr32[0] = htonl(dip);
        ip->ip6_ctlun.ip6_un1.ip6_un1_flow = htonl(0x60000000);
        ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(len);
        ip->ip6_ctlun.ip6_un1.ip6_un1_nxt = proto;
        ip->ip6_ctlun.ip6_un1.ip6_un1_hlim = 255;
    }

    static void EthInit(ethhdr *eth, unsigned short proto) {
        eth->h_proto = proto;
        eth->h_dest[5] = 5;
        eth->h_source[5] = 4;
    }
    static void EthInit(ethhdr *eth, uint8_t *smac, uint8_t *dmac, unsigned short proto) {
        eth->h_proto = htons(proto);
        memcpy(eth->h_dest, dmac, 6);
        memcpy(eth->h_source, smac, 6);
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

class Tcp6Packet {
public:
    Tcp6Packet(uint16_t sp, uint16_t dp, uint32_t sip, uint32_t dip) {
        uint16_t len;
        Init(sp, dp);
        len = sizeof(pkt.ip) + sizeof(pkt.tcp) + sizeof(pkt.payload);
        IpUtils::Ip6Init(&pkt.ip, IPPROTO_TCP, len, sip, dip);
        IpUtils::EthInit(&pkt.eth, ETH_P_IPV6);
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
    struct tcp6_packet pkt;
};

class Udp6Packet {
public:
    Udp6Packet(uint16_t sp, uint16_t dp, uint32_t sip, uint32_t dip) {
        uint16_t len;
        Init(sp, dp);
        len = sizeof(pkt.ip) + sizeof(pkt.udp) + sizeof(pkt.payload);
        IpUtils::Ip6Init(&pkt.ip, IPPROTO_UDP, len, sip, dip);
        IpUtils::EthInit(&pkt.eth, ETH_P_IPV6);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.udp.source = htons(sport);
        pkt.udp.dest = htons(dport);
        pkt.udp.len = htons(sizeof(pkt.payload));
        pkt.udp.check = htons(0); //ignoring checksum for now.
        
    }
    struct udp6_packet pkt;
};

class Icmp6Packet {
public:        
    Icmp6Packet(uint8_t *smac, uint8_t *dmac, uint32_t sip, uint32_t dip) {
        uint16_t len;
        len = sizeof(pkt.ip) + sizeof(pkt.icmp);
        IpUtils::Ip6Init(&(pkt.ip), IPPROTO_ICMPV6, len, sip, dip);
        IpUtils::EthInit(&(pkt.eth), smac, dmac, ETH_P_IPV6);
        pkt.icmp.type = ICMP_ECHO; 
        pkt.icmp.code = 0;
        pkt.icmp.checksum = IpUtils::IPChecksum((uint16_t *)&pkt.icmp, sizeof(icmp_packet));
        pkt.icmp.un.echo.id = 0;
        pkt.icmp.un.echo.sequence = 0; 
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; } 
private:
    icmp6_packet pkt;
};

class PktGen {
public:
    const static int kMaxPktLen=1024;
    PktGen() : len(0) { memset(buff, 0, kMaxPktLen);};
    virtual ~PktGen() {};

    void AddEthHdr(const std::string &dmac, const std::string &smac, uint16_t proto) {
        struct ethhdr *eth = (struct ethhdr *)(buff + len);

        MacAddress::FromString(dmac).ToArray(eth->h_dest, sizeof(eth->h_dest));
        MacAddress::FromString(smac).ToArray(eth->h_source, sizeof(eth->h_source));
        eth->h_proto = htons(proto);
        len += sizeof(ethhdr);
    };

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

    void AddIp6Hdr(const char *sip, const char *dip, uint16_t proto) {
        struct ip6_hdr *ip = (struct ip6_hdr *)(buff + len);
        bzero(ip, sizeof(*ip));
        inet_pton(AF_INET6, sip, ip->ip6_src.s6_addr);
        inet_pton(AF_INET6, dip, ip->ip6_dst.s6_addr);
        ip->ip6_ctlun.ip6_un1.ip6_un1_flow = htonl(0x60000000);
        ip->ip6_ctlun.ip6_un1.ip6_un1_plen = htons(len);
        ip->ip6_ctlun.ip6_un1.ip6_un1_nxt = proto;
        ip->ip6_ctlun.ip6_un1.ip6_un1_hlim = 255;
        len += sizeof(ip6_hdr);
    };

    void AddUdpHdr(uint16_t sport, uint16_t dport, int plen) {
        struct udphdr *udp = (struct udphdr *)(buff + len);
        udp->dest = htons(dport);
        udp->source = htons(sport);
        len += sizeof(udphdr) + len;
    };

    void AddTcpHdr(uint16_t sport, uint16_t dport, bool syn, bool fin, bool ack,
                   int plen) {
        struct tcphdr *tcp = (struct tcphdr *)(buff + len);
        tcp->dest = htons(dport);
        tcp->source = htons(sport);
        tcp->fin = fin;
        tcp->syn = syn;
        tcp->ack = ack;
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

    void AddAgentHdr(int if_id, int cmd, int param = 0, int vrf = -1,
                     int label = -1) {
        agent_hdr *hdr= (agent_hdr *)(buff + len);
        Interface *intf = InterfaceTable::GetInstance()->FindInterface(if_id);
        if (vrf == -1) {
            if (intf && intf->vrf()) {
                vrf = intf->vrf()->vrf_id();
            }
        }
        uint16_t nh = 0;
        //Get NH
        if (label > 0) {
            MplsLabel *mpls_label =
                Agent::GetInstance()->mpls_table()->FindMplsLabel(label);
            if (mpls_label) {
                nh = mpls_label->nexthop()->id();
            }
        } else {
            if (intf && intf->type() == Interface::VM_INTERFACE) {
                VmInterface *vm_intf =
                    static_cast<VmInterface *>(intf);
                if (intf->vrf() && intf->vrf()->vrf_id() == (uint32_t)vrf) {
                    if (intf->flow_key_nh()) {
                        nh = intf->flow_key_nh()->id();
                    }
                } else {
                    const VrfEntry *vrf_p =
                        Agent::GetInstance()->vrf_table()->
                        FindVrfFromId(vrf);
                    //Consider vlan assigned VRF case
                    uint32_t label = vm_intf->GetServiceVlanLabel(vrf_p);
                    if (label) {
                        nh = Agent::GetInstance()->mpls_table()->
                            FindMplsLabel(label)->nexthop()->id();
                    }
                }
            } else if (intf && intf->flow_key_nh()) {
                nh = intf->flow_key_nh()->id();
            }
        }

        hdr->hdr_ifindex = htons(if_id);
        hdr->hdr_cmd = htons(cmd);
        hdr->hdr_cmd_param = htonl(param);
        hdr->hdr_vrf = htons(vrf);
        hdr->hdr_cmd_param_1 = htonl(nh);
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
