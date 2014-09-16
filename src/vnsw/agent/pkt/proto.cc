/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"

////////////////////////////////////////////////////////////////////////////////

Proto::Proto(Agent *agent, const char *task_name, PktHandler::PktModuleName mod,
             boost::asio::io_service &io) 
    : agent_(agent), io_(io),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId(task_name), mod,
                  boost::bind(&Proto::ProcessProto, this, _1)) {
    agent->pkt()->pkt_handler()->Register(mod,
           boost::bind(&Proto::ValidateAndEnqueueMessage, this, _1) );
}

Proto::~Proto() { 
    work_queue_. Shutdown();
}

bool Proto::ValidateAndEnqueueMessage(boost::shared_ptr<PktInfo> msg) {
    if (!Validate(msg.get())) {
        return true;
    }

    if (RemovePktBuff()) {
        msg->pkt = NULL;
        msg->eth = NULL;
        msg->arp = NULL;
        msg->ip = NULL;
        msg->transp.tcp = NULL;
        msg->data = NULL;
    }

    return work_queue_.Enqueue(msg);
}

bool Proto::ProcessProto(boost::shared_ptr<PktInfo> msg_info) {
    ProtoHandler *handler = AllocProtoHandler(msg_info, io_);
    if (handler->Run())
        delete handler;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
