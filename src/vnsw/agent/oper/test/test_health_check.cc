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
        count_ = 0;
        //agent->health_check_table()->set_test_mode(true);
        rid_ = Agent::GetInstance()->interface_table()->Register(
                            boost::bind(&HealthCheckConfigTest::ItfUpdate, this, _2));
        TestPkt0Interface *tap = (TestPkt0Interface *)
                (Agent::GetInstance()->pkt()->control_interface());
        tap->RegisterCallback(
                boost::bind(&HealthCheckConfigTest::TestClientReceive, this, _1, _2));
    }

    virtual void TearDown() {
        client->WaitForIdle();
        EXPECT_TRUE(agent->health_check_table()->Size() == 0);
        Agent::GetInstance()->interface_table()->Unregister(rid_);
    }

    void TestClientReceive(uint8_t *buf, std::size_t len) {
        // Nothing to do for now.
        count_++;
        return;
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

    void ItfUpdate(DBEntryBase *entry) {
        Interface *itf = static_cast<Interface *>(entry);
        tbb::mutex::scoped_lock lock(mutex_);
        unsigned int i;
        for (i = 0; i < itf_id_.size(); ++i)
            if (itf_id_[i] == itf->id())
                break;
        if (entry->IsDeleted()) {
            if (itf_count_ && i < itf_id_.size()) {
                itf_count_--;
                LOG(DEBUG, "HC test : interface deleted " << itf_id_[0]);
                itf_id_.erase(itf_id_.begin()); // we delete in create order
            }
        } else {
            if (i == itf_id_.size()) {
                itf_count_++;
                itf_id_.push_back(itf->id());
                LOG(DEBUG, "HC test : interface added " << itf->id());
            }
        }
    }

    uint32_t GetItfCount() {
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_count_;
    }

    std::size_t GetItfId(int index) {
        tbb::mutex::scoped_lock lock(mutex_);
        return itf_id_[index];
    }

protected:
    Agent *agent;
    DBTableBase::ListenerId rid_;
    uint32_t itf_count_;
    std::vector<std::size_t> itf_id_;
    tbb::mutex mutex_;
    uint32_t count_;
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

// To test the fix for failure to exchange ICMP messages
// Test segment_hc_1 is updated version of segment_hc above
// The following steps are followed in the test case
// a. Create VM and VMIs.
// b. Attach IPAM to each of VNs created by user.
// c. Create HC service
// d. Create a transparent service template.
// e. Create a service instance with above template
// f. Create port tuple
// g. Associate port tuple with the VMIs.
// h. Associate port tuple with Service Instance.
// i. Associate health check to service instance,
//    specifically to the left interface.
// j. clean up in reverse order.
TEST_F(HealthCheckConfigTest, segment_hc_1) {
    using boost::uuids::nil_uuid;

    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
        {"vnet11", 11, "2.1.1.10", "00:00:00:01:02:10", 11, 10},
        {"vnet12", 12, "3.1.1.10", "00:00:00:01:03:10", 12, 10},
    };

    CreateVmportEnv(input, 3);
    client->WaitForIdle();

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

    // Create a Health Check Service
    AddHealthCheckService("HC_test", 1, "", "PING", "segment");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);
    EXPECT_TRUE(hc->IsSegmentHealthCheckService() == true);

    // Create a Service Template
    CreateTransparentV2ST("template-1", true, true, true);

    // Create a Service Instance
    CreateServiceInstance("instance-1", "vn10", "1.1.1.10",
                            "vn11", "2.1.1.10", "vn12", "3.1.1.10");

    // Link service instance with a service template
    AddLink("service-instance", "instance-1",
            "service-template", "template-1",
            "service-instance-service-template");

    // Create a port-tuple
    AddNode("port-tuple", "pt1", 1);
    client->WaitForIdle();

    // Link port-tuple to the VM interfaces
    // and to the service instance
    AddLink("virtual-machine-interface", "vnet10",
            "port-tuple", "pt1", "port-tuple-interface");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet11",
            "port-tuple", "pt1", "port-tuple-interface");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet12",
            "port-tuple", "pt1", "port-tuple-interface");
    client->WaitForIdle();

    AddLink("service-instance", "instance-1",
            "port-tuple", "pt1", "service-instance-port-tuple");
    client->WaitForIdle();

    // Link segment health check to service instance
    std::stringstream buf;
    buf << "<interface-type>" << "left" << "</interface-type>";
    AddLinkNode("service-health-check-service-instance", "HC_test-1", buf.str().c_str());
    AddLink("service-health-check-service-instance", "HC_test-1",
            "service-instance", "instance-1",
            "service-health-check-service-instance");
    AddLink("service-health-check-service-instance", "HC_test-1",
            "service-health-check", "HC_test",
            "service-health-check-service-instance");
    client->WaitForIdle();

    AddVmiServiceType("vnet10", 10, "management");
    AddVmiServiceType("vnet11", 11, "left");
    AddVmiServiceType("vnet12", 12, "right");
    client->WaitForIdle();

    // Associate health-check to left interface
    AddLink("virtual-machine-interface", "vnet11",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    //Verify that interface has correct association of other end of SI
    WAIT_FOR(100, 1000, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 1000, (intf2->si_other_end_vmi() == intf3->GetUuid()));
    WAIT_FOR(100, 1000, (intf3->si_other_end_vmi() == intf2->GetUuid()));

    // right interfaces because of absence of service_ip on IPAM
    WAIT_FOR(100, 1000, intf2->hc_instance_set().size() != 0);
    WAIT_FOR(100, 1000, intf3->hc_instance_set().size() != 0);

    // Sleep for 6 seconds to allow tx for 3 icmp messages
    // of the segment health check to be transmitted.
    // Since response to icmp message doesnt come, health
    // check fails.
    sleep (6);

    // Disassociate health-check from left interface
    DelLink("virtual-machine-interface", "vnet11",
            "service-health-check", "HC_test", "service-port-health-check");
    client->WaitForIdle();

    DelLink("service-instance", "instance-1",
            "port-tuple", "pt1", "service-instance-port-tuple");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "vnet10", "port-tuple", "pt1",
            "port-tuple-interface");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "vnet11", "port-tuple", "pt1",
            "port-tuple-interface");
    client->WaitForIdle();

    DelLink("virtual-machine-interface", "vnet12", "port-tuple", "pt1",
            "port-tuple-interface");
    client->WaitForIdle();

    DelNode("port-tuple", "pt1");
    client->WaitForIdle();

    DelLink("service-health-check-service-instance", "HC_test-1",
            "service-health-check", "HC_test",
            "service-health-check-service-instance");
    DelLink("service-health-check-service-instance", "HC_test-1",
            "service-instance", "instance-1",
            "service-health-check-service-instance");
    DelNode("service-health-check-service-instance", "HC_test-1");
    client->WaitForIdle();

    DelLink("service-instance", "instance-1",
            "service-template", "template-1",
            "service-instance-service-template");

    DeleteServiceInstance("instance-1");
    client->WaitForIdle();

    DeleteServiceTemplate("template-1");
    client->WaitForIdle();

    // Validate health check instance is NOT running on both left and right
    // interface
    WAIT_FOR(100, 100, intf2->hc_instance_set().size() == 0);
    WAIT_FOR(100, 100, intf3->hc_instance_set().size() == 0);

    WAIT_FOR(100, 100, (intf1->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf2->si_other_end_vmi() == nil_uuid()));
    WAIT_FOR(100, 100, (intf3->si_other_end_vmi() == nil_uuid()));

    DelHealthCheckService("HC_test");
    client->WaitForIdle();
    WAIT_FOR(100, 100, (FindHealthCheck(1) == NULL));

    EXPECT_TRUE((count_ >= 1));

    EXPECT_TRUE(intf1->ipv4_active());
    EXPECT_TRUE(intf2->ipv4_active());
    EXPECT_TRUE(intf3->ipv4_active());

    DelIPAM("vn10", "vdns1");
    DelIPAM("vn11", "vdns2");
    DelIPAM("vn12", "vdns3");
    client->WaitForIdle();

    DeleteVmportEnv(input, 3, true);
    client->WaitForIdle();

    WAIT_FOR(100, 100, (VmInterfaceGet(input[0].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[1].intf_id) == NULL));
    WAIT_FOR(100, 100, (VmInterfaceGet(input[2].intf_id) == NULL));
    client->WaitForIdle();
}

TEST_F(HealthCheckConfigTest, http_monitor_type_1) {
    boost::system::error_code ec;
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    AddHealthCheckService("HC_test", 1, "http://10.10.10.1/test/", "HTTP");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);
    EXPECT_TRUE(hc->monitor_type() == "HTTP");
    EXPECT_TRUE(hc->url_path() == "/test/");
    EXPECT_TRUE(hc->dest_ip() == Ip4Address::from_string("10.10.10.1", ec));

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

TEST_F(HealthCheckConfigTest, http_monitor_type_2) {
    boost::system::error_code ec;
    struct PortInfo input[] = {
        {"vnet10", 10, "1.1.1.10", "00:00:00:01:01:10", 10, 10},
    };

    EXPECT_TRUE(agent->health_check_table()->Size() == 0);
    AddHealthCheckService("HC_test", 1, "/", "HTTP");
    client->WaitForIdle();
    WAIT_FOR(100, 100, agent->health_check_table()->Size() == 1);

    HealthCheckService *hc = FindHealthCheck(1);
    EXPECT_TRUE(hc != NULL);
    EXPECT_TRUE(hc->name().compare("HC_test") == 0);
    EXPECT_TRUE(hc->monitor_type() == "HTTP");
    EXPECT_TRUE(hc->url_path() == "");

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
