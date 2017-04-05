/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_sandesh_context.h"
#include <string>

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_origin.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_server_show_internal_types.h"
#include "ifmap/test/config_cassandra_client_test.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using boost::assign::list_of;

class ConfigJsonParserTest : public ::testing::Test {
public:
    void ValidateFQNameCacheResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDToFQNameResp *resp =
            dynamic_cast<const ConfigDBUUIDToFQNameResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_fqname_cache().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_fqname_cache().size(); ++i) {
            string result_match = resp->get_fqname_cache()[i].get_obj_type() +
                ':' + resp->get_fqname_cache()[i].get_fq_name();
            TASK_UTIL_EXPECT_EQ(result[i], result_match);
            cout << resp->get_fqname_cache()[i].log() << endl;
        }
        validate_done_ = true;
    }

    void ValidateObjCacheResponse(Sandesh *sandesh,
            vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDCacheResp *resp =
            dynamic_cast<const ConfigDBUUIDCacheResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_uuid_cache().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            TASK_UTIL_EXPECT_EQ(result[i],
                                resp->get_uuid_cache()[i].get_uuid());
            cout << resp->get_uuid_cache()[i].log() << endl;
        }
        validate_done_ = true;
    }


protected:
    ConfigJsonParserTest() :
        thread_(&evm_),
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        ifmap_server_(new IFMapServer(&db_, &graph_, evm_.io_service())),
        config_client_manager_(new ConfigClientManager(&evm_,
            ifmap_server_.get(), "localhost", "config-test", config_options_)),
        ifmap_sandesh_context_(new IFMapSandeshContext(ifmap_server_.get())),
        validate_done_(false) {
        ifmap_server_->set_config_manager(config_client_manager_.get());
    }

    void SandeshSetup() {
        Sandesh::set_module_context("IFMap", ifmap_sandesh_context_.get());
        if (!getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
            return;
        int port =
            strtoul(getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"), NULL, 0);
        if (!port)
            port = 5910;
        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
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
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(&db_, &graph_);
        SandeshSetup();

        thread_.Start();
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        ifmap_server_->Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
        SandeshTearDown();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
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

    void ParseEventsJson(std::string events_file) {
        ConfigCassandraClientTest::ParseEventsJson(config_client_manager_.get(),
                events_file);
    }

    void FeedEventsJson() {
        ConfigCassandraClientTest::FeedEventsJson(config_client_manager_.get());
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    const IFMapConfigOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    bool validate_done_;
};

TEST_F(ConfigJsonParserTest, BulkSync) {
    if (getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE")) {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        ParseEventsJson(getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE"));
    } else {
        ParseEventsJson("controller/src/ifmap/client/testdata/bulk_sync.json");
    }
    FeedEventsJson();
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_NE(0, table->Size());
    if (getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
        TASK_UTIL_EXEC_AND_WAIT(evm_, "/usr/bin/python");
    ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
}

// In a single message, adds vn1, vn2, vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInOneShot) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
}

// Verify introspect for FQName cache
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> fq_name_expected_entries = list_of("virtual_network:vn1")
                                                     ("virtual_network:vn2")
                                                     ("virtual_network:vn3");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Given valid uuid
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_SpecificUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> fq_name_expected_entries = list_of("virtual_network:vn1");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->set_uuid("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Given invalid uuid
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_InvalidUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> fq_name_expected_entries;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->set_uuid("deadbeef-dead-beef-dead-beefdeaddead");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Request iterate
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_ReqIterate) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> fq_name_expected_entries = list_of("virtual_network:vn2")
                                                     ("virtual_network:vn3");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Request iterate with deleted object
// The upper bound on the deleted uuid should return remaining valid entries
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_ReqIterate_Deleted) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> fq_name_expected_entries = list_of("virtual_network:vn1")
                                                     ("virtual_network:vn2")
                                                     ("virtual_network:vn3");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("00000000-0000-0000-0000-000000000001");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9")
               ("634ae160-d3ef-4e82-b58d-d196211eb4da")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Specific valid UUID
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_SpecificUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_uuid("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Specific UUID (invalid)
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_InvalidUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> obj_cache_expected_entries;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_uuid("deadbeef-dead-beef-dead-beefdeaddead");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Request iterate
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_ReqIterate) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e82-b58d-d196211eb4da")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db");

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Request iterate with deleted object
// upper_bound should return rest of the UUID in the list
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_ReqIterate_Deleted) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);

    string next_batch;
    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9")
               ("634ae160-d3ef-4e82-b58d-d196211eb4da")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db");

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("000000-0000-0000-0000-000000000001");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// In a multiple messages, adds (vn1, vn2), and vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInMultipleShots) {
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test01.1.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(2, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);

    // Verify that vn3 is still not added
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    // Resume events processing
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
}

// In a single message, adds vn1, vn2, vn3, then deletes, vn3, then adds vn4,
// vn5, then deletes vn5, vn4 and vn2. Only vn1 should remain.
TEST_F(ConfigJsonParserTest, ServerParser) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn4") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn5") == NULL);
}

// In 4 separate messages: 1) adds vn1, vn2, vn3, 2) deletes vn3, 3) adds vn4,
// vn5, 4) deletes vn5, vn4 and vn2. Only vn1 should remain.
// Same as ServerParser except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParserInParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(2, table->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(4, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn4")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn5")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    // Only vn1 should exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn4") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn5") == NULL);
}

// In a single message, adds vn1, vn2, vn3 and then deletes all of them.
TEST_F(ConfigJsonParserTest, ServerParser1) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In 2 separate messages, adds vn1, vn2, vn3 and then deletes all of them.
// Same as ServerParser1 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser1InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test1_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In a single message, adds vn1, vn2, vn3 in separate updateResult stanza's
// and then adds them again in a single stanza
TEST_F(ConfigJsonParserTest, ServerParser2) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test2.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 4 separate messages: 1) adds vn1, 2) adds vn2, 3) adds vn3 4) adds all of
// them again in a single stanza
// Same as ServerParser2 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser2InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test2_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(2, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
}

// In a single message, deletes vn1, vn2, vn3 in a deleteResult stanza and then
// deletes them again in a single stanza
TEST_F(ConfigJsonParserTest, ServerParser3) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test3.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In 2 separate messages, 1) deletes vn1, vn2, vn3 2) deletes them again
// Same as ServerParser3 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser3InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test3_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, ServerParser4) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test4.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") == NULL );
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 2 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete link(vr,vm)
// Same as ServerParser4 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser4InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test4_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(NodeLookup("virtual-router", "vr1"),
                NodeLookup("virtual-machine", "vm1"),
                "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(NodeLookup("virtual-router", "vr1"),
                NodeLookup("virtual-machine", "vm1"),
                "virtual-router-virtual-machine") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create link(vr,vm)         2) delete link(vr,vm)
// Both vr and vm nodes should get deleted since they dont have any properties
TEST_F(ConfigJsonParserTest, ServerParser5) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test5.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser6) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test6.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 2 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
// Same as ServerParser6 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser6InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test6_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);


    TASK_UTIL_EXPECT_TRUE(LinkLookup(NodeLookup("virtual-router", "vr1"),
                NodeLookup("virtual-machine", "vm1"),
                "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser7) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test7.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(NodeLookup("virtual-router", "vr1"),
                NodeLookup("virtual-machine", "vm1"),
                "virtual-router-virtual-machine") == NULL);
}

// In 3 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr and link(vr,vm)
// 3) add vr-with-properties
// Both vr and vm nodes should continue to live
// Same as ServerParser7 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser7InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test7_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") == NULL);
}

// In a single message:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
TEST_F(ConfigJsonParserTest, ServerParser9) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test9.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
}

// In 3 separate messages:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm)
// 3) add link(vr,vm)
// Both vr and vm nodes should continue to live
// Same as ServerParser9 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser9InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test9_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
TEST_F(ConfigJsonParserTest, ServerParser10) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test10.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 2 separate messages:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// The vr should disappear and vm should continue to live
// Similar to ServerParser6, except that in step2, we delete link and then vr
// Same as ServerParser10 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser10InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test10_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// vm nodes should continue to live but not vr
TEST_F(ConfigJsonParserTest, ServerParser11) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test11.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 3 separate messages:
// 1) create vr-with-properties, then vm-with-properties, then link(vr,vm)
// 2) delete link(vr,vm), then delete vr
// 3) add link(vr,vm)
// vm nodes should continue to live but not vr
TEST_F(ConfigJsonParserTest, ServerParser11InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test11_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    // vr1 should not have any object
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create link(vr,vm), then link(vr,gsc)
// 2) delete link(vr,vm), then link(vr,gsc)
// No nodes should exist.
TEST_F(ConfigJsonParserTest, ServerParser12) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test12.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,vm), then link(vr,gsc)
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser13) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test13.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("global-system-config", "gsc"),
        "global-system-config-virtual-router") == NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser14) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test14.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
}

// In 3 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete gsc, then link(vr,gsc)
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser14InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test14_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("global-system-config", "gsc"),
        "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(ConfigJsonParserTest, ServerParser15) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test15.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    // Object should not exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In 3 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete vr
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser15InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test15_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("global-system-config", "gsc"),
        "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());

    // Object should not exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->HasAdjacencies(
                &graph_));

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(ConfigJsonParserTest, ServerParser16) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test16.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Object should not exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// In 3 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(ConfigJsonParserTest, ServerParser16InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test16_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("global-system-config", "gsc"),
        "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}


//
// Validate the handling of object without type field
// Steps:
// 1. Add the VM object
// 2. Delete the VM object
// 3. Update the VM object without type field
//
TEST_F(ConfigJsonParserTest, MissingTypeField) {
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test17.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Delete the VM entry and send VM entry update with missing type field
    FeedEventsJson();

    // Verify that VM object is gone
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
}

//
// Validate the handling of object without fq-name field
// Steps:
// 1. Add the VM object
// 2. Update the VM object without fq-name field
// 3. Delete the VM object
//
TEST_F(ConfigJsonParserTest, MissingFQNameField) {
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
        "controller/src/ifmap/testdata/server_parser_test17_1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Update the VM entry with missing fq-name field
    FeedEventsJson();

    // Verify that VM object is gone
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);

    // Delete the VM entry
    FeedEventsJson();

    // Verify that delete of VM object which was assumed to be deleted is
    // handled well!!
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
