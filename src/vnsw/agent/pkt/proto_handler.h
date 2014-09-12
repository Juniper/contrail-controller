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

    void Send(uint16_t, uint16_t, uint16_t, uint16_t, PktHandler::PktModuleName);

    uint16_t EthHdr(char *buff, uint8_t len, const MacAddress &src,
                    const MacAddress &dest, const uint16_t proto,
                    uint16_t vlan_id);

    void EthHdr(const MacAddress &, const MacAddress &,
                    const uint16_t);

    void VlanHdr(uint8_t *ptr, uint16_t tci);
    void IpHdr(uint16_t, in_addr_t, in_addr_t, uint8_t);
    uint16_t IpHdr(char *, uint16_t, uint16_t, in_addr_t, in_addr_t, uint8_t);
    void UdpHdr(uint16_t, in_addr_t, uint16_t, in_addr_t, uint16_t);
    uint16_t UdpHdr(char *, uint16_t, uint16_t, in_addr_t, uint16_t,
                    in_addr_t, uint16_t);
    uint16_t IcmpHdr(char *buff, uint16_t buf_len, uint8_t type, uint8_t code,
                     uint16_t word1, uint16_t word2);
    void IcmpChecksum(char *buff, uint16_t buf_len);

    uint32_t Sum(uint16_t *, std::size_t, uint32_t);
    uint16_t Csum(uint16_t *, std::size_t, uint32_t);
    uint16_t UdpCsum(in_addr_t, in_addr_t, std::size_t, udphdr *);

    Agent *agent() { return agent_; }
    uint32_t GetVrfIndex() const { return pkt_info_->GetAgentHdr().vrf; }
    uint16_t GetInterfaceIndex() const {
        return pkt_info_->GetAgentHdr().ifindex;
    }
    uint16_t GetLength() const { return pkt_info_->len; }
    uint32_t GetCmdParam() const { return pkt_info_->GetAgentHdr().cmd_param; }

    uint32_t EncapHeaderLen() const;
protected:
    Agent   *agent_;
    boost::shared_ptr<PktInfo> pkt_info_;
    boost::asio::io_service &io_;

private:
    DISALLOW_COPY_AND_ASSIGN(ProtoHandler);
};

#endif // vnsw_agent_proto_handler_hpp
