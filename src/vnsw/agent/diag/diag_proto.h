/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_proto_hpp
#define vnsw_agent_diag_proto_hpp

#include <pkt/pkt_handler.h>

class DiagProto : public Proto {
public:
    DiagProto(Agent *agent, boost::asio::io_service &io)
        : Proto(agent, "Agent::Diag", PktHandler::DIAG, io) {}

    virtual ~DiagProto() {}

    ProtoHandler *AllocProtoHandler(PktInfo *info,
                                    boost::asio::io_service &io) {
        return new DiagPktHandler(agent(), info, io);
    }

private:
    DISALLOW_COPY_AND_ASSIGN(DiagProto);
};

#endif
