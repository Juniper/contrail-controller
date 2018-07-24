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
#include "oper/tag.h"
#include "filter/policy_set.h"

using namespace boost::assign;

void RouterIdDepInit(Agent *agent) {
}

struct PortInfo input[] = {
    {"intf1", 1, "1.1.1.1", "00:00:00:01:01:01", 1, 1, "fd10::2"},
    {"intf2", 2, "1.1.1.1", "00:00:00:01:01:01", 1, 2, "fd10::2"},
};
IpamInfo ipam_info[] = {
    {"1.1.1.0", 24, "1.1.1.10"},
};

class TagTest : public ::testing::Test {

    virtual void SetUp() {
        agent = Agent::GetInstance();
        CreateVmportEnv(input, 2);
        AddIPAM("vn1", ipam_info, 1);
        client->WaitForIdle();
        EXPECT_TRUE(VmPortActive(1));
        AddNode("firewall-policy", "fp1", 1);
        AddNode("firewall-policy", "fp2", 2);
        AddNode("project", "admin", 1);
        client->WaitForIdle();
    }

    virtual void TearDown() {
        DeleteVmportEnv(input, 2, true);
        client->WaitForIdle();
        DelIPAM("vn1");
        client->WaitForIdle();
        EXPECT_TRUE(agent->policy_set_table()->Size() == 0);
        DelNode("firewall-policy", "fp1");
        DelNode("firewall-policy", "fp2");
        DelNode("project", "admin");
        client->WaitForIdle();
    }
protected:

    bool VmiCheckTagValue(const std::string &type, uint32_t id) {
        const VmInterface* vm_intf =
            static_cast<const VmInterface *>(VmPortGet(1));

        VmInterface::TagEntrySet::const_iterator tag_it;
        for (tag_it = vm_intf->tag_list().list_.begin();
             tag_it != vm_intf->tag_list().list_.end(); tag_it++) {

            if (tag_it->type_ == TagEntry::GetTypeVal(type, "")) {
                if (id == tag_it->tag_->tag_id()) {
                    return true;
                }
            }
        }

        return false;
    }
    bool VmiCheckTagValue(int intf_id, const std::string &type, uint32_t id) {
        const VmInterface* vm_intf =
            static_cast<const VmInterface *>(VmPortGet(intf_id));

        VmInterface::TagEntrySet::const_iterator tag_it;
        for (tag_it = vm_intf->tag_list().list_.begin();
             tag_it != vm_intf->tag_list().list_.end(); tag_it++) {

            if (tag_it->type_ == TagEntry::GetTypeVal(type, "")) {
                if (id == tag_it->tag_->tag_id()) {
                    return true;
                }
            }
        }

        return false;
    }

    Agent *agent;
};

//Test creation and deletion of bridge-domain
TEST_F(TagTest, Test1) {
    AddTag("Tag1", 1, 1);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 1);

    DelNode("tag", "Tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, Test2) {
    AddTag("tag1", 1, 1);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 1);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list().size() == 1);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list().size() == 0);
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    client->WaitForIdle();
}

//Add VMI to tag link and verify that
//policy list gets populated
TEST_F(TagTest, Test3) {
    AddTag("tag1", 1, 1);
    AddLink("tag", "tag1", "application-policy-set", "aps1");
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    DelLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();

    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);;

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    client->WaitForIdle();
}

//Add VMI to tag link and verify that
//policy list gets populated
TEST_F(TagTest, Test4) {
    AddTag("tag1", 1, 1);
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    AddPolicySetFirewallPolicyLink("link2", "aps1", "fp2", "2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 2);
    const FirewallPolicyList &list = vmi->fw_policy_list();

    EXPECT_TRUE(list[0] == AclGet(1));
    EXPECT_TRUE(list[1] == AclGet(2));

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "3");
    client->WaitForIdle();
    EXPECT_TRUE(list[0] == AclGet(2));
    EXPECT_TRUE(list[1] == AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, GlobalPolicySet) {
    AddGlobalPolicySet("gps", 2);
    AddTag("tag1", 1, 1);
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    AddPolicySetFirewallPolicyLink("link2", "gps", "fp2", "2");
    client->WaitForIdle();

    EXPECT_TRUE(vmi->fw_policy_list().size() == 2);
    const FirewallPolicyList &list = vmi->fw_policy_list();

    EXPECT_TRUE(list[0] == AclGet(2));
    EXPECT_TRUE(list[1] == AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "gps", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelNode("application-policy-set", "gps");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "gps", "fp2");
    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, Inheritance) {
    AddTag("VmiTag", 1, 1, "application");
    AddTag("VmTag", 2, 2, "application");
    AddTag("VnTag", 3, 3, "application");
    AddTag("ProjectTag", 4, 4, "application");
    client->WaitForIdle();

    AddNode("project", "admin", 1);
    AddLink("virtual-machine-interface", "intf1", "project", "admin");
    AddLink("project", "admin", "tag", "ProjectTag");
    client->WaitForIdle();

    EXPECT_TRUE(VmiCheckTagValue("application", 4));

    AddLink("virtual-machine", "vm1", "tag", "VmTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 2));

    AddLink("virtual-network", "vn1", "tag", "VnTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 2));

    AddLink("virtual-machine-interface", "intf1", "tag", "VmiTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 1));
    EXPECT_FALSE(VmiCheckTagValue("application", 2));
    EXPECT_FALSE(VmiCheckTagValue("application", 4));

    DelLink("virtual-machine", "vm1", "tag", "VmTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 1));

    DelLink("virtual-machine-interface", "intf1", "tag", "VmiTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 3));
    EXPECT_FALSE(VmiCheckTagValue("application", 2));

    DelLink("virtual-network", "vn1", "tag", "VnTag");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 4));
    EXPECT_FALSE(VmiCheckTagValue("application", 3));

    DelLink("project", "admin", "tag", "ProjectTag");
    client->WaitForIdle();

    EXPECT_FALSE(VmiCheckTagValue("application", 4));
    client->WaitForIdle();

    DelNode("tag", "VmiTag");
    DelNode("tag", "VmTag");
    DelNode("tag", "VnTag");
    DelNode("tag", "ProjectTag");
    DelNode("project", "admin");
    DelLink("virtual-machine-interface", "intf1", "project", "admin");
    client->WaitForIdle();
}

TEST_F(TagTest, MultiTagInheritance) {
    AddTag("VmiTag1", 1, 1, "application");
    AddTag("VmiTag2", 2, 2, "tier");
    AddTag("VnTag1", 3, 3, "application");
    AddTag("VnTag2", 4, 4, "tier");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "VmiTag1");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 1));

    AddLink("virtual-network", "vn1", "tag", "VnTag1");
    AddLink("virtual-network", "vn1", "tag", "VnTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 1));
    EXPECT_TRUE(VmiCheckTagValue("tier", 4));

    AddLink("virtual-machine-interface", "intf1", "tag", "VmiTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("tier", 2));
    EXPECT_TRUE(VmiCheckTagValue("application", 1));

    DelLink("virtual-machine-interface", "intf1", "tag", "VmiTag1");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 3));
    EXPECT_TRUE(VmiCheckTagValue("tier", 2));

    DelLink("virtual-machine-interface", "intf1", "tag", "VmiTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("application", 3));
    EXPECT_TRUE(VmiCheckTagValue("tier", 4));

    DelLink("virtual-network", "vn1", "tag", "VnTag1");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue("application", 3));
    EXPECT_TRUE(VmiCheckTagValue("tier", 4));

    DelLink("virtual-network", "vn1", "tag", "VnTag2");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue("tier", 4));

    DelNode("tag", "VmiTag1");
    DelNode("tag", "VmiTag2");
    DelNode("tag", "VnTag1");
    DelNode("tag", "VnTag2");
    client->WaitForIdle();
}

//Verify label tag are derived on below order
//1> At VMI if present only label at VMI should be taken
//2> If label is not present at VMI, pick from VM
TEST_F(TagTest, label) {
    AddTag("VmiTag1", 1, 1, "label");
    AddTag("VmiTag2", 2, 2, "label");
    AddTag("VnTag1", 3, 3, "label");
    AddTag("VnTag2", 4, 4, "label");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "VmiTag1");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("label", 1));

    AddLink("virtual-network", "vn1", "tag", "VnTag1");
    AddLink("virtual-network", "vn1", "tag", "VnTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("label", 1));
    EXPECT_FALSE(VmiCheckTagValue("label", 4));

    AddLink("virtual-machine-interface", "intf1", "tag", "VmiTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("label", 2));
    EXPECT_TRUE(VmiCheckTagValue("label", 1));

    DelLink("virtual-machine-interface", "intf1", "tag", "VmiTag1");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue("label", 1));
    EXPECT_TRUE(VmiCheckTagValue("label", 2));
    EXPECT_FALSE(VmiCheckTagValue("label", 4));

    DelLink("virtual-machine-interface", "intf1", "tag", "VmiTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue("label", 3));
    EXPECT_TRUE(VmiCheckTagValue("label", 4));

    DelLink("virtual-network", "vn1", "tag", "VnTag1");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue("label", 3));
    EXPECT_TRUE(VmiCheckTagValue("label", 4));

    DelLink("virtual-network", "vn1", "tag", "VnTag2");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue("label", 4));

    DelNode("tag", "VmiTag1");
    DelNode("tag", "VmiTag2");
    DelNode("tag", "VnTag1");
    DelNode("tag", "VnTag2");
    client->WaitForIdle();
}

TEST_F(TagTest, VmapplicationPolicy) {
    AddTag("tag1", 1, 1);
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("virtual-machine", "vm1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    AddPolicySetFirewallPolicyLink("link2", "aps1", "fp2", "2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 2);
    const FirewallPolicyList &list = vmi->fw_policy_list();

    EXPECT_TRUE(list[0] == AclGet(1));
    EXPECT_TRUE(list[1] == AclGet(2));

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "3");
    client->WaitForIdle();
    EXPECT_TRUE(list[0] == AclGet(2));
    EXPECT_TRUE(list[1] == AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("virtual-machine", "vm1", "tag", "tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, VnapplicationPolicy) {
    AddTag("tag1", 1, 1);
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("virtual-network", "vn1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    AddPolicySetFirewallPolicyLink("link2", "aps1", "fp2", "2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 2);
    const FirewallPolicyList &list = vmi->fw_policy_list();

    EXPECT_TRUE(list[0] == AclGet(1));
    EXPECT_TRUE(list[1] == AclGet(2));

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "3");
    client->WaitForIdle();
    EXPECT_TRUE(list[0] == AclGet(2));
    EXPECT_TRUE(list[1] == AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("virtual-network", "vn1", "tag", "tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, ProjectapplicationPolicy) {
    AddLink("virtual-machine-interface", "intf1", "project", "admin");
    client->WaitForIdle();

    AddTag("tag1", 1, 1);
    AddNode("application-policy-set", "aps1", 1);
    client->WaitForIdle();

    AddLink("project", "admin", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 1);

    AddPolicySetFirewallPolicyLink("link2", "aps1", "fp2", "2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 2);
    const FirewallPolicyList &list = vmi->fw_policy_list();

    EXPECT_TRUE(list[0] == AclGet(1));
    EXPECT_TRUE(list[1] == AclGet(2));

    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "3");
    client->WaitForIdle();
    EXPECT_TRUE(list[0] == AclGet(2));
    EXPECT_TRUE(list[1] == AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("project", "admin", "tag", "tag1");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "intf1", "project", "admin");
    client->WaitForIdle();
}

//Attach multiple APS to same application tag
TEST_F(TagTest, MultiAps) {
    AddTag("tag1", 1, 1);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 1);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    AddLink("tag", "tag1", "application-policy-set", "aps2");

    AddNode("application-policy-set", "aps1", 1);
    AddNode("application-policy-set", "aps2", 2);
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list().size() == 2);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list().size() == 1);

    DelLink("tag", "tag1", "application-policy-set", "aps2");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelNode("application-policy-set", "aps2");
    client->WaitForIdle();
}

TEST_F(TagTest, MultiAps1) {
    AddTag("tag1", 1, 1);
    AddNode("policy-management", "gr1", 1);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 1);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    AddLink("tag", "tag1", "application-policy-set", "aps2");

    AddNode("application-policy-set", "aps1", 1);
    AddNode("application-policy-set", "aps2", 2);
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list().size() == 2);

    std::string second_aps = t->policy_set_list()[1]->name();
    AddLink("application-policy-set", second_aps.c_str(),
            "policy-management", "gr1");
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list()[0]->name() == second_aps);
    EXPECT_TRUE(t->policy_set_list()[1]->name() != second_aps);
    EXPECT_TRUE(t->policy_set_list().size() == 2);

    DelLink("application-policy-set", second_aps.c_str(),
            "policy-management", "gr1");
    client->WaitForIdle();

    EXPECT_TRUE(t->policy_set_list()[0]->name() != second_aps);
    EXPECT_TRUE(t->policy_set_list()[1]->name() == second_aps);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("tag", "tag1", "application-policy-set", "aps2");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelNode("application-policy-set", "aps2");
    DelNode("policy-management", "gr1");
    client->WaitForIdle();
}

TEST_F(TagTest, MultiAps2) {
    AddTag("tag1", 1, 1);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 1);

    AddLink("tag", "tag1", "application-policy-set", "aps1");
    AddLink("tag", "tag1", "application-policy-set", "aps2");

    AddNode("application-policy-set", "aps1", 1);
    AddPolicySetFirewallPolicyLink("link1", "aps1", "fp1", "1");
    AddPolicySetFirewallPolicyLink("link2", "aps1", "fp2", "2");
    AddNode("application-policy-set", "aps2", 2);
    AddPolicySetFirewallPolicyLink("link3", "aps2", "fp1", "1");
    AddPolicySetFirewallPolicyLink("link4", "aps2", "fp2", "2");
    client->WaitForIdle();

    AddLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();

    VmInterface *vmi = static_cast<VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vmi->fw_policy_list().size() == 4);

    DelPolicySetFirewallPolicyLink("link1", "aps2", "fp2");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 3);

    DelLink("virtual-machine-interface", "intf1", "tag", "tag1");
    client->WaitForIdle();
    EXPECT_TRUE(vmi->fw_policy_list().size() == 0);

    DelLink("tag", "tag1", "application-policy-set", "aps1");
    DelLink("tag", "tag1", "application-policy-set", "aps2");
    DelNode("tag", "tag1");
    DelNode("application-policy-set", "aps1");
    DelNode("application-policy-set", "aps2");
    DelPolicySetFirewallPolicyLink("link1", "aps1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "aps1", "fp2");
    DelPolicySetFirewallPolicyLink("link3", "aps2", "fp1");
    DelPolicySetFirewallPolicyLink("link4", "aps2", "fp2");
    client->WaitForIdle();
}

TEST_F(TagTest, CustomTag) {
    AddTag("tag1", 1, 0xF0001);
    client->WaitForIdle();

    TagKey key(MakeUuid(1));
    TagEntry* t =
        static_cast<TagEntry *>(agent->tag_table()->FindActiveEntry(&key));
    EXPECT_TRUE(t->tag_id() == 0xF0001);
    EXPECT_TRUE(t->tag_type() == 0xF);

    DelNode("tag", "tag1");
    client->WaitForIdle();
}

TEST_F(TagTest, VmiWithCustomTag) {
    AddTag("tag1", 1, 0xF0001);
    AddTag("tag2", 2, 0xF10001);
    client->WaitForIdle();

    const VmInterface* vm_intf =
        static_cast<const VmInterface *>(VmPortGet(1));
    EXPECT_TRUE(vm_intf->tag_list().list_.size() == 0);

    AddLink("virtual-machine-interface", "intf1", "tag", "tag1");
    AddLink("virtual-machine-interface", "intf1", "tag", "tag2");
    client->WaitForIdle();

    EXPECT_TRUE(vm_intf->tag_list().list_.size() == 2);

    DelLink("virtual-machine-interface", "intf1", "tag", "tag1");
    DelLink("virtual-machine-interface", "intf1", "tag", "tag2");
    client->WaitForIdle();

    DelNode("tag", "tag1");
    DelNode("tag", "tag2");
    client->WaitForIdle();
}
// verify that subinterface inherits VM tags from
// parent VMI
TEST_F(TagTest, InheritanceSubInterface) {
    AddTag("VmTag1", 1, 1, "application");
    AddTag("VmTag2", 2, 2, "application");
    client->WaitForIdle();

    AddLink("virtual-machine", "vm1", "tag", "VmTag1");
    client->WaitForIdle();

    //Sub interface has a link to VM
    //Ensure tag of directly linked VM gets picked
    AddVlan("intf2", 2, 100);
    AddLink("virtual-machine-interface", "intf1",
            "virtual-machine-interface", "intf2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue(1, "application", 1));

    AddLink("virtual-machine", "vm2", "tag", "VmTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue(2, "application", 2));

    //Delete directly linked VM, VM attribute of parent
    //should get picked
    DelLink("virtual-machine-interface", "intf2",
            "virtual-machine", "vm2");
    DelLink("virtual-machine", "vm2", "tag", "VmTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue(2, "application", 1));

    DelLink("virtual-machine", "vm1", "tag", "VmTag1");
    client->WaitForIdle();
    EXPECT_FALSE(VmiCheckTagValue(2, "application", 1));

    AddTag("VmTag2", 2, 2, "application");
    AddLink("virtual-machine", "vm1", "tag", "VmTag2");
    client->WaitForIdle();
    EXPECT_TRUE(VmiCheckTagValue(2, "application", 2));

    DelLink("virtual-machine", "vm1", "tag", "VmTag2");
    client->WaitForIdle();
    DelLink("virtual-machine-interface", "intf1",
            "virtual-machine-interface", "intf2");
    DelNode("tag", "VmTag1");
    DelNode("tag", "VmTag2");
    client->WaitForIdle();
}

int main (int argc, char **argv) {
    GETUSERARGS();
    client = TestInit(init_file, ksync_init);

    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
