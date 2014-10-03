/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef VNSW_AGENT_SERVICES_ICMP_ERROR_HANDLER_H_
#define VNSW_AGENT_SERVICES_ICMP_ERROR_HANDLER_H_

#include "pkt/proto_handler.h"
#include "pkt/flow_table.h"
#include "services/icmp_proto.h"

class FlowEntry;
class IcmpErrorProto;

// ICMP protocol handler
class IcmpErrorHandler : public ProtoHandler {
 public:
    static const int ICMP_PAYLOAD_LEN = 128;
    IcmpErrorHandler(Agent *agent, IcmpErrorProto *proto,
                     boost::shared_ptr<PktInfo> info,
                     boost::asio::io_service *io);
    virtual ~IcmpErrorHandler();

    bool Run();

 private:
    bool ValidatePacket();
    bool SendIcmpError(VmInterface *intf);

    IcmpErrorProto *proto_;
    DISALLOW_COPY_AND_ASSIGN(IcmpErrorHandler);
};

#endif  // VNSW_AGENT_SERVICES_ICMP_ERROR_HANDLER_H_
