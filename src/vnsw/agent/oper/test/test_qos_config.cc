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

class QosConfigClassTest : public ::testing::Test {

    virtual void SetUp() {
        agent = Agent::GetInstance();
    }

    virtual void TearDown() {
        client->WaitForIdle();
        EXPECT_TRUE(agent->qos_config_table()->Size() == 0);
    }

protected:
    Agent *agent;
};

TEST_F(QosConfigClassTest, Test1) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};
    data.dscp_[1] = 1;
    data.dscp_[2] = 2;

    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    data.dscp_.clear();
    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    DelNode("qos-config", "qos_config");
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test2) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 1};
    data.dscp_[1] = 1;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 3;
    data.mpls_exp_[6] = 7;

    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    data.default_forwarding_class_ = 2;
    data.dscp_[1] = 3;
    data.dscp_[2] = 4;
    data.vlan_priority_[0] = 4;
    data.vlan_priority_[1] = 5;
    data.mpls_exp_[0] = 3;
    data.mpls_exp_[6] = 7;

    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    DelNode("qos-config", "qos_config");
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test3) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};
    data.dscp_[1] = 1;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 3;
    data.mpls_exp_[6] = 7;

    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    data.dscp_[1] = 3;
    data.dscp_[2] = 4;
    data.dscp_[3] = 3;
    data.vlan_priority_[0] = 4;
    data.vlan_priority_[1] = 5;
    data.vlan_priority_[2] = 5;
    data.mpls_exp_[0] = 3;
    data.mpls_exp_[6] = 7;

    AddQosConfig(data);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);

    DelNode("qos-config", "qos_config");
    client->WaitForIdle();
    EXPECT_TRUE(QosConfigFind(1) == false);
}

TEST_F(QosConfigClassTest, Test4) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};
    TestQosConfigData data1 =
        {"qos_config1", 2, "project", 2};

    data.dscp_[1] = 1;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 3;
    data.mpls_exp_[6] = 7;

    AddQosConfig(data);
    AddQosConfig(data1);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);
    VerifyQosConfig(agent, &data1);

    DelNode("qos-config", "qos_config");
    DelNode("qos-config", "qos_config1");
    client->WaitForIdle();
    EXPECT_TRUE(QosConfigFind(1) == false);
    EXPECT_TRUE(QosConfigFind(2) == false);
}

TEST_F(QosConfigClassTest, Test5) {
    TestForwardingClassData fc_data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };
    AddGlobalConfig(fc_data, 2);
    client->WaitForIdle();

    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};
    TestQosConfigData data1 =
        {"qos_config1", 2, "project", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddQosConfig(data);
    AddQosConfig(data1);
    client->WaitForIdle();
    VerifyQosConfig(agent, &data);
    VerifyQosConfig(agent, &data1);

    EXPECT_TRUE(QosConfigGet(1)->dscp_map().find(1)->second ==
                ForwardingClassGet(2)->id());
    EXPECT_TRUE(QosConfigGet(1)->dscp_map().find(2)->second ==
                ForwardingClassGet(2)->id());
    DelGlobalConfig(fc_data, 2);
    client->WaitForIdle();
    EXPECT_TRUE(QosConfigFind(1) == true);
    EXPECT_TRUE(QosConfigFind(2) == true);

    DelNode("qos-config", "qos_config");
    DelNode("qos-config", "qos_config1");
    client->WaitForIdle();
    EXPECT_TRUE(QosConfigFind(1) == false);
    EXPECT_TRUE(QosConfigFind(2) == false);
}

TEST_F(QosConfigClassTest, Test6) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddQosConfig(data);
    AddVn("vn1", 1);
    client->WaitForIdle();

    AddLink("qos-config", "qos_config", "virtual-network", "vn1");
    client->WaitForIdle();

    EXPECT_TRUE(VnGet(1)->qos_config() == QosConfigGet(1));

    DelLink("qos-config", "qos_config", "virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VnGet(1)->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test7) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddVn("vn1", 1);
    AddLink("qos-config", "qos_config", "virtual-network", "vn1");
    client->WaitForIdle();

    AddQosConfig(data);
    client->WaitForIdle();

    EXPECT_TRUE(VnGet(1)->qos_config() == QosConfigGet(1));

    DelLink("qos-config", "qos_config", "virtual-network", "vn1");
    client->WaitForIdle();
    EXPECT_TRUE(VnGet(1)->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DelNode("virtual-network", "vn1");
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test8) {
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;


    CreateVmportEnv(input, 1);
    AddQosConfig(data);
    client->WaitForIdle();

    AddLink("qos-config", "qos_config", "virtual-machine-interface", "vnet10");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortGet(10)->qos_config() == QosConfigGet(1));

    DelLink("qos-config", "qos_config", "virtual-machine-interface", "vnet10");
    client->WaitForIdle();
    EXPECT_TRUE(VmPortGet(10)->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DeleteVmportEnv(input, 1, true);
}

TEST_F(QosConfigClassTest, Test9) {
    TestQosConfigData data =
        {"qos_config", 1, "vhost", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddQosConfig(data);
    client->WaitForIdle();

    TestForwardingClassData fc_data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };
    AddGlobalConfig(fc_data, 2);
    client->WaitForIdle();

    AddNode("global-qos-config",
            "default-global-system-config:default-global-qos-config", 1);
    AddLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(InetInterfaceGet(agent->vhost_interface_name().c_str())
                ->qos_config() == QosConfigGet(1));
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == NULL);

    DelLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(InetInterfaceGet(agent->vhost_interface_name().c_str())
                ->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DelNode("global-qos-config",
            "default-global-system-config:default-global-qos-config");
    DelGlobalConfig(fc_data, 2);
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test10) {
    TestQosConfigData data =
        {"qos_config", 1, "fabric", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddQosConfig(data);
    client->WaitForIdle();

    TestForwardingClassData fc_data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };
    AddGlobalConfig(fc_data, 2);
    client->WaitForIdle();

    AddNode("global-qos-config",
            "default-global-system-config:default-global-qos-config", 1);
    AddLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == QosConfigGet(1));

    DelLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DelNode("global-qos-config",
            "default-global-system-config:default-global-qos-config");
    DelGlobalConfig(fc_data, 2);
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test11) {
    TestQosConfigData data =
        {"qos_config", 2, "fabric", 1};

    TestQosConfigData data1 =
        {"qos_config1", 1, "fabric", 2};

    data.dscp_[1] = 2;
    data.dscp_[2] = 2;
    data.vlan_priority_[0] = 1;
    data.vlan_priority_[1] = 2;
    data.mpls_exp_[0] = 1;
    data.mpls_exp_[6] = 2;

    AddQosConfig(data);
    AddQosConfig(data1);
    client->WaitForIdle();

    TestForwardingClassData fc_data[] = {
        {1, 1, 1, 1, 1},
        {2, 2, 2, 2, 2}
    };
    AddGlobalConfig(fc_data, 2);
    client->WaitForIdle();

    AddNode("global-qos-config",
            "default-global-system-config:default-global-qos-config", 1);
    AddLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == QosConfigGet(2));


    AddLink("qos-config", "qos_config1", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == QosConfigGet(1));

    DelLink("qos-config", "qos_config1", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == QosConfigGet(2));
    //Safety check against fabric QOS config getting applied on vhost
    EXPECT_TRUE(InetInterfaceGet(agent->vhost_interface_name().c_str())
                ->qos_config() == NULL);


    DelLink("qos-config", "qos_config", "global-qos-config",
            "default-global-system-config:default-global-qos-config");
    client->WaitForIdle();
    EXPECT_TRUE(EthInterfaceGet(agent->fabric_interface_name().c_str())
                ->qos_config() == NULL);

    DelNode("qos-config", "qos_config");
    DelNode("qos-config", "qos_config1");
    DelNode("global-qos-config", 
            "default-global-system-config:default-global-qos-config");
    DelGlobalConfig(fc_data, 2);
    client->WaitForIdle();
}

TEST_F(QosConfigClassTest, Test12) {

    AddNode("qos-config", "qos_config", 0, NULL, true);
    AddNode("forwarding-class", "fc1", 0, NULL, true);
    AddNode("qos-queue", "queue1", 0, NULL, true);
    client->WaitForIdle();

    DelNode("qos-config", "qos_config");
    DelNode("forwarding-class", "fc1");
    DelNode("qos-queue", "queue1");
    client->WaitForIdle();
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
