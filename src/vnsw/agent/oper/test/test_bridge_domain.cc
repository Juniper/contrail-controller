/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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
#include "oper/bridge_domain.h"

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

class BridgeDomainTest : public ::testing::Test {

    virtual void SetUp() {
        agent = Agent::GetInstance();
    }

    virtual void TearDown() {
        client->WaitForIdle();
        EXPECT_TRUE(agent->bridge_domain_table()->Size() == 0);
    }

protected:
    Agent *agent;
};

TEST_F(BridgeDomainTest, Test1) {
    AddBridgeDomain("bridge1", 1, 1);
    client->WaitForIdle();
    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();
    EXPECT_TRUE(agent->bridge_domain_table()->Size() == 0);
}

TEST_F(BridgeDomainTest, Test2) {
    AddBridgeDomain("bridge1", 1, 1);
    client->WaitForIdle();

    AddVn("vn1", 1, true);
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    BridgeDomainEntry *bd = BridgeDomainGet(1);
    EXPECT_TRUE(bd->vrf() == NULL);

    AddVrf("vrf1", 1);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    EXPECT_TRUE(bd->vrf() != NULL);
    EXPECT_TRUE(bd->vrf()->GetName() == "vrf1:1");
    EXPECT_TRUE(bd->vrf()->IsPbbVrf() == true);
    EXPECT_TRUE(bd->vrf()->bmac_vrf_name() == "vrf1");
    EXPECT_TRUE(bd->vrf()->table_label() != MplsTable::kInvalidLabel);

    DelNode("bridge-domain", "bridge1");
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelLink("virtual-network", "vn1", "routing-instance", "vrf1");
    DelVn("vn1");
    DelVrf("vrf1");
    client->WaitForIdle();

    bd = BridgeDomainGet(1);
    EXPECT_TRUE(bd == NULL);
    EXPECT_FALSE(VrfFind("vrf1:1", true));
    EXPECT_FALSE(VrfFind("vrf1", true));
}

TEST_F(BridgeDomainTest, Test3) {
    AddBridgeDomain("bridge1", 1, 1, false);
    client->WaitForIdle();

    BridgeDomainEntry *bd = BridgeDomainGet(1);
    EXPECT_TRUE(bd->learning_enabled() == false);
    client->WaitForIdle();

    AddBridgeDomain("bridge1", 1, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(bd->learning_enabled() == true);

    AddBridgeDomain("bridge1", 1, 1, false);
    client->WaitForIdle();
    EXPECT_TRUE(bd->learning_enabled() == false);

    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();
}

TEST_F(BridgeDomainTest, Test4) {
    AddBridgeDomain("bridge1", 1, 1, false);
    AddVn("vn1", 1, true);
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    AddVrf("vrf1", 1);
    AddLink("virtual-network", "vn1", "routing-instance", "vrf1");
    client->WaitForIdle();

    BridgeDomainEntry *bd = BridgeDomainGet(1);
    EXPECT_TRUE(bd->vrf() != NULL);

    MplsLabel *mpls =
        agent->mpls_table()->FindMplsLabel(bd->vrf()->table_label());
    EXPECT_TRUE(mpls->nexthop()->GetType() == NextHop::VRF);
    EXPECT_TRUE(mpls->nexthop()->learning_enabled() == false);

    AddBridgeDomain("bridge1", 1, 1, true);
    client->WaitForIdle();
    EXPECT_TRUE(mpls->nexthop()->learning_enabled() == true);

    AddBridgeDomain("bridge1", 1, 1, false);
    client->WaitForIdle();
    EXPECT_TRUE(mpls->nexthop()->learning_enabled() == false);

    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();
}

TEST_F(BridgeDomainTest, Test5) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    MacAddress smac(0x00, 0x00, 0x00, 0x01, 0x01, 0x01);
    AgentRoute *rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt != NULL);

    AddBridgeDomain("bridge1", 1, 1, false);
    AddLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    client->WaitForIdle();

    AddVmportBridgeDomain(input[0].name, 0);
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1",
            "virtual-machine-interface-bridge-domain");
    AddLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name,
            "virtual-machine-interface-bridge-domain");
    client->WaitForIdle();

    BridgeDomainEntry *bd = BridgeDomainGet(1);
    rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt->GetActiveLabel() == bd->vrf()->table_label());

    const VmInterface *vm_intf = static_cast<const VmInterface *>(
            VmPortGet(1));
    EXPECT_TRUE(vm_intf->pbb_interface());
    const VrfEntry *vrf = VrfGet("vrf1:1");
    EXPECT_TRUE(vm_intf->GetPbbVrf() == vrf->vrf_id());
    EXPECT_TRUE(vm_intf->GetIsid() == 1);
    EXPECT_TRUE(vm_intf->flow_key_nh()->learning_enabled() == false);

    AddBridgeDomain("bridge1", 1, 1, true);
    client->WaitForIdle();
    rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(vm_intf->flow_key_nh()->learning_enabled() == true);
    EXPECT_TRUE(rt->GetActiveLabel() == bd->vrf()->table_label());

    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "bridge-domain", "bridge1");
    DelLink("virtual-machine-interface-bridge-domain", input[0].name,
            "virtual-machine-interface", input[0].name);
    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelNode("virtual-machine-interface-bridge-domain", input[0].name);
    client->WaitForIdle();

    EXPECT_TRUE(rt->GetActiveLabel() == vm_intf->l2_label());
    EXPECT_TRUE(vm_intf->flow_key_nh()->learning_enabled() == false);

    DelLink("virtual-network", "vn1", "bridge-domain", "bridge1");
    DelNode("bridge-domain", "bridge1");
    client->WaitForIdle();

    DeleteVmportEnv(input, 1, true);
    client->WaitForIdle();

    EXPECT_FALSE(VrfFind("vrf1:1", true));
}

TEST_F(BridgeDomainTest, Test6) {
    struct PortInfo input[] = {
        {"vnet1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1}
    };
    CreateVmportEnv(input, 1);
    client->WaitForIdle();

    const VmInterface *vm_intf = static_cast<const VmInterface *>(
            VmPortGet(1));

    MacAddress smac(0x00, 0x00, 0x00, 0x01, 0x01, 0x01);
    AgentRoute *rt = EvpnRouteGet("vrf1", smac, Ip4Address(0), 0);
    EXPECT_TRUE(rt != NULL);

    std::stringstream str;
    str << "<pbb-etree-enable>"<< "false" << "</pbb-etree-enable>";
    AddNode("virtual-network", "vn1", 1,  str.str().c_str());
    client->WaitForIdle();

    EXPECT_TRUE(vm_intf->l2_interface_nh_no_policy()->etree_leaf() == false);
    EXPECT_TRUE(vm_intf->l2_interface_nh_policy()->etree_leaf() == false);
    EXPECT_TRUE(rt->GetActiveNextHop()->etree_leaf() == false);

    str.clear();
    str << "<pbb-etree-enable>"<< "true" << "</pbb-etree-enable>";
    AddNode("virtual-network", "vn1", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->l2_interface_nh_no_policy()->etree_leaf() == true);
    EXPECT_TRUE(vm_intf->l2_interface_nh_policy()->etree_leaf() == true);
    EXPECT_TRUE(rt->GetActiveNextHop()->etree_leaf() == true);

    str.clear();
    str << "<pbb-etree-enable>"<< "false" << "</pbb-etree-enable>";
    AddNode("virtual-network", "vn1", 1, str.str().c_str());
    client->WaitForIdle();
    EXPECT_TRUE(vm_intf->l2_interface_nh_no_policy()->etree_leaf() == false);
    EXPECT_TRUE(vm_intf->l2_interface_nh_policy()->etree_leaf() == false);
    EXPECT_TRUE(rt->GetActiveNextHop()->etree_leaf() == false);

    DeleteVmportEnv(input, 1, true);
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
