/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_INIT_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_INIT_H_
#include "mac_learning/mac_learning_proto.h"
#include "mac_learning/mac_learning.h"
#include "mac_learning/mac_learning_mgmt.h"
#include "mac_learning/mac_learning_db_client.h"
#include "mac_learning/mac_learning_types.h"

extern SandeshTraceBufferPtr MacLearningTraceBuf;
class MacLearningModule {
public:
    MacLearningModule(Agent *agent);
    virtual ~MacLearningModule() {}

    void Init();
    void Shutdown();

    MacLearningMgmtManager* mac_learning_mgmt() const {
        return mac_learning_mgmt_.get();
    }

    MacLearningDBClient* mac_learning_db_client() const {
        return mac_learning_db_client_.get();
    }

    MacLearningProto* mac_learning_proto() const {
        return mac_learning_proto_.get();
    }
private:
    Agent *agent_;
    boost::scoped_ptr<MacLearningProto> mac_learning_proto_;
    boost::scoped_ptr<MacLearningMgmtManager> mac_learning_mgmt_;
    boost::scoped_ptr<MacLearningDBClient> mac_learning_db_client_;
};
#endif
