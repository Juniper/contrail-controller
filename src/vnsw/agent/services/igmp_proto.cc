/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "oper/vn.h"
#include "services/igmp_proto.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "pkt/pkt_init.h"

using namespace boost::asio;
using boost::asio::ip::udp;

IgmpProto::IgmpProto(Agent *agent, boost::asio::io_service &io) :
    Proto(agent, "Agent::Services", PktHandler::IGMP, io) {

    // limit the number of entries in the workqueue
    work_queue_.SetSize(agent->params()->services_queue_limit());
    work_queue_.SetBounded(true);

    iid_ = agent->interface_table()->Register(
                  boost::bind(&IgmpProto::ItfNotify, this, _2));
    vnid_ = agent->vn_table()->Register(
                  boost::bind(&IgmpProto::VnNotify, this, _2));
}

IgmpProto::~IgmpProto() {
}

void IgmpProto::Shutdown() {
    agent_->interface_table()->Unregister(iid_);
    agent_->vn_table()->Unregister(vnid_);
}

ProtoHandler *IgmpProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new IgmpHandler(agent(), info, io);
}

void IgmpProto::ItfNotify(DBEntryBase *entry) {
}

void IgmpProto::VnNotify(DBEntryBase *entry) {
}

