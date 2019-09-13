/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
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

#include "test_cmn_util.h"
#include "test_flow_util.h"


class HbsFlowTest : public ::testing::Test {
public:
    virtual void SetUp() {
        client->WaitForIdle();
    }

    virtual void TearDown() {
    }
};

//Verify HBS flag is not set in fwd and reverse flows if host based services
//are not enabled

TEST_F(HbsFlowTest, basic_10) {
    struct PortInfo input[] = {
        {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };
    IpamInfo ipam_info[] = { {"1.1.1.0", 24, "1.1.1.10"}, };

    //Create VM Ports
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    //Add ACL policy on the VN
    AddAcl("test_acl", 1000, "vn1", "vn1", "pass");
    //Add VN
    AddLink("virtual-network", "vn1", "access-control-list", "test_acl");
    client->WaitForIdle();

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    const VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));
    const VmInterface *intf2 = static_cast<VmInterface *>(VmPortGet(2));


    //Define ICMP flow between two VM interfaces
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    //Verify the R flag in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);

    //Delete VM Ports
    DelLink("virtual-network", "vn1", "access-control-list", "test_acl");
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFindRetDel(1));
    EXPECT_FALSE(VrfFind("vrf1", true));
    client->WaitForIdle();
}
//Enable Host based services. Verify HBS Flag in the forward and reverse flow 
//between two Vms on same compute

TEST_F(HbsFlowTest, basic_11) {
    struct PortInfo input[] = {
        {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };
    IpamInfo ipam_info[] = { {"1.1.1.0", 24, "1.1.1.10"}, };

    //Create VM Ports
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    //Add HBS in the ACL policy on the VN
    HbsAcl("hbs_acl", 1000, "vn1", "vn1", "pass", "true");
    //Add VN
    AddLink("virtual-network", "vn1", "access-control-list", "hbs_acl");
    client->WaitForIdle();

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    const VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));
    const VmInterface *intf2 = static_cast<VmInterface *>(VmPortGet(2));


    //Define ICMP flow between two VM interfaces
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.2", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_LEFT);
    //Verify the R flag in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_RIGHT);

    //Disable HBS policy and check if the flow is reevaluated and HBS info
    //is reset in flow
    AddAcl("hbf_acl", 1000, "vn1", "vn1", "pass");
    client->WaitForIdle();

    //Verify if HBS flags are reset
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);

    //Delete VM Ports
    DelLink("virtual-network", "vn1", "access-control-list", "hbs_acl");
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFindRetDel(1));
    EXPECT_FALSE(VrfFind("vrf1", true));
    client->WaitForIdle();
}

//Enable Host based services. Verify HBS Flag in the forward and reverse flow 
//between two Vms on different compute

TEST_F(HbsFlowTest, basic_12) {
    struct PortInfo input[] = {
        {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1},
        {"intf2", 2, "1.1.1.2", "00:00:00:01:01:02", 1, 2}
    };
    IpamInfo ipam_info[] = { {"1.1.1.0", 24, "1.1.1.10"}, };

    //Create VM Ports
    CreateVmportEnv(input, 2);
    client->WaitForIdle();
    AddIPAM("vn1", ipam_info, 1);
    client->WaitForIdle();
    //Add HBS in the ACL policy on the VN
    HbsAcl("hbs_acl", 1000, "vn1", "vn1", "pass", "true");
    //Add VN
    AddLink("virtual-network", "vn1", "access-control-list", "hbs_acl");
    client->WaitForIdle();

    //Verfiy VM Ports are active
    EXPECT_TRUE(VmPortActive(1));
    EXPECT_TRUE(VmPortActive(2));
    const VmInterface *intf1 = static_cast<VmInterface *>(VmPortGet(1));
    const VmInterface *intf2 = static_cast<VmInterface *>(VmPortGet(2));

    //Create Remote VM routes
    char vm_ip[]  = "1.1.1.3";
    char server_ip[] = "10.1.1.3";
    boost::system::error_code ec;
    bgp_peer_ = CreateBgpPeer(Ip4Address::from_string("0.0.0.1", ec),
                              "xmpp channel");
    client->WaitForIdle();
    Inet4TunnelRouteAdd(bgp_peer_, "vrf1", vm_ip, 32, server_ip,
                        TunnelType::AllType(), 16, "vn1",
                        SecurityGroupList(), TagList(), PathPreference());
    client->WaitForIdle();
    //Define ICMP flow between two VM interfaces
    TestFlow flow[] = {
        {
            TestFlowPkt(Address::INET, "1.1.1.1", "1.1.1.3", 1, 0, 0, "vrf1",
                        intf1->id()),
            {
                new VerifyVn("vn1", "vn1"),
            }
        }
    };

    //Create the ICMP flow
    CreateFlow(flow, 1);

    //Verify the L flag in forward flow
    FlowEntry *fe = FlowGet(0, "1.1.1.1", "1.1.1.3", 1, 0, 0,
                            intf1->flow_key_nh()->id());
    EXPECT_TRUE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_LEFT);
    //Verify the R flag in reverse flow
    FlowEntry *rfe = fe->reverse_flow_entry();
    EXPECT_TRUE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_RIGHT);

    //Disable HBS policy and check if the flow is reevaluated and HBS info
    //is reset in flow
    AddAcl("hbf_acl", 1000, "vn1", "vn1", "pass");
    client->WaitForIdle();

    //Verify if HBS flags are reset
    EXPECT_FALSE(fe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(fe->GetHbsInterface() , FlowEntry::HBS_INTERFACE_INVALID);
    EXPECT_FALSE(rfe->is_flags_set(FlowEntry::HbfFlow));
    EXPECT_EQ(rfe->GetHbsInterface(), FlowEntry::HBS_INTERFACE_INVALID);

    //Delete VM Ports
    DelLink("virtual-network", "vn1", "access-control-list", "hbs_acl");
    DeleteVmportEnv(input, 2, true);
    client->WaitForIdle();
    DelIPAM("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFindRetDel(1));
    EXPECT_FALSE(VrfFind("vrf1", true));
    client->WaitForIdle();
    DeleteBgpPeer(bgp_peer_);
    client->WaitForIdle();

}

int main(int argc, char **argv) {
    GETUSERARGS();

    LoggingInit();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);

    client = TestInit(init_file, ksync_init, false, false, false);

    int ret = RUN_ALL_TESTS();

    usleep(10000);
    client->WaitForIdle();
    usleep(10000);
    TestShutdown();
    delete client;

    return ret;
}
