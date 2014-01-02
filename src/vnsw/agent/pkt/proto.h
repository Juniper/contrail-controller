/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_proto_hpp
#define vnsw_agent_proto_hpp

#include "pkt_handler.h"

class Agent;
class ProtoHandler;

// Protocol task (work queue for each protocol)
class Proto {
public:
    Proto(Agent *agent, const char *task_name, PktHandler::PktModuleName mod,
          boost::asio::io_service &io);
    virtual ~Proto();

    Agent *agent() const { return agent_; }

    virtual bool Validate(PktInfo *msg) {
        return true;
    }

    virtual bool RemovePktBuff() {
        return false;
    }

    virtual ProtoHandler *AllocProtoHandler(PktInfo *info,
                                            boost::asio::io_service &io) = 0;
    bool ValidateAndEnqueueMessage(PktInfo *msg);
    bool ProcessProto(PktInfo *msg_info);

protected:
    Agent *agent_;
    WorkQueue<PktInfo *> work_queue_;
    boost::asio::io_service &io_;
    DISALLOW_COPY_AND_ASSIGN(Proto);
};

#endif // vnsw_agent_proto_hpp
