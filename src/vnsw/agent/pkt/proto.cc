/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include "pkt/proto.h"
#include "pkt/agent_stats.h"
#include "pkt/proto_handler.h"
#include "pkt/pkt_init.h"

////////////////////////////////////////////////////////////////////////////////

Proto::Proto(Agent *agent, const char *task_name, PktHandler::PktModuleName mod,
             boost::asio::io_service &io) 
    : agent_(agent), module_(mod), trace_(true), io_(io),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId(task_name), mod,
                  boost::bind(&Proto::ProcessProto, this, _1)) {
    agent->pkt()->pkt_handler()->Register(mod, this);
}

Proto::Proto(Agent *agent, const char *task_name, PktHandler::PktModuleName mod,
             boost::asio::io_service &io, uint32_t workq_iterations) :
    agent_(agent), module_(mod), trace_(true), io_(io),
    work_queue_(TaskScheduler::GetInstance()->GetTaskId(task_name), mod,
                boost::bind(&Proto::ProcessProto, this, _1), workq_iterations,
                workq_iterations) {
    agent->pkt()->pkt_handler()->Register(mod, this);
}

Proto::~Proto() { 
    work_queue_.Shutdown();
}

bool Proto::Enqueue(boost::shared_ptr<PktInfo> msg) {
    return work_queue_.Enqueue(msg);
}

// PktHandler enqueues the packet as-is without decoding based on "cmd" in
// agent_hdr. Decode the pacekt first. Its possible that protocol handler may
// change based on packet decode
bool Proto::ProcessProto(boost::shared_ptr<PktInfo> msg_info) {
    PktHandler *pkt_handler = agent_->pkt()->pkt_handler();
    if (msg_info->module == PktHandler::INVALID) {
        msg_info->module =
            pkt_handler->ParsePacket(msg_info->agent_hdr, msg_info.get(),
                                     msg_info->packet_buffer()->data());
        if (msg_info->module == PktHandler::INVALID) {
            agent_->stats()->incr_pkt_dropped();
            return true;
        }

        msg_info->packet_buffer()->set_module(msg_info->module);
        if (msg_info->module != module_) {
            pkt_handler->Enqueue(msg_info->module, msg_info);
            return true;
        }
    }

    if (trace_) {
        pkt_handler->AddPktTrace(module_, PktTrace::In, msg_info.get());
    }

    ProtoHandler *handler = AllocProtoHandler(msg_info, io_);
    if (handler->Run())
        delete handler;
    return true;
}
