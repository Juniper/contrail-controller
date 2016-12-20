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
        ResourceReq req;
        MplsData *mplsdata = new MplsData(); 
        mplsdata->mpls_label = 11;
        mplsdata->data = "data";
        MplsKey *rkey = new MplsKey();
        rkey->key = 10;
        req.resourcekey.reset(rkey);
        req.resourcedata.reset(mplsdata);
        mgr->AddResourceData(req);   
        ResourceReq req1;
        MplsData *mplsdata1 = new MplsData(); 
        mplsdata1->mpls_label = 12;
        mplsdata1->data = "data";
        MplsKey *rkey1 = new MplsKey();
        rkey1->key = 11;
        req1.resourcekey.reset(rkey1);
        req1.resourcedata.reset(mplsdata1);
        mgr->AddResourceData(req1);   
        mgr->SetChanged(false);
        mgr->WriteToFile();
    }
    
    void SandeshReadProcess() {
        std::cout<<"after decode"<<"\n";
        ResourceManager *mgr = agent->resource_mgr_factory()->GetResourceMgr(
                ResourceMgrFactory::MPLS_TYPE);
        mgr->SetChanged(false);
        std::map<ResourceKey*, ResourceData*>mplsdata;
        mgr->ReadResourceData(mplsdata);
        std::map<ResourceKey* , ResourceData*>::iterator it = mplsdata.begin();
        while (it != mplsdata.end()) {
            MplsData *data =  static_cast<MplsData*>(it->second);
            MplsKey *key = static_cast<MplsKey *>(it->first);
            //EXPECT_EQ(data->mpls_label, 11); 
            std::cout<<"==================="<<"\n";
            std::cout<<"Key"<<key->key<<"\n";
            std::cout<<"data"<<data->mpls_label<<"\n";
            std::cout<<"==================="<<"\n";
            ++it;
            delete data;
            delete key;
        }   
        EXPECT_EQ(mplsdata.size(), 2);
        
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
