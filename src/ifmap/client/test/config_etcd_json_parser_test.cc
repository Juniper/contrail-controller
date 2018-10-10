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
#include "config_etcd_client_test.h"
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

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"

using namespace std;
using namespace autogen;
using boost::assign::list_of;

class ConfigEtcdPartitionTest : public ConfigEtcdPartition {
public:
    static const int kUUIDReadRetryTimeInMsec = 300000;
    ConfigEtcdPartitionTest(ConfigEtcdClient *client, size_t idx)
        : ConfigEtcdPartition(client, idx),
        retry_time_ms_(kUUIDReadRetryTimeInMsec) {
    }

    virtual int UUIDRetryTimeInMSec(const UUIDCacheEntry *obj) const {
        return retry_time_ms_;
    }

    void SetRetryTimeInMSec(int time) {
        retry_time_ms_ = time;
    }

    uint32_t GetUUIDReadRetryCount(string &uuid) {
        string uuid_key = uuid.substr(uuid.rfind('/') + 1);
        const UUIDCacheEntry *obj = GetUUIDCacheEntry(uuid_key);
        if (obj)
            return (obj->GetRetryCount());
        return 0;
    }

    void RestartTimer(UUIDCacheEntry *obj, string &uuid, 
                      string &value) {
        obj->GetRetryTimer()->Cancel();
        obj->GetRetryTimer()->Start(10,
                boost::bind(
                        &ConfigEtcdPartition::UUIDCacheEntry::
                        EtcdReadRetryTimerExpired,
                        obj, uuid, value),
                boost::bind(
                        &ConfigEtcdPartition::UUIDCacheEntry::
                        EtcdReadRetryTimerErrorHandler,
                        obj));
    }

    void FireUUIDReadRetryTimer(string &uuid, string &value) {
        string uuid_key = uuid.substr(uuid.rfind('/') + 1);
        UUIDCacheEntry *obj = GetUUIDCacheEntry(uuid_key);
        if (obj) {
            if (obj->IsRetryTimerCreated()) {
                RestartTimer(obj, uuid, value);
            }
        }
    }

private:
    int retry_time_ms_;
};

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
        config_options_.config_db_use_etcd = true;
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

    string GetJsonValue(const string &uuid) {
        string val = config_etcd_client_->GetJsonValue(uuid);
        return val;
    }

    ConfigEtcdPartitionTest *GetConfigEtcdPartition(
            string &uuid) {
        ConfigEtcdClientTest *config_etcd_client =
            dynamic_cast<ConfigEtcdClientTest *>(
                    config_client_manager_.get()->config_db_client());
        return(dynamic_cast<ConfigEtcdPartitionTest *>
                (config_etcd_client->GetPartition(uuid)));
    }

    int GetConfigEtcdPartitionInstanceId(string &uuid) {
        ConfigEtcdClientTest *config_etcd_client =
            dynamic_cast<ConfigEtcdClientTest *>(
                    config_client_manager_.get()->config_db_client());
        return(config_etcd_client->GetPartition(uuid)->GetInstanceId());
    }

    uint32_t GetConfigEtcdPartitionUUIDReadRetryCount(string uuid) {
        ConfigEtcdClientTest *config_etcd_client =
            dynamic_cast<ConfigEtcdClientTest *>(
                    config_client_manager_.get()->config_db_client());
        ConfigEtcdPartitionTest *config_etcd_partition =
            dynamic_cast<ConfigEtcdPartitionTest *>(
                    config_etcd_client->GetPartition(uuid));
        return(config_etcd_partition->GetUUIDReadRetryCount(uuid));
    }

    void SetUUIDRetryTimeInMSec(string uuid, int time) {
        ConfigEtcdClientTest *config_etcd_client =
            dynamic_cast<ConfigEtcdClientTest *>(
                    config_client_manager_.get()->config_db_client());
        ConfigEtcdPartitionTest *config_etcd_partition =
            dynamic_cast<ConfigEtcdPartitionTest *>(
                    config_etcd_client->GetPartition(uuid));
        config_etcd_partition->SetRetryTimeInMSec(time);
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
    if (getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE")) {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        ParseDatabase(getenv("CONFIG_JSON_PARSER_TEST_DATA_FILE"));
    } else {
        ParseDatabase("controller/src/ifmap/client/testdata/bulk_sync_etcd.json");
    }
    config_client_manager_->Initialize();
    task_util::WaitForIdle();

    IFMapTable *vn_table = IFMapTable::FindTable(&db_,
                                       "virtual-network");
    TASK_UTIL_EXPECT_NE(0, vn_table->Size());

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
               ("TCP")
               ("UDP");
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
                "UDP");
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

// In a multiple messages, adds (vn1, vn2), and vn3.
TEST_F(ConfigEtcdJsonParserTest, ServerParserAddInMultipleShots) {
    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test01.1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser) {
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParserInParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser1) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test1.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In 2 separate messages, adds vn1, vn2, vn3 and then deletes all of them.
// Same as ServerParser1 except that the various operations are happening in
// separate messages.
TEST_F(ConfigEtcdJsonParserTest, ServerParser1InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test1_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser2) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test2.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser2InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test2_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser3) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test3.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn2") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-network", "vn3") == NULL);
}

// In 2 separate messages, 1) deletes vn1, vn2, vn3 2) deletes them again
// Same as ServerParser3 except that the various operations are happening in
// separate messages.
TEST_F(ConfigEtcdJsonParserTest, ServerParser3InParts) {
    IFMapTable *table = IFMapTable::FindTable(&db_, "virtual-network");
    TASK_UTIL_EXPECT_EQ(0, table->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test3_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser4) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test4.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

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
TEST_F(ConfigEtcdJsonParserTest, ServerParser4InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test4_p1.json");
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
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties
// 2) delete vr, then link(vr,vm)
// The vr should disappear and vm should continue to live
TEST_F(ConfigEtcdJsonParserTest, ServerParser6) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test6.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser6InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test6_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser7) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test7.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser7InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test7_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser9) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test9.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser9InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test9_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser10) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test10.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser10InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test10_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser11) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test11.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser11InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test11_p1.json");
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
// 1) create link(vr,vm), then vr-with-properties, then vm-with-properties,
// 2) create link(vr,gsc), then gsc-with-properties
// 3) delete link(vr,vm), then link(vr,gsc)
TEST_F(ConfigEtcdJsonParserTest, DISABLED_ServerParser13) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test13.json");
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
TEST_F(ConfigEtcdJsonParserTest, DISABLED_ServerParser14) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test14.json");
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
TEST_F(ConfigEtcdJsonParserTest, DISABLED_ServerParser14InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test14_p1.json");
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser15) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test15.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    // Object should not exist
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "vr1") == NULL);
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
TEST_F(ConfigEtcdJsonParserTest, ServerParser16) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test16.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

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
TEST_F(ConfigEtcdJsonParserTest, ServerParser16InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    // Using datafile from test13_p1
    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test16_p1.json");
    FeedEventsJson();
    FeedEventsJson();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

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
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

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
TEST_F(ConfigEtcdJsonParserTest, ServerParser17InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test16_p2.json");
    FeedEventsJson();
    task_util::WaitForIdle();
    string uuid = "/UPDATE/virtual_router/8c5eeb87-0b08-4724-b53f-0a0368055374";
    string value = GetJsonValue(uuid);
    SetUUIDRetryTimeInMSec(uuid, 100);
    task_util::TaskFire(boost::bind(
                &ConfigEtcdPartitionTest::FireUUIDReadRetryTimer,
                GetConfigEtcdPartition(uuid), uuid, value),
                "etcd::Reader",
                GetConfigEtcdPartitionInstanceId(uuid));
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, GetConfigEtcdPartitionUUIDReadRetryCount(uuid));
    FeedEventsJson();
    FeedEventsJson();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, GetConfigEtcdPartitionUUIDReadRetryCount(uuid));
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());

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
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// Verify out of order delete sequence for ref and parent
TEST_F(ConfigEtcdJsonParserTest, ServerParser18InParts) {
    IFMapTable *vrtable = IFMapTable::FindTable(&db_, "virtual-router");
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    IFMapTable *gsctable = IFMapTable::FindTable(&db_, "global-system-config");
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test18.json");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vmtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1")->Find(
                IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, gsctable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") != NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc")->Find(
                 IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, vrtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0, vrtable->Size());
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());
    TASK_UTIL_EXPECT_EQ(0, gsctable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-router", "gsc:vr1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("virtual-machine", "vm1") == NULL);
    TASK_UTIL_EXPECT_TRUE(NodeLookup("global-system-config", "gsc") == NULL);
}

// Verify that Draft objects are ignored
TEST_F(ConfigEtcdJsonParserTest, ServerParserDraftObject) {
    IFMapTable *pmtable = IFMapTable::FindTable(&db_, "policy-management");
    TASK_UTIL_EXPECT_EQ(0, pmtable->Size());
    IFMapTable *frtable = IFMapTable::FindTable(&db_, "firewall-rule");
    TASK_UTIL_EXPECT_EQ(0, frtable->Size());

    ParseEventsJson(
            "controller/src/ifmap/testdata/etcd_server_parser_test19.json");
    // Create the firewall-rule draft object with draft-mode-state 'created',
    //  draft-policy-management and normal-policy-management objects.
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "draft-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);
    TASK_UTIL_EXPECT_EQ(2, pmtable->Size());
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
    TASK_UTIL_EXPECT_EQ(0, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "draft-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);

    // Delete the firewall-rule draft object and re-add as normal object with
    // a new parent, giving it a new FQName.
    FeedEventsJson();
    task_util::WaitForIdle();
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(1, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4")
        != NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4")->Find(
        IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);

    // Delete the policy management objects and firewall-rule object.
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(0, pmtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "draft-policy-management") == NULL);
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("policy-management", "normal-policy-management") == NULL);
    TASK_UTIL_EXPECT_EQ(0, frtable->Size());
    TASK_UTIL_EXPECT_TRUE(
        NodeLookup("firewall-rule",
        "normal-policy-management:3438e2fa-bfcd-4745-bd58-52713ae27ac4") ==
        NULL);
}

// Validate the handling of object without type field
// Steps:
// 1. Add the VM object
// 2. Delete the VM object
// 3. Update the VM object without type field
//
TEST_F(ConfigEtcdJsonParserTest, MissingTypeField) {
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson("controller/src/ifmap/testdata/etcd_server_parser_test17.json");
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

// Validate the handling of object without fq-name field
// Steps:
// 1. Add the VM object
// 2. Update the VM object without fq-name field
// 3. Delete the VM object
//
TEST_F(ConfigEtcdJsonParserTest, MissingFQNameField) {
    IFMapTable *vmtable = IFMapTable::FindTable(&db_, "virtual-machine");
    TASK_UTIL_EXPECT_EQ(0, vmtable->Size());

    ParseEventsJson(
        "controller/src/ifmap/testdata/etcd_server_parser_test17_1.json");
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
    ConfigFactory::Register<ConfigEtcdClient>(
        boost::factory<ConfigEtcdClientTest *>());
    ConfigFactory::Register<ConfigEtcdPartition>(
        boost::factory<ConfigEtcdPartitionTest *>());
    ConfigFactory::Register<etcd::etcdql::EtcdIf>(
        boost::factory<EqlIfTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParser *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
