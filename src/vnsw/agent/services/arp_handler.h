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
                 uint32_t itf, uint32_t vrf);
    friend void intrusive_ptr_add_ref(const ArpHandler *p);
    friend void intrusive_ptr_release(const ArpHandler *p);

private:
    bool HandlePacket();
    bool HandleMessage();
    void EntryDelete(ArpKey &key);
    uint16_t ArpHdr(const MacAddress &smac, in_addr_t sip,
                    const MacAddress &tmac, in_addr_t tip, uint16_t op);

    ether_arp *arp_;
    in_addr_t arp_tpa_;
    mutable tbb::atomic<uint32_t> refcount_;
    DISALLOW_COPY_AND_ASSIGN(ArpHandler);
};

#endif // vnsw_agent_arp_handler_hpp
