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
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"
#include "config_etcd_client_test.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using namespace autogen;
using boost::assign::list_of;

class ConfigEtcdJsonParserTest : public ::testing::Test {
public:
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
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            string json_str = resp->get_uuid_cache()[i].get_json_str();
            for (size_t j = 0; j < result.size(); ++j) {
                size_t match = json_str.find(result[j]);
                TASK_UTIL_EXPECT_NE(match, string::npos);
            }
            cout << resp->get_uuid_cache()[i].log() << endl;
        }
        validate_done_ = true;
    }

    void ValidateObjCacheResponseFieldRemoved(Sandesh *sandesh,
            const vector<string> &result, const string &next_batch) {
        const ConfigDBUUIDCacheResp *resp =
            dynamic_cast<const ConfigDBUUIDCacheResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        for (size_t i = 0; i < resp->get_uuid_cache().size(); ++i) {
            string json_str = resp->get_uuid_cache()[i].get_json_str();
            for (size_t j = 0; j < result.size(); ++j) {
                size_t match = json_str.find(result[j]);
                TASK_UTIL_EXPECT_EQ(match, string::npos);
            }
            cout << resp->get_uuid_cache()[i].log() << endl;
        }
        validate_done_ = true;
    }

protected:
    ConfigEtcdJsonParserTest() :
        thread_(&evm_),
        db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
        ifmap_server_(new IFMapServer(&db_,
                                      &graph_,
                                      evm_.io_service())),
        config_client_manager_(new ConfigClientManagerMock(
                                                  &evm_,
                                                  "localhost",
                                                  "config-test",
                                                  GetConfigOptions())),
        ifmap_sandesh_context_(new IFMapSandeshContext(ifmap_server_.get())),
        validate_done_(false) {

        // Instantiate ETCD test client
        config_etcd_client_ = dynamic_cast<ConfigEtcdClientTest *>(
                config_client_manager_->config_db_client());

        // Disable etcd watcher
        config_etcd_client_->set_watch_disable(true);

        // Link config client manager to ifmap_server
        ifmap_server_->set_config_manager(config_client_manager_.get());
    }

    ConfigClientOptions GetConfigOptions() {
        config_options_.use_etcd = true;
        return config_options_;
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
        Sandesh::InitGenerator("ConfigEtcdEtcdJsonParserTest", hostname, "IFMapTest",
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

        TASK_UTIL_EXPECT_EQ(1, domaintable->Size());
        TASK_UTIL_EXPECT_TRUE(NodeLookup("domain", "default-domain") != NULL);
        TASK_UTIL_EXPECT_TRUE(NodeLookup("domain", "default-domain")->Find(
                                  IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
        TASK_UTIL_EXPECT_EQ(1, projecttable->Size());
        TASK_UTIL_EXPECT_TRUE(
            NodeLookup("project", "default-domain:service") != NULL);
        TASK_UTIL_EXPECT_TRUE(
            NodeLookup("project", "default-domain:service")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
        TASK_UTIL_EXPECT_EQ(1, vmitable->Size());
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

    void ParseDatabase(std::string events_file) {
        EqlIfTest::ParseDatabase(events_file);
    }

    void ParseEventsJson(std::string events_file) {
        config_client_manager_->set_end_of_rib_computed(true);
        config_etcd_client_->ParseEventsJson(events_file);
    }

    void FeedEventsJson() {
        config_etcd_client_->FeedEventsJson();
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    ConfigClientOptions config_options_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<ConfigClientManagerMock> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    ConfigEtcdClientTest *config_etcd_client_;
    bool validate_done_;
};

TEST_F(ConfigEtcdJsonParserTest, BulkSync) {
    ParseDatabase("controller/src/ifmap/client/testdata/bulk_sync_etcd.json");
    config_client_manager_->Initialize();
    task_util::WaitForIdle();

    IFMapTable *bgpaas_table = IFMapTable::FindTable(&db_,
                                           "bgp-as-a-service");
    TASK_UTIL_EXPECT_EQ(1, bgpaas_table->Size());

    IFMapTable *vmi_table = IFMapTable::FindTable(&db_,
                                        "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(2, vmi_table->Size());

    IFMapTable *vn_table = IFMapTable::FindTable(&db_,
                                       "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, vn_table->Size());

    if (getenv("CONFIG_JSON_PARSER_TEST_INTROSPECT"))
        TASK_UTIL_EXEC_AND_WAIT(evm_, "/usr/bin/python");
    ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
}

// In a single message, adds vn1, vn2, vn3.
TEST_F(ConfigEtcdJsonParserTest, ServerParserAddInOneShot) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn2") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network",
                "default-domain:demo:vn3") != NULL);
}

// Verify introspect for Object cache
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Specific valid UUID
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache_SpecificUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_search_string("3ef-4e81");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Specific UUID (invalid)
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache_InvalidUUID) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReq *req = new ConfigDBUUIDCacheReq;
    req->set_search_string("deadbeef-dead-beef");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Request iterate
TEST_F(ConfigEtcdJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_uuid_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("ae160-d3||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigEtcdJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_obj_type_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("virtual_network||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(ConfigEtcdJsonParserTest,
        IntrospectVerify_ObjectCache_ReqIterate_fq_name_srch) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("vn||634ae160-d3ef-4e81-b58d-d196211eb4d9");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache - Request iterate with deleted object
// upper_bound should return rest of the UUID in the list
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache_ReqIterate_Deleted) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test01.json");
    FeedEventsJson();

    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(3, table->Size());

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
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponse, this,
        _1, obj_cache_expected_entries, next_batch));
    ConfigDBUUIDCacheReqIterate *req = new ConfigDBUUIDCacheReqIterate;
    req->set_uuid_info("634ae160||000000-0000-0000-0000-000000000001");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

// Verify introspect for Object cache field (ref, parent, prop) deleted
// from cache.
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache_Field_Deleted) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());
    ConfigDBUUIDCacheReq *req;
    string next_batch;

    ParseEventsJson(
        "controller/src/ifmap/testdata/etcd_server_parser_test16_p4.json");
    // feed vm1,vr1,gsc1 and vr1 ref vm1, vr1 parent is gsc1
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    usleep(500000);
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_expected_entries =
        list_of("global_system_config")
               ("8c5eeb87-0b08-4b0c-b53f-0a036805575c")
               ("virtual_machine_refs")
               ("id_perms");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldAdded, this,
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
        list_of("parent_type")
               ("global_system_config")
               ("8c5eeb87-0b08-4b0c-b53f-0a036805575c");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
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
    vector <string> obj_cache_not_expected_entries_1 = 
        list_of("virtual_machine_refs")
               ("8c5eeb87-0b08-4725-b53f-0a0368055375");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
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
        list_of("id_perms");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries_2, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("8c5eeb87-0b08-4724-b53f-0a0368055374");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}
/*
// Verify introspect for Object cache field (propm, propl) deleted from cache
TEST_F(ConfigEtcdJsonParserTest, IntrospectVerify_ObjectCache_Propm_PropL_Deleted) {
    ConfigDBUUIDCacheReq *req;
    string next_batch;
    IFMapTable *domaintable = IFMapTable::FindTable(&db_, "domain");
    TASK_UTIL_EXPECT_EQ(0, domaintable->Size());
    IFMapTable *projecttable = IFMapTable::FindTable(&db_, "project");
    TASK_UTIL_EXPECT_EQ(0, projecttable->Size());
    IFMapTable *vmitable = IFMapTable::FindTable(&db_,
                                                 "virtual-machine-interface");
    TASK_UTIL_EXPECT_EQ(0, vmitable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_vmi_list_map_prop_p1.json");
    // feed domain, project vmi
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(1, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmitable->Size());
    usleep(500000);
    validate_done_ = false;
    ifmap_sandesh_context_->set_page_limit(2);
    vector<string> obj_cache_expected_entries =
        list_of("virtual_machine_interface_bindings")
               ("host_id")
               ("vif_type")
               ("virtual_machine_interface_fat_flow_protocols")
               ("tcp")
               ("udp");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldAdded, this,
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
        list_of("vif_type")(
                "udp");
    Sandesh::set_response_callback(boost::bind(
        &ConfigEtcdJsonParserTest::ValidateObjCacheResponseFieldRemoved, this,
        _1, obj_cache_not_expected_entries, next_batch));
    req = new ConfigDBUUIDCacheReq;
    req->set_search_string("c4287577-b6af-4cca-a21d-6470a08af68a");
    req->HandleRequest();
    req->Release();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, domaintable->Size());
    TASK_UTIL_EXPECT_EQ(0, projecttable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmitable->Size());
}
*/
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigFactory::Register<ConfigEtcdClient>(
        boost::factory<ConfigEtcdClientTest *>());
    ConfigFactory::Register<etcd::etcdql::EtcdIf>(
        boost::factory<EqlIfTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParser *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
