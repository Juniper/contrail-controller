/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#if 0
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
#endif
#include "testing/gunit.h"

#include <base/logging.h>
#include <io/event_manager.h>
#include <tbb/task.h>
#include <base/task.h>

#include <cmn/agent_cmn.h>

#include "cfg/cfg_init.h"
#include "cfg/cfg_interface.h"
#include "oper/operdb_init.h"
#include "controller/controller_init.h"
#include "pkt/pkt_init.h"
#include "services/services_init.h"
#include "vrouter/ksync/ksync_init.h"
#include "oper/interface_common.h"
#include "oper/nexthop.h"
#include "route/route.h"
#include "oper/vrf.h"
#include "oper/mpls.h"
#include "oper/vm.h"
#include "oper/vn.h"
#include "filter/acl.h"
#include "test_cmn_util.h"
#include "vr_types.h"
#include <controller/controller_export.h>
#include <ksync/ksync_sock_user.h>
#include <boost/assign/list_of.hpp>
#include "uve/test/test_uve_util.h"

void RouterIdDepInit(Agent *agent) {
}

class PolicyTest : public ::testing::Test {
public:
    PolicyTest() : util_() {
        agent_ = Agent::GetInstance();
    }

    void SetPolicyDisabledStatus(struct PortInfo *input, bool status) {
        ostringstream str;

        str << "<virtual-machine-interface-disable-policy>";
        if (status) {
            str << "true";
        } else {
            str << "false";
        }
        str << "</virtual-machine-interface-disable-policy>";

        AddNode("virtual-machine-interface", input[0].name, input[0].intf_id,
                str.str().c_str());
    }

    TestUveUtil util_;
    Agent *agent_;
};

TEST_F(PolicyTest, IntfPolicyDisable_Vn) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:00:00:01", 1, 1},
    };

    CreateVmportEnv(input, 1, 1);
    client->WaitForIdle();
    EXPECT_TRUE(VmPortFind(1));

    //Verify that interface has policy enabled
    VmInterface *intf = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-enabled NH
    const NextHop *nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-enabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-enabled NH
    MacAddress mac(intf->vm_mac());
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Configure disable-policy as true on interface
    //---------------------------------------------
    SetPolicyDisabledStatus(input, true);
    client->WaitForIdle();

    //Verify that policy is disabled on interface
    EXPECT_FALSE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-disabled NH
    nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-disabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-disabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-disabled NH
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh still points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route still points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Configure disable-policy as false on interface
    //---------------------------------------------
    SetPolicyDisabledStatus(input, false);
    client->WaitForIdle();

    //Verify that policy is enabled
    EXPECT_TRUE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-enabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-enabled NH
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    DeleteVmportEnv(input, 1, true, 1);
    client->WaitForIdle();
    EXPECT_FALSE(VmPortFind(1));
    client->Reset();
}

TEST_F(PolicyTest, IntfPolicyDisable_Fip) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };

    //Add VN
    util_.VnAdd(input[0].vn_id);
    // Nova Port add message
    util_.NovaPortAdd(&input[0]);
    // Config Port add
    util_.ConfigPortAdd(&input[0]);
    //Verify that the port is inactive
    EXPECT_TRUE(VmPortInactive(input, 0));

    //Add necessary objects and links to make vm-intf active
    util_.VmAdd(input[0].vm_id);
    util_.VrfAdd(input[0].vn_id);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    AddVmPortVrf("vnet1", "", 0);
    client->WaitForIdle();
    AddInstanceIp("instance0", input[0].vm_id, input[0].addr);
    AddLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    client->WaitForIdle(3);
    AddLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    client->WaitForIdle(3);
    EXPECT_TRUE(VmPortActive(input, 0));
    const VmInterface *intf = VmInterfaceGet(input[0].intf_id);
    EXPECT_TRUE(intf != NULL);
    EXPECT_FALSE(intf->policy_enabled());

    //Create a VN for floating-ip
    client->Reset();
    AddVn("default-project:vn2", 2);
    client->WaitForIdle();
    WAIT_FOR(1000, 100, (client->vn_notify_ >= 1));
    AddVrf("default-project:vn2:vn2");
    client->WaitForIdle();
    EXPECT_TRUE(client->VrfNotifyWait(1));
    EXPECT_TRUE(VrfFind("default-project:vn2:vn2"));
    AddLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle();

    // Configure Floating-IP
    AddFloatingIpPool("fip-pool1", 1);
    AddFloatingIp("fip1", 1, "71.1.1.100");
    AddLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    AddLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    client->WaitForIdle();
    AddLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    client->WaitForIdle();
    WAIT_FOR(1000, 500, ((VmPortFloatingIpCount(1, 1) == true)));
    EXPECT_TRUE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-enabled NH
    const NextHop *nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-enabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-enabled NH
    MacAddress mac(intf->vm_mac());
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Configure disable-policy as true on interface
    //---------------------------------------------
    SetPolicyDisabledStatus(input, true);
    client->WaitForIdle();

    //Verify that policy is disabled on interface
    EXPECT_FALSE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-disabled NH
    nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-disabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-disabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-disabled NH
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_FALSE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh still points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route still points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Configure disable-policy as false on interface
    //---------------------------------------------
    SetPolicyDisabledStatus(input, false);
    client->WaitForIdle();

    //Verify that policy is enabled
    EXPECT_TRUE(intf->policy_enabled());

    //Verify that interface's MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 MPLS label points to policy-enabled NH
    nh = MplsToNextHop(intf->l2_label());
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's route points to policy-enabled NH
    nh = RouteToNextHop("vrf1", intf->primary_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's L2 route points to policy-enabled NH
    nh = L2RouteToNextHop("vrf1", mac);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's flow_key_nh points to policy-enabled NH
    nh = intf->flow_key_nh();
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Verify that interface's mdata route points to policy-enabled NH
    nh = RouteToNextHop(agent_->fabric_vrf_name(), intf->mdata_ip_addr(), 32);
    EXPECT_TRUE(nh != NULL);
    EXPECT_TRUE(nh->PolicyEnabled());

    //Delete the floating-IP
    DelLink("floating-ip", "fip1", "floating-ip-pool", "fip-pool1");
    DelLink("virtual-machine-interface", "vnet1", "floating-ip", "fip1");
    DelFloatingIp("fip1");
    client->WaitForIdle();

    //cleanup
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network",
            "default-project:vn2");
    DelLink("floating-ip-pool", "fip-pool1", "virtual-network", "vn1");
    DelFloatingIpPool("fip-pool1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "routing-instance", "vrf1");
    DelLink("virtual-machine-interface-routing-instance", "vnet1",
            "virtual-machine-interface", "vnet1");
    DelLink("virtual-machine", "vm1", "virtual-machine-interface", "vnet1");
    DelLink("virtual-network", "vn1", "virtual-machine-interface", "vnet1");
    client->WaitForIdle();
    DelLink("virtual-network", "default-project:vn2", "routing-instance",
            "default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelLink("virtual-machine-interface", input[0].name,
            "instance-ip", "instance0");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle(3);

    DelNode("virtual-network", "default-project:vn2");
    DelVrf("default-project:vn2:vn2");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface-routing-instance", "vnet1");
    client->WaitForIdle(3);
    DelNode("virtual-machine", "vm1");
    DelNode("routing-instance", "vrf1");
    client->WaitForIdle(3);
    DelNode("virtual-machine-interface", "vnet1");
    DelInstanceIp("instance0");
    client->WaitForIdle(3);
    IntfCfgDel(input, 0);
    util_.VnDelete(input[0].vn_id);
    client->WaitForIdle(3);
    WAIT_FOR(1000, 500, (VnGet(input[0].vn_id) == NULL));

    //clear counters at the end of test case
    client->Reset();
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
