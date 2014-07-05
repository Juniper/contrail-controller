/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_uuid_mapper.h"

#include <fstream>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/test/ifmap_client_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;

class IFMapVmUuidMapperTest : public ::testing::Test {
protected:
    IFMapVmUuidMapperTest() :
        server_(&db_, &db_graph_, evm_.io_service()), parser_(NULL) {
    }

    virtual void SetUp() {
        IFMapLinkTable_Init(&db_, &db_graph_);
        parser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(parser_);
        vnc_cfg_Server_ModuleInit(&db_, &db_graph_);
        bgp_schema_ParserInit(parser_);
        bgp_schema_Server_ModuleInit(&db_, &db_graph_);
        server_.Initialize();
        vm_uuid_mapper_ = server_.vm_uuid_mapper();
    }

    virtual void TearDown() {
        server_.Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        task_util::WaitForIdle();
        db_.Clear();
        parser_->MetadataClear("vnc_cfg");
        evm_.Shutdown();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    void CheckNodeBits(IFMapNode *node, int index, bool binterest,
                       bool badvertised) {
        IFMapExporter *exporter = server_.exporter();
        IFMapNodeState *state = exporter->NodeStateLookup(node);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
    }

    DB db_;
    DBGraph db_graph_;
    EventManager evm_;
    IFMapServer server_;
    IFMapServerParser *parser_;
    IFMapVmUuidMapper *vm_uuid_mapper_;
};

class IFMapVmUuidMapperTestWithParam1
    : public IFMapVmUuidMapperTest,
      public ::testing::WithParamInterface<string> {
};

// Receive config first and then vm-sub
TEST_P(IFMapVmUuidMapperTestWithParam1, ConfigThenSubscribe) {
    string filename = GetParam();
    string content = FileRead(filename);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 3);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 3);
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

TEST_P(IFMapVmUuidMapperTestWithParam1, SubscribeThenConfig) {
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    string vr_name;
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117", &vr_name));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "93e76278-1990-4905-a472-8e9188f41b2c", &vr_name));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", &vr_name));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 3);

    string filename = GetParam();
    string content = FileRead(filename);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

// Receive config first, then vm-sub, then vm-unsub
TEST_P(IFMapVmUuidMapperTestWithParam1, CfgSubUnsub) {
    string filename = GetParam();
    string content = FileRead(filename);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 3);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 3);
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 0);
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
}

// Receive vm-sub first, then config
// Receive vm-sub first, then config, then vm-unsub
TEST_P(IFMapVmUuidMapperTestWithParam1, SubscribeConfigUnsub) {
    EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    string vr_name;
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117", &vr_name));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "93e76278-1990-4905-a472-8e9188f41b2c", &vr_name));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", &vr_name));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 3);

    string filename = GetParam();
    string content = FileRead(filename);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 2);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 1);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-network"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine"), 0);
    TASK_UTIL_EXPECT_EQ(c1.NodeKeyCount("virtual-machine-interface"), 0);
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

// Receive vm-sub first, then vm-unsub - no config received
TEST_P(IFMapVmUuidMapperTestWithParam1, SubUnsub) {
            
    // Data based on src/ifmap/testdata/cli1_vn1_vm3_add.xml
    // and src/ifmap/testdata/cli1_vn1_vm3_add_vmname.xml

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 3);

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 0);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

INSTANTIATE_TEST_CASE_P(UuidMapper, IFMapVmUuidMapperTestWithParam1,
    ::testing::Values(
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add.xml",
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add_vmname.xml"
        ));

struct ConfigFileNames {
    string AddConfigFileName;
    string DeleteConfigFileName;
};

class IFMapVmUuidMapperTestWithParam2
    : public IFMapVmUuidMapperTest,
      public ::testing::WithParamInterface<ConfigFileNames> {
};

// Receive config-add then config-delete - no sub/unsub received
TEST_P(IFMapVmUuidMapperTestWithParam2, CfgaddCfgdel) {
    ConfigFileNames config_files = GetParam();
    string content = FileRead(config_files.AddConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);

    content = FileRead(config_files.DeleteConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

TEST_P(IFMapVmUuidMapperTestWithParam2, SubCfgaddCfgdelUnsub) {

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 3);

    // Config adds
    ConfigFileNames config_files = GetParam();
    string content = FileRead(config_files.AddConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(4, c1.node_count()); // vr and 3 vm's
    TASK_UTIL_EXPECT_EQ(3, c1.link_count()); // 3 links
    TASK_UTIL_EXPECT_EQ(7, c1.count()); // node+link
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    content = FileRead(config_files.DeleteConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    // Nodes are config-deleted but not marked deleted and will be seen as
    // 'change' on the client.
    EXPECT_EQ(8, c1.node_count());
    EXPECT_EQ(3, c1.link_count());
    EXPECT_EQ(11, c1.count());
    EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 0);
    task_util::WaitForIdle();
    usleep(10000);

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    // 4 node deletes, 3 link deletes, c1.count should go up by 7
    TASK_UTIL_EXPECT_EQ(4, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(18, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

TEST_P(IFMapVmUuidMapperTestWithParam2, CfgaddSubUnsubCfgdel) {
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);

    // Config adds
    ConfigFileNames config_files = GetParam();
    string content = FileRead(config_files.AddConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // Subscribes
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    size_t cli_index = static_cast<size_t>(c1.index());

    TASK_UTIL_EXPECT_EQ(4, c1.node_count()); // vr and 3 vm's
    TASK_UTIL_EXPECT_EQ(3, c1.link_count()); // 3 links
    TASK_UTIL_EXPECT_EQ(7, c1.count()); // node+link

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    IFMapNode *vm1 = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 != NULL);
    CheckNodeBits(vm1, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    IFMapNode *vm2 = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 != NULL);
    CheckNodeBits(vm2, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    IFMapNode *vm3 = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 != NULL);
    CheckNodeBits(vm3, cli_index, true, true);

    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 0);
    task_util::WaitForIdle();
    usleep(10000);

    // None of the nodes should be marked deleted since config-object is
    // still around.
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    vm1 = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 != NULL);
    EXPECT_FALSE(vm1->IsDeleted());
    CheckNodeBits(vm1, cli_index, false, false);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    vm2 = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 != NULL);
    EXPECT_FALSE(vm2->IsDeleted());
    CheckNodeBits(vm2, cli_index, false, false);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    vm3 = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 != NULL);
    EXPECT_FALSE(vm3->IsDeleted());
    CheckNodeBits(vm3, cli_index, false, false);

    // The vm-unsub should remove the vr-vm link and hence send deletes to the
    // client. Should get 3 node deletes (all the VMs but not the VR), 3 link 
    // deletes, count() should go up by 6. 
    TASK_UTIL_EXPECT_EQ(1, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(13, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    content = FileRead(config_files.DeleteConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    // Although the vms are gone, the client is around and the interest bits on
    // the vr is still set and hence we will still send the vr-delete to him
    TASK_UTIL_EXPECT_EQ(14, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();
}

TEST_P(IFMapVmUuidMapperTestWithParam2, CfgaddSubCfgdelUnsub) {
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_.AddClient(&c1);

    // Config adds
    ConfigFileNames config_files = GetParam();
    string content = FileRead(config_files.AddConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // Subscribes
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    size_t cli_index = static_cast<size_t>(c1.index());

    TASK_UTIL_EXPECT_EQ(4, c1.node_count()); // vr and 3 vm's
    TASK_UTIL_EXPECT_EQ(3, c1.link_count()); // 3 links
    TASK_UTIL_EXPECT_EQ(7, c1.count()); // node+link

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    IFMapNode *vm1 = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 != NULL);
    CheckNodeBits(vm1, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    IFMapNode *vm2 = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 != NULL);
    CheckNodeBits(vm2, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    IFMapNode *vm3 = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 != NULL);
    CheckNodeBits(vm3, cli_index, true, true);

    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    content = FileRead(config_files.DeleteConfigFileName);
    assert(content.size() != 0);
    parser_->Receive(&db_, content.c_str(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    vm1 = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 != NULL);
    EXPECT_FALSE(vm1->IsDeleted());
    CheckNodeBits(vm1, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    vm2 = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 != NULL);
    EXPECT_FALSE(vm2->IsDeleted());
    CheckNodeBits(vm2, cli_index, true, true);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    vm3 = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 != NULL);
    EXPECT_FALSE(vm3->IsDeleted());
    CheckNodeBits(vm3, cli_index, true, true);

    TASK_UTIL_EXPECT_EQ(8, c1.node_count()); // vr and 3 vm's
    TASK_UTIL_EXPECT_EQ(3, c1.link_count()); // 3 links
    TASK_UTIL_EXPECT_EQ(11, c1.count()); // node+link
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 3);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_.ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 0);
    task_util::WaitForIdle();
    usleep(10000);

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    vm1 = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 == NULL);

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    vm2 = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 == NULL);

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    vm3 = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 == NULL);

    // The vm-unsub should remove the vr-vm link and hence send deletes to the
    // client. Should get 4 node deletes, 3 link deletes, count() should go up 
    // by 7. 
    TASK_UTIL_EXPECT_EQ(4, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(18, c1.count());
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    c1.PrintNodes();
    c1.PrintLinks();
}

// Todo: need to add a testcase when VM has >1 properties.
TEST_F(IFMapVmUuidMapperTest, VmAddNoProp) {
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
