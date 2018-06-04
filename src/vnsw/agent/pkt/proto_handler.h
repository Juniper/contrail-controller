/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_proto_handler_hpp
#define vnsw_agent_proto_handler_hpp

#include "pkt_handler.h"

// Pseudo header for UDP checksum
struct PseudoUdpHdr {
    in_addr_t src;
    in_addr_t dest;
    uint8_t   res;
    uint8_t   prot;
    uint16_t  len;
    PseudoUdpHdr(in_addr_t s, in_addr_t d, uint8_t p, uint16_t l) :
        src(s), dest(d), res(0), prot(p), len(l) { }
};

struct vlanhdr {
    uint16_t tpid;
    uint16_t tci;
};

// Protocol handler, to process an incoming packet
// Each protocol has a handler derived from this
class ProtoHandler {
public:
    ProtoHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                 boost::asio::io_service &io);
    virtual ~ProtoHandler();

    virtual bool Run() = 0;

    void Send(uint32_t itf, uint32_t vrf, uint16_t, PktHandler::PktModuleName);
    void Send(uint32_t itf, uint32_t vrf, uint16_t cmd,
              uint32_t param1, uint32_t param2, PktHandler::PktModuleName mod);

    int EthHdr(const MacAddress &src, const MacAddress &dest,
               const uint16_t proto);
    int EthHdr(char *buff, uint16_t len, const MacAddress &src,
               const MacAddress &dest, const uint16_t proto, uint16_t vlan_id);
    int EthHdr(char *buff, uint16_t len, const Interface *interface,
               const MacAddress &src, const MacAddress &dest,
               const uint16_t proto);
    int EthHdr(char *buff, uint16_t len, uint32_t ifindex,
               const MacAddress &src, const MacAddress &dest,
               const uint16_t proto);

    void VlanHdr(uint8_t *ptr, uint16_t tci);
    void IpHdr(uint16_t len, in_addr_t src, in_addr_t dest, uint8_t protocol,
               uint16_t id, uint8_t ttl);
    uint16_t IpHdr(char *buff, uint16_t buf_len, uint16_t len, in_addr_t src,
                   in_addr_t dest, uint8_t protocol, uint16_t id, uint8_t ttl);
    void Ip6Hdr(ip6_hdr *ip, uint16_t plen, uint8_t next_header,
                uint8_t hlim, uint8_t *src, uint8_t *dest);
    void UdpHdr(uint16_t len, in_addr_t src, uint16_t src_port, in_addr_t dest,
                uint16_t dest_port);
    void UdpHdr(udphdr *hdr, uint16_t len, const uint8_t *src, uint16_t src_port,
                const uint8_t *dest, uint16_t dest_port, uint8_t next_hdr);
    void UdpHdr(uint16_t len, const uint8_t *src, uint16_t src_port,
                const uint8_t *dest, uint16_t dest_port, uint8_t next_hdr);
    uint16_t UdpHdr(udphdr *udp, uint16_t buf_len, uint16_t len, in_addr_t src,
                    uint16_t src_port, in_addr_t dest, uint16_t dest_port);
    uint16_t IcmpHdr(char *buff, uint16_t buf_len, uint8_t type, uint8_t code,
                     uint16_t word1, uint16_t word2);
    void IcmpChecksum(char *buff, uint16_t buf_len);

    void IgmpChecksum(char *buff, uint16_t buf_len);

    uint32_t Sum(uint16_t *, std::size_t, uint32_t) const;
    uint16_t Csum(uint16_t *, std::size_t, uint32_t) const;
    uint16_t UdpCsum(in_addr_t, in_addr_t, std::size_t, udphdr *) const;
    uint16_t Ipv6Csum(const uint8_t *src, const uint8_t *dest,
                        uint16_t plen, uint8_t next_hdr, uint16_t *hdr) const;
    uint16_t Icmpv6Csum(const uint8_t *src, const uint8_t *dest,
                        icmp6_hdr *icmp, uint16_t plen) const;

    Agent *agent() const { return agent_; }
    uint32_t GetVrfIndex() const { return pkt_info_->GetAgentHdr().vrf; }
    uint32_t GetInterfaceIndex() const {
        return pkt_info_->GetAgentHdr().ifindex;
    }
    uint16_t GetLength() const { return pkt_info_->len; }
    uint32_t GetCmdParam() const { return pkt_info_->GetAgentHdr().cmd_param; }

    PktInfo *pkt_info() const { return pkt_info_.get(); }
    uint32_t EncapHeaderLen() const;
protected:
    Agent   *agent_;
    boost::shared_ptr<PktInfo> pkt_info_;
    boost::asio::io_service &io_;

private:
    void FillUdpHdr(udphdr *udp, uint16_t len,
                    uint16_t src_port, uint16_t dest_port);

    DISALLOW_COPY_AND_ASSIGN(ProtoHandler);
};

#endif // vnsw_agent_proto_handler_hpp
