/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <cmn/agent_cmn.h>
#include "vr_types.h"

#include "testing/gunit.h"
#include "test_cmn_util.h"

using namespace std;

void RouterIdDepInit(Agent *agent) {
}

class AliasIPCfg : public ::testing::Test {
    virtual void SetUp() {
        client->WaitForIdle();
        AddVm("vm1", 1);
        AddVn("vn1", 1);
        AddVn("default-project:vn2", 2);
        AddVrf("vrf1");
        AddVrf("default-project:vn2:vn2");
        AddPort("vnet1", 1);
        AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
        AddLink("virtual-network", "default-project:vn2",
                "routing-instance", "default-project:vn2:vn2");
        AddAliasIpPool("aip-pool2", 2);
        AddAliasIp("aip1", 2, "2.2.2.100");
        AddLink("alias-ip-pool", "aip-pool2", "virtual-network",
                "default-project:vn2");
        AddLink("alias-ip", "aip1", "alias-ip-pool", "aip-pool2");

        // Add vm-port interface to vrf link
        AddVmPortVrf("vmvrf1", "", 0);
        AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "routing-instance", "vrf1");
        AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
        client->WaitForIdle();

        client->WaitForIdle();
        EXPECT_TRUE(VmFind(1));
        EXPECT_TRUE(VnFind(1));
        EXPECT_TRUE(VnFind(2));
        EXPECT_TRUE(VrfFind("vrf1"));
        EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
        DelLink("virtual-network", "default-project:vn2", "routing-instance",
                "default-project:vn2:vn2");
        DelLink("alias-ip-pool", "aip-pool2", "virtual-network",
                "default-project:vn2");
        DelLink("alias-ip", "aip1", "alias-ip-pool", "aip-pool2");
        client->WaitForIdle();

        DelAliasIp("aip1");
        DelAliasIpPool("aip-pool2");
        client->WaitForIdle();

        // Delete virtual-machine-interface to vrf link attribute
        DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "routing-instance", "vrf1");
        DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
                "virtual-machine-interface", "vnet1");
        DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
        client->WaitForIdle();

        DelPort("vnet1");
        DelVn("vn1");
        DelVn("default-project:vn2");
        DelVrf("vrf1");
        DelVrf("default-project:vn2:vn2");
        DelVm("vm1");
        client->WaitForIdle();

        EXPECT_FALSE(VrfFind("vrf1"));
        EXPECT_FALSE(VrfFind("default-project:vn2:vn2"));
        EXPECT_FALSE(VnFind(1));
        EXPECT_FALSE(VnFind(2));
        EXPECT_FALSE(VmFind(1));
    }
};

class CfgTest : public ::testing::Test {
};

TEST_F(CfgTest, AliasIp_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"vnet2", 2, "2.2.2.2", "00:00:00:01:01:01", 2, 2},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"}
    };

    IpamInfo ipam_info2[] = {
        {"2.2.2.0", 24, "2.2.2.10"}
    };

    client->WaitForIdle();
    client->Reset();
    AddVm("vm1", 1);
    AddVm("vm2", 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VmNotifyWait(2));
    EXPECT_TRUE(VmFind(1));
    EXPECT_TRUE(VmFind(2));

    client->Reset();
    AddVrf("vrf1");
    AddVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(2));

    AddVn("vn1", 1);
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    EXPECT_TRUE(client->VnNotifyWait(2));
    EXPECT_EQ(2U, Agent::GetInstance()->vn_table()->Size());

    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");

    client->Reset();
    IntfCfgAdd(input, 0);
    IntfCfgAdd(input, 1);
    client->WaitForIdle();
    EXPECT_TRUE(client->PortNotifyWait(2));
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    AddIPAM("default-project:vn2", ipam_info2, 1);
    client->WaitForIdle();

    AddPort(input[0].name, input[0].intf_id);
    AddPort(input[1].name, input[1].intf_id);
    client->WaitForIdle();

    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
        "virtual-machine-interface", "vnet1");

    AddVmPortVrf("vmvrf2", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "default-project:vn2:vn2");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf2",
        "virtual-machine-interface", "vnet2");
    client->WaitForIdle();
    // Create alias-ip on default-project:vn2
    client->Reset();
    AddAliasIpPool("aip-pool1", 1);
    AddAliasIpPool("aip-pool2", 2);

    AddAliasIp("aip2", 2, "2.2.2.100");

    AddLink("alias-ip-pool", "aip-pool2", "virtual-network",
            "default-project:vn2");

    AddLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");

    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    client->Reset();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");

    AddLink("virtual-network", "default-project:vn2", "virtual-machine-interface",
            "vnet2");
    AddLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");

    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddInstanceIp("instance1", input[0].vm_id, input[1].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    AddLink("virtual-machine-interface", input[1].name,
            "instance-ip", "instance1");

    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.2"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.100"), 32));

    PhysicalInterface::CreateReq(Agent::GetInstance()->interface_table(),
                            "enet1", Agent::GetInstance()->fabric_vrf_name(),
                            PhysicalInterface::FABRIC,
                            PhysicalInterface::ETHERNET, false, nil_uuid(),
                            Ip4Address(0), Interface::TRANSPORT_ETHERNET);
    client->WaitForIdle();

    // Delete links
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");

    DelLink("virtual-network", "default-project:vn2", "virtual-machine-interface",
            "vnet2");
    DelLink("virtual-machine", "vm2", "virtual-machine-interface", "vnet2");

    DelLink("virtual-machine-interface", input[0].name, "instance-ip", 
            "instance0");
    DelLink("virtual-machine-interface", input[1].name, "instance-ip", 
            "instance1");

    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");

    DelLink("alias-ip-pool", "aip-pool2", "virtual-network",
            "default-project:vn2");
    DelLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");

    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");

    PhysicalInterface::DeleteReq(Agent::GetInstance()->interface_table(), "enet1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortInactive(input, 1));
    EXPECT_TRUE(VmPortAliasIpCount(1, 0));
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2",
                           Ip4Address::from_string("2.2.2.100"), 32));

    DelAliasIp("aip2");
    DelAliasIpPool("aip-pool1");
    DelAliasIpPool("aip-pool2");

    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "routing-instance", "default-project:vn2:vn2");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf2",
            "virtual-machine-interface", "vnet2");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf2");
    client->WaitForIdle();

    IntfCfgDel(input, 0);
    IntfCfgDel(input, 1);

    DelIPAM("vn1");
    client->WaitForIdle();
    DelIPAM("default-project:vn2");
    client->WaitForIdle();

    DelVn("vn1");
    DelVn("default-project:vn2");

    DelVrf("vrf1");
    DelVrf("default-project:vn2:vn2");

    DelVm("vm1");
    DelVm("vm2");

    DelPort(input[0].name);
    DelPort(input[1].name);

    client->WaitForIdle();
    EXPECT_FALSE(VrfFind("vrf1"));
    EXPECT_FALSE(VrfFind("default-project:vn2:vn2"));
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(VnFind(2));
    EXPECT_FALSE(VmFind(1));
}

// Create Nova intf first
// Intf config without FIP
// Add FIP later
TEST_F(AliasIPCfg, AliasIp_CfgOrder_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    IntfCfgAdd(input, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    AddPort(input[0].name, input[0].intf_id);
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.100"), 32));

    // Cleanup
    LOG(DEBUG, "Doing cleanup");

    // Delete links
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortInactive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 0));
    client->WaitForIdle();
    IntfCfgDel(input, 0);
    DelIPAM("vn1");
    client->WaitForIdle();
}

// Intf config with FIP
// Create Nova intf first
TEST_F(AliasIPCfg, AliasIp_CfgOrder_2) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    AddPort(input[0].name, input[0].intf_id);
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2", Ip4Address::from_string("2.2.2.100"), 32));

    // Cleanup
    LOG(DEBUG, "Doing cleanup");

    IntfCfgDel(input, 0);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(input, 0));
    client->WaitForIdle();

    // Delete links
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
}

TEST_F(CfgTest, AliasIp_CfgOrder_3) {
    client->WaitForIdle();
    client->Reset();

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    AddAliasIpPool("aip-pool2", 1);
    AddPort(input[0].name, input[0].intf_id);
    AddAliasIp("aip2", 2, "2.2.2.100");
    AddLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");
    AddVm("vm1", 1);
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddVn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");
    AddVrf("default-project:vn2:vn2");
    AddVn("default-project:vn2", 2);
    AddLink("alias-ip-pool", "aip-pool2", "virtual-network", "default-project:vn2");
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    IntfCfgAdd(input, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
        "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.100"), 32));
    client->WaitForIdle();

    // Cleanup
    LOG(DEBUG, "Doing cleanup");
    DelLink("alias-ip-pool", "aip-pool2", "virtual-network", "default-project:vn2");
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    DelAliasIpPool("aip-pool2");
    DelAliasIp("aip2");
    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    client->WaitForIdle();
    DelVm("vm1");
    DelVn("vn1");
    DelVrf("vrf1");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

TEST_F(CfgTest, AliasIp_CfgOrder_4) {
    client->WaitForIdle();
    client->Reset();

    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    IntfCfgAdd(input, 0);
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    AddAliasIpPool("aip-pool2", 1);
    AddPort(input[0].name, input[0].intf_id);
    AddAliasIp("aip2", 2, "2.2.2.100");
    AddLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");
    AddVm("vm1", 1);
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddVn("vn1", 1);
    AddVrf("vrf1");
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");
    AddVrf("default-project:vn2:vn2");
    AddVn("default-project:vn2", 2);
    // Add vm-port interface to vrf link
    AddVmPortVrf("vmvrf1", "", 0);
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    AddLink("virtual-machine-interface-routing-instance", "vmvrf1",
        "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    AddLink("alias-ip-pool", "aip-pool2", "virtual-network", "default-project:vn2");
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    int i = 0;
    while (i++ < 100) {
        if (VmPortActive(input, 0) == true) {
            break;
        }
        usleep(1000);
    }
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.100"), 32));
    client->WaitForIdle();

    // Cleanup
    LOG(DEBUG, "Doing cleanup");
    // Delete virtual-machine-interface to vrf link attribute
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vmvrf1",
            "virtual-machine-interface", "vnet1");
    DelNode("virtual-machine-interface-routing-instance", "vmvrf1");
    client->WaitForIdle();
    DelLink("alias-ip-pool", "aip-pool2", "virtual-network", "default-project:vn2");
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("alias-ip", "aip2", "alias-ip-pool", "aip-pool2");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip2");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    DelAliasIpPool("aip-pool2");
    DelAliasIp("aip2");
    DelVm("vm1");
    DelVn("vn1");
    DelVrf("vrf1");
    DelVrf("default-project:vn2:vn2");
    DelVn("default-project:vn2");
    IntfCfgDel(input, 0);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
}

// Add of new FIP
TEST_F(AliasIPCfg, AliasIp_Add_1) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
    };

    IpamInfo ipam_info[] = {
        {"1.1.1.0", 24, "1.1.1.10"},
    };

    AddPort(input[0].name, input[0].intf_id);
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    client->WaitForIdle();

    IntfCfgAdd(input, 0);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();

    // Port Active since VRF and VM already added
    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortAliasIpCount(1, 1));
    EXPECT_TRUE(RouteFind("vrf1", Ip4Address::from_string("1.1.1.1"), 32));
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("2.2.2.100"), 32));

    // Add one more FIP
    AddAliasIp("aip1_1", 3, "3.3.3.100");
    AddLink("alias-ip", "aip1_1", "alias-ip-pool", "aip-pool2");
    AddLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1_1");
    client->WaitForIdle();
    EXPECT_TRUE(RouteFind("default-project:vn2:vn2",
                          Ip4Address::from_string("3.3.3.100"), 32));

    // Cleanup
    LOG(DEBUG, "Doing cleanup");

    IntfCfgDel(input, 0);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();

    EXPECT_FALSE(VmPortFind(input, 0));
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2",
                           Ip4Address::from_string("3.3.3.100"), 32));
    EXPECT_FALSE(RouteFind("default-project:vn2:vn2",
                           Ip4Address::from_string("2.2.2.100"), 32));
    client->WaitForIdle();

    // Delete links
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1");
    DelLink("alias-ip-pool", "aip-pool2", "virtual-network",
            "default-project:vn2");
    DelLink("alias-ip", "aip1_1", "alias-ip-pool", "aip-pool2");
    DelLink("virtual-machine-interface", "vnet1", "alias-ip", "aip1_1");
    DelLink("instance-ip", "instance0", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();

    DelAliasIp("aip1_1");
    client->WaitForIdle();
}

int main(int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
