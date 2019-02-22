/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <base/os.h>
#include <base/address_util.h>
#include "test/test_cmn_util.h"
#include "test_flow_util.h"
#include "ksync/ksync_sock_user.h"
#include "oper/tunnel_nh.h"
#include "pkt/flow_table.h"

#define vmi1_mac "00:00:00:00:00:01"
#define vmi2_mac "00:00:00:00:00:02"
#define vmi3_mac "00:00:00:00:00:03"

#define vmi1_ip "11.1.1.1"
#define vmi2_ip "11.1.1.2"
#define vmi3_ip "12.1.1.1"

#define remote_vm1_ip_5 "11.1.1.5"
#define remote_vm1_ip_subnet "11.1.1.0"
#define remote_vm1_ip_plen 24

#define remote_vm3_ip "12.1.1.3"

#define vmi11_ip "100.1.1.3"
#define vmi11_mac "00:00:00:00:01:03"
#define vmi1_fip "100.1.1.10"
#define vmi2_fip "100.1.1.11"

/****************************************************************************
 * Test cases for native-loadbalancer packets kubernetes use-case
 * In case of kubernetes, floating-ip VN is same as interface native-vn
 ****************************************************************************/
struct PortInfo input1[] = {
    {"vmi1", 1, vmi1_ip, vmi1_mac, 1, 1},
    {"vmi2", 2, vmi2_ip, vmi2_mac, 1, 2},
    {"vmi3", 3, vmi3_ip, vmi3_mac, 1, 3}
};

struct PortInfo input2[] = {
    {"vmi11", 11, vmi11_ip, vmi11_mac, 2, 11},
};

IpamInfo ipam_info1[] = {
    {"11.1.1.0", 24, "11.1.1.10"},
};

IpamInfo fip_ipam_info[] = {
    {"100.1.1.0", 24, "100.1.1.1"},
};

class FlowFipTestBase : public ::testing::Test {
public:
    FlowFipTestBase() : agent_(Agent::GetInstance()) {
        flow_proto_ = agent_->pkt()->get_flow_proto();
        task_scheduler_ = agent_->task_scheduler();
        strcpy(router_id_, agent_->router_id().to_string().c_str());
        eth_intf_ = agent_->fabric_interface_name();;
    }

    virtual ~FlowFipTestBase() { }

    virtual void SetUp() {
        eth_ = EthInterfaceGet("vnet0");
        EXPECT_TRUE(eth_ != NULL);

        // Create virtual-networks
        AddVn("vn1", 1);
        AddIPAM("vn1", ipam_info1, 1);
        client->WaitForIdle();

        AddVn("vn2", 2);
        AddIPAM("vn2", fip_ipam_info, 1);
        client->WaitForIdle();

        CreateVmportEnv(input1, 3, 1);
        CreateVmportEnv(input2, 1, 1);
        client->WaitForIdle();

        vmi1_ = VmInterfaceGet(input1[0].intf_id);
        assert(vmi1_);
        vmi2_ = VmInterfaceGet(input1[1].intf_id);
        assert(vmi2_);
        vmi3_ = VmInterfaceGet(input1[2].intf_id);
        assert(vmi3_);

        vmi11_ = VmInterfaceGet(input2[0].intf_id);
        assert(vmi11_);

        vn1_ = VnGet(1);
        EXPECT_TRUE(vn1_ != NULL);
        vn2_ = VnGet(2);
        EXPECT_TRUE(vn2_ != NULL);
    }

    virtual void TearDown() {
        FlushFlowTable();
        client->Reset();

        DelIPAM("vn1");
        DelIPAM("vn2");
        client->WaitForIdle();

        DeleteVmportEnv(input1, 3, true, 1);
        DeleteVmportEnv(input2, 1, true, 1);
        client->WaitForIdle();

        EXPECT_FALSE(VmPortFind(input1, 0));
        EXPECT_FALSE(VmPortFind(input1, 1));
        EXPECT_FALSE(VmPortFind(input1, 2));
        EXPECT_FALSE(VmPortFind(input2, 0));

        EXPECT_EQ(0U, agent()->vm_table()->Size());
        EXPECT_EQ(0U, agent()->vn_table()->Size());
        EXPECT_EQ(0U, agent()->acl_table()->Size());
        EXPECT_EQ(2U, agent()->vrf_table()->Size());
        StopAgingTimer(false);
        client->WaitForIdle();
        WAIT_FOR(100, 1, (0U == flow_proto_->FlowCount()));
    }

    bool StopAgingTrigger(bool stop) {
        FlowStatsCollectorObject *obj = agent_->flow_stats_manager()->
            default_flow_stats_collector_obj();
        for (int i = 0; i < FlowStatsCollectorObject::kMaxCollectors; i++) {
            FlowStatsCollector *fsc = obj->GetCollector(i);
            fsc->TestStartStopTimer(stop);
        }
        return true;
    }

    void StopAgingTimer(bool stop) {
        int task_id = task_scheduler_->GetTaskId(kTaskFlowStatsCollector);
        TaskTrigger *trigger =
            new TaskTrigger(boost::bind(&FlowFipTestBase::StopAgingTrigger,
                                        this, stop), task_id, 0);
        std::auto_ptr<TaskTrigger> trigger_ptr(trigger);
        trigger_ptr->Set();
        client->WaitForIdle();
    }

    Agent *agent() const { return agent_; }

    void SNatL3Flow();
    void NoNatL2Flow();
    void DNatL3Flow();
    void DNatL2Flow();
    void DNatL3FlowSameVn();

protected:
    Agent *agent_;
    TaskScheduler *task_scheduler_;
    std::string eth_intf_;
    FlowProto *flow_proto_;
    PhysicalInterface *eth_;
    char router_id_[80];
    VmInterface *vmi1_;
    VmInterface *vmi2_;
    VmInterface *vmi3_;
    VmInterface *vmi11_;

    VnEntry *vn1_;
    VnEntry *vn2_;
};

class FlowFipTest : public FlowFipTestBase {
public:
    FlowFipTest() : FlowFipTestBase() {
    }

    virtual ~FlowFipTest() { }

    virtual void SetUp() {
        FlowFipTestBase::SetUp();

        // Configure Floating-IP in vn2
        AddFloatingIpPool("fip-pool1", 1);
        AddFloatingIp("fip1", 1, vmi1_fip);
        AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        AddLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");
        AddLink("virtual-machine-interface", "vmi1", "floating-ip", "fip1");
        client->WaitForIdle();

        // Verify that bridge-table entry points to receive-nh
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vn2_->GetVrf()->GetBridgeRouteTable());
        const AgentRoute *rt = table->FindRoute(vmi1_->vm_mac());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::L2_RECEIVE);

        // Configure Floating-IP in vn1
        AddFloatingIpPool("fip-pool2", 1);
        AddFloatingIp("fip2", 1, vmi2_fip);
        AddLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool2");
        AddLink("floating-ip-pool", "fip-pool2", "virtual-network", "vn1");
        AddLink("virtual-machine-interface", "vmi2", "floating-ip", "fip2");
        client->WaitForIdle();

        // Verify that bridge-table entry points to interface-nh
        table = static_cast<BridgeAgentRouteTable *>
            (vn1_->GetVrf()->GetBridgeRouteTable());
        rt = table->FindRoute(vmi2_->vm_mac());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    }

    virtual void TearDown() {
        DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
        DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn2");
        DelLink("virtual-machine-interface", "vmi1", "floating-ip", "fip1");
        DelFloatingIpPool("fip-pool1");
        DelFloatingIp("fip1");
        client->WaitForIdle();

        DelLink("floating-ip", "fip2", "floating-ip-pool", "fip-pool2");
        DelLink("floating-ip-pool", "fip-pool2", "virtual-network", "vn1");
        DelLink("virtual-machine-interface", "vmi2", "floating-ip", "fip2");
        DelFloatingIpPool("fip-pool2");
        DelFloatingIp("fip2");
        client->WaitForIdle();

        FlowFipTestBase::TearDown();
    }
};

void FlowFipTestBase::SNatL3Flow() {
    string sip = vmi1_->primary_ip_addr().to_string();
    string smac = vmi1_->vm_mac().ToString();
    string dip = vmi11_->primary_ip_addr().to_string();
    // mac-dest will always be VRRP mac in this case
    string dmac = agent_->vrrp_mac().ToString();

    // Send TCP packet with vrrp-mac (l3-flow)
    TxL2Packet(vmi1_->id(), smac.c_str(), dmac.c_str(),
               sip.c_str(), dip.c_str(), 6, 0, vmi1_->vrf_id(), 1000, 80);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(0, sip.c_str(), dip.c_str(), IPPROTO_TCP, 1000, 80,
                           vmi1_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, dip.c_str(), vmi1_fip, IPPROTO_TCP, 80, 1000,
                            vmi11_->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
}

void FlowFipTestBase::NoNatL2Flow() {
    string sip = vmi1_->primary_ip_addr().to_string();
    string smac = vmi1_->vm_mac().ToString();
    string dip = vmi2_->primary_ip_addr().to_string();
    string dmac = vmi2_->vm_mac().ToString();

    TxL2Packet(vmi1_->id(), smac.c_str(), dmac.c_str(),
               sip.c_str(), dip.c_str(), 6, 0, vmi1_->vrf_id(), 1000, 80);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(0, sip.c_str(), dip.c_str(), IPPROTO_TCP, 1000, 80,
                           vmi1_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, dip.c_str(), sip.c_str(), IPPROTO_TCP, 80, 1000,
                            vmi2_->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
}

void FlowFipTestBase::DNatL3Flow() {
    string sip = vmi11_->primary_ip_addr().to_string();
    string smac = vmi11_->vm_mac().ToString();
    string dip = vmi1_fip;
    string dmac = agent_->vrrp_mac().ToString();

    // Send TCP packet with vrrp-mac (l3-flow)
    TxL2Packet(vmi11_->id(), smac.c_str(), dmac.c_str(),
               sip.c_str(), dip.c_str(), 6, 0, vmi11_->vrf_id(), 1000, 80);
    client->WaitForIdle();
    FlowEntry *fe = FlowGet(0, sip.c_str(), dip.c_str(), IPPROTO_TCP, 1000, 80,
                            vmi11_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, vmi1_ip, sip.c_str(), IPPROTO_TCP, 80, 1000,
                             vmi1_->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
}

void FlowFipTestBase::DNatL2Flow() {
    string sip = vmi1_->primary_ip_addr().to_string();
    string smac = vmi1_->vm_mac().ToString();
    string dip = vmi2_fip;
    string dmac = vmi2_->vm_mac().ToString();

    // Send TCP packet with interface-mac (l2-flow)
    TxL2Packet(vmi1_->id(), smac.c_str(), dmac.c_str(),
               sip.c_str(), dip.c_str(), 6, 0, vmi1_->vrf_id(), 1000, 80);
    client->WaitForIdle();

    // Validate flow created with port-nat
    FlowEntry *fe = FlowGet(0, sip.c_str(), dip.c_str(), IPPROTO_TCP, 1000,
                            80, vmi1_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, vmi2_ip, sip.c_str(), IPPROTO_TCP, 80,
                             1000, vmi2_->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
}

void FlowFipTestBase::DNatL3FlowSameVn() {
    string sip = vmi1_->primary_ip_addr().to_string();
    string smac = vmi1_->vm_mac().ToString();
    string dip = vmi2_fip;
    string dmac = agent_->vrrp_mac().ToString();

    // Send TCP packet with interface-mac (l2-flow)
    TxL2Packet(vmi1_->id(), smac.c_str(), dmac.c_str(),
               sip.c_str(), dip.c_str(), 6, 0, vmi1_->vrf_id(), 1000, 80);
    client->WaitForIdle();

    // Validate flow created with port-nat
    FlowEntry *fe = FlowGet(0, sip.c_str(), dip.c_str(), IPPROTO_TCP, 1000,
                            80, vmi1_->flow_key_nh()->id());
    EXPECT_TRUE(fe != NULL);
    FlowEntry *rfe = FlowGet(0, vmi2_ip, sip.c_str(), IPPROTO_TCP, 80,
                             1000, vmi2_->flow_key_nh()->id());
    EXPECT_TRUE(rfe != NULL);
}

// Local flow, SNAT
// From vmi1_ to vmi11_(on vn2). SNat with floating-ip
// vmi1_ and fip are in different network. So only l3-case valid
TEST_F(FlowFipTest, SNatL3Flow) {
    SNatL3Flow();
}

// Local flow, No-NAT
// vmi1_ to vmi2_ both on vn1.
// vmi1_ and vmi2_ in same network. Only l2-case valid
TEST_F(FlowFipTest, NoNatL2Flow) {
    NoNatL2Flow();
}

// Local flow, DNat
// vmi11_ (in vn2) to vmi1_ on vn1. VN different. Only L3-Flow valid
TEST_F(FlowFipTest, DNatL3Flow) {
    DNatL3Flow();
}

// Local flow, DNat
// vmi1_ (in vn1) to FIP on vmi2_ (in vn1). VN same. Only L2-Flow valid
TEST_F(FlowFipTest, DNatL2Flow) {
    DNatL2Flow();
}

// Local flow, DNat
// vmi1_ (in vn1) to FIP on vmi2_ (in vn1). VN same. L3-Flow.
// This case should not happen in real deployments since sip and dip are in
// same subnet. Only a UT scenario
TEST_F(FlowFipTest, DNatL3FlowSameVn) {
    DNatL3FlowSameVn();
}

class FlowNativeLbFipTest : public FlowFipTestBase {
public:
    FlowNativeLbFipTest() : FlowFipTestBase() {
    }

    virtual ~FlowNativeLbFipTest() { }

    virtual void SetUp() {
        FlowFipTestBase::SetUp();

        // Configure Floating-IP from native-loadbalancer in vn2
        // vmi <-> fip <-> iip <-> vn <-> ipam
        AddActiveActiveInstanceIp("fip-iip1", 100, vmi1_ip);
        AddFloatingIp("fip1", 1, vmi1_fip);
        AddLink("instance-ip", "fip-iip1", "virtual-network", "vn2");
        AddLink("floating-ip", "fip1", "instance-ip", "fip-iip1");
        AddLink("virtual-machine-interface", "vmi1", "floating-ip", "fip1");
        client->WaitForIdle();

        // Verify that bridge-table entry points to receive-nh
        BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
            (vn2_->GetVrf()->GetBridgeRouteTable());
        const AgentRoute *rt = table->FindRoute(vmi1_->vm_mac());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::L2_RECEIVE);

        // Configure Floating-IP from native-loadbalancer in vn1
        AddActiveActiveInstanceIp("fip-iip2", 100, vmi2_ip);
        AddFloatingIp("fip2", 1, vmi2_fip);
        AddLink("instance-ip", "fip-iip2", "virtual-network", "vn1");
        AddLink("floating-ip", "fip2", "instance-ip", "fip-iip2");
        AddLink("virtual-machine-interface", "vmi2", "floating-ip", "fip2");
        client->WaitForIdle();

        // Verify that bridge-table entry points to interface-nh
        table = static_cast<BridgeAgentRouteTable *>
            (vn1_->GetVrf()->GetBridgeRouteTable());
        rt = table->FindRoute(vmi2_->vm_mac());
        EXPECT_TRUE(rt->GetActiveNextHop()->GetType() == NextHop::INTERFACE);
    }

    virtual void TearDown() {
        DelLink("instance-ip", "fip-iip1", "virtual-network", "vn2");
        DelLink("floating-ip", "fip1", "instance-ip", "fip-iip1");
        DelLink("virtual-machine-interface", "vmi1", "floating-ip", "fip1");
        DelInstanceIp("fip-iip1");
        DelFloatingIp("fip1");
        client->WaitForIdle();

        DelLink("instance-ip", "fip-iip2", "virtual-network", "vn1");
        DelLink("floating-ip", "fip2", "instance-ip", "fip-iip2");
        DelLink("virtual-machine-interface", "vmi2", "floating-ip", "fip2");
        DelInstanceIp("fip-iip2");
        DelFloatingIp("fip2");
        client->WaitForIdle();

        FlowFipTestBase::TearDown();
    }
};

// Local flow, SNAT
// From vmi1_ to vmi11_(on vn2). SNat with floating-ip
// vmi1_ and fip are in different network. So only l3-case valid
TEST_F(FlowNativeLbFipTest, SNatL3Flow) {
    SNatL3Flow();
}

// Local flow, No-NAT
// vmi1_ to vmi2_ both on vn1.
// vmi1_ and vmi2_ in same network. Only l2-case valid
TEST_F(FlowNativeLbFipTest, NoNatL2Flow) {
    NoNatL2Flow();
}

// Local flow, DNat
// vmi11_ (in vn2) to vmi1_ on vn1. VN different. Only L3-Flow valid
TEST_F(FlowNativeLbFipTest, DNatL3Flow) {
    DNatL3Flow();
}

// Local flow, DNat
// vmi1_ (in vn1) to FIP on vmi2_ (in vn1). VN same. Only L2-Flow valid
TEST_F(FlowNativeLbFipTest, DNatL2Flow) {
    DNatL2Flow();
}

// Local flow, DNat
// vmi1_ (in vn1) to FIP on vmi2_ (in vn1). VN same. L3-Flow.
// This case should not happen in real deployments since sip and dip are in
// same subnet. Only a UT scenario
TEST_F(FlowNativeLbFipTest, DNatL3FlowSameVn) {
    DNatL3FlowSameVn();
}

int main(int argc, char *argv[]) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init, true, true, true,
                      (1000000 * 60 * 10), (3 * 60 * 1000));
    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
