/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class ServiceVlanTest : public ::testing::Test {
    virtual void SetUp() {
        client->Reset();
        client->WaitForIdle();
    }

    virtual void TearDown() {
        client->WaitForIdle();
        DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
        DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
        DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
        DelVn("vn1");
        DelVrf("vrf1");
        DelVm("vm1");
        DelPort("vnet1");
        client->WaitForIdle();
    }
};

static int ServiceVlanGetLabel(const string &vrf, const string &addr) {
    Inet4UnicastRouteEntry *rt = RouteGet(vrf, Ip4Address::from_string(addr), 32);
    EXPECT_TRUE(rt != NULL);
    if (rt == NULL) {
        return false;
    }

    return rt->GetActiveLabel();
}

static bool ValidateServiceVlan(int ifid, const string &vrf, const string &addr,
                                uint16_t tag) {
    EXPECT_TRUE(VlanNhFind(ifid, tag));

    Inet4UnicastRouteEntry *rt = RouteGet(vrf, Ip4Address::from_string(addr), 32);
    EXPECT_TRUE(rt != NULL);
    if (rt == NULL) {
        return false;
    }

    int label = rt->GetActiveLabel();
    MplsLabel *mpls = Agent::GetInstance()->mpls_table()->FindMplsLabel(label);
    EXPECT_TRUE(mpls != NULL);
    if (mpls == NULL) {
        return false;
    }

    const NextHop *nh = mpls->nexthop();
    EXPECT_TRUE(nh != NULL);
    if (nh == NULL) {
        return false;
    }

    EXPECT_EQ(NextHop::VLAN, nh->GetType());
    if (nh->GetType() != NextHop::VLAN) {
        return false;
    }

    const VlanNH *vlan = static_cast<const VlanNH *>(nh);
    EXPECT_EQ(tag, vlan->GetVlanTag());

    return (vlan->GetVlanTag() == tag);
}

static bool ValidateServiceVlanDel(int ifid, const string &vrf,
                                   const string &addr, uint16_t tag, int label){
    EXPECT_FALSE(VlanNhFind(ifid, tag));

    Inet4UnicastRouteEntry *rt = RouteGet(vrf, Ip4Address::from_string(addr), 32);
    EXPECT_TRUE(rt == NULL);

    MplsLabel *mpls = Agent::GetInstance()->mpls_table()->FindMplsLabel(label);
    EXPECT_TRUE(mpls == NULL);

    return ((rt == NULL) && (mpls == NULL));
}

TEST_F(ServiceVlanTest, FloatingIp_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input, 1);

    AddVrf("vrf2", 2);
    AddVrf("vrf3", 3);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    // Add service interface-1
    AddVmPortVrf("ser1", "1.1.1.2", 1);
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    AddLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    // Add service interface-2
    AddVmPortVrf("ser2", "1.1.1.3", 2);
    AddLink("virtual-machine-interface-routing-instance", "ser2",
            "routing-instance", "vrf3");
    AddLink("virtual-machine-interface-routing-instance", "ser2",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlan(1, "vrf2", "1.1.1.2", 1));
    int label1 = ServiceVlanGetLabel("vrf2", "1.1.1.2");

    EXPECT_TRUE(ValidateServiceVlan(1, "vrf3", "1.1.1.3", 2));
    int label2 = ServiceVlanGetLabel("vrf3", "1.1.1.3");

    LOG(DEBUG, "Deleting links");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface-routing-instance", "ser2",
            "routing-instance", "vrf3");
    DelLink("virtual-machine-interface-routing-instance", "ser2",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlanDel(1, "vrf2", "1.1.1.2", 1, label1));
    EXPECT_TRUE(ValidateServiceVlanDel(1, "vrf3", "1.1.1.3", 2, label2));

    DeleteVmportEnv(input, 1, 1);
    DelNode("virtual-machine-interface-routing-intance", "ser1");
    DelNode("virtual-machine-interface-routing-intance", "ser2");
    DelVrf("vrf2");
    DelVrf("vrf3");

}

TEST_F(ServiceVlanTest, FloatingIp_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    // Add virtual-machine-interface-routing-instance node
    AddVmPortVrf("ser1", "1.1.1.2", 1);
    client->WaitForIdle();

    // Add links // vmi-routing-instance node <-> vmi and 
    // Add link vmi-routing-instance <-> routing-instanace
    char buff[2048];
    int len = 0;
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "routing-instance", "vrf2", 2);
    AddLinkString(buff, len, "virtual-machine-interface-routing-instance",
                  "ser1", "virtual-machine-interface", "vnet1");
    AddLinkString(buff, len, "virtual-machine-interface-routing-instance", 
                  "ser1", "routing-instance", "vrf2");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlan(1, "vrf2", "1.1.1.2", 1));
    int label1 = ServiceVlanGetLabel("vrf2", "1.1.1.2");

    LOG(DEBUG, "Deleting links");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlanDel(1, "vrf2", "1.1.1.2", 1, label1));

    DeleteVmportEnv(input, 1, 1);
    DelNode("virtual-machine-interface-routing-intance", "ser1");
    DelVrf("vrf2");
}

TEST_F(ServiceVlanTest, FloatingIp_3) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    CreateVmportEnv(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortActive(input, 0));

    // Add virtual-machine-interface-routing-instance node
    AddVmPortVrf("ser1", "1.1.1.2", 1);
    client->WaitForIdle();

    // Add links // vmi-routing-instance node <-> vmi and 
    // Add link vmi-routing-instance <-> routing-instanace
    char buff[2048];
    int len = 0;
    AddXmlHdr(buff, len);
    AddLinkString(buff, len, "virtual-machine-interface-routing-instance",
                  "ser1", "virtual-machine-interface", "vnet1");
    AddNodeString(buff, len, "routing-instance", "vrf2", 2);
    AddLinkString(buff, len, "virtual-machine-interface-routing-instance", 
                  "ser1", "routing-instance", "vrf2");
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlan(1, "vrf2", "1.1.1.2", 1));
    int label1 = ServiceVlanGetLabel("vrf2", "1.1.1.2");

    LOG(DEBUG, "Deleting links");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "routing-instance", "vrf2");
    DelLink("virtual-machine-interface-routing-instance", "ser1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(ValidateServiceVlanDel(1, "vrf2", "1.1.1.2", 1, label1));

    DeleteVmportEnv(input, 1, 1);
    DelNode("virtual-machine-interface-routing-intance", "ser1");
    DelVrf("vrf2");
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    client->WaitForIdle();
    TestShutdown();
    delete client;
    return ret;
}
