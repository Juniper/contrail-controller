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
        boost::uuids::uuid hc_uuid = MakeUuid(id);
        HealthCheckServiceKey key(hc_uuid);
        return static_cast<HealthCheckService *>(table->FindActiveEntry(&key));
    }

    void AddVmiServiceType(std::string intf_name, int intf_id, string type) {
        std::ostringstream buf;
        buf << "<virtual-machine-interface-properties>";
        buf << "<service-interface-type>";
        buf << type;
        buf << "</service-interface-type>";
        buf << "</virtual-machine-interface-properties>";
        char cbuf[10000];
        strcpy(cbuf, buf.str().c_str());
        AddNode("virtual-machine-interface", intf_name.c_str(),
                intf_id, cbuf);
        client->WaitForIdle();
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

/* Verify agent parses port-tuple config and updates its VmInterface object with
 * uuid of other end of the service-instance */
TEST_F(HealthCheckConfigTest, port_tuple) {
    using boost::uuids::nil_uuid;

    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
        {"vnet11", 11, "2.1.1.10", "00:00:00:01:02:10", 11, 11},
        {"vnet12", 12, "3.1.1.10", "00:00:00:01:03:10", 12, 12},
    };

    CreateVmportEnv(input, 3);
    client->WaitForIdle();

    AddVmiServiceType("vnet10", 10, "management");
    AddVmiServiceType("vnet11", 11, "left");
    AddVmiServiceType("vnet12", 12, "right");
    client->WaitForIdle();

    VmInterface *intf1 = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf1 != NULL);
    EXPECT_TRUE(intf1->si_other_end_vmi() == nil_uuid());

    VmInterface *intf2 = VmInterfaceGet(input[1].intf_id);
    EXPECT_TRUE(intf2 != NULL);
    EXPECT_TRUE(intf2->si_other_end_vmi() == nil_uuid());

    VmInterface *intf3 = VmInterfaceGet(input[2].intf_id);
    EXPECT_TRUE(intf3 != NULL);
    EXPECT_TRUE(intf3->si_other_end_vmi() == nil_uuid());

    AddNode("port-tuple", "pt1", 1);
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet10",
            "port-tuple", "pt1", "port-tuple-interface");
    AddLink("virtual-machine-interface", "vnet11",
            "port-tuple", "pt1", "port-tuple-interface");
    AddLink("virtual-machine-interface", "vnet12",
            "port-tuple", "pt1", "port-tuple-interface");
    client->WaitForIdle();

    WAIT_FOR(100, 1000, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 1000, (intf2->si_other_end_vmi() == intf3->GetUuid()));
    WAIT_FOR(100, 1000, (intf3->si_other_end_vmi() == intf2->GetUuid()));
    EXPECT_TRUE(intf2->is_left_si() == true);
    EXPECT_TRUE(intf3->is_left_si() == false);

    DelLink("virtual-machine-interface", "vnet10", "port-tuple", "pt1",
            "port-tuple-interface");
    DelLink("virtual-machine-interface", "vnet11", "port-tuple", "pt1",
            "port-tuple-interface");
    DelLink("virtual-machine-interface", "vnet12", "port-tuple", "pt1",
            "port-tuple-interface");
    DelNode("port-tuple", "pt1");
    client->WaitForIdle();

    WAIT_FOR(100, 100, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf2->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf3->si_other_end_vmi() == nil_uuid()));
    EXPECT_TRUE(intf2->is_left_si() == false);
    EXPECT_TRUE(intf3->is_left_si() == false);

    DeleteVmportEnv(input, 3, true);

    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmInterfaceGet(input[0].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[1].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[2].intf_id) == NULL));
}

TEST_F(HealthCheckConfigTest, segment_hc) {
    using boost::uuids::nil_uuid;

    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
        {"vnet11", 11, "2.1.1.10", "00:00:00:01:02:10", 11, 11},
        {"vnet12", 12, "3.1.1.10", "00:00:00:01:03:10", 12, 12},
    };

    CreateVmportEnv(input, 3);
    client->WaitForIdle();

    AddVmiServiceType("vnet10", 10, "management");
    AddVmiServiceType("vnet11", 11, "left");
    AddVmiServiceType("vnet12", 12, "right");
    client->WaitForIdle();

    VmInterface *intf1 = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf1 != NULL);
    EXPECT_TRUE(intf1->si_other_end_vmi() == nil_uuid());

    VmInterface *intf2 = VmInterfaceGet(input[1].intf_id);
    EXPECT_TRUE(intf2 != NULL);
    EXPECT_TRUE(intf2->si_other_end_vmi() == nil_uuid());

    VmInterface *intf3 = VmInterfaceGet(input[2].intf_id);
    EXPECT_TRUE(intf3 != NULL);
    EXPECT_TRUE(intf3->si_other_end_vmi() == nil_uuid());

    EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    AddHealthCheckService("HC_test", 1, "local-ip", "PING", "segment");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);
    EXPECT_TRUE(hc->IsSegmentHealthCheckService() == true);

    AddNode("port-tuple", "pt1", 1);
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet10",
            "port-tuple", "pt1", "port-tuple-interface");
    AddLink("virtual-machine-interface", "vnet11",
            "port-tuple", "pt1", "port-tuple-interface");
    AddLink("virtual-machine-interface", "vnet12",
            "port-tuple", "pt1", "port-tuple-interface");
    client->WaitForIdle();

    //Verify that interface has correct association of other end of SI
    WAIT_FOR(100, 1000, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 1000, (intf2->si_other_end_vmi() == intf3->GetUuid()));
    WAIT_FOR(100, 1000, (intf3->si_other_end_vmi() == intf2->GetUuid()));

    //Associate health-check to left interface
    AddLink("virtual-machine-interface", "vnet11",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    // Validate health check instance is still NOT running on both left and
    // right interfaces because of absence of service_ip on IPAM
    WAIT_FOR(100, 1000, intf2->hc_instance_set().size() == 0);
    WAIT_FOR(100, 1000, intf3->hc_instance_set().size() == 0);

    IpamInfo ipam_info1[] = {
        {"1.1.1.0", 24, "1.1.1.200", true},
    };
    IpamInfo ipam_info2[] = {
        {"2.1.1.0", 24, "2.1.1.200", true},
    };
    IpamInfo ipam_info3[] = {
        {"3.1.1.0", 24, "3.1.1.200", true},
    };
    AddIPAM("vn10", ipam_info1, 1, NULL, "vdns1");
    AddIPAM("vn11", ipam_info2, 1, NULL, "vdns2");
    AddIPAM("vn12", ipam_info3, 1, NULL, "vdns3");
    client->WaitForIdle();

    // Validate health check instance is still NOT running on both left and
    // right interfaces because of absence of service_ip on IPAM
    WAIT_FOR(100, 1000, intf2->hc_instance_set().size() != 0);
    WAIT_FOR(100, 1000, intf3->hc_instance_set().size() != 0);

    //Disassociate health-check from left interface
    DelLink("virtual-machine-interface", "vnet11",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    // Validate health check instance is NOT running on both left and right
    // interface
    WAIT_FOR(100, 100, intf2->hc_instance_set().size() == 0);
    WAIT_FOR(100, 100, intf3->hc_instance_set().size() == 0);

    DelIPAM("vn10", "vdns1");
    DelIPAM("vn11", "vdns2");
    DelIPAM("vn12", "vdns3");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "vnet10", "port-tuple", "pt1",
            "port-tuple-interface");
    DelLink("virtual-machine-interface", "vnet11", "port-tuple", "pt1",
            "port-tuple-interface");
    DelLink("virtual-machine-interface", "vnet12", "port-tuple", "pt1",
            "port-tuple-interface");
    DelNode("port-tuple", "pt1");
    client->WaitForIdle();

    WAIT_FOR(100, 100, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf2->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf3->si_other_end_vmi() == nil_uuid()));

    DelHealthCheckService("HC_test");
    client->WaitForIdle();
    WAIT_FOR(100, 100, (FindHealthCheck(1) == NULL));

    DeleteVmportEnv(input, 3, true);
    client->WaitForIdle();
    WAIT_FOR(100, 100, (VmInterfaceGet(input[0].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[1].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[2].intf_id) == NULL));
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
