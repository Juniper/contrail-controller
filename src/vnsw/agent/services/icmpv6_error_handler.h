/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_SERVICES_ICMPV6_ERROR_HANDLER_H_
#define VNSW_AGENT_SERVICES_ICMPV6_ERROR_HANDLER_H_

#include "pkt/proto_handler.h"
#include "services/icmp_proto.h"

class Icmpv6ErrorProto;

// ICMP protocol handler
class Icmpv6ErrorHandler : public ProtoHandler {
 public:
    static const int ICMPV6_PAYLOAD_LEN = 128;
    Icmpv6ErrorHandler(Agent *agent, Icmpv6ErrorProto *proto,
                       boost::shared_ptr<PktInfo> info,
                       boost::asio::io_service *io);
    virtual ~Icmpv6ErrorHandler();

    bool Run();

 private:
    bool ValidatePacket();
    bool SendIcmpv6Error(VmInterface *intf);

    Icmpv6ErrorProto *proto_;
    DISALLOW_COPY_AND_ASSIGN(Icmpv6ErrorHandler);
};

#endif  // VNSW_AGENT_SERVICES_ICMPV6_ERROR_HANDLER_H_
