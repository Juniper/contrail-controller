/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <sys/socket.h>

#include <net/if.h>

#ifdef __linux__
#include <linux/netlink.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>
#endif

#ifdef __FreeBSD__
#include <sys/sockio.h>
#include <ifaddrs.h>
#endif

#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>
#include "oper/qos_config.h"

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

class FwdClassTest : public ::testing::Test {

    virtual void SetUp() {
        agent = Agent::GetInstance();
    }

    virtual void TearDown() {

    }

protected:
    Agent *agent;
};

TEST_F(FwdClassTest, Test1) {
    AddGlobalConfig(NULL, 0);
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 0);
}

TEST_F(FwdClassTest, Test2) {
    TestForwardingClassData data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };

    AddGlobalConfig(data, 2);
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 2);
    VerifyForwardingClass(agent, data, 2);

    DelGlobalConfig(data, 2);
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 0);
}

TEST_F(FwdClassTest, Test3) {
    TestForwardingClassData data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };

    AddGlobalConfig(data, 2);
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 2);
    VerifyForwardingClass(agent, data, 2);

    TestForwardingClassData data2[] = {
        {1, 2, 3, 4, 5},
        {2, 3, 4, 5, 6}
    };

    AddGlobalConfig(data2, 2);
    DelLink("forwarding-class", "fc1", "qos-queue", "qosqueue1");
    DelLink("forwarding-class", "fc2", "qos-queue", "qosqueue2");
    DelNode("qos-queue", "qosqueue1");
    DelNode("qos-queue", "qosqueue2");
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 2);
    VerifyForwardingClass(agent, data2, 2);

    DelGlobalConfig(data2, 2);
    client->WaitForIdle();
    EXPECT_TRUE(agent->forwarding_class_table()->Size() == 0);
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
