/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <test_cmn_util.h>

using namespace std;
void RouterIdDepInit(Agent *agent) {
}

class PolicySetTest : public ::testing::Test {
};


TEST_F(PolicySetTest, Test1) {
    AddNode("application-policy-set", "app1", 1);
    client->WaitForIdle();

    EXPECT_TRUE(PolicySetFind(1));

    DelNode("application-policy-set", "app1");
    client->WaitForIdle();

    EXPECT_FALSE(PolicySetFind(1));
}

TEST_F(PolicySetTest, Test2) {
    AddNode("application-policy-set", "app1", 1);
    client->WaitForIdle();
    EXPECT_TRUE(PolicySetFind(1));

    AddPolicySetFirewallPolicyLink("link1", "app1", "fp1", "1");
    client->WaitForIdle();
    AddNode("firewall-policy", "fp1", 1);
    client->WaitForIdle();

    PolicySet *ps = PolicySetGet(1);
    EXPECT_EQ(ps->GetAcl(0), AclGet(1));

    AddNode("firewall-policy", "fp2", 2);
    AddPolicySetFirewallPolicyLink("link2", "app1", "fp2", "2");
    client->WaitForIdle();

    EXPECT_EQ(ps->GetAcl(0), AclGet(1));
    EXPECT_EQ(ps->GetAcl(1), AclGet(2));

    //Swap the list
    AddPolicySetFirewallPolicyLink("link1", "app1", "fp1", "2");
    AddPolicySetFirewallPolicyLink("link2", "app1", "fp2", "1");
    client->WaitForIdle();

    EXPECT_EQ(ps->GetAcl(0), AclGet(2));
    EXPECT_EQ(ps->GetAcl(1), AclGet(1));

    DelPolicySetFirewallPolicyLink("link1", "app1", "fp1");
    DelPolicySetFirewallPolicyLink("link2", "app1", "fp2");
    DelNode("application-policy-set", "app1");
    DelNode("firewall-policy", "fp1");
    DelNode("firewall-policy", "fp2");
    client->WaitForIdle();
}

TEST_F(PolicySetTest, Global) {
    AddGlobalPolicySet("app1", 1);
    client->WaitForIdle();

    PolicySet *ps = PolicySetGet(1);
    EXPECT_TRUE(ps->global());

    DelNode("application-policy-set", "app1");
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
