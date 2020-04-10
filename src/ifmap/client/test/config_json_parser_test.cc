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
#include "config-client-mgr/config_client_options.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
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
using namespace autogen;
using boost::assign::list_of;

#include "config-client-mgr/test/config_cassandra_client_partition_test.h"

class ConfigJsonParserTest : public ::testing::Test {
 public:
    void ValidateFQNameCacheResponse(Sandesh *sandesh,
            const vector<string> &result, const string &next_batch) {
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
            const vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDCacheResp *resp =
            dynamic_cast<const ConfigDBUUIDCacheResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_uuid_cache().size());
        TASK_UTIL_EXPECT_EQ(next_batch, resp->get_next_batch());
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            string result_match = resp->get_uuid_cache()[i].get_uuid() +
                '/' + resp->get_uuid_cache()[i].get_fq_name();
            TASK_UTIL_EXPECT_EQ(result[i], result_match);
            cout << resp->get_uuid_cache()[i].log() << endl;
        }
        validate_done_ = true;
    }

    void ValidateObjCacheResponseFieldAdded(Sandesh *sandesh,
            const vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDCacheResp *resp =
            dynamic_cast<const ConfigDBUUIDCacheResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        set<string> setResult;
        for (size_t i = 0; i < result.size(); i++) {
            setResult.insert(result[i]);
        }
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            for (size_t j = 0;
                 j < resp->get_uuid_cache()[i].get_field_list().size(); j++) {
                string key =
                    resp->get_uuid_cache()[i].get_field_list()[j].field_name;
                set<string>::iterator it = setResult.find(key);
                if (it != setResult.end()) {
                   setResult.erase(it);
                }
            }
            cout << resp->get_uuid_cache()[i].log() << endl;
        }
        TASK_UTIL_EXPECT_TRUE(setResult.empty());
        validate_done_ = true;
    }

    void ValidateObjCacheResponseFieldRemoved(Sandesh *sandesh,
            const vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDCacheResp *resp =
            dynamic_cast<const ConfigDBUUIDCacheResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        set<string> setResult;
        for (size_t i = 0; i < result.size(); i++) {
            setResult.insert(result[i]);
        }
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            for (size_t j = 0;
                 j < resp->get_uuid_cache()[i].get_field_list().size(); j++) {
                string key =
                    resp->get_uuid_cache()[i].get_field_list()[j].field_name;
                set<string>::iterator it = setResult.find(key);
                TASK_UTIL_EXPECT_TRUE(it == setResult.end());
            }
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
            "localhost", "config-test", config_options_)),
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

        ConfigJsonParser *config_json_parser =
         static_cast<ConfigJsonParser *>
            (config_client_manager_->config_json_parser());
        config_json_parser->ifmap_server_set(ifmap_server_.get());
        vnc_cfg_JsonParserInit(config_json_parser);
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_json_parser);
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
        ConfigJsonParser *config_json_parser =
         static_cast<ConfigJsonParser *>
            (config_client_manager_->config_json_parser());
        config_json_parser->MetadataClear("vnc_cfg");
        SandeshTearDown();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void ListMapVmiVerifyCommon(const vector<string> expected_results,
            int property_id, uint64_t expected_vmi_table_count) {
        IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
        IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
        IFMapTable *vmitable =
            IFMapTable::FindTable(&db_, "virtual-machine-interface");

        TASK_UTIL_EXPECT_EQ(1U, domaintable->Size());
        TASK_UTIL_EXPECT_TRUE(NodeLookup("domain", "default-domain") != NULL);
        TASK_UTIL_EXPECT_TRUE(NodeLookup("domain", "default-domain")->Find(
                                  IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
        TASK_UTIL_EXPECT_EQ(1U, projecttable->Size());
        TASK_UTIL_EXPECT_TRUE(
            NodeLookup("project", "default-domain:service") != NULL);
        TASK_UTIL_EXPECT_TRUE(
            NodeLookup("project", "default-domain:service")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
        TASK_UTIL_EXPECT_EQ(1U, vmitable->Size());
        TASK_UTIL_EXPECT_TRUE(
            NodeLookup(
                "virtual-machine-interface",
                "default-domain:service:c4287577-b6af-4cca-a21d-6470a08af68a")
                    != NULL);
        VirtualMachineInterface *vmi =
            reinterpret_cast<VirtualMachineInterface *>(NodeLookup(
            "virtual-machine-interface",
            "default-domain:service:"
            "c4287577-b6af-4cca-a21d-6470a08af68a")->Find(
                    IFMapOrigin(IFMapOrigin::CASSANDRA)));
        TASK_UTIL_EXPECT_TRUE(vmi != NULL);
        if (property_id == VirtualMachineInterface::BINDINGS) {
            if (vmi->IsPropertySet(VirtualMachineInterface::BINDINGS)) {
                std::vector<KeyValuePair> bindings = vmi->bindings();
                for (uint32_t i = 0; i < bindings.size(); i++) {
                    cout << "Entry: " << i << " Key: " << bindings[i].key
                        << " Value: " << bindings[i].value;
                    cout << endl;
                }
                TASK_UTIL_EXPECT_EQ(expected_results.size(), bindings.size());
                for (size_t i = 0; i < bindings.size(); i++) {
                    string result_match = bindings[i].key + ':'
                        + bindings[i].value;
                    TASK_UTIL_EXPECT_EQ(expected_results[i], result_match);
                }
            } else {
                TASK_UTIL_EXPECT_TRUE(expected_results.size() == 0);
            }
       } else if (property_id == VirtualMachineInterface::FAT_FLOW_PROTOCOLS) {
            if (vmi->IsPropertySet(
                 VirtualMachineInterface::FAT_FLOW_PROTOCOLS)) {
                 std::vector<ProtocolType> fat_flow_protocols =
                     vmi->fat_flow_protocols();
                for (uint32_t i = 0; i < fat_flow_protocols.size(); i++) {
                     cout << "Entry: " << i << " Protocol " <<
                         fat_flow_protocols[i].protocol << " Port: "
                         << fat_flow_protocols[i].port;
                     cout << endl;
                }
                TASK_UTIL_EXPECT_EQ(expected_results.size(),
                                    fat_flow_protocols.size());
                for (size_t i = 0; i < fat_flow_protocols.size(); i++) {
                     string result_match = fat_flow_protocols[i].protocol + ':'
                        + integerToString(fat_flow_protocols[i].port);
                     TASK_UTIL_EXPECT_EQ(expected_results[i], result_match);
                }
            }
        } else {
            TASK_UTIL_EXPECT_TRUE(0);
        }
        cout << "vmitable input count:" << vmitable->input_count() << endl;
        TASK_UTIL_EXPECT_EQ(expected_vmi_table_count, vmitable->input_count());
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

    ConfigCassandraClientPartitionTest *GetConfigCassandraPartition(
            const string uuid) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager_.get()->config_db_client());
        return(dynamic_cast<ConfigCassandraClientPartitionTest *>
                (config_cassandra_client->GetPartition(uuid)));
    }

    int GetConfigCassandraPartitionInstanceId(string uuid ) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager_.get()->config_db_client());
        return(config_cassandra_client->GetPartition(uuid)->GetInstanceId());
    }

    uint32_t GetConfigCassandraPartitionUUIDReadRetryCount(string uuid ) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager_.get()->config_db_client());
        ConfigCassandraClientPartitionTest *config_cassandra_partition =
            dynamic_cast<ConfigCassandraClientPartitionTest *>(
                    config_cassandra_client->GetPartition(uuid));
        return(config_cassandra_partition->GetUUIDReadRetryCount(uuid));
    }

    void SetUUIDRetryTimeInMSec(string uuid, int time) {
        ConfigCassandraClientTest *config_cassandra_client =
            dynamic_cast<ConfigCassandraClientTest *>(
                    config_client_manager_.get()->config_db_client());
        ConfigCassandraClientPartitionTest *config_cassandra_partition =
            dynamic_cast<ConfigCassandraClientPartitionTest *>(
                    config_cassandra_client->GetPartition(uuid));
        config_cassandra_partition->SetRetryTimeInMSec(time);
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    const ConfigClientOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    bool validate_done_;
};

typedef std::tuple<string, bool> TestParamsFilesAndFlag;

class ConfigJsonParserTestWithParams1
    : public ConfigJsonParserTest,
      public ::testing::WithParamInterface<TestParamsFilesAndFlag> {
};
// Flag indicates timestamp in testdata
INSTANTIATE_TEST_CASE_P(
    JsonParser1, ConfigJsonParserTestWithParams1, ::testing::Values(
        std::make_tuple(
            "controller/src/ifmap/testdata/vmi_map_prop_with_ts.json",
            true),
        std::make_tuple(
            "controller/src/ifmap/testdata/vmi_map_prop.json",
            false)));

class ConfigJsonParserTestWithParams2
    : public ConfigJsonParserTest,
      public ::testing::WithParamInterface<TestParamsFilesAndFlag> {
};

INSTANTIATE_TEST_CASE_P(
    JsonParser2, ConfigJsonParserTestWithParams2, ::testing::Values(
        std::make_tuple(
            "controller/src/ifmap/testdata/vmi_list_prop_with_ts.json",
            true),
        std::make_tuple(
            "controller/src/ifmap/testdata/vmi_list_prop.json",
            false)));

class ConfigJsonParserTestWithParams3
    : public ConfigJsonParserTest,
      public ::testing::WithParamInterface<TestParamsFilesAndFlag> {
};

INSTANTIATE_TEST_CASE_P(JsonParser3, ConfigJsonParserTestWithParams3,
        ::testing::Values(std::make_tuple(
            "controller/src/ifmap/testdata/vmi_list_map_prop_with_ts.json",
            true),
            std::make_tuple(
                "controller/src/ifmap/testdata/vmi_list_map_prop.json",
                false)));

TEST_F(ConfigJsonParserTest, BulkSync) {
    if (getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE")) {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        ParseEventsJson(getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE"));
    } else {
        ParseEventsJson("controller/src/ifmap/client/testdata/bulk_sync.json");
    }
    FeedEventsJson();
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_NE(0U, table->Size());
    if (getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
        TASK_UTIL_EXEC_AND_WAIT(evm_, "/usr/bin/python");
    ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
}

// In a single message, adds vn1, vn2, vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInOneShot) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);
}

// Verify introspect for FQName cache
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn1")
        ("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(3);
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache when we get only an UPDATE
// as the first operation for a UUID without a CREATE.
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_UpdateWithoutCreate) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.3.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn1")
        ("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(3);
    string next_batch;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    validate_done_ = false;
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache when we get only an UPDATE
// as the first operation for a UUID followed by a CREATE.
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_UpdateBeforeCreate) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.4.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn1")
        ("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(3);
    string next_batch;
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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn1");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->set_search_string("634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Given invalid uuid
TEST_F(ConfigJsonParserTest, IntrospectVerify_FQNameCache_InvalidUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries;
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReq *req = new ConfigDBUUIDToFQNameReq;
    req->set_search_string("deadbeef-dead-beef-dead");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for FQName cache - Request iterate
TEST_F(ConfigJsonParserTest,
        IntrospectVerify_FQNameCache_ReqIterate_uuid_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("d196211||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigJsonParserTest,
        IntrospectVerify_FQNameCache_ReqIterate_obj_type_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("virtual_network||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigJsonParserTest,
        IntrospectVerify_FQNameCache_ReqIterate_fq_name_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn2")
        ("virtual_network:default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
        _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("vn||634ae160-d3ef-4e81-b58d-d196211eb4d9");
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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> fq_name_expected_entries =
        list_of("virtual_network:default-domain:demo:vn1")
        ("virtual_network:default-domain:demo:vn2");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch = "160-d3ef-||634ae160-d3ef-4e82-b58d-d196211eb4da";
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateFQNameCacheResponse, this,
    _1, fq_name_expected_entries, next_batch));
    ConfigDBUUIDToFQNameReqIterate *req = new ConfigDBUUIDToFQNameReqIterate;
    req->set_uuid_info("160-d3ef-||00000000-0000-0000-0000-000000000001");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9/default-domain:demo:vn1")
               ("634ae160-d3ef-4e82-b58d-d196211eb4da/default-domain:demo:vn2")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db/default-domain:demo:vn3");
    ifmap_sandesh_context_->set_page_limit(3);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9/default-domain:demo:vn1");
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;
    validate_done_ = false;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_search_string("3ef-4e81");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Specific UUID (invalid)
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_InvalidUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    ifmap_sandesh_context_->set_page_limit(2);
    validate_done_ = false;
    string next_batch;
    vector<string> obj_cache_expected_entries;
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_search_string("deadbeef-dead-beef");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Request iterate
TEST_F(ConfigJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_uuid_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e82-b58d-d196211eb4da/default-domain:demo:vn2")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db/default-domain:demo:vn3");
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("ae160-d3||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_obj_type_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e82-b58d-d196211eb4da/default-domain:demo:vn2")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db/default-domain:demo:vn3");
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("virtual_network||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_fq_name_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e82-b58d-d196211eb4da/default-domain:demo:vn2")
               ("634ae160-d3ef-4e83-b58d-d196211eb4db/default-domain:demo:vn3");
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    string next_batch;

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("vn||634ae160-d3ef-4e81-b58d-d196211eb4d9");
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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);

    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_expected_entries =
        list_of("634ae160-d3ef-4e81-b58d-d196211eb4d9/default-domain:demo:vn1")
               ("634ae160-d3ef-4e82-b58d-d196211eb4da/default-domain:demo:vn2");
    string next_batch = "634ae160||634ae160-d3ef-4e82-b58d-d196211eb4da";

    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("634ae160||000000-0000-0000-0000-000000000001");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}
// Verify introspect for Object cache field (ref, parent, prop) deleted
// from cache.
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_Field_Deleted) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());
    ConfigDBUUIDCacheReq *req;
    string next_batch;

    ParseEventsJson(
        "controller/src/ifmap/testdata/server_parser_test16_p4.json");
    // feed vm1,vr1,gsc1 and vr1 ref vm1, vr1 parent is gsc1
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    usleep(500000);
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_expected_entries =
        list_of("parent:global_system_config:"
                "8c5eeb87-0b08-4b0c-b53f-0a036805575c")
               ("ref:virtual_machine:8c5eeb87-0b08-4725-b53f-0a0368055375")
               ("prop:id_perms");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldAdded, this,
        _1, obj_cache_expected_entries, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("8c5eeb87-0b08-4724-b53f-0a0368055374");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    // feed remove vr1 parent
    FeedEventsJson();
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_not_expected_entries =
        list_of("parent:global_system_config:"
                "8c5eeb87-0b08-4b0c-b53f-0a036805575c");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("8c5eeb87-0b08-4724-b53f-0a0368055374");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    // feed vr1 update, without ref
    FeedEventsJson();
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector <string> obj_cache_not_expected_entries_1 = list_of(
        "ref:virtual_machine:8c5eeb87-0b08-4725-b53f-0a0368055375");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries_1, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("8c5eeb87-0b08-4724-b53f-0a0368055374");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    // feed vr1 update, without id_perms
    FeedEventsJson();
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_not_expected_entries_2 =
        list_of("prop:id_perms");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries_2, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("8c5eeb87-0b08-4724-b53f-0a0368055374");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}
// Verify introspect for Object cache field (propm, propl) deleted from cache
TEST_F(ConfigJsonParserTest, IntrospectVerify_ObjectCache_Propm_PropL_Deleted) {
    ConfigDBUUIDCacheReq *req;
    string next_batch;
    IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    IFMapTable *vmitable = IFMapTable::FindTable(&db_,
                                                 "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/vmi_list_map_prop_p1.json");
    // feed domain, project vmi
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(1U, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmitable->Size());
    usleep(500000);
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_expected_entries =
        list_of("propm:virtual_machine_interface_bindings:host_id")(
                "propm:virtual_machine_interface_bindings:vif_type")(
                "propl:virtual_machine_interface_fat_flow_protocols:1")(
                "propl:virtual_machine_interface_fat_flow_protocols:2");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldAdded, this,
        _1, obj_cache_expected_entries, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("c4287577-b6af-4cca-a21d-6470a08af68a");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    // remove one propm and one proml
    FeedEventsJson();
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_not_expected_entries =
        list_of("propm:virtual_machine_interface_bindings:vif_type")(
                "propl:virtual_machine_interface_fat_flow_protocols:2");
    Sandesh::set_response_callback(boost::bind(
        &ConfigJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("c4287577-b6af-4cca-a21d-6470a08af68a");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());
}
// In a multiple messages, adds (vn1, vn2), and vn3.
TEST_F(ConfigJsonParserTest, ServerParserAddInMultipleShots) {
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test01.1.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(2U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);

    // Verify that vn3 is still not added
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    // Resume events processing
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
}

// In a single message, adds vn1, vn2, vn3, then deletes, vn3, then adds vn4,
// vn5, then deletes vn5, vn4 and vn2. Only vn1 should remain.
TEST_F(ConfigJsonParserTest, ServerParser) {
    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(1U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(2U, table->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(4U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn4")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn5")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, table->Size());
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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(2U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") != NULL);
}

// In a single message, deletes vn1, vn2, vn3 in a deleteResult stanza and then
// deletes them again in a single stanza
TEST_F(ConfigJsonParserTest, ServerParser3) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test3.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In 2 separate messages, 1) deletes vn1, vn2, vn3 2) deletes them again
// Same as ServerParser3 except that the various operations are happening in
// separate messages.
TEST_F(ConfigJsonParserTest, ServerParser3InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test3_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, table->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test4.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") == NULL);
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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test4_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test5.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
}

// In a single message:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
TEST_F(ConfigJsonParserTest, DISABLED_ServerParser6) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test6.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test6_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test7.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test7_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test9.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test9_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
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
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test10.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test10_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test11.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test11_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test12.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test13.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test14.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test14_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test15.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test15_p1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test16.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    // Object should not exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);

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
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test16_p1.json");
    FeedEventsJson();
    FeedEventsJson();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "gsc:vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);

    // Using datafile from test13_p2
    FeedEventsJson();
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                 IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "gsc:vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("global-system-config", "gsc"),
        NodeLookup("virtual-router", "gsc:vr1"),
                   "global-system-config-virtual-router") != NULL);

    // Need new datafile for step 3
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// Verify out of order add sequence for ref and parent
// In 3 separate messages:
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,gsc), then delete gsc, then delete vr
TEST_F(ConfigJsonParserTest, ServerParser17InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test16_p2.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    string uuid = "8c5eeb87-0b08-4724-b53f-0a0368055374";
    SetUUIDRetryTimeInMSec(uuid, 100);
    task_util::TaskFire(boost::bind(
                &ConfigCassandraClientPartitionTest::FireUUIDReadRetryTimer,
                GetConfigCassandraPartition(uuid), uuid),
                "config_client::Reader",
                GetConfigCassandraPartitionInstanceId(uuid));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1U,
                        GetConfigCassandraPartitionUUIDReadRetryCount(uuid));
    FeedEventsJson();
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U,
                        GetConfigCassandraPartitionUUIDReadRetryCount(uuid));
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                 IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "gsc:vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("global-system-config", "gsc"),
        NodeLookup("virtual-router", "gsc:vr1"),
                   "global-system-config-virtual-router") != NULL);

    FeedEventsJson();
    FeedEventsJson();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// Verify out of order delete sequence for ref and parent
TEST_F(ConfigJsonParserTest, ServerParser18InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test18.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, gsctable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                 IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vrtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("virtual-router", "gsc:vr1"),
        NodeLookup("virtual-machine", "vm1"),
        "virtual-router-virtual-machine") != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(
        NodeLookup("global-system-config", "gsc"),
        NodeLookup("virtual-router", "gsc:vr1"),
                   "global-system-config-virtual-router") != NULL);

    FeedEventsJson();
    FeedEventsJson();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0U, gsctable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// Verify that Draft objects are ignored
TEST_F(ConfigJsonParserTest, ServerParserDraftObject) {
    IFMapTable *pmtable = IFMapTable::FindTable(&db_, "policy-management");
    TASK_UTIL_EXPECT_EQ(0U, pmtable->Size());
    IFMapTable *frtable = IFMapTable::FindTable(&db_, "firewall-rule");
    TASK_UTIL_EXPECT_EQ(0U, frtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/server_parser_test19.json");
    // Create the firewall-rule draft object with draft-mode-state 'created',
    //  draft-policy-management and normal-policy-management objects.
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "draft-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);
    TASK_UTIL_EXPECT_EQ(2U, pmtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "draft-policy-management") != NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "draft-policy-management")->Find(
        IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "normal-policy-management") != NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "normal-policy-management")->Find(
        IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Update the firewall-rule draft object with draft-mode-state "updated".
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "draft-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);

    // Delete the firewall-rule draft object and re-add as normal object with
    // a new parent, giving it a new FQName.
    FeedEventsJson();
    task_util::WaitForIdle();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4")
        != NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4")->Find(
        IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Delete the policy management objects and firewall-rule object.
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0U, pmtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "draft-policy-management") == NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "normal-policy-management") == NULL);
    TASK_UTIL_EXPECT_EQ(0U, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);
}

// TODO(pdsouza) This test currently does not verify missing type field
// Validate the handling of object without type field
// Steps:
// 1. Add the VM object
// 2. Delete the VM object
// 3. Update the VM object without type field
//
TEST_F(ConfigJsonParserTest, MissingTypeField) {
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/server_parser_test17.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Delete the VM entry and send VM entry update with missing type field
    FeedEventsJson();

    // Verify that VM object is gone
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
}

TEST_P(ConfigJsonParserTestWithParams1, VerifyMapProperties) {
    string file = std::get<0>(GetParam());
    bool is_timestamp_test = std::get<1>(GetParam());
    uint64_t vmitable_step_count = is_timestamp_test ? 1 : 3;
    uint64_t vmitable_count;
    IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    IFMapTable *vmitable = IFMapTable::FindTable(&db_,
                                                 "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());

    ParseEventsJson(file);

    // Add 3 propm vmi bindings
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results1 = list_of("host_id:site.one.com")
                                              ("vif_type:vrouter")
                                              ("vnic_type:normal");
    vmitable_count = 3;
    ListMapVmiVerifyCommon(expected_results1, VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Update first propm vmi binding
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results2 = list_of("host_id:site.two.com")
                                              ("vif_type:vrouter")
                                              ("vnic_type:normal");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results2, VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Remove second propm vmi binding
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results3 = list_of("host_id:site.two.com")
                                              ("vnic_type:normal");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results3, VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Remove all propm vmi bindings
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results4;
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results4, VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Add back 2 propm vmi bindings
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results5 = list_of("host_id:site.three.com")
                                              ("vif_type:vrouter");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results5, VirtualMachineInterface::BINDINGS,
                           vmitable_count);
    // Update one and replace one  propm vmi binding
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results6 = list_of("host_id:site.four.com")
                                              ("vnic_type:normal");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results6, VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // No change - 2 propm vmi bindings
    FeedEventsJson();
    task_util::WaitForIdle();
    vmitable_count += is_timestamp_test ? 0 : vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results6, VirtualMachineInterface::BINDINGS,
                           vmitable_count);
    // Remove all config
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());
    cout << "vmitable input count:" <<  vmitable->input_count() << endl;
    vmitable_count += 3;
    TASK_UTIL_EXPECT_EQ(vmitable_count, vmitable->input_count());
}

TEST_P(ConfigJsonParserTestWithParams2, VerifyListProperties) {
    string file = std::get<0>(GetParam());
    bool is_timestamp_test = std::get<1>(GetParam());
    uint64_t vmitable_step_count = is_timestamp_test?1:3;
    uint64_t vmitable_count;
    IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    IFMapTable *vmitable = IFMapTable::FindTable(&db_,
                                                 "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());

    ParseEventsJson(file);

    // Add 4 propl fat flow protocol entires
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results1 =
        list_of("TCP:80") ("TCP:443") ("UDP:80") ("UDP:443");
    vmitable_count = 3;
    ListMapVmiVerifyCommon(expected_results1,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);

    // Update first propl fat flow protocol entry
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results2 =
        list_of("TCP:90") ("TCP:443") ("UDP:80") ("UDP:443");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results2,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    // Remove second propl fat flow protocol entry
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results3 =
        list_of("TCP:90") ("UDP:80") ("UDP:443");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results3,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);

    // Remove all propl fat flow protocol entires
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results4;
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results4,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);

    // Add back 2  propl fat flow protocol entires
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results5 =
        list_of("TCP:100") ("TCP:543");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results5,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);

    // Update one and replace one  propl fat flow protocol entry
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_results6 =
        list_of("TCP:110") ("UDP:80");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results6,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);

    // No change - 2  propl fat flow protocol entries
    FeedEventsJson();
    task_util::WaitForIdle();
    vmitable_count += is_timestamp_test?0:vmitable_step_count;
    ListMapVmiVerifyCommon(expected_results6,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    // Remove all config
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());
    cout << "vmitable input count:" <<  vmitable->input_count() << endl;
    vmitable_count += 3;
    TASK_UTIL_EXPECT_EQ(vmitable_count, vmitable->input_count());
}

TEST_P(ConfigJsonParserTestWithParams3, VerifyListMapProperties) {
    string file = std::get<0>(GetParam());
    bool is_timestamp_test = std::get<1>(GetParam());
    uint64_t vmitable_step_count = is_timestamp_test?2:4;
    uint64_t vmitable_count;
    IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    IFMapTable *vmitable = IFMapTable::FindTable(&db_,
                                                 "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());

    ParseEventsJson(file);

    // Add 2 propl/propm entires each
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_results1 =
        list_of("TCP:443")("UDP:80");
    vmitable_count = 4;
    ListMapVmiVerifyCommon(expected_list_results1,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    vector<string> expected_map_results1 =
        list_of("host_id:site.one.com") ("vif_type:vrouter");
    ListMapVmiVerifyCommon(expected_map_results1,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Update first of each propl/propl entries
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_results2 =
        list_of("TCP:543")("UDP:80");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_results2,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    vector<string> expected_map_results2 =
        list_of("host_id:site.two.com")("vif_type:vrouter");
    ListMapVmiVerifyCommon(expected_map_results2,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Remove second of each propl/propl entries
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_results3 = list_of("TCP:543");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_results3,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    vector<string> expected_map_results3 = list_of("host_id:site.two.com");
    ListMapVmiVerifyCommon(expected_map_results3,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Remove all propl/propl entries
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_map_results4;
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_map_results4,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    ListMapVmiVerifyCommon(expected_list_map_results4,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Add back 2  propl/propm entires each
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_results5 =
        list_of("TCP:643")("UDP:90");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_results5,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    vector<string> expected_map_results5 =
        list_of("host_id:site.three.com")("vif_type:vrouter");
    ListMapVmiVerifyCommon(expected_map_results5,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Update one and replace one propl/propm entry each
    FeedEventsJson();
    task_util::WaitForIdle();
    vector<string> expected_list_results6 =
        list_of("TCP:743")("UDP:400");
    vmitable_count += vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_results6,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    vector<string> expected_map_results6 =
        list_of("host_id:site.four.com")("vnic_type:normal");
    ListMapVmiVerifyCommon(expected_map_results6,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // No change - 2 propl/propm entires each
    FeedEventsJson();
    task_util::WaitForIdle();
    vmitable_count += is_timestamp_test?0:vmitable_step_count;
    ListMapVmiVerifyCommon(expected_list_results6,
                           VirtualMachineInterface::FAT_FLOW_PROTOCOLS,
                           vmitable_count);
    ListMapVmiVerifyCommon(expected_map_results6,
                           VirtualMachineInterface::BINDINGS,
                           vmitable_count);

    // Remove all config
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0U, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(0U, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(0U, vmitable->Size());
    cout << "vmitable input count:" <<  vmitable->input_count() << endl;
    vmitable_count += 4;
    TASK_UTIL_EXPECT_EQ(vmitable_count, vmitable->input_count());
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
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());

    ParseEventsJson(
        "controller/src/ifmap/testdata/server_parser_test17_1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1U, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Update the VM entry with missing fq-name field
    FeedEventsJson();

    // Verify that VM object is gone
    TASK_UTIL_EXPECT_EQ(0U, vmtable->Size());
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
    ConfigFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    ConfigFactory::Register<ConfigCassandraPartition>(
        boost::factory<ConfigCassandraClientPartitionTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParser *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
