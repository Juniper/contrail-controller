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
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/test/ifmap_test_util.h"

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

    DB db_;
    DBGraph graph_;
    ConfigJsonParser parser_;
};

TEST_F(ConfigJsonParserTest, VirtualNetworkParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vn.json");
    assert(message.size() != 0);
    parser_.Receive(message, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vnn = NodeLookup("virtual-network", "dd:dp:vn1");
    EXPECT_TRUE(vnn != NULL);
    IFMapObject *obj = vnn->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);
}

TEST_F(ConfigJsonParserTest, AclParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/acl.json");
    assert(message.size() != 0);
    parser_.Receive(message, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "access-control-list");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *acln = NodeLookup("access-control-list", "dd:acl1:iacl");
    EXPECT_TRUE(acln != NULL);
    IFMapObject *obj = acln->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);
}

TEST_F(ConfigJsonParserTest, VmiParse) {
    string message =
        FileRead("controller/src/ifmap/client/testdata/vmi.json");
    assert(message.size() != 0);
    parser_.Receive(message, IFMapOrigin::CASSANDRA);
    task_util::WaitForIdle();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    IFMapNode *vmin = NodeLookup("virtual-machine-interface",
        "dd:VMI:42f6d841-d1c7-40b8-b1c4-ca2ab415c81d");
    EXPECT_TRUE(vmin != NULL);
    IFMapObject *obj = vmin->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(obj != NULL);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}

