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
#include "pkt/test/test_flow_util.h"
#include "uve/test/test_uve_util.h"

#define vm1_ip "11.1.1.1"
#define vm2_ip "11.1.1.2"

void RouterIdDepInit(Agent *agent) {
}

typedef enum {
    INGRESS = 0,
    EGRESS = 1,
    BIDIRECTION = 2
} AclDirection;

static string AddAclXmlString(const char *node_name, const char *name, int id,
                              int proto, const char *action) {
    char buff[10240];
    sprintf(buff,
    "<?xml version=\"1.0\"?>\n"
    "<config>\n"
    "   <update>\n"
    "       <node type=\"%s\">\n"
    "           <name>%s</name>\n"
    "           <id-perms>\n"
    "               <permissions>\n"
    "                   <owner></owner>\n"
    "                   <owner_access>0</owner_access>\n"
    "                   <group></group>\n"
    "                   <group_access>0</group_access>\n"
    "                   <other_access>0</other_access>\n"
    "               </permissions>\n"
    "               <uuid>\n"
    "                   <uuid-mslong>0</uuid-mslong>\n"
    "                   <uuid-lslong>%d</uuid-lslong>\n"
    "               </uuid>\n"
    "           </id-perms>\n"
    "           <access-control-list-entries>\n"
    "                <acl-rule>\n"
    "                    <match-condition>\n"
    "                        <src-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </src-address>\n"
    "                        <protocol>%d</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 10000 </end-port>\n"
    "                        </dst-port>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            %s\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "                <acl-rule>\n"
    "                    <match-condition>\n"
    "                        <src-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </src-address>\n"
    "                        <protocol>any</protocol>\n"
    "                        <src-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 60000 </end-port>\n"
    "                        </src-port>\n"
    "                        <dst-address>\n"
    "                            <virtual-network> any </virtual-network>\n"
    "                        </dst-address>\n"
    "                        <dst-port>\n"
    "                            <start-port> 0 </start-port>\n"
    "                            <end-port> 60000 </end-port>\n"
    "                        </dst-port>\n"
    "                    </match-condition>\n"
    "                    <action-list>\n"
    "                        <simple-action>\n"
    "                            deny\n"
    "                        </simple-action>\n"
    "                    </action-list>\n"
    "                </acl-rule>\n"
    "           </access-control-list-entries>\n"
    "       </node>\n"
    "   </update>\n"
    "</config>\n", node_name, name, id, proto, action);
    string s(buff);
    return s;
}

static void AddAclEntry(const char *name, int id, int proto,
                        const char *action, AclDirection direction) {
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    if (direction == EGRESS) {
        strncat(acl_name, "egress-access-control-list", max_len);
    } else {
        strncat(acl_name, "ingress-access-control-list", max_len);
    }
    std::string s = AddAclXmlString("access-control-list", acl_name, id, proto,
                                    action);
    pugi::xml_document xdoc_;
    pugi::xml_parse_result result = xdoc_.load(s.c_str());
    EXPECT_TRUE(result);
    Agent::GetInstance()->ifmap_parser()->ConfigParse(xdoc_.first_child(), 0);
    client->WaitForIdle();
}

static void AddSgEntry(const char *sg_name, const char *name, int id,
                       int proto, const char *action, AclDirection direction) {

    AddSg(sg_name, 1);
    char acl_name[1024];
    uint16_t max_len = sizeof(acl_name) - 1;
    strncpy(acl_name, name, max_len);
    switch (direction) {
        case INGRESS:
            AddAclEntry(name, id, proto, action, direction);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case EGRESS:
            AddAclEntry(name, id, proto, action, direction);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
        case BIDIRECTION:
            AddAclEntry(name, id, proto, action, EGRESS);
            strncat(acl_name, "egress-access-control-list", max_len);
            AddLink("security-group", sg_name, "access-control-list", acl_name);

            strncpy(acl_name, name, max_len);
            strncat(acl_name, "ingress-access-control-list", max_len);
            AddAclEntry(name, id+1, proto, action, INGRESS);
            AddLink("security-group", sg_name, "access-control-list", acl_name);
            break;
    }
}

class PolicyTest : public ::testing::Test {
public:
    PolicyTest() : util_() {
        agent_ = Agent::GetInstance();
        flow_proto_ = agent_->pkt()->get_flow_proto();
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
    FlowProto *flow_proto_;
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

TEST_F(PolicyTest, IntfPolicyDisable_Flow) {
    struct PortInfo input[] = {
        {"flow0", 6, vm1_ip, "00:00:00:01:01:01", 5, 1},
        {"flow1", 7, vm2_ip, "00:00:00:01:01:02", 5, 2},
    };

    VmInterface *flow0, *flow1;

    CreateVmportEnv(input, 2, 1);
    client->WaitForIdle(5);

    EXPECT_TRUE(VmPortActive(input, 0));
    EXPECT_TRUE(VmPortActive(input, 1));
    WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 0)));
    WAIT_FOR(100, 1000, (VmPortPolicyEnable(input, 1)));

    flow0 = VmInterfaceGet(input[0].intf_id);
    assert(flow0);
    flow1 = VmInterfaceGet(input[1].intf_id);
    assert(flow1);

    AddSgEntry("sg1", "sg_acl1", 10, 1, "pass", BIDIRECTION);
    AddLink("virtual-machine-interface", "flow1", "security-group", "sg1");
    client->WaitForIdle();
    EXPECT_EQ(flow1->sg_list().list_.size(), 1U);

    TestFlow flow[] = {
        //Add a ICMP forward and reverse flow
        {  TestFlowPkt(Address::INET, vm1_ip, vm2_ip, 1, 0, 0, "vrf5",
                       flow0->id()),
        {
            new VerifyVn("vn5", "vn5"),
            new VerifyVrf("vrf5", "vrf5")
        }
        }
    };

    CreateFlow(flow, 1);
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Since policy is enabled on reverse flow's VMI, verify that out SG rules
    //are present
    const FlowEntry *fe = flow[0].pkt_.FlowFetch();
    EXPECT_TRUE(fe->data().match_p.out_sg_rule_present);

    //Delete all the flows
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    EXPECT_EQ(0U, flow_proto_->FlowCount());

    //Disable policy on flow1 VMI
    SetPolicyDisabledStatus(&input[1], true);
    client->WaitForIdle();

    //Verify that policy status of interfaces
    WAIT_FOR(100, 1000, ((VmPortPolicyEnabled(input, 0)) == true));
    WAIT_FOR(100, 1000, ((VmPortPolicyEnabled(input, 1)) == false));

    //Setup flows again
    CreateFlow(flow, 1);
    EXPECT_EQ(2U, flow_proto_->FlowCount());

    //Since policy is disabled on reverse flow's VMI, verify that out SG rules
    //are not present
    fe = flow[0].pkt_.FlowFetch();
    EXPECT_FALSE(fe->data().match_p.out_sg_rule_present);

    //Cleanup
    client->EnqueueFlowFlush();
    client->WaitForIdle(10);
    client->Reset();
    DeleteVmportEnv(input, 2, true, 1);
    client->WaitForIdle(3);
    WAIT_FOR(1000, 1000, (VmPortFind(input, 0) == false));
    WAIT_FOR(1000, 1000, (VmPortFind(input, 1) == false));
    EXPECT_EQ(0U, flow_proto_->FlowCount());
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
