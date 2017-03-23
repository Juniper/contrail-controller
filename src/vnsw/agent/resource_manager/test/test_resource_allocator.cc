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
        NextHopKey *nh_key = new VrfNHKey("vrf1", false, false);
        ResourceManager::KeyPtr key
                  (new NexthopIndexResourceKey(agent->resource_manager(),
                                               nh_key));
        NextHopKey *nh_key1 = new VrfNHKey("vrf2", false, false);
        ResourceManager::KeyPtr key1
                  (new NexthopIndexResourceKey(agent->resource_manager(),
                                               nh_key1));
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
        WAIT_FOR(200000, 1, BackUpResourceTable::FindFile("/tmp/backup", 
                "contrail_vrf_resource-").empty() != true);
        agent->resource_manager()->backup_mgr()->Init(); 
        client->WaitForIdle();
        NextHopKey *nh_key = new VrfNHKey("vrf1", false, false);
        ResourceManager::KeyPtr key
                  (new NexthopIndexResourceKey(agent->resource_manager(),
                                               nh_key));
        NextHopKey *nh_key1 = new VrfNHKey("vrf2", false, false);
        ResourceManager::KeyPtr key1
                  (new NexthopIndexResourceKey(agent->resource_manager(),
                                               nh_key1));
        ResourceTable *table = key.get()->resource_table();
        IndexResourceData *data =
            static_cast<IndexResourceData *>(table->FindKey(key));
        // Check the data restored
        label = data->index();
        // Clear the Mpls label
        agent->resource_manager()->Release(Resource::MPLS_INDEX, label);
        client->WaitForIdle();
        data = static_cast<IndexResourceData *>(table->FindKey(key));
        // deleted check for NULL
        EXPECT_TRUE(data == NULL);
        data = static_cast<IndexResourceData *>(table->FindKey(key1));
        // make sure that key1 present
        EXPECT_TRUE(data != NULL);
        label = data->index();
        agent->resource_manager()->Release(Resource::MPLS_INDEX, label);
        data = static_cast<IndexResourceData *>(table->FindKey(key1));
        EXPECT_TRUE(data == NULL);
    }
    Agent *agent;
    uint32_t label;
};

TEST_F(SandeshReadWriteUnitTest, StructBinaryWrite) {
    SandeshWriteProcess();
}

TEST_F(SandeshReadWriteUnitTest, StructBinaryRead) {
    SandeshReadProcess();
    system("rm /tmp/backup/*");
}
// Create VM Port & delete Port.
// This make sure that file created. and take the size & md5sum.
// Delete the port this should update the file with new md5.
// size of a file should be less than that of with vm_port
TEST_F(SandeshReadWriteUnitTest, SandesMd5_verification) {

    struct PortInfo input1[] = {
            {"vnet8", 8, "8.1.1.1", "00:00:00:01:01:01", 1, 1}
        };

    client->Reset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input1, 0));
    EXPECT_TRUE(VmPortFind(8));
    client->Reset();
    // 4 interface nh, 1 vrf nh and 1 for bridge route
    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 6);
    WAIT_FOR(200000, 1, BackUpResourceTable::FindFile("/tmp/backup",
             "contrail_interface_resource-").empty() != true);
    std::string file_name = "/tmp/backup/" +
        BackUpResourceTable::FindFile("/tmp/backup",
                "contrail_interface_resource-"); 
    struct stat st;
    EXPECT_TRUE(stat(file_name.c_str(), &st) != -1);
    uint32_t size_with_port = (uint32_t)st.st_size;
    uint32_t  md5sum;
    // Take the md5sum before port delete
    EXPECT_TRUE(BackUpResourceTable::CalculateHashSum(file_name, &md5sum));
    system("rm -rf /tmp/backup/*");
    client->WaitForIdle();
    client->WaitForIdle();
    DeleteVmportEnv(input1, 1, true);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(8));
    EXPECT_TRUE(Agent::GetInstance()->mpls_table()->Size() == 0);
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, MakeUuid(8), "");
    WAIT_FOR(100, 1000,(Agent::GetInstance()->interface_table()->Find(&key, true)
                                == NULL));
    WAIT_FOR(200000, 1, BackUpResourceTable::FindFile("/tmp/backup",
             "contrail_interface_resource-").empty() != true);
    std::string file_name1 = "/tmp/backup/" +
        BackUpResourceTable::FindFile("/tmp/backup",
                "contrail_interface_resource-");
    struct stat st1;
    EXPECT_TRUE(stat(file_name1.c_str(), &st1) != -1);
    // Make sure that MD5 sum changes after port delete.
    std::stringstream ss;
    ss << md5sum;
    EXPECT_TRUE(file_name1.find(ss.str()) == std::string::npos);
    uint32_t size_with_out_port = (uint32_t)st1.st_size;
    EXPECT_TRUE(size_with_port > size_with_out_port);
    client->Reset();
}

// Verify same index is returned from resource table when alloc api is
// called with same resource manager key
TEST_F(SandeshReadWriteUnitTest, MplsLabelReuse) {
    NextHopKey *nh_key = new VrfNHKey("vrf1", true, false);
    ResourceManager::KeyPtr key
              (new NexthopIndexResourceKey(agent->resource_manager(),
                                           nh_key));
    label = agent->mpls_table()->AllocLabel(key);
    ResourceTable *table = key.get()->resource_table();
    IndexResourceData *data =
        static_cast<IndexResourceData *>(table->FindKey(key));
    // Check that Key Data Created
    EXPECT_TRUE(data != NULL);
    // Invoke create label api again and make sure same label is returned
    uint32_t label2 = agent->mpls_table()->AllocLabel(key);
    EXPECT_TRUE(label == label2);
    client->WaitForIdle();
    table->DeleteKey(key);
    data = static_cast<IndexResourceData *>(table->FindKey(key));
    EXPECT_TRUE(data == NULL);
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true,
                      30000, 1000, true, true, 30000, true);
    AgentParam *param = client->param();
    param->set_restart_backup_enable(true);
    usleep(100000);
    bool success = RUN_ALL_TESTS();
    client->WaitForIdle();
    param->set_restart_backup_enable(false);
    system("rm -rf /tmp/backup");
    client->WaitForIdle();
    TestShutdown();
    delete client;
    usleep(100000);
    return success;
}
