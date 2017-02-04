/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "mac_learning_proto_handler.h"
#include "mac_learning_mgmt.h"
#include "mac_learning_init.h"

SandeshTraceBufferPtr MacLearningTraceBuf(SandeshTraceBufferCreate("MacLearning",
                                                                   5000));

MacLearningModule::MacLearningModule(Agent *agent):
    agent_(agent), mac_learning_proto_(NULL) {
}

void MacLearningModule::Init() {
    EventManager *event = agent_->event_manager();
    boost::asio::io_service &io = *event->io_service();

    mac_learning_proto_.reset(new MacLearningProto(agent_, io));
    agent_->set_mac_learning_proto(mac_learning_proto_.get());

    mac_learning_mgmt_.reset(new MacLearningMgmtManager(agent_));

    mac_learning_db_client_.reset(new MacLearningDBClient(agent_));
    mac_learning_db_client_->Init();
}

void MacLearningModule::Shutdown() {
    mac_learning_db_client_->Shutdown();
}
