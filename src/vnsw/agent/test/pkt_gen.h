/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_pkt_gen_h
#define vnsw_agent_test_pkt_gen_h

#include "base/os.h"
#include <netinet/ip.h>
#include <net/ethernet.h>
#include <netinet/ip_icmp.h>

#include <pkt/pkt_handler.h>
#include <vr_interface.h>

#define TCP_PAYLOAD_SIZE     64
#define UDP_PAYLOAD_SIZE     64

#define ARPOP_REQUEST   1
#define ARPOP_REPLY     2

#define ARPHRD_ETHER    1

struct icmp_packet {
    struct ether_header eth;
    struct ip ip;
    struct icmp icmp;
} __attribute__((packed));

struct tcp_packet {
    struct ether_header eth;
    struct ip ip;
    struct tcphdr tcp;
    char   payload[TCP_PAYLOAD_SIZE];
}__attribute__((packed));

struct udp_packet {
    struct ether_header eth;
    struct ip ip;
    struct udphdr udp;
    uint8_t payload[];
}__attribute__((packed));

struct icmp6_packet {
    struct ether_header eth;
    struct ip6_hdr  ip;
    struct icmp icmp;
} __attribute__((packed));

struct tcp6_packet {
    struct ether_header eth;
    struct ip6_hdr  ip;
    struct tcphdr tcp;
    char   payload[TCP_PAYLOAD_SIZE];
}__attribute__((packed));

struct udp6_packet {
    struct ether_header eth;
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

    static void IpInit(struct ip *ip, uint8_t proto, uint16_t len,
                       uint32_t sip, uint32_t dip) {
        ip->ip_src.s_addr = htonl(sip);
        ip->ip_dst.s_addr = htonl(dip);
        ip->ip_v= 4;
        ip->ip_hl = 5;
        ip->ip_tos = 0;
        ip->ip_id = htons(10); //taking a random value
        ip->ip_len = htons(len);
        ip->ip_off = 0;
        ip->ip_ttl = 255;
        ip->ip_p = proto;
        ip->ip_sum = htons(IPChecksum((uint16_t *)ip, sizeof(struct ip)));
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

    static void EthInit(struct ether_header *eth, unsigned short proto) {
        eth->ether_type = proto;
        eth->ether_dhost[5] = 5;
        eth->ether_shost[5] = 4;
    }
    static void EthInit(struct ether_header *eth, uint8_t *smac, uint8_t *dmac, unsigned short proto) {
        eth->ether_type = htons(proto);
        memcpy(eth->ether_dhost, dmac, 6);
        memcpy(eth->ether_shost, smac, 6);
    }
};

class TcpPacket {
public:
    TcpPacket(uint16_t sp, uint16_t dp, uint32_t sip, uint32_t dip) {
        uint16_t len;
        Init(sp, dp);
        len = sizeof(pkt.ip) + sizeof(pkt.tcp) + sizeof(pkt.payload);
        IpUtils::IpInit(&pkt.ip, IPPROTO_TCP, len, sip, dip);
        IpUtils::EthInit(&pkt.eth, ETHERTYPE_IP);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; }
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.tcp.th_sport = htons(sport);
        pkt.tcp.th_dport = htons(dport);
        pkt.tcp.th_seq = htonl(1);
        pkt.tcp.th_ack = htonl(0);

        pkt.tcp.th_off = 5;
        pkt.tcp.th_flags = 0;

        pkt.tcp.th_win = htons(0);
        pkt.tcp.th_sum = htons(0);
        pkt.tcp.th_urp = htons(0);
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
        IpUtils::EthInit(&pkt.eth, ETHERTYPE_IP);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; }
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.udp.uh_sport = htons(sport);
        pkt.udp.uh_dport = htons(dport);
        pkt.udp.uh_ulen = htons(sizeof(pkt.payload));
        pkt.udp.uh_sum = htons(0); //ignoring checksum for now.
    }
    struct udp_packet pkt;
};

class ArpPacket {
public:
    ArpPacket(uint16_t arpop, uint8_t smac, uint32_t sip, uint32_t tip) {
        memset(&pkt, 0, sizeof(struct ether_arp));
        pkt.ea_hdr.ar_hrd = ARPHRD_ETHER;
        pkt.ea_hdr.ar_pro = ETHERTYPE_IP;
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
        IpUtils::EthInit(&(pkt.eth), smac, dmac, ETHERTYPE_IP);
        pkt.icmp.icmp_type = ICMP_ECHO;
        pkt.icmp.icmp_code = 0;
        pkt.icmp.icmp_cksum = IpUtils::IPChecksum((uint16_t *)&pkt.icmp, sizeof(icmp_packet));
        pkt.icmp.icmp_id = 0;
        pkt.icmp.icmp_seq = 0;
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
        IpUtils::EthInit(&pkt.eth, ETHERTYPE_IPV6);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; }
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.tcp.th_sport = htons(sport);
        pkt.tcp.th_dport = htons(dport);
        pkt.tcp.th_seq = htonl(1);
        pkt.tcp.th_ack = htonl(0);

        pkt.tcp.th_off = 5;
        pkt.tcp.th_flags = 0;

        pkt.tcp.th_win = htons(0);
        pkt.tcp.th_sum = htons(0);
        pkt.tcp.th_urp = htons(0);
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
        IpUtils::EthInit(&pkt.eth, ETHERTYPE_IPV6);
    }
    unsigned char *GetPacket() const { return (unsigned char *)&pkt; }
private:
    void Init(uint16_t sport, uint16_t dport) {
        pkt.udp.uh_sport = htons(sport);
        pkt.udp.uh_dport = htons(dport);
        pkt.udp.uh_ulen = htons(sizeof(pkt.payload));
        pkt.udp.uh_sum = htons(0); //ignoring checksum for now.

    }
    struct udp6_packet pkt;
};

class Icmp6Packet {
public:
    Icmp6Packet(uint8_t *smac, uint8_t *dmac, uint32_t sip, uint32_t dip) {
        uint16_t len;
        len = sizeof(pkt.ip) + sizeof(pkt.icmp);
        IpUtils::Ip6Init(&(pkt.ip), IPPROTO_ICMPV6, len, sip, dip);
        IpUtils::EthInit(&(pkt.eth), smac, dmac, ETHERTYPE_IPV6);
        pkt.icmp.icmp_type = ICMP_ECHO;
        pkt.icmp.icmp_code = 0;
        pkt.icmp.icmp_cksum = IpUtils::IPChecksum((uint16_t *)&pkt.icmp, sizeof(icmp_packet));
        pkt.icmp.icmp_id = 0;
        pkt.icmp.icmp_seq = 0;
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
        struct ether_header *eth = (struct ether_header *)(buff + len);

        MacAddress::FromString(dmac).ToArray(eth->ether_dhost, sizeof(eth->ether_dhost));
        MacAddress::FromString(smac).ToArray(eth->ether_shost, sizeof(eth->ether_shost));
        eth->ether_type = htons(proto);
        len += sizeof(struct ether_header);
    };

    void AddIpHdr(const char *sip, const char *dip, uint16_t proto) {
        struct ip *ip = (struct ip *)(buff + len);

        ip->ip_hl = 5;
        ip->ip_v = 4;
        ip->ip_len = 100;
        ip->ip_src.s_addr = inet_addr(sip);
        ip->ip_dst.s_addr = inet_addr(dip);
        ip->ip_p = proto;
        len += sizeof(struct ip);
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
        udp->uh_dport = htons(dport);
        udp->uh_sport = htons(sport);
        len += sizeof(udphdr) + len;
    };

    void AddTcpHdr(uint16_t sport, uint16_t dport, bool syn, bool fin, bool ack,
                   int plen) {
        struct tcphdr *tcp = (struct tcphdr *)(buff + len);
        tcp->th_dport = htons(dport);
        tcp->th_sport = htons(sport);
        tcp->th_flags = (fin ? TH_FIN : 0) | (syn ? TH_SYN : 0) | (ack ? TH_ACK : 0);
        len += sizeof(tcphdr) + len;
    };

    void AddIcmpHdr() {
        struct icmp *icmp = (struct icmp *)(buff + len);
        icmp->icmp_type = 0;
        icmp->icmp_id = 0;
        len += sizeof(struct icmp) + len;
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
