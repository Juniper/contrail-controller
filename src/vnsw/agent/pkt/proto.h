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

    virtual bool Validate(PktInfo *msg) { return true; }
    virtual bool Enqueue(boost::shared_ptr<PktInfo> msg);
    virtual ProtoHandler *AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                            boost::asio::io_service &io) = 0;

    void FreeBuffer(PktInfo *msg);
    bool ProcessProto(boost::shared_ptr<PktInfo> msg_info);
    void set_trace(bool val) { trace_ = val; }
    void set_free_buffer(bool val) { free_buffer_ = val; }
    Agent *agent() const { return agent_; }
protected:
    Agent *agent_;
    PktHandler::PktModuleName module_;
    bool trace_;
    bool free_buffer_;
    boost::asio::io_service &io_;

private:
    WorkQueue<boost::shared_ptr<PktInfo> > work_queue_;
    DISALLOW_COPY_AND_ASSIGN(Proto);
};

#endif // vnsw_agent_proto_hpp
