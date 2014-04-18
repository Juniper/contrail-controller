/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>

#include "testing/gunit.h"
#include "test/test_cmn_util.h"
#include "vnc_cfg_types.h"

void RouterIdDepInit(Agent *agent) {
}

class CfgTest : public ::testing::Test {
public:
    virtual void SetUp() {
        notify_count_ = 0;
    }

    virtual void TearDown() {
    }

    static void Init() {
        Agent::GetInstance()->cfg()->cfg_listener()->Unregister("network-ipam");
        Agent::GetInstance()->cfg()->cfg_listener()->Register
            ("network-ipam", boost::bind(&CfgTest::IpamNotify, _1),
             autogen::NetworkIpam::ID_PERMS);
    }

    static void Shutdown() {
    }

    static void IpamNotify(IFMapNode *node) {
        notify_count_++;
    }

    static int notify_count_;
};

int CfgTest::notify_count_;

TEST_F(CfgTest, DbNodeWithoutIdPerms_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();

    // Send VN Node without ID Perms. No entry should be created
    AddXmlHdr(buff, len);
    sprintf(buff + len, 
            "       <node type=\"virtual-network\">\n"
            "           <name>vn1</name>\n"
            "       </node>\n");
    len = strlen(buff);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(0));

    // Send VN Node with ID Perms. VN Entry must be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));

    // Delete VN Entry
    DelVn("vn1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
}

TEST_F(CfgTest, LinkWithoutIdPerms_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();

    // Send VN Node without ID Perms. No entry should be created
    sprintf(buff, "<?xml version=\"1.0\"?> \
        <config>\
                <update> \
                        <node type=\"virtual-network\"> \
                                <name>vn1</name> \
                        </node> \
                        <node type=\"access-control-list\"> \
                                <name>acl1</name> \
                        </node> \
                        <link> \
                                <node type=\"virtual-network\"> \
                                        <name>vn1</name> \
                                </node> \
                                <node type=\"access-control-list\"> \
                                        <name>acl1</name>\
                                </node> \
                        </link> \
                </update> \
        </config>");
    len = strlen(buff);
    ApplyXmlString(buff);
    EXPECT_FALSE(VnFind(0));
    EXPECT_FALSE(AclFind(0));

    // Send VM Node with ID Perms. VM must be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));

    // Add link for an unvisited node("vn1"). VM should not have VN reference
    AddLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn->GetAcl() == NULL);

    // Create VN
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "access-control-list", "acl1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_TRUE(AclFind(1));

    // Resend VM node. Link between VN and VM should be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "access-control-list", "acl1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    // Verify link is created
    vn = VnGet(1);
    EXPECT_TRUE(vn->GetAcl() != NULL);

    // Delete link between VM and VN
    DelLink("virtual-network", "vn1", "access-control-list", "acl1");
    client->WaitForIdle();
    vn = VnGet(1);
    EXPECT_TRUE(vn->GetAcl() == NULL);

    // Delete VN and VM Entry
    DelVn("vn1");
    DelNode("access-control-list", "acl1");
    client->WaitForIdle();
    EXPECT_FALSE(VnFind(1));
    EXPECT_FALSE(AclFind(1));
}

TEST_F(CfgTest, NonDbNodeWithoutIdPerms_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();

    // Send VN Node without ID Perms. No entry should be created
    AddXmlHdr(buff, len);
    sprintf(buff + len, 
            "       <node type=\"network-ipam\">\n"
            "           <name>ipam1</name>\n"
            "       </node>\n");
    len = strlen(buff);
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 0);

    // Send VN Node with ID Perms. VN Entry must be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "network-ipam", "ipam1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 1);

    // Delete VN Entry
    DelNode("network-ipam", "ipam1");
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 2);
}

TEST_F(CfgTest, NonDbLinkWithoutIdPerms_1) {
    char buff[4096];
    int len = 0;

    client->WaitForIdle();

    // Send VN Node without ID Perms. No entry should be created
    sprintf(buff, "<?xml version=\"1.0\"?> \
        <config>\
                <update> \
                        <node type=\"virtual-network\"> \
                                <name>vn1</name> \
                        </node> \
                        <node type=\"network-ipam\"> \
                                <name>ipam1</name> \
                        </node> \
                        <link> \
                                <node type=\"virtual-network\"> \
                                        <name>vn1</name> \
                                </node> \
                                <node type=\"network-ipam\"> \
                                        <name>ipam1</name>\
                                </node> \
                        </link> \
                </update> \
        </config>");
    len = strlen(buff);
    ApplyXmlString(buff);
    EXPECT_FALSE(VnFind(0));
    EXPECT_EQ(notify_count_, 0);

    // Send VN Node with ID Perms. VN must be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    VnEntry *vn = VnGet(1);
    EXPECT_TRUE(vn != NULL);

    // Add link for an unvisited node("vn1"). VM should not have VN reference
    AddLink("virtual-network", "vn1", "network-ipam", "ipam1");
    client->WaitForIdle();
    vn = VnGet(1);
    EXPECT_TRUE(vn != NULL);
    EXPECT_EQ(notify_count_, 0);

    // Create network-ipam with ID-PERMS
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "network-ipam", "ipam1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 1);

    // Resend VN node. Link between VN and network-ipam should be created
    AddXmlHdr(buff, len);
    AddNodeString(buff, len, "virtual-network", "vn1", 1); 
    AddXmlTail(buff, len);
    ApplyXmlString(buff);
    client->WaitForIdle();
    EXPECT_TRUE(VnFind(1));
    EXPECT_EQ(notify_count_, 1);
    
    // Delete entries
    DelLink("virtual-network", "vn1", "network-ipam", "ipam1");
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 2);

    DelVn("vn1");
    DelNode("network-ipam", "ipam1");
    client->WaitForIdle();
    EXPECT_EQ(notify_count_, 3);
    EXPECT_FALSE(VnFind(1));
}

#define VN_NOUUID_ADD_STR \
    "<?xml version=\"1.0\"?>" "<config>" "<update>" \
        "<node type=\"virtual-network\"> <name>vn1</name> </node>" \
     "</update> </config>"

#define VM_NOUUID_ADD_STR \
    "<?xml version=\"1.0\"?>" "<config>" "<update>" \
        "<node type=\"virtual-machine\"> <name>vm1</name> </node>" \
     "</update> </config>"

class CfgUuidTest : public CfgTest {
public:
    virtual void SetUp() {
        vn_table = Agent::GetInstance()->cfg()->cfg_vn_table();
        vm_table = Agent::GetInstance()->cfg()->cfg_vm_table();
        link_table = static_cast<IFMapAgentLinkTable *>
            (Agent::GetInstance()->GetDB()->FindTable(IFMAP_AGENT_LINK_DB_NAME));

        WAIT_FOR(100, 10000, (vn_table->Size() == 0));
        WAIT_FOR(100, 10000, (vm_table->Size() == 0));
        WAIT_FOR(100, 10000, (link_table->Size() == 0));
        const IFMapAgentLinkTable::LinkDefMap &def_list = link_table->GetLinkDefMap();
        WAIT_FOR(100, 10000, (def_list.size() == 0));
        client->WaitForIdle();
    }

    virtual void TearDown() {
        // Delete the nodes
        DelLink("virtual-network", "vn1", "virtual-machine", "vm1");
        DelVn("vn1");
        DelVm("vm1");
        WAIT_FOR(100, 10000, (vn_table->Size() == 0));
        WAIT_FOR(100, 10000, (vm_table->Size() == 0));
        WAIT_FOR(100, 10000, (link_table->Size() == 0));
        const IFMapAgentLinkTable::LinkDefMap &def_list = link_table->GetLinkDefMap();
        WAIT_FOR(100, 10000, (def_list.size() == 0));
    }

    static void Init() { }
    static void Shutdown() { }

    DBTable *vn_table;
    DBTable *vm_table;
    IFMapAgentLinkTable *link_table;
};

TEST_F(CfgUuidTest, AddNoUuid_1) {
    // Add VM, VN node without UUID
    ApplyXmlString(VN_NOUUID_ADD_STR);
    ApplyXmlString(VM_NOUUID_ADD_STR);

    // Add Link to VM and VN without UUID
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();

    // VM, VN and Link should not be present in DB
    EXPECT_EQ(vn_table->Size(), 0U);
    EXPECT_EQ(vm_table->Size(), 0U);
    EXPECT_EQ(link_table->Size(), 0U);

    client->WaitForIdle();
    // Add VN with UUID. VN must be added. Link is still not added
    AddVn("vn1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Add VM with UUID. VM must be added. Link is now added
    AddVm("vm1", 1);
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
}

TEST_F(CfgUuidTest, AddNoUuid_2) {
    client->WaitForIdle();
    // Add VN with UUID
    AddVn("vn1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));

    // Add Link to VM and VN without UUID. Link deferred
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Add VM with UUID. VM must be added. Link is now added
    AddVm("vm1", 1);
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
}

TEST_F(CfgUuidTest, AddNoUuid_3) {
    client->WaitForIdle();
    // Add VM with UUID
    AddVm("vm1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));

    // Add Link to VM and VN without UUID. Link deferred
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Add VN with UUID. VM must be added. Link is now added
    AddVn("vn1", 1);
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
}

TEST_F(CfgUuidTest, AddNoUuid_4) {
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Add VM with UUID
    AddVm("vm1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));

    // Add VN with UUID. VM must be added. Link is now added
    AddVn("vn1", 1);
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
}

TEST_F(CfgUuidTest, AddNoUuid_5) {
    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Add VN with UUID
    AddVn("vn1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));

    // Add VM with UUID. VM must be added. Link is now added
    AddVm("vm1", 1);
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
}

TEST_F(CfgUuidTest, DupAddNoUuid_1) {
    const IFMapAgentLinkTable::LinkDefMap &def_list = link_table->GetLinkDefMap();

    client->WaitForIdle();
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));
    WAIT_FOR(100, 10000, (def_list.size() == 2));

    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (link_table->Size() == 0));
    WAIT_FOR(100, 10000, (def_list.size() == 2));

    // Add VN with UUID
    AddVn("vn1", 1);
    client->WaitForIdle();
    AddVn("vn1", 1);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));

    // Add VM with UUID. VM must be added. Link is now added
    AddVm("vm1", 1);
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (def_list.size() == 0));

    AddVm("vm1", 1);
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (def_list.size() == 0));

    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (def_list.size() == 0));

    AddLink("virtual-machine", "vm1", "virtual-network", "vn1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));
    WAIT_FOR(100, 10000, (def_list.size() == 0));
}

TEST_F(CfgUuidTest, DelNoUuid_1) {
    client->WaitForIdle();
    // Add VM with UUID
    AddVm("vm1", 1);
    AddVn("vn1", 1);
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));

    // Change VN with UUID 0
    ApplyXmlString(VN_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 0));
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Change VM with UUID 0
    ApplyXmlString(VM_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 0));
    WAIT_FOR(100, 10000, (vm_table->Size() == 0));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));
}

TEST_F(CfgUuidTest, DelNoUuid_2) {
    client->WaitForIdle();
    // Add VM with UUID
    AddVm("vm1", 1);
    AddVn("vn1", 1);
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));

    // Change VM with UUID 0
    ApplyXmlString(VM_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (vm_table->Size() == 0));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Change VN with UUID 0
    ApplyXmlString(VN_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 0));
    WAIT_FOR(100, 10000, (vm_table->Size() == 0));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));
}

TEST_F(CfgUuidTest, DelNoUuid_3) {
    client->WaitForIdle();
    // Add VM with UUID
    AddVm("vm1", 1);
    AddVn("vn1", 1);
    AddLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 1));

    DelLink("virtual-network", "vn1", "virtual-machine", "vm1");
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (vm_table->Size() == 1));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Change VM with UUID 0
    ApplyXmlString(VM_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 1));
    WAIT_FOR(100, 10000, (vm_table->Size() == 0));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));

    // Change VN with UUID 0
    ApplyXmlString(VN_NOUUID_ADD_STR);
    client->WaitForIdle();
    WAIT_FOR(100, 10000, (vn_table->Size() == 0));
    WAIT_FOR(100, 10000, (vm_table->Size() == 0));
    WAIT_FOR(100, 10000, (link_table->Size() == 0));
}

int main(int argc, char **argv) {
    GETUSERARGS();

    client = TestInit(init_file, ksync_init);
    CfgTest::Init();
    int ret = RUN_ALL_TESTS();
    TestShutdown();
    delete client;
    return ret;
}
