/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

// NO_HEAPCHECK=1 BUILD_ONLY=1 scons -uj32 --optimization=production src/bgp:bgp_ifmap_xmpp_integration_test && LOG_DISABLE=1 BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE=1 BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT=5910 BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME=overcloud-contrailcontroller-1 BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE=/cs-shared/db_dumps/orange.json build/production/bgp/test/bgp_ifmap_xmpp_integration_test
// Visit <server>:5910 to access introspect.

#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_sandesh_context.h"
#include <string>

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_ifmap_sandesh.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
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
#include "ifmap/ifmap_xmpp.h"
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

class BgpIfmapXmppIntegrationTest : public ::testing::Test {
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
    BgpIfmapXmppIntegrationTest() :
        thread_(&evm_),
        config_db_(new DB(TaskScheduler::GetInstance()->GetTaskId(
                              "db::IFMapTable"))),
        config_graph_(new DBGraph()),
        ifmap_server_(new IFMapServer(config_db_, config_graph_,
                                      evm_.io_service())),
        ifmap_sandesh_context_(new IFMapSandeshContext(ifmap_server_.get())),
        config_client_manager_(new ConfigClientManager(&evm_,
            "localhost", "config-test", config_options_)),
        bgp_sandesh_context_(new BgpSandeshContext()),
        validate_done_(false) {
        ifmap_server_->set_config_manager(config_client_manager_.get());
    }

    void SandeshSetup() {
        bgp_sandesh_context_->bgp_server = server_.get();
        bgp_sandesh_context_->xmpp_peer_manager = channel_manager_.get();
        RegisterSandeshShowIfmapHandlers(bgp_sandesh_context_.get());
        Sandesh::set_module_context("IFMap", ifmap_sandesh_context_.get());
        Sandesh::set_module_context("BGP", bgp_sandesh_context_.get());
        int port = 0;
        if (getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT")) {
            port = strtoul(getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT"),
                                  NULL, 0);
        }
        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
        Sandesh::InitGenerator("BgpIfmapXmppIntegrationTest", hostname,
                               "IFMapTest", "Test", &evm_, port,
                               bgp_sandesh_context_.get());
        std::cout << "Introspect at http://localhost:" << Sandesh::http_port()
            << std::endl;
    }

    void SandeshTearDown() {
        Sandesh::Uninit();
        task_util::WaitForIdle();
    }

    virtual void SetUp() {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
        IFMapLinkTable_Init(config_db_, config_graph_);
        ConfigJsonParser *config_json_parser =
         static_cast<ConfigJsonParser *>
            (config_client_manager_->config_json_parser());
        config_json_parser->ifmap_server_set(ifmap_server_.get());
        vnc_cfg_JsonParserInit(config_json_parser);
        vnc_cfg_Server_ModuleInit(config_db_, config_graph_);
        bgp_schema_JsonParserInit(config_json_parser);
        bgp_schema_Server_ModuleInit(config_db_, config_graph_);
        // "overcloud-contrailcontroller-1" for att db.
        string self = "nodea27";
        if (getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME"))
            self = getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME");
        server_.reset(new BgpServerTest(&evm_, self, config_db_,
                                        config_graph_));
        xmpp_server_test_ = new XmppServerTest(&evm_, "bgp.contrail.com");
        ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_test_,
                                    ifmap_server_.get()));
        ifmap_server_->set_ifmap_channel_manager(ifmap_channel_mgr_.get());
        channel_manager_.reset(new BgpXmppChannelManager(xmpp_server_test_,
            server_.get()));
        server_->session_manager()->Initialize(0);
        xmpp_server_test_->Initialize(0, false);
        cout << "BGP server started at port "
             << server_->session_manager()->GetPort() << endl;
        cout << "XMPP server started at port " << xmpp_server_test_->GetPort()
             << endl;
        SandeshSetup();

        thread_.Start();
        task_util::WaitForIdle();
    }

    void IFMapCleanUp() {
        ifmap_server_->Shutdown();
        task_util::WaitForIdle();
        IFMapLinkTable_Clear(config_db_);
        IFMapTable::ClearTables(config_db_);
        task_util::WaitForIdle();
        config_db_->Clear();
    }

    void TearDown() {
        xmpp_server_test_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, xmpp_server_test_->ConnectionCount());
        channel_manager_.reset();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_server_test_);
        xmpp_server_test_ = NULL;
        server_->Shutdown();
        task_util::WaitForIdle();
        SandeshTearDown();
        task_util::WaitForIdle();
        IFMapCleanUp();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void ListMapVmiVerifyCommon(const vector<string> expected_results,
            int property_id, uint64_t expected_vmi_table_count) {
        IFMapTable *domaintable = IFMapTable::FindTable(config_db_, "domain");
        IFMapTable *projecttable = IFMapTable::FindTable(config_db_, "project");
        IFMapTable *vmitable =
            IFMapTable::FindTable(config_db_, "virtual-machine-interface");

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
        return ifmap_test_util::IFMapNodeLookup(config_db_, type, name);
    }

    IFMapLink *LinkLookup(IFMapNode *left, IFMapNode *right,
                          const string &metadata) {
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            config_db_->FindTable("__ifmap_metadata__.0"));
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
    DB *config_db_;
    DBGraph *config_graph_;
    const ConfigClientOptions config_options_;
    boost::scoped_ptr<BgpServerTest> server_;
    XmppServerTest *xmpp_server_test_;
    boost::scoped_ptr<BgpXmppChannelManager> channel_manager_;
    boost::scoped_ptr<IFMapServer> ifmap_server_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    boost::scoped_ptr<IFMapChannelManager> ifmap_channel_mgr_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<BgpSandeshContext> bgp_sandesh_context_;
    bool validate_done_;
};

TEST_F(BgpIfmapXmppIntegrationTest, BulkSync) {
    bool default_config_file = true;
    if (getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE")) {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        ParseEventsJson(getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE"));
        default_config_file = false;
    } else {
        ParseEventsJson("controller/src/ifmap/client/testdata/bulk_sync.json");
    }
    FeedEventsJson();
    IFMapTable *table = IFMapTable::FindTable(config_db_, "virtual-network");
    if (default_config_file) {
        TASK_UTIL_EXPECT_EQ(14, table->Size());
    } else {
        TASK_UTIL_EXPECT_NE(0, table->Size());
    }
    if (getenv("BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE"))
        TASK_UTIL_EXEC_AND_WAIT(evm_, "/usr/bin/python");
    ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    BgpServerTest::GlobalSetUp();
    ConfigFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
    ConfigFactory::Register<ConfigCassandraPartition>(
        boost::factory<ConfigCassandraClientPartitionTest *>());
    ConfigFactory::Register<ConfigJsonParserBase>(
        boost::factory<ConfigJsonParser *>());
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
