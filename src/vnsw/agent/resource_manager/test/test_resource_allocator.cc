/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

//
// sandesh_rw_test.cc
//
// Sandesh Read-Write Test
//

#include "base/time_util.h"
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include "testing/gunit.h"
#include <cmn/agent.h>
#include "base/logging.h"
#include "base/util.h"
#include <test/test_cmn_util.h>
#include <resource_manager/resource_manager.h>
#include "resource_manager/resource_backup.h"
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

class SandeshReadWriteUnitTest : public ::testing::Test {
protected:
    SandeshReadWriteUnitTest() {
    }
    virtual void SetUp() {
        agent = Agent::GetInstance();
    }
    // Write the Interface Key to Backup filesystem
    // Delete the Key from In memory before reading the Key.
    void SandeshWriteProcess() {
        ResourceManager::KeyPtr key
                  (new InterfaceIndexResourceKey(agent->resource_manager(),
                                                 MakeUuid(1), MacAddress(),
                                                 true, 2, 1));
        ResourceManager::KeyPtr key1
                  (new InterfaceIndexResourceKey(agent->resource_manager(),
                                                 MakeUuid(2), MacAddress(),
                                                 true, 2, 2));
        label = agent->mpls_table()->AllocLabel(key);
        ResourceTable *table = key.get()->resource_table();
        IndexResourceData *data =
            static_cast<IndexResourceData *>(table->FindKey(key));
        // Check that Key Data Created
        EXPECT_TRUE(data != NULL);
        label = agent->mpls_table()->AllocLabel(key1);
        client->WaitForIdle();
        table->DeleteKey(key);
        data = static_cast<IndexResourceData *>(table->FindKey(key));
        EXPECT_TRUE(data == NULL);
        //Delete the Key so that restore key will not assert on
        //duplicate key
        table->DeleteKey(key1);
    }
    // Restore the Data  from file and delete it.
   void SandeshReadProcess() {
        struct stat st;
        WAIT_FOR(200000, 1,
                 stat("/tmp/backup/contrail_interface_resource", &st) != -1);
        agent->resource_manager()->backup_mgr()->Init(); 
        client->WaitForIdle();
        ResourceManager::KeyPtr key
            (new InterfaceIndexResourceKey(agent->resource_manager(),
                                                 MakeUuid(1), MacAddress(),
                                                 true, 2, 1));
        ResourceManager::KeyPtr key1
                  (new InterfaceIndexResourceKey(agent->resource_manager(),
                                                 MakeUuid(2), MacAddress(),
                                                 true, 2, 2));
        ResourceTable *table = key.get()->resource_table();
        IndexResourceData *data =
            static_cast<IndexResourceData *>(table->FindKey(key));
        // Check the data restored
        label = data->index();
        // Clear the Mpls label
        agent->mpls_table()->FreeLabel(label);
        client->WaitForIdle();
        data = static_cast<IndexResourceData *>(table->FindKey(key));
        // deleted check for NULL
        EXPECT_TRUE(data == NULL);
        data = static_cast<IndexResourceData *>(table->FindKey(key1));
        // make sure that key1 present
        EXPECT_TRUE(data != NULL);
        label = data->index();
        agent->mpls_table()->FreeLabel(label);
        data = static_cast<IndexResourceData *>(table->FindKey(key1));
        EXPECT_TRUE(data == NULL);
    }
    Agent *agent;
    int label;
};


TEST_F(SandeshReadWriteUnitTest, StructBinaryWrite) {
    SandeshWriteProcess();
}

TEST_F(SandeshReadWriteUnitTest, StructBinaryRead) {
    SandeshReadProcess();
}


int main(int argc, char **argv) {
    LoggingInit();
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true,
                      30000, 1000, true, true, 30000, true);
    AgentParam *param = client->param();
    param->set_restart_backup_enable(true);
    usleep(100000);
    bool success = RUN_ALL_TESTS();
    client->WaitForIdle();
    param->set_restart_backup_enable(false);
    TestShutdown();
    system("rm -rf /tmp/backup");
    delete client;
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
