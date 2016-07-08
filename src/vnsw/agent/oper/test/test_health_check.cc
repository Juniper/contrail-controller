/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
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
#include "oper/health_check.h"

void RouterIdDepInit(Agent *agent) {
}

class HealthCheckConfigTest : public ::testing::Test {
public:
    virtual void SetUp() {
        agent = Agent::GetInstance();
        //agent->health_check_table()->set_test_mode(true);
    }

    virtual void TearDown() {
        client->WaitForIdle();
        EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    }

    HealthCheckService *FindHealthCheck(int id) {
        HealthCheckTable *table = agent->health_check_table();
        uuid hc_uuid = MakeUuid(id);
        HealthCheckServiceKey key(hc_uuid);
        return static_cast<HealthCheckService *>(table->FindActiveEntry(&key));
    }

protected:
    Agent *agent;
};

TEST_F(HealthCheckConfigTest, Basic) {
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    AddHealthCheckService("HC_test", 1, "local-ip", "PING");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet10",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf != NULL);

    // Validate health check instance running on interface
    WAIT_FOR(100, 100, intf->hc_instance_set().size() != 0);

    DelLink("virtual-machine-interface", "vnet10",
            "service-health-check", "HC_test");
    client->WaitForIdle();

    // Validate health check instance stopped running on interface
    WAIT_FOR(100, 100, intf->hc_instance_set().size() == 0);

    DelHealthCheckService("HC_test");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

TEST_F(HealthCheckConfigTest, interface_config_before_nova) {
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    AddHealthCheckService("HC_test", 1, "local-ip", "PING");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);

    CreateVmportWithoutNova(input, 1);
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet10",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf == NULL);

    IntfCfgAddThrift(input, 0);
    client->WaitForIdle();

    intf = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf != NULL);

    // Validate health check instance running on interface
    WAIT_FOR(100, 100, intf->hc_instance_set().size() != 0);

    DelLink("virtual-machine-interface", "vnet10",
            "service-health-check", "HC_test");
    client->WaitForIdle();

    // Validate health check instance stopped running on interface
    WAIT_FOR(100, 100, intf->hc_instance_set().size() == 0);

    DelHealthCheckService("HC_test");
    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
