/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

//
// sandesh_rw_test.cc
//
// Sandesh Read-Write Test
//

#include <map>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include "base/time_util.h"
#include "../resource_mgr_factory.h"
#include "controller/controller_init.h"
#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include "vrouter/ksync/ksync_init.h"
#include "testing/gunit.h"
#include <cmn/agent.h>
#include "base/logging.h"
#include "base/util.h"
#include <test/test_cmn_util.h>
class SandeshReadWriteUnitTest : public ::testing::Test {
protected:
    SandeshReadWriteUnitTest() {
    }
    virtual void SetUp() {
        agent = Agent::GetInstance();
        if (agent)
            std::cout <<"Agent Intialized"<<"\n";
    }
    void SandeshReadWriteProcess() {
        std::cout<<"after decode"<<"\n";
        ResourceManager *mgr = agent->resource_mgr_factory()->GetResourceMgr(
                ResourceMgrFactory::MPLS_TYPE);
        MplsData mplsdata; 
        mplsdata.key = 10;
        mplsdata.mpls_label = 11;
        mplsdata.data = "data";
        mgr->AddResourceData(mplsdata);   
        mgr->SetChanged(false);
        mgr->WriteToFile();
    }
    
    void SandeshReadProcess() {
        std::cout<<"after decode"<<"\n";
        ResourceManager *mgr = agent->resource_mgr_factory()->GetResourceMgr(
                ResourceMgrFactory::MPLS_TYPE);
        mgr->SetChanged(false);
        std::vector<ResourceData*>mplsdata;
        uint32_t marker = 1;
        mgr->ReadResourceData(mplsdata, marker);
        std::vector<ResourceData*>::iterator it = mplsdata.begin();
        if (it != mplsdata.end()) {
            MplsData *data =  static_cast<MplsData*>(*it);
            EXPECT_EQ(data->key, 10);
            EXPECT_EQ(data->mpls_label, 11); 
            std::cout<<"==================="<<"\n";
            std::cout<<data->key<<data->mpls_label<<"\n";
            std::cout<<"==================="<<"\n";
            delete data;
        }   
    }

    Agent *agent;

};


TEST_F(SandeshReadWriteUnitTest, StructBinaryWrite) {
    SandeshReadWriteProcess();
}

TEST_F(SandeshReadWriteUnitTest, StructBinaryRead) {
    SandeshReadProcess();
}


int main(int argc, char **argv) {
    LoggingInit();
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);
    usleep(100000);
    bool success = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
