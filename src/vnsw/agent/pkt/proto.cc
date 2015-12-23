/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"
#include "cmn/agent_stats.h"

////////////////////////////////////////////////////////////////////////////////

Proto::Proto(Agent *agent, const char *task_name, PktHandler::PktModuleName mod,
             boost::asio::io_service &io) 
    : agent_(agent), module_(mod), trace_(true), free_buffer_(false), io_(io),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId(task_name), mod,
                  boost::bind(&Proto::ProcessProto, this, _1)) {
    agent->pkt()->pkt_handler()->Register(mod, this);
    std::ostringstream str;
    str << "Proto work queue. Module " << mod;
    work_queue_.set_name(str.str());
}

Proto::~Proto() { 
    work_queue_.Shutdown();
}

void Proto::FreeBuffer(PktInfo *msg) {
    msg->pkt = NULL;
    msg->eth = NULL;
    msg->arp = NULL;
    msg->ip = NULL;
    msg->transp.tcp = NULL;
    msg->data = NULL;
    msg->reset_packet_buffer();
}

bool Proto::Enqueue(boost::shared_ptr<PktInfo> msg) {
    if (Validate(msg.get()) == false) {
        return true;
    }

    if (free_buffer_) {
        FreeBuffer(msg.get());
    }

    return work_queue_.Enqueue(msg);
}

// PktHandler enqueues the packet as-is without decoding based on "cmd" in
// agent_hdr. Decode the pacekt first. Its possible that protocol handler may
// change based on packet decode
bool Proto::ProcessProto(boost::shared_ptr<PktInfo> msg_info) {
    PktHandler *pkt_handler = agent_->pkt()->pkt_handler();
    assert(msg_info->module != PktHandler::INVALID);
    if (trace_) {
        pkt_handler->AddPktTrace(module_, PktTrace::In, msg_info.get());
    }

    ProtoHandler *handler = AllocProtoHandler(msg_info, io_);
    if (handler->Run())
        delete handler;
    return true;
}
