/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

// NO_HEAPCHECK=1 BUILD_ONLY=1 scons -uj32 --optimization=production src/bgp:bgp_ifmap_xmpp_integration_test && LOG_DISABLE=1 CAT_CONTROL_NODE_TEST_PAUSE=1 CAT_CONTROL_NODE_TEST_INTROSPECT=5910 CAT_CONTROL_NODE_INTEGRATION_TEST_SELF_NAME=overcloud-contrailcontroller-1 CAT_CONTROL_NODE_TEST_DATA_FILE=/cs-shared/db_dumps/orange.json build/production/bgp/test/bgp_cat_control_node_test
// Visit <server>:5910 to access introspect.

#include "base/address_util.h"
#include "bgp/bgp_ifmap_sandesh.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "config-client-mgr/config_client_options.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/test/config_cassandra_client_test.h"
#include "io/process_signal.h"
#include "io/test/event_manager_test.h"
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include "schema/bgp_schema_types.h"
#include "signal.h"
#include "string"
#include "sys/stat.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_sandesh.h"

using namespace std;
using namespace autogen;
using boost::assign::list_of;
using process::Signal;

#include "config-client-mgr/test/config_cassandra_client_partition_test.h"

static EventManager evm_;
static bool reconfig_;
static boost::shared_ptr<TaskTrigger> reconfig_trigger_;

class BgpCatControlNodeTest : public ::testing::Test {
 public:
    bool ReconfigHandler() {
        reconfig_ = false;
        config_client_manager_->PostShutdown();
        ParseEventsJson(config_file_);
        FeedEventsJson();
        config_client_manager_->EndOfConfig();
        if (reconfig_)
            return false;
        return true;
    }

 protected:
    BgpCatControlNodeTest() :
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
        xmpp_sandesh_context_(new XmppSandeshContext()),
        validate_done_(false) {
        RegisterSandeshShowXmppExtensions(bgp_sandesh_context_.get());
        ifmap_server_->set_config_manager(config_client_manager_.get());
    }

    void SandeshSetup() {
        bgp_sandesh_context_->bgp_server = server_.get();
        bgp_sandesh_context_->xmpp_peer_manager = channel_manager_.get();
        RegisterSandeshShowIfmapHandlers(bgp_sandesh_context_.get());
        xmpp_sandesh_context_->xmpp_server = xmpp_server_test_;
        Sandesh::set_module_context("IFMap", ifmap_sandesh_context_.get());
        Sandesh::set_module_context("BGP", bgp_sandesh_context_.get());
        Sandesh::set_module_context("XMPP", xmpp_sandesh_context_.get());
        int port = 0;
        if (getenv("CAT_CONTROL_NODE_TEST_INTROSPECT")) {
            port = strtoul(getenv("CAT_CONTROL_NODE_TEST_INTROSPECT"),
                                  NULL, 0);
        }
        boost::system::error_code error;
        string hostname(boost::asio::ip::host_name(error));
        Sandesh::InitGenerator("BgpCatControlNodeTest", hostname,
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
        ifmap_server_->Initialize();
        // "overcloud-contrailcontroller-1" for att db.
        string self = "control-node";
        if (getenv("CAT_CONTROL_NODE_TEST_SELF_NAME"))
            self = getenv("CAT_CONTROL_NODE_TEST_SELF_NAME");
        server_.reset(new BgpServerTest(&evm_, self, config_db_,
                                        config_graph_));
        server_->set_peer_lookup_disable(true);
        xmpp_server_test_ = new XmppServerTest(&evm_, "bgp.contrail.com");
        ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_test_,
                                    ifmap_server_.get()));
        ifmap_server_->set_ifmap_channel_manager(ifmap_channel_mgr_.get());
        channel_manager_.reset(new BgpXmppChannelManager(xmpp_server_test_,
            server_.get()));

        string dir = getenv("USER_DIR") ?: "";
        int pid = getpid();

        SandeshSetup();

        string h = boost::lexical_cast<string>(Sandesh::http_port());
        string ufile = dir + "/conf/" + boost::lexical_cast<string>(pid) +
            ".json";

        int xmpp = 0, bgp_port = 0;
        xmpp = strtoul(getenv("CAT_XMPP_PORT") ?: "0", NULL, 0);
        bgp_port = strtoul(getenv("CAT_BGP_PORT") ?: "0", NULL, 0);
        boost::system::error_code ec;
        IpAddress addr = AddressFromString(
            getenv("CAT_BGP_IP_ADDRESS") ?: "0.0.0.0", &ec);
        server_->session_manager()->Initialize(bgp_port, addr);
        server_->session_manager()->Initialize(bgp_port);
        xmpp_server_test_->Initialize(xmpp, false);

        string b = boost::lexical_cast<string>(
            server_->session_manager()->GetPort());
        string x = boost::lexical_cast<string>(xmpp_server_test_->GetPort());
        string p = boost::lexical_cast<string>(pid);

        ofstream myfile;
        myfile.open (ufile.c_str());
        string data = "{ \"Pid\": " + p +
                      ", \"BGPPort\": " + b +
                      ", \"XMPPPort\": " + x +
                      ", \"HTTPPort\": " + h + "}";
        myfile << data;
        cout << data << endl;
        myfile.close();

        cout << "BGP server started at port "
             << server_->session_manager()->GetPort() << endl;
        cout << "XMPP server started at port " << xmpp_server_test_->GetPort()
             << endl;

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
        ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
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

    void ParseEventsJson(std::string events_file) {
        ConfigCassandraClientTest::ParseEventsJson(config_client_manager_.get(),
                events_file);
    }

    void FeedEventsJson() {
        ConfigCassandraClientTest::FeedEventsJson(config_client_manager_.get());
    }

    int GetVNTableSize(std::string conf_file) {
        std::ifstream ifs(conf_file.c_str());
        contrail_rapidjson::IStreamWrapper isw(ifs);

        contrail_rapidjson::Document d;
        d.ParseStream(isw);
        int num_vn = 0;

        const contrail_rapidjson::Value& vn = d["cassandra"]["config_db_uuid"]["obj_fq_name_table"]["virtual_network"];
        for (contrail_rapidjson::Value::ConstMemberIterator itr = vn.MemberBegin(); itr != vn.MemberEnd(); ++itr) {
            num_vn++;
        }
        return num_vn;
    }

    int GetVMTableSize(std::string conf_file) {
        std::ifstream ifs(conf_file.c_str());
        contrail_rapidjson::IStreamWrapper isw(ifs);

        contrail_rapidjson::Document d;
        d.ParseStream(isw);
        int num_vm = 0;

        const contrail_rapidjson::Value& vm = d["cassandra"]["config_db_uuid"]["obj_fq_name_table"]["virtual_machine"];
        for (contrail_rapidjson::Value::ConstMemberIterator itr = vm.MemberBegin(); itr != vm.MemberEnd(); ++itr) {
            num_vm++;
        }
        return num_vm;
    }

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
    boost::scoped_ptr<XmppSandeshContext> xmpp_sandesh_context_;
    bool validate_done_;
    string config_file_;
};

TEST_F(BgpCatControlNodeTest, BulkSync) {
    bool default_config_file = true;
    config_file_ = getenv("CAT_CONTROL_NODE_TEST_DATA_FILE") ?: "";
    if (getenv("CONTRAIL_CAT_FRAMEWORK")) {
        while (access(config_file_.c_str(), F_OK)) {
            sleep(3);
        }
    }
    if (!config_file_.empty() && !access(config_file_.c_str(), F_OK)) {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(false);
        default_config_file = false;
    } else {
        config_file_ = "controller/src/ifmap/client/testdata/bulk_sync.json";
    }
    ParseEventsJson(config_file_);
    FeedEventsJson();
    IFMapTable *vn_table = IFMapTable::FindTable(config_db_, "virtual-network");
    IFMapTable *vm_table = IFMapTable::FindTable(config_db_, "virtual-machine");
    if (default_config_file) {
        TASK_UTIL_EXPECT_EQ(14, vn_table->Size());
        TASK_UTIL_EXPECT_EQ(9, vm_table->Size());
    } else {
        TASK_UTIL_EXPECT_EQ(GetVNTableSize(config_file_), vn_table->Size());
        TASK_UTIL_EXPECT_EQ(GetVMTableSize(config_file_), vm_table->Size());
    }
    task_util::WaitForIdle();
    config_client_manager_->EndOfConfig();
    if (!getenv("CAT_CONTROL_NODE_TEST_PAUSE"))
        return;

    reconfig_trigger_.reset(new TaskTrigger(
        boost::bind(&BgpCatControlNodeTest::ReconfigHandler, this),
        TaskScheduler::GetInstance()->GetTaskId("config_client::Init"), 0));
    while(true) sleep(1000);
}

void ReConfigSignalHandler(const boost::system::error_code &error, int sig) {
    reconfig_ = true;
    if (reconfig_trigger_ && !reconfig_trigger_->IsSet())
        reconfig_trigger_->Set();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    BgpServerTest::GlobalSetUp();

    std::vector<Signal::SignalHandler> sighup_handlers = boost::assign::list_of
        (boost::bind(&ReConfigSignalHandler, _1, _2));
    Signal::SignalCallbackMap smap = boost::assign::map_list_of
        (SIGHUP, sighup_handlers)
    ;
    Signal signal(&evm_, smap);

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
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    int status = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return status;
}
