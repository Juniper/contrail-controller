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
#include "io/test/event_manager_test.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/test/config_cassandra_client_test.h"
#include "ifmap/test/ifmap_client_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using contrail_rapidjson::Document;
using contrail_rapidjson::SizeType;
using contrail_rapidjson::Value;

class IFMapVmUuidMapperTest : public ::testing::Test {
protected:
    IFMapVmUuidMapperTest() :
        thread_(&evm_),
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        server_(new IFMapServer(&db_, &db_graph_, evm_.io_service())),
        config_client_manager_(new ConfigClientManager(&evm_,
            server_.get(), "localhost", "config-test", config_options_)),
        ifmap_sandesh_context_(new IFMapSandeshContext(server_.get())) {
        config_cassandra_client_ = dynamic_cast<ConfigCassandraClientTest *>(
            config_client_manager_->config_db_client());
    }

    void SandeshSetup() {
        if (!getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
            return;
        int port =
            strtoul(getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"), NULL, 0);
        if (!port)
            port = 5910;
        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
        Sandesh::set_module_context("IFMap", ifmap_sandesh_context_.get());
        Sandesh::InitGenerator("ConfigJsonParserTest", hostname, "IFMapTest",
            "Test", &evm_, port, ifmap_sandesh_context_.get());
        std::cout << "Introspect at http://localhost:" << Sandesh::http_port()
            << std::endl;
    }

    void SandeshTearDown() {
        if (!getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
            return;
        Sandesh::Uninit();
        task_util::WaitForIdle();
    }

    virtual void SetUp() {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
        IFMapLinkTable_Init(server_->database(), server_->graph());
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(server_->database(), server_->graph());
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(server_->database(), server_->graph());
        server_->Initialize();
        server_->set_config_manager(config_client_manager_.get());
        config_client_manager_->EndOfConfig();
        vm_uuid_mapper_ = server_->vm_uuid_mapper();
        task_util::WaitForIdle();
        SandeshSetup();
        thread_.Start();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        server_->Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
        SandeshTearDown();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    void CheckNodeBits(IFMapNode *node, int index, bool binterest,
                       bool badvertised) {
        IFMapExporter *exporter = server_->exporter();
        IFMapNodeState *state = exporter->NodeStateLookup(node);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
    }
    void SimulateDeleteClient(IFMapClient *c1) {
        server_->SimulateDeleteClient(c1);
    }

    void ParseEventsJson (string events_file) {
        ConfigCassandraClientTest::ParseEventsJson(config_client_manager_.get(),
                events_file);
    }

    void FeedEventsJson () {
        ConfigCassandraClientTest::FeedEventsJson(config_client_manager_.get());
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph db_graph_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    ConfigCassandraClientTest *config_cassandra_client_;
    IFMapVmUuidMapper *vm_uuid_mapper_;
};

class IFMapVmUuidMapperTestWithParam1
    : public IFMapVmUuidMapperTest,
      public ::testing::WithParamInterface<string> {
};

// Receive config first and then vm-sub
TEST_P(IFMapVmUuidMapperTestWithParam1, ConfigThenSubscribe) {
    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    TASK_UTIL_EXPECT_EQ(3, c1.NodeKeyCount("virtual-machine"));

    TASK_UTIL_EXPECT_EQ(3, c1.NodeKeyCount("virtual-machine-interface"));
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
}

// Add all the config and then simulate receiving a vm-subscribe just after the
// node was marked deleted.
TEST_P(IFMapVmUuidMapperTestWithParam1, VmSubUnsubWithDeletedNode) {
    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());

    IFMapClientMock
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);

    IFMapNode *vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm != NULL);
    TASK_UTIL_ASSERT_TRUE(
        vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // vm-subscribe for the first VM
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    task_util::WaitForIdle();
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    task_util::WaitForIdle();

    // Simulate receiving a vm-subscribe for the second VM after the VM node is
    // deleted. The second VM should show up in the pending list.
    vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm != NULL);
    TASK_UTIL_ASSERT_TRUE(
        vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    vm->MarkDelete();
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vm_uuid_mapper_->PendingVmRegCount());
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    string vr_name;
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegExists(
        "93e76278-1990-4905-a472-8e9188f41b2c", &vr_name));
    task_util::WaitForIdle();

    // Send a vm-unsubscribe while the node is deleted. The pending vm-reg
    // should be removed.
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->PendingVmRegExists(
        "93e76278-1990-4905-a472-8e9188f41b2c", &vr_name));
    task_util::WaitForIdle();
}

// Vm-sub with deleted node followed by vm_uuid_mapper processing a node-delete
// followed by processing revival of the node.
TEST_P(IFMapVmUuidMapperTestWithParam1, DeletedNodeRevival) {
    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());

    IFMapClientMock
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);

    // Simulate receiving a vm-subscribe for a VM *after* the VM node is marked
    // deleted. The VM should show up in the pending list.
    IFMapNode *vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm != NULL);
    IFMapObject *obj = vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    vm->MarkDelete();
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    task_util::WaitForIdle();
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(1, vm_uuid_mapper_->PendingVmRegCount());
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    task_util::WaitForIdle();

    // Simulate the vm_uuid_mapper processing the VM node-delete. The counts in
    // the mapper maps should go down by one but the pending list should still
    // have the VM's reg request.
    DBTable *table = vm->table();
    DBTablePartBase *partition = table->GetTablePartition(0);
    ASSERT_TRUE(table != NULL);
    vm_uuid_mapper_->VmNodeProcess(partition, vm);
    task_util::WaitForIdle();
    EXPECT_EQ(2, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(2, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(1, vm_uuid_mapper_->PendingVmRegCount());
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));

    // Simulate that the deleted node is getting revived by removing the
    // deleted flag and the vm_uuid_mapper processes this node revival.
    vm->ClearDelete();
    vm_uuid_mapper_->VmNodeProcess(partition, vm);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

// Config is received, then client gets deleted, then vm-subscribe is received.
TEST_P(IFMapVmUuidMapperTestWithParam1, ConfigDeleteClientSubscribe) {
    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());

    // Dont create the client but trigger receiving VM subscribes from that
    // client to simulate the condition where the client is deleted before the
    // subscribe is processed.
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    // Check the VM's. XMPP as origin must not exist.
    IFMapNode *vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm != NULL);
    IFMapObject *obj = vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    obj = vm->Find(IFMapOrigin(IFMapOrigin::XMPP));
    ASSERT_TRUE(obj == NULL);

    vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm != NULL);
    obj = vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    obj = vm->Find(IFMapOrigin(IFMapOrigin::XMPP));
    ASSERT_TRUE(obj == NULL);

    vm = vm_uuid_mapper_->GetVmNodeByUuid(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm != NULL);
    obj = vm->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    obj = vm->Find(IFMapOrigin(IFMapOrigin::XMPP));
    ASSERT_TRUE(obj == NULL);
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
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
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
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(3, vm_uuid_mapper_->PendingVmRegCount());

    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

// Vm-subscribe is received, then client gets deleted, then config is received.
TEST_P(IFMapVmUuidMapperTestWithParam1, SubscribeDeleteClientThenConfig) {
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));

    // Dont create the client but trigger receiving VM subscribes from that
    // client to simulate the condition where the client is deleted before the
    // subscribe is processed.
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    // Since the client does not exist, none of the VMs should make it to the
    // pending list.
    string vr_name;
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->PendingVmRegExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117", &vr_name));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->PendingVmRegExists(
        "93e76278-1990-4905-a472-8e9188f41b2c", &vr_name));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->PendingVmRegExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", &vr_name));
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());

    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

// Receive config first, then vm-sub, then vm-unsub
TEST_P(IFMapVmUuidMapperTestWithParam1, CfgSubUnsub) {
    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_NE(0, c1.count());
    TASK_UTIL_EXPECT_NE(0, c1.node_count());
    TASK_UTIL_EXPECT_NE(0, c1.link_count());

    TASK_UTIL_EXPECT_FALSE(c1.NodeExists("virtual-network",
                                         "default-domain:demo:vn28"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-router",
        "default-global-system-config:a1s27.contrail.juniper.net"));
    TASK_UTIL_EXPECT_EQ(3, c1.NodeKeyCount("virtual-machine"));
    TASK_UTIL_EXPECT_TRUE(c1.NodeExists("virtual-network",
                                        "default-domain:demo:vn27"));
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(3, c1.NodeKeyCount("virtual-machine-interface"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 1);
    TASK_UTIL_EXPECT_EQ(2, c1.NodeKeyCount("virtual-machine"));

    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(2, c1.NodeKeyCount("virtual-machine-interface"));

    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 2);
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-machine"));
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-machine-interface"));
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-machine"));
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-machine-interface"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
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
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
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
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(3, vm_uuid_mapper_->PendingVmRegCount());

    ParseEventsJson(GetParam());
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 1);
    TASK_UTIL_EXPECT_EQ(2, c1.NodeKeyCount("virtual-machine"));

    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(2, c1.NodeKeyCount("virtual-machine-interface"));

    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 2);
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-machine"));

    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(1, c1.NodeKeyCount("virtual-machine-interface"));

    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 3);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-network"));
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-machine"));
    TASK_UTIL_EXPECT_EQ(0, c1.NodeKeyCount("virtual-machine-interface"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

// Receive vm-sub first, then vm-unsub - no config received
TEST_P(IFMapVmUuidMapperTestWithParam1, SubUnsub) {
            
    // Data based on src/ifmap/testdata/cli1_vn1_vm3_add.xml
    // and src/ifmap/testdata/cli1_vn1_vm3_add_vmname.xml

    // VM Subscribe
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(3, vm_uuid_mapper_->PendingVmRegCount());

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", false, 0);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

INSTANTIATE_TEST_CASE_P(UuidMapper, IFMapVmUuidMapperTestWithParam1,
    ::testing::Values(
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add.json",
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add_vmname.json"
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
    ParseEventsJson(config_files.AddConfigFileName);
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());

    ParseEventsJson(config_files.DeleteConfigFileName);
    FeedEventsJson();

    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_FALSE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

TEST_P(IFMapVmUuidMapperTestWithParam2, SubCfgaddCfgdelUnsub) {

    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a", true, 3);
    task_util::WaitForIdle();

    EXPECT_EQ(0, c1.count());
    EXPECT_EQ(0, c1.node_count());
    EXPECT_EQ(0, c1.link_count());
    EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->PendingVmRegCount());

    // Config adds
    ConfigFileNames config_files = GetParam();
    ParseEventsJson(config_files.AddConfigFileName);
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(4, c1.node_count()); // vr and 3 vm's
    TASK_UTIL_EXPECT_EQ(3, c1.link_count()); // 3 links
    TASK_UTIL_EXPECT_EQ(7, c1.count()); // node+link
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    ParseEventsJson(config_files.DeleteConfigFileName);
    FeedEventsJson();

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
    EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_->ProcessVmSubscribe(
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
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
}

TEST_P(IFMapVmUuidMapperTestWithParam2, CfgaddSubUnsubCfgdel) {
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);

    // Config adds
    ConfigFileNames config_files = GetParam();
    ParseEventsJson(config_files.AddConfigFileName);
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // Subscribes
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
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

    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_->ProcessVmSubscribe(
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
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    ParseEventsJson(config_files.DeleteConfigFileName);
    FeedEventsJson();

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
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();
}

TEST_P(IFMapVmUuidMapperTestWithParam2, CfgaddSubCfgdelUnsub) {
    IFMapClientMock 
        c1("default-global-system-config:a1s27.contrail.juniper.net");
    server_->AddClient(&c1);

    // Config adds
    ConfigFileNames config_files = GetParam();
    ParseEventsJson(config_files.AddConfigFileName);

    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "2d308482-c7b3-4e05-af14-e732b7b50117"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "93e76278-1990-4905-a472-8e9188f41b2c"));
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->VmNodeExists(
        "43d086ab-52c4-4a1f-8c3d-63b321e36e8a"));
    TASK_UTIL_EXPECT_EQ(0, c1.node_count());
    TASK_UTIL_EXPECT_EQ(0, c1.link_count());
    TASK_UTIL_EXPECT_EQ(0, c1.count());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // Subscribes
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", true, 1);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", true, 2);
    server_->ProcessVmSubscribe(
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

    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // Config deletes
    ParseEventsJson(config_files.DeleteConfigFileName);
    FeedEventsJson();

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
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(3, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
    c1.PrintNodes();
    c1.PrintLinks();

    // VM Unsubscribe
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "2d308482-c7b3-4e05-af14-e732b7b50117", false, 2);
    server_->ProcessVmSubscribe(
        "default-global-system-config:a1s27.contrail.juniper.net",
        "93e76278-1990-4905-a472-8e9188f41b2c", false, 1);
    server_->ProcessVmSubscribe(
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
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->UuidMapperCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->NodeUuidMapCount());
    TASK_UTIL_EXPECT_EQ(0, vm_uuid_mapper_->PendingVmRegCount());
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
    ConfigAmqpClient::set_disable(true);
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    bool success = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return success;
}
