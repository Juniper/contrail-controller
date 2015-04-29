/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include <boost/assign/list_of.hpp>
#include "base/util.h"
#include "oper/config_manager.h"
#include "oper/physical_device_vn.h"

class ConfigManagerTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent_ = Agent::GetInstance();
        mgr_ = agent_->config_manager();
        client->WaitForIdle();
        intf_count_ = agent_->interface_table()->Size();
        vn_count_ = agent_->vn_table()->Size();
        vrf_count_ = agent_->vrf_table()->Size();

        AddPhysicalDevice("dev-1", 1);
        AddPhysicalInterface("pintf-1", 1, "pintf-1");
        AddLink("physical-router", "dev-1", "physical-interface", "pintf-1");
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelLink("physical-router", "dev-1", "physical-interface", "pintf-1");
        DeletePhysicalDevice("dev-1");
        DeletePhysicalInterface("pintf-1");
        client->WaitForIdle();

        WAIT_FOR(1000, 1000, (mgr_->VmiNodeCount() == 0));
        WAIT_FOR(1000, 1000, (mgr_->LogicalInterfaceNodeCount() == 0));
        WAIT_FOR(1000, 1000, (mgr_->PhysicalDeviceVnCount() == 0));

        WAIT_FOR(1000, 1000, (agent_->interface_table()->Size() == intf_count_));
        WAIT_FOR(1000, 1000, (agent_->physical_device_vn_table()->Size() == 0));
        WAIT_FOR(1000, 1000, (agent_->vn_table()->Size() == vn_count_));
        WAIT_FOR(1000, 1000, (agent_->vrf_table()->Size() == vrf_count_));
    }

    bool AddInterface(uint32_t max_count);
    bool DelInterface(uint32_t max_count);

    Agent *agent_;
    ConfigManager *mgr_;
    uint32_t intf_count_;
    uint32_t vn_count_;
    uint32_t vrf_count_;
};

void MakePortInfo(struct PortInfo *info, uint32_t id) {
    sprintf(info->name, "vnet-%d", id);
    info->intf_id = id;
    sprintf(info->addr, "1.1.1.%d", id);
    sprintf(info->mac, "00:00:00:00:00:%02x", id);
    info->vn_id = id;
    info->vm_id = id;
}

bool ConfigManagerTest::AddInterface(uint32_t max_count) {
    uint32_t count = 0;
    for (count = 1; count <= max_count; count++) {
        char name[32];
        sprintf(name, "li-%d", count);
        AddLogicalInterface(name, count, name, count);
        AddLink("physical-interface", "pintf-1", "logical-interface", name);
        client->WaitForIdle();
    }

    struct PortInfo info[] = {
        {"vnet", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    for (count = 1; count <= max_count; count++) {
        MakePortInfo(info, count);
        AddPort(info[0].name, info[0].intf_id);
        char li_name[32];
        sprintf(li_name, "li-%d", count);
        AddLink("logical-interface", li_name, "virtual-machine-interface",
                info[0].name);

        CreateVmportEnv(info, 1);
        client->WaitForIdle();
    }

    int inactive_count = 0;
    for (count = 1; count <= max_count; count++) {
        WAIT_FOR(1000, 1000, (VmPortActive(count) == true));
        if (VmPortActive(count) == false) {
            inactive_count++;
            cout << "Interface " << count << " inactive"
                << " Entry " << VmPortGet(count) << endl;
        }
    }

    EXPECT_EQ(inactive_count, 0);
    for (count = 1; count <= max_count; count++) {
        char name[32];
        sprintf(name, "li-%d", count);
        LogicalInterface *li = LogicalInterfaceGet(count, name);
        EXPECT_TRUE(li != NULL);
        EXPECT_TRUE(li->vm_interface() != NULL);
    }
    WAIT_FOR(1000, 1000,
             (agent_->physical_device_vn_table()->Size() == max_count));
    return true;
}

bool ConfigManagerTest::DelInterface(uint32_t max_count) {
    uint32_t count = 0;
    struct PortInfo info[] = {
        {"vnet", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    for (count = 1; count <= max_count; count++) {
        char li_name[32];
        sprintf(li_name, "li-%d", count);
        MakePortInfo(info, count);
        DelLink("logical-interface", li_name, "virtual-machine-interface",
                info[0].name);
        DeleteVmportEnv(info, 1, true);
        client->WaitForIdle();
    }

    for (count = 1; count <= max_count; count++) {
        char name[32];
        sprintf(name, "li-%d", count);
        DelLink("physical-interface", "pintf-1", "logical-interface", name);
        DeleteLogicalInterface(name);
        client->WaitForIdle();
    }

    for (count = 1; count <= max_count; count++) {
        WAIT_FOR(1000, 1000, (VmPortFind(count) == false));
    }
    return true;
}

TEST_F(ConfigManagerTest, intf_1) {
    AddInterface(1);
    DelInterface(1);
}

TEST_F(ConfigManagerTest, intf_2) {
    AddInterface(ConfigManager::kIterationCount);
    DelInterface(ConfigManager::kIterationCount);
}

TEST_F(ConfigManagerTest, intf_3) {
    AddInterface(ConfigManager::kIterationCount * 2);
    DelInterface(ConfigManager::kIterationCount * 2);
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
