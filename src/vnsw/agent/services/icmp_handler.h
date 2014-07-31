/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_icmp_handler_h_
#define vnsw_agent_icmp_handler_h_

#include "pkt/proto_handler.h"

// ICMP protocol handler
class IcmpHandler : public ProtoHandler {
public:
    IcmpHandler(Agent *agent, boost::shared_ptr<PktInfo> info,
                boost::asio::io_service &io);
    virtual ~IcmpHandler();

    bool Run();

private:
    bool CheckPacket();
    void SendResponse();

    icmp *icmp_;
    uint16_t icmp_len_;
    DISALLOW_COPY_AND_ASSIGN(IcmpHandler);
};

#endif // vnsw_agent_icmp_handler_h_
