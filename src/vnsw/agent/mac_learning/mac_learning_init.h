/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __MAC_LEARNING_INIT_H___
#define __MAC_LEARNING_INIT_H___
#include "mac_learning/mac_learning_proto.h"
#include "mac_learning/mac_learning.h"
#include "mac_learning/mac_learning_mgmt.h"
#include "mac_learning/mac_learning_db_client.h"
#include "mac_learning/mac_learning_types.h"

extern SandeshTraceBufferPtr MacLearningTraceBuf;
class MacLearningModule {
public:
    MacLearningModule(Agent *agent);
    ~MacLearningModule() {}

    void Init();
    void Shutdown();

    MacLearningMgmtManager* mac_learning_mgmt() {
        return mac_learning_mgmt_.get();
    }

    MacLearningDBClient* mac_learning_db_client() {
        return mac_learning_db_client_.get();
    }
private:
    Agent *agent_;
    boost::scoped_ptr<MacLearningProto> mac_learning_proto_;
    boost::scoped_ptr<MacLearningMgmtManager> mac_learning_mgmt_;
    boost::scoped_ptr<MacLearningDBClient> mac_learning_db_client_;
};
#endif
