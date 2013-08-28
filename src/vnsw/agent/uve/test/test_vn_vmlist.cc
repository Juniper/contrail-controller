/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/init_config.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <cmn/agent_cmn.h>
#include <base/task.h>
#include <io/event_manager.h>
#include <base/util.h>
#include <ifmap_agent_parser.h>
#include <ifmap_agent_table.h>
#include <cfg/interface_cfg.h>
#include <cfg/init_config.h>
#include <oper/vn.h>
#include <oper/vm.h>
#include <oper/interface.h>
#include <oper/mirror_table.h>
#include <uve/uve_client.h>

#include "testing/gunit.h"
#include "test_cmn_util.h"
#include "vr_types.h"

using namespace std;

void RouterIdDepInit() {
}

class UveVnVmListTest : public ::testing::Test {
};

TEST_F(UveVnVmListTest, VmAdd_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:02:02:02", 2, 2},
    };

    //Create VM, VN, VRF and Vmport
    GetUveClient()->VnVmListUpdateCountReset();
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_EQ(2U, GetUveClient()->VnVmListSize());
    EXPECT_EQ(2U, GetUveClient()->VnVmListUpdateCount());

    struct PortInfo input1[] = {
        {"vnet3", 3, "1.1.1.3", "00:00:00:01:01:03", 1, 3},
    };
    struct PortInfo input2[] = {
        {"vnet4", 4, "1.1.1.4", "00:00:00:01:01:04", 1, 4},
    };
    GetUveClient()->VnVmListUpdateCountReset();
    CreateVmportEnv(input1, 1);
    client->WaitForIdle();

    CreateVmportEnv(input2, 1);
    client->WaitForIdle();
    EXPECT_EQ(4U, GetUveClient()->VnVmListSize());
    EXPECT_EQ(2U, GetUveClient()->VnVmListUpdateCount());

    client->Reset();
    GetUveClient()->VnVmListUpdateCountReset();
    DeleteVmportEnv(input1, 1, false);
    client->WaitForIdle();
    DeleteVmportEnv(input2, 1, false);
    client->WaitForIdle();
    EXPECT_EQ(2U, GetUveClient()->VnVmListSize());
    EXPECT_EQ(2U, GetUveClient()->VnVmListUpdateCount());

    GetUveClient()->VnVmListUpdateCountReset();
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    EXPECT_EQ(0U, GetUveClient()->VnVmListSize());
    EXPECT_EQ(2U, GetUveClient()->VnVmListUpdateCount());
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    usleep(10000);
    return RUN_ALL_TESTS();
}
