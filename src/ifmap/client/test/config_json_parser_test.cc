/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/client/config_json_parser.h"
#include <fstream>
#include <string>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/test/ifmap_test_util.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class ConfigJsonParserTest : public ::testing::Test {
protected:
    ConfigJsonParserTest() :
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        parser_(&db_) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(&parser_);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(&parser_);
        bgp_schema_Server_ModuleInit(&db_, &graph_);
    }

    virtual void TearDown() {
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        parser_.MetadataClear("vnc_cfg");
        task_util::WaitForIdle();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    IFMapNode *NodeLookup(const string &type, const string &name) {
        return ifmap_test_util::IFMapNodeLookup(&db_, type, name);
    }

    IFMapLink *LinkLookup(IFMapNode *left, IFMapNode *right,
                          const string &metadata) {
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
                                     db_.FindTable("__ifmap_metadata__.0"));
        IFMapLink *link =  link_table->FindLink(metadata, left, right);
        return (link ? (link->IsDeleted() ? NULL : link) : NULL);
    }

    DB db_;
    DBGraph graph_;
    ConfigJsonParser parser_;
};

TEST_F(ConfigJsonParserTest, VirtualNetworkParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vn.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:dp:vn1");
    ASSERT_TRUE(vnn != NULL);
    IFMapObject *obj = vnn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);

    // No refs but link to parent (project-virtual-network) exists
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *projn = NodeLookup("project", "dd:dp");
    ASSERT_TRUE(projn != NULL);
    IFMapLink *link = LinkLookup(projn, vnn, "project-virtual-network");
    EXPECT_TRUE(link != NULL);

    autogen::VirtualNetwork *vn = static_cast<autogen::VirtualNetwork *>(obj);
    TASK_UTIL_EXPECT_TRUE(vn->IsPropertySet(autogen::VirtualNetwork::ID_PERMS));
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::NETWORK_ID));
    TASK_UTIL_EXPECT_TRUE(
        vn->IsPropertySet(autogen::VirtualNetwork::FLOOD_UNKNOWN_UNICAST));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::DISPLAY_NAME));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::ANNOTATIONS));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::IMPORT_ROUTE_TARGET_LIST));
    TASK_UTIL_EXPECT_FALSE(
        vn->IsPropertySet(autogen::VirtualNetwork::EXPORT_ROUTE_TARGET_LIST));
    TASK_UTIL_EXPECT_EQ(vn->network_id(), 2);
    autogen::PermType2 pt2 = vn->perms2();
    int cmp = pt2.owner.compare("cloud-admin");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 7);
}

TEST_F(ConfigJsonParserTest, AclParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/acl.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "access-control-list");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *acln = NodeLookup("access-control-list", "dd:acl1:iacl");
    ASSERT_TRUE(acln != NULL);
    IFMapObject *obj = acln->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);

    // No refs but link to parent (security-group-access-control-list)
    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    TASK_UTIL_EXPECT_EQ(1, link_table->Size());
    IFMapNode *sgn = NodeLookup("security-group", "dd:acl1");
    ASSERT_TRUE(sgn != NULL);
    IFMapLink *link = LinkLookup(sgn, acln, "security-group-access-control-list");
    EXPECT_TRUE(link != NULL);

    autogen::AccessControlList *acl = static_cast<autogen::AccessControlList *>
                                          (obj);
    TASK_UTIL_EXPECT_TRUE(
        acl->IsPropertySet(autogen::AccessControlList::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        acl->IsPropertySet(autogen::AccessControlList::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        acl->IsPropertySet(autogen::AccessControlList::ANNOTATIONS));
    int cmp = acl->display_name().compare("ingress-access-control-list");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    autogen::PermType2 pt2 = acl->perms2();
    cmp = pt2.owner.compare("a35f7a1a65084b6c9ba402710ba0948a");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 5);
}

TEST_F(ConfigJsonParserTest, VmiParseAddDeleteProperty) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vmi.json");
    assert(message.size() != 0);
    bool add_change = true;
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vmin = NodeLookup("virtual-machine-interface",
        "dd:VMI:42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    ASSERT_TRUE(vmin != NULL);
    IFMapObject *obj = vmin->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);

    IFMapNode *rin = NodeLookup("routing-instance", "dd:vn1:vn1");
    ASSERT_TRUE(rin != NULL);
    table = IFMapTable::FindTable(&db_, "routing-instance");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *sgn = NodeLookup("security-group", "dd:sg1:default");
    ASSERT_TRUE(sgn != NULL);
    table = IFMapTable::FindTable(&db_, "security-group");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:vn1");
    ASSERT_TRUE(vnn != NULL);
    table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *projn = NodeLookup("project", "dd:VMI");
    ASSERT_TRUE(projn);
    table = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
        db_.FindTable("__ifmap_metadata__.0"));
    // refs: vmi-sg, vmi-vn, vmi-vmirn, vmirn-rn (vmi-rn has metadata)
    // parent: project-virtual-machine-interface
    TASK_UTIL_EXPECT_EQ(5, link_table->Size());
    IFMapLink *link = LinkLookup(vmin, sgn,
        "virtual-machine-interface-security-group");
    EXPECT_TRUE(link != NULL);
    link = LinkLookup(vmin, vnn, "virtual-machine-interface-virtual-network");
    EXPECT_TRUE(link != NULL);

    autogen::VirtualMachineInterface *vmi =
        static_cast<autogen::VirtualMachineInterface *>(obj);
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::ANNOTATIONS));

    int cmp =
        vmi->display_name().compare("42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    autogen::PermType2 pt2 = vmi->perms2();
    cmp = pt2.owner.compare("6ca1f1de004d48f99cf4528539b20013");
    TASK_UTIL_EXPECT_EQ(cmp, 0);
    TASK_UTIL_EXPECT_EQ(pt2.owner_access, 7);

    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISABLE_POLICY));
    // vmi1.json does not have the DISABLE_POLICY property. After processing
    // this message, the node should not have this property.
    message = FileRead("controller/src/ifmap/client/testdata/vmi1.json");
    assert(message.size() != 0);
    parser_.Receive(message, add_change, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISABLE_POLICY));
    // All other properties should still be set/unset as before.
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::DISPLAY_NAME));
    TASK_UTIL_EXPECT_TRUE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::PERMS2));
    TASK_UTIL_EXPECT_FALSE(
        vmi->IsPropertySet(autogen::VirtualMachineInterface::ANNOTATIONS));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}

