/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_arp_handler_hpp
#define vnsw_agent_arp_handler_hpp

#include "pkt/proto_handler.h"

#define GRATUITOUS_ARP 0x0100 // keep this different from standard ARP commands

struct ArpKey;
class ArpEntry;

class ArpHandler : public ProtoHandler {
public:
    ArpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
               boost::asio::io_service &io);
    virtual ~ArpHandler();

    bool Run();
    void SendArp(uint16_t op, const MacAddress &smac, in_addr_t sip,
                 const MacAddress &tmac, in_addr_t tip,
                 uint16_t itf, uint16_t vrf);

private:
    bool HandlePacket();
    bool HandleMessage();
    void EntryDelete(ArpKey &key);
    uint16_t ArpHdr(const MacAddress &, in_addr_t, const MacAddress &,
                    in_addr_t, uint16_t);

    ether_arp *arp_;
    in_addr_t arp_tpa_;

    DISALLOW_COPY_AND_ASSIGN(ArpHandler);
};

#endif // vnsw_agent_arp_handler_hpp
