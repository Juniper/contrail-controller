/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <algorithm>
#include <net/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "oper/health_check.h"
#include "oper/metadata_ip.h"
#include "pkt/flow_table.h"

#define VMI_MAX_COUNT 256

IpamInfo ipam_info_1[] = {
    {"1.1.1.0", 24, "1.1.1.254"},
};

// A non-ecmp port
struct PortInfo input[] = {
    {"vif1", 1, "1.1.1.1", "00:01:01:01:01:01", 1, 1},
    {"vif2", 2, "1.1.1.2", "00:01:01:01:01:02", 1, 1},
    {"vif3", 3, "1.1.1.3", "00:01:01:01:01:03", 1, 1},
};

class HealthCheckFlowTest : public ::testing::Test {
public:
    HealthCheckFlowTest() :
        agent_(Agent::GetInstance()),
        flow_proto_(agent_->pkt()->get_flow_proto()) {
    }
    virtual ~HealthCheckFlowTest() { }

    virtual void SetUp() {
        CreateVmportEnv(input, 3);
        AddIPAM("vn1", ipam_info_1, 1);
        client->WaitForIdle();

        AddHealthCheckService("HC-1", 1, "http://local-ip/", "HTTP");
        AddHealthCheckService("HC-2", 2, "http://local-ip:8080/", "HTTP");
        AddHealthCheckService("HC-3", 3, "local-ip", "PING");
        AddLink("virtual-machine-interface", "vif1", "service-health-check",
                "HC-1", "service-port-health-check");
        AddLink("virtual-machine-interface", "vif2", "service-health-check",
                "HC-2", "service-port-health-check");
        AddLink("virtual-machine-interface", "vif3", "service-health-check",
                "HC-3", "service-port-health-check");
        client->WaitForIdle();


        FlowStatsTimerStartStop(agent_, true);
        GetInfo();

        EXPECT_TRUE(vmi_[1]->ip_active(Address::INET));
        EXPECT_TRUE(vmi_[2]->ip_active(Address::INET));
        EXPECT_TRUE(vmi_[3]->ip_active(Address::INET));

        InetInterfaceKey key("vhost0");
        vhost_ = static_cast<InetInterface *>
            (agent_->interface_table()->FindActiveEntry(&key));
        router_id_ = agent_->router_id();
    }

    virtual void TearDown() {
        FlushFlowTable();

        DeleteVmportEnv(input, 3, true);
        client->WaitForIdle();

        DelIPAM("vn1");
        client->WaitForIdle();

        DelLink("virtual-machine-interface", "vif1", "service-health-check",
                "HC-1");
        DelLink("virtual-machine-interface", "vif2", "service-health-check",
                "HC-2");
        DelLink("virtual-machine-interface", "vif3", "service-health-check",
                "HC-3");
        DelHealthCheckService("HC-1");
        DelHealthCheckService("HC-2");
        DelHealthCheckService("HC-3");
        client->WaitForIdle();

        FlowStatsTimerStartStop(agent_, false);
        WAIT_FOR(1000, 1000, (agent_->vrf_table()->Size() == 1));
    }

    void GetInfo() {
        for (uint32_t i = 1; i <= VMI_MAX_COUNT; i++) {
            vmi_[i] = VmInterfaceGet(i);
            if (vmi_[i] == NULL)
                continue;

            EXPECT_TRUE(VmPortActive(i));
            const VmInterface::HealthCheckInstanceSet &set =
                vmi_[i]->hc_instance_set();
            hc_instance_[i] = *set.begin();
            mip_[i] = hc_instance_[i]->ip();
        }
    }

    void FlushFlowTable() {
        client->EnqueueFlowFlush();
        client->WaitForIdle();
        WAIT_FOR(1000, 1000, (flow_proto_->FlowCount() == 0));
    }

    void UpdateHealthCheck(uint8_t id, bool state) {
        string msg = "success";
        if (state == false)
            msg = "failure";
        HealthCheckInstanceEvent *event = new HealthCheckInstanceEvent
            (hc_instance_[id], HealthCheckInstanceEvent::MESSAGE_READ, msg);
        agent_->health_check_table()->InstanceEventEnqueue(event);
        client->WaitForIdle();

        WAIT_FOR(1000, 1000, (vmi_[id]->is_hc_active() == state));
    }

    void IcmpTest(uint8_t id) {
        TxIpPacket(vhost_->id(), router_id_.to_string().c_str(),
                   mip_[id]->GetLinkLocalIp().to_string().c_str(), 1);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(vhost_->flow_key_nh()->id(),
                                  router_id_.to_string(),
                                  mip_[id]->GetLinkLocalIp().to_string(), 1,
                                  0, 0);
        EXPECT_TRUE(flow != NULL);
        EXPECT_FALSE(flow->IsShortFlow());
        EXPECT_TRUE(flow->IsNatFlow());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow != NULL);
        EXPECT_FALSE(rflow->IsShortFlow());
        EXPECT_TRUE(rflow->IsNatFlow());

        EXPECT_TRUE(rflow->key().src_addr.to_v4() ==
                    vmi_[id]->primary_ip_addr());
        EXPECT_TRUE(rflow->key().dst_addr == mip_[id]->service_ip());
    }

    void HttpTestFromVHost(uint8_t id, uint16_t port) {
        TxTcpPacket(vhost_->id(), router_id_.to_string().c_str(),
                    mip_[id]->GetLinkLocalIp().to_string().c_str(), 10000,
                    port, false);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(vhost_->flow_key_nh()->id(),
                                  router_id_.to_string(),
                                  mip_[id]->GetLinkLocalIp().to_string(),
                                  6, 10000, port);
        EXPECT_TRUE(flow != NULL);
        EXPECT_FALSE(flow->IsShortFlow());
        EXPECT_TRUE(flow->IsNatFlow());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow != NULL);
        EXPECT_FALSE(rflow->IsShortFlow());
        EXPECT_TRUE(rflow->IsNatFlow());

        EXPECT_TRUE(rflow->key().src_addr.to_v4() ==
                    vmi_[id]->primary_ip_addr());
        EXPECT_TRUE(rflow->key().dst_addr == mip_[id]->service_ip());
    }

    void HttpTestFromVmi(uint8_t id, uint16_t sport, uint16_t dport) {
        string sip = vmi_[id]->primary_ip_addr().to_string();
        TxTcpPacket(vmi_[id]->id(), sip.c_str(),
                    mip_[id]->service_ip().to_string().c_str(), sport, dport,
                    false);
        client->WaitForIdle();

        FlowEntry *flow = FlowGet(vmi_[id]->flow_key_nh()->id(), sip,
                                  mip_[id]->service_ip().to_string(),
                                  6, sport, dport);
        EXPECT_TRUE(flow != NULL);
        EXPECT_FALSE(flow->IsShortFlow());
        EXPECT_TRUE(flow->IsNatFlow());

        FlowEntry *rflow = flow->reverse_flow_entry();
        EXPECT_TRUE(rflow != NULL);
        EXPECT_FALSE(rflow->IsShortFlow());
        EXPECT_TRUE(rflow->IsNatFlow());

        EXPECT_TRUE(rflow->key().src_addr == router_id_);
        EXPECT_TRUE(rflow->key().dst_addr.to_v4() ==
                    mip_[id]->GetLinkLocalIp());
    }

    void AddFatFlow(struct PortInfo *input, uint8_t id,
                    const std::string &protocol, int port) {
        ostringstream str;

        str << "<virtual-machine-interface-fat-flow-protocols>"
               "<fat-flow-protocol>"
               "<protocol>" << protocol << "</protocol>"
               "<port>" << port << "</port>"
               "</fat-flow-protocol>"
               "</virtual-machine-interface-fat-flow-protocols>";
        AddNode("virtual-machine-interface", input[id].name, input[id].intf_id,
                str.str().c_str());
        client->WaitForIdle();
    }

protected:
    Agent *agent_;
    Ip4Address router_id_;
    FlowProto *flow_proto_;
    InetInterface *vhost_;
    VmInterface *vmi_[VMI_MAX_COUNT];
    HealthCheckInstance *hc_instance_[VMI_MAX_COUNT];
    const MetaDataIp *mip_[VMI_MAX_COUNT];
};

TEST_F(HealthCheckFlowTest, Ping_Active_1) {
    IcmpTest(3);
}

TEST_F(HealthCheckFlowTest, Http_Active_1) {
    HttpTestFromVHost(1, 80);
}

TEST_F(HealthCheckFlowTest, Http_Active_Non_Service_Port_1) {
    HttpTestFromVHost(1, 81);
}

TEST_F(HealthCheckFlowTest, Http_Active_Non_Default_Service_Port_1) {
    EXPECT_EQ(hc_instance_[2]->service()->url_port(), 8080);
    HttpTestFromVHost(2, 8080);
}

TEST_F(HealthCheckFlowTest, Ping_InActive_1) {
    UpdateHealthCheck(3, false);
    IcmpTest(3);
}

TEST_F(HealthCheckFlowTest, Http_InActive_Service_Port_1) {
    UpdateHealthCheck(1, false);
    HttpTestFromVHost(1, 80);
}

TEST_F(HealthCheckFlowTest, Http_InActive_Non_Service_Port_1) {
    UpdateHealthCheck(1, false);
    HttpTestFromVHost(1, 81);
}

TEST_F(HealthCheckFlowTest, Http_InActive_Non_Default_Service_Port_1) {
    UpdateHealthCheck(2, false);
    EXPECT_EQ(hc_instance_[2]->service()->url_port(), 8080);
    HttpTestFromVHost(2, 8080);
}

TEST_F(HealthCheckFlowTest, Active_FatFlow_1) {
    AddFatFlow(input, 0, "tcp", 80);
    HttpTestFromVHost(1, 80);
}

TEST_F(HealthCheckFlowTest, Active_FatFlow_From_Vmi_1) {
    AddFatFlow(input, 0, "tcp", 80);
    HttpTestFromVmi(1, 80, 0);
}

int main(int argc, char *argv[]) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init, true, true, true, 100*1000);
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
