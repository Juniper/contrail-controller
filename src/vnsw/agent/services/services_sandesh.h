/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _vnsw_agent_services_sandesh_h_
#define _vnsw_agent_services_sandesh_h_

#include <services/services_types.h>

struct DhcpOptions;
struct dhcphdr;
struct dnshdr;
struct ArpKey;
class ArpEntry;

class ServicesSandesh {
public:
    ServicesSandesh() {}
    virtual ~ServicesSandesh() {}

    void HandleRequest(PktHandler::PktModuleName mod, std::string ctxt, bool more);
    void MetadataHandleRequest(std::string ctxt, bool more);
    void PktStatsSandesh(std::string ctxt, bool more);

    static void MacToString(const unsigned char *mac, std::string &mac_str);
    static std::string & DhcpMsgType(uint32_t msg_type);

private:
    std::string IntToString(uint32_t val);
    std::string IntToHexString(uint32_t val);
    void PktToHexString(uint8_t *pkt, int32_t len, std::string &msg);
    std::string IpProtocol(uint16_t prot);

    void DhcpStatsSandesh(std::string ctxt, bool more);
    void ArpStatsSandesh(std::string ctxt, bool more);
    void DnsStatsSandesh(std::string ctxt, bool more);
    void IcmpStatsSandesh(std::string ctxt, bool more);

    void ArpPktTrace(PktTrace::Pkt &pkt, ArpPktSandesh *resp);
    void DhcpPktTrace(PktTrace::Pkt &pkt, DhcpPktSandesh *resp);
    void DnsPktTrace(PktTrace::Pkt &pkt, DnsPktSandesh *resp);
    void IcmpPktTrace(PktTrace::Pkt &pkt, IcmpPktSandesh *resp);
    void OtherPktTrace(PktTrace::Pkt &pkt, PktSandesh *resp);

    void FillPktData(PktTrace::Pkt &pkt, PktData &resp);
    uint16_t FillVrouterHdr(PktTrace::Pkt &pkt, VrouterHdr &resp);
    void FillArpHdr(ether_arp *arp, ArpHdr &resp);
    void FillMacHdr(ether_header *eth, MacHdr &resp);
    void FillIpv4Hdr(ip *ip, Ipv4Hdr &resp);
    void FillIcmpv4Hdr(icmp *icmp, Icmpv4Hdr &resp);
    void FillUdpHdr(udphdr *udp, UdpHdr &resp);
    void FillDhcpOptions(DhcpOptions *opt, std::string &resp, std::string &other, int32_t len);
    void FillDhcpv4Hdr(dhcphdr *dhcp, Dhcpv4Hdr &resp, int32_t len);
    void FillDnsHdr(dnshdr *dns, DnsHdr &resp, int32_t dnslen);
    std::string FillOptionString(char *data, int32_t len, std::string msg);
    std::string FillOptionInteger(uint32_t data, std::string msg);
    std::string FillOptionIp(uint32_t data, std::string msg);
    void FillHostRoutes(uint8_t *data, int len, std::string &resp);

    DISALLOW_COPY_AND_ASSIGN(ServicesSandesh);
};

class ArpSandesh {
public:
    static const uint8_t entries_per_sandesh = 100;
    ArpSandesh(SandeshResponse *resp) : iter_(0), resp_(resp) {};
    bool SetArpEntry(const ArpKey &key, const ArpEntry *entry); 
    void Response() { resp_->Response(); }

private:
    int iter_;
    SandeshResponse *resp_;
    DISALLOW_COPY_AND_ASSIGN(ArpSandesh);
};

#endif // _vnsw_agent_services_sandesh_h_
