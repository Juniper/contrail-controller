/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmpv6_handler_h_
#define vnsw_agent_icmpv6_handler_h_

#include "pkt/proto_handler.h"

#define IPV6_ICMP_NEXT_HEADER 58

// ICMPv6 protocol handler
class Icmpv6Handler : public ProtoHandler {
public:
    Icmpv6Handler(Agent *agent, boost::shared_ptr<PktInfo> info,
                  boost::asio::io_service &io);
    virtual ~Icmpv6Handler();

    bool Run();
    bool RouterAdvertisement(Icmpv6Proto *proto);

private:
    bool CheckPacket();
    uint16_t FillRouterAdvertisement(uint8_t *buf, uint8_t *src,
                                     uint8_t *dest, const Ip6Address &prefix,
                                     uint8_t plen);
    void SendRAResponse(uint16_t ifindex, uint16_t vrfindex,
                        uint8_t *src_ip, uint8_t *dest_ip,
                        const unsigned char *dest_mac,
                        const Ip6Address &prefix, uint8_t plen);
    void SendPingResponse();
    void SendIcmpv6Response(uint16_t ifindex, uint16_t vrfindex,
                            uint8_t *src_ip, uint8_t *dest_ip,
                            const unsigned char *dest_mac, uint16_t len);

    icmp6_hdr *icmp_;
    uint16_t icmp_len_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6Handler);
};

#endif // vnsw_agent_icmpv6_handler_h_
