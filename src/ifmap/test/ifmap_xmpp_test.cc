/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/timer_impl.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "ifmap/client/config_amqp_client.h"
#include "ifmap/client/config_cass2json_adapter.h"
#include "ifmap/client/config_cassandra_client.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/client/config_json_parser.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_exporter.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/ifmap_util.h"
#include "ifmap/ifmap_uuid_mapper.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/test/config_cassandra_client_test.h"
#include "ifmap/test/ifmap_xmpp_client_mock.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_client.h"
#include "testing/gunit.h"

#include <stdlib.h>
#include <iostream>
#include <fstream>

using namespace std;
using contrail_rapidjson::Document;
using contrail_rapidjson::SizeType;
using contrail_rapidjson::Value;

class XmppIfmapTest : public ::testing::Test {
protected:
    static const string kDefaultClientName;
    static const string kDefaultXmppServerAddress;
    static const string kDefaultXmppServerName;
    static const string kDefaultXmppServerConfigName;

    XmppIfmapTest()
         : thread_(&evm_),
           db_(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable")),
           ifmap_server_(&db_, &graph_, evm_.io_service()),
           config_client_manager_(new ConfigClientManager(&evm_,
               &ifmap_server_, "localhost", "config-test", config_options_)),
           ifmap_sandesh_context_(new IFMapSandeshContext(&ifmap_server_)),
           exporter_(ifmap_server_.exporter()),
           xmpp_server_(NULL), vm_uuid_mapper_(NULL) {
        config_cassandra_client_=dynamic_cast<ConfigCassandraClientTest *>(
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
        IFMap_Initialize();

        xmpp_server_ = new XmppServer(&evm_, kDefaultXmppServerName);
        xmpp_server_->Initialize(0, false);

        LOG(DEBUG, "Created Xmpp Server at port " << xmpp_server_->GetPort());
        ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_,
                                                         &ifmap_server_));
        ifmap_server_.set_ifmap_channel_manager(ifmap_channel_mgr_.get());
        thread_.Start();
    }

    virtual void TearDown() {
        ifmap_server_.Shutdown();
        task_util::WaitForIdle();

        IFMapTable::ClearTables(&db_);
        config_client_manager_->config_json_parser()->MetadataClear("vnc_cfg");
        task_util::WaitForIdle();

        db_.Clear();
        DB::ClearFactoryRegistry();

        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void IFMap_Initialize() {
        ConfigCass2JsonAdapter::set_assert_on_parse_error(true);
        IFMapLinkTable_Init(&db_, &graph_);
        vnc_cfg_JsonParserInit(config_client_manager_->config_json_parser());
        vnc_cfg_Server_ModuleInit(&db_, &graph_);
        bgp_schema_JsonParserInit(config_client_manager_->config_json_parser());
        bgp_schema_Server_ModuleInit(&db_, &graph_);
        SandeshSetup();
        vm_uuid_mapper_ = ifmap_server_.vm_uuid_mapper();
        ifmap_server_.Initialize();
        ifmap_server_.set_config_manager(config_client_manager_.get());
        config_client_manager_->EndOfConfig();
        task_util::WaitForIdle();
    }

    void ParseEventsJson (string events_file) {
        ConfigCassandraClientTest::ParseEventsJson(config_client_manager_.get(),
                events_file);
    }

    void FeedEventsJson () {
        ConfigCassandraClientTest::FeedEventsJson(config_client_manager_.get());
    }

    IFMapNode *TableLookup(const string &type, const string &name) {
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        if (tbl == NULL) {
            return NULL;
        }
        return tbl->FindNode(name);
    }

    IFMapLink *LinkLookup(IFMapNode *lhs, IFMapNode *rhs,
                          const string &metadata) {
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
                                     db_.FindTable("__ifmap_metadata__.0"));
        IFMapLink *link =  link_table->FindLink(metadata, lhs, rhs);
        return (link ? (link->IsDeleted() ? NULL : link) : NULL);
    }

    bool LinkOriginLookup(IFMapLink *link, IFMapOrigin::Origin orig) {
        return link->HasOrigin(orig);
    }

    bool NodeOriginLookup(IFMapNode *node, IFMapOrigin::Origin orig) {
        if (node->Find(orig) == NULL) {
            return false;
        } else {
            return true;
        }
    }

    void ConfigUpdate(XmppServer *server, const XmppConfigData *config) {
    }

    void ConfigUpdate(XmppClient *client, const XmppConfigData *config) {
        client->ConfigUpdate(config);
    }

    static void on_timeout(const boost::system::error_code &error,
                           bool *trigger) {
        if (error) {
            LOG(DEBUG, "Error is " << error.message());
            return;
        }
        *trigger = true;
    }

    // Usage of this causes the thread to be blocked at RunOnce()
    // waiting for an event to be triggered
    void EventWait(boost::function<bool()> condition, int timeout) {
        if (condition()) {
            return;
        }
        bool is_expired = false;
        TimerImpl timer(*evm_.io_service());
        boost::system::error_code ec;
        timer.expires_from_now(timeout * 1000, ec);
        timer.async_wait(boost::bind(&XmppIfmapTest::on_timeout, 
                         boost::asio::placeholders::error, &is_expired));
        while (!is_expired) {
            evm_.RunOnce();
            task_util::WaitForIdle();
            if ((condition)()) {
                timer.cancel(ec);
                break;
            }
        }
        ASSERT_TRUE((condition)());
    }

    int GetSentMsgs(XmppServer *server, const string &client_name) {
        IFMapXmppChannel *scli = ifmap_channel_mgr_->FindChannel(client_name);
        return ((scli == NULL) ? 0 : scli->msgs_sent());
    }

    IFMapClient *GetIfmapClientFromChannel(const string &client_name) {
        XmppConnection *connection = xmpp_server_->FindConnection(client_name);
        assert(connection);
        XmppChannel *xchannel = connection->ChannelMux();
        assert(xchannel);
        IFMapXmppChannel *ixchannel = ifmap_channel_mgr_->FindChannel(xchannel);
        assert(ixchannel);
        return ixchannel->Sender();
    }

    void QueueClientAddToIFMapServer(const string &client_name) {
        IFMapXmppChannel *scli = ifmap_channel_mgr_->FindChannel(client_name);
        scli->ProcessVrSubscribe(client_name);
    }

    void TriggerNotReady(const string &client_name) {
        IFMapXmppChannel *scli = ifmap_channel_mgr_->FindChannel(client_name);
        XmppChannel *channel = scli->channel();
        ifmap_channel_mgr_->ProcessChannelNotReady(channel);
    }

    void CheckLinkBits(DBGraphEdge *edge, int index, bool binterest,
                       bool badvertised) {
        IFMapLink *link = static_cast<IFMapLink *>(edge);
        IFMapLinkState *state = exporter_->LinkStateLookup(link);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
    }

    void CheckNodeBits(DBGraphVertex *vertex, int index, bool binterest,
                       bool badvertised) {
        IFMapNode *node = static_cast<IFMapNode *>(vertex);
        IFMapNodeState *state = exporter_->NodeStateLookup(node);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
    }

    void CheckClientBits(const string &client_name, size_t index,
                         bool binterest, bool badvertised) {
        IFMapNode *node = TableLookup("virtual-router", client_name);
        if (node) {
            IFMapNodeState *state = exporter_->NodeStateLookup(node);
            TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
            TASK_UTIL_EXPECT_TRUE(state->advertised().test(index)
                                  == badvertised);
            graph_.Visit(node,
                         boost::bind(&XmppIfmapTest::CheckNodeBits, this, _1,
                                     index, binterest, badvertised),
                         boost::bind(&XmppIfmapTest::CheckLinkBits, this, _1,
                                     index, binterest, badvertised));
        }
    }

    void CheckNodeBitsAndCount(DBGraphVertex *vertex, int index, bool binterest,
            bool badvertised, std::set<string> *visited_entries) {
        IFMapNode *node = static_cast<IFMapNode *>(vertex);
        IFMapNodeState *state = exporter_->NodeStateLookup(node);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
        visited_entries->insert(vertex->ToString());
    }

    void CheckLinkBitsAndCount(DBGraphEdge *edge, int index, bool binterest,
            bool badvertised, std::set<string> *visited_entries) {
        IFMapLink *link = static_cast<IFMapLink *>(edge);
        IFMapLinkState *state = exporter_->LinkStateLookup(link);
        TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
        TASK_UTIL_EXPECT_TRUE(state->advertised().test(index) == badvertised);
        visited_entries->insert(edge->ToString());
    }

    int ClientGraphWalkVerify(const string &client_name, size_t index,
                              bool binterest, bool badvertised) {
        std::set<string> visited_entries;
        IFMapNode *node = TableLookup("virtual-router", client_name);
        if (node) {
            IFMapNodeState *state = exporter_->NodeStateLookup(node);
            TASK_UTIL_EXPECT_TRUE(state->interest().test(index) == binterest);
            TASK_UTIL_EXPECT_TRUE(state->advertised().test(index)
                                  == badvertised);
            graph_.Visit(node,
                boost::bind(&XmppIfmapTest::CheckNodeBitsAndCount, this, _1,
                            index, binterest, badvertised, &visited_entries),
                boost::bind(&XmppIfmapTest::CheckLinkBitsAndCount, this, _1,
                            index, binterest, badvertised, &visited_entries),
                exporter_->get_traversal_white_list());
        }
        return visited_entries.size();
    }

    void SetObjectsPerMessage(int num) {
        ifmap_server_.sender()->SetObjectsPerMessage(num);
    }

    // This will also release the IFMapClient and IfmapXmppChannel. When the
    // channel goes down due to tcp-close, we will not find the
    // IFMapXmppChannel in channel_map_ and wont try to redo everything again.
    void TriggerDeleteClient(IFMapClient *client, string client_name) {
        IFMapXmppChannel *scli =ifmap_channel_mgr_->FindChannel(client_name);
        ifmap_server_.SimulateDeleteClient(client);
        ifmap_channel_mgr_->DeleteIFMapXmppChannel(scli);
    }

    void TriggerLinkDeleteToExporter(IFMapLink *link, IFMapNode *left,
                                     IFMapNode *right) {
        graph_.Unlink(link);
        link->MarkDelete();
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            db_.FindTable("__ifmap_metadata__.0"));
        DBTablePartBase *partition = link_table->GetTablePartition(0);
        exporter_->LinkTableExport(partition, link);
    }

    size_t InterestConfigTrackerSize(int index) {
        return exporter_->ClientConfigTrackerSize(IFMapExporter::INTEREST,
                                                  index);
    }

    EventManager evm_;
    ServerThread thread_;
    DB db_;
    DBGraph graph_;
    const IFMapConfigOptions config_options_;
    IFMapServer ifmap_server_;
    boost::scoped_ptr<ConfigClientManager> config_client_manager_;
    boost::scoped_ptr<IFMapSandeshContext> ifmap_sandesh_context_;
    ConfigCassandraClientTest *config_cassandra_client_;
    IFMapExporter *exporter_;
    XmppServer *xmpp_server_;
    auto_ptr<IFMapChannelManager> ifmap_channel_mgr_;
    IFMapVmUuidMapper *vm_uuid_mapper_;
};

const string XmppIfmapTest::kDefaultClientName = "phys-host-1";
const string XmppIfmapTest::kDefaultXmppServerAddress = "127.0.0.1";
const string XmppIfmapTest::kDefaultXmppServerName = "bgp.contrail.com";
const string XmppIfmapTest::kDefaultXmppServerConfigName =
    "bgp.contrail.com/config";

namespace {

static string GetUserName() {
    return string(getenv("LOGNAME"));
}

static XmppChannel* GetXmppChannel(XmppServer *server,
                                   const string &client_name) {
    XmppConnection *connection = server->FindConnection(client_name);
    if (connection) {
        return connection->ChannelMux();
    }
    return NULL;
}

static bool ServerIsEstablished(XmppServer *server, const string &client_name) {
    XmppConnection *connection = server->FindConnection(client_name);
    if (connection == NULL) {
        return false;
    }
    return (connection->GetStateMcState() == xmsm::ESTABLISHED); 
}

bool IsIFMapClientUnregistered(IFMapServer *ifmap_server,
                               const string &client_name) {
    if (ifmap_server->FindClient(client_name) == NULL) {
        return true;
    }
    return false;
}

TEST_F(XmppIfmapTest, Connection) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_connection.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);

    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->Has2Messages());
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    // verify ifmap_server client creation
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Create 2 client connections back2back with the same client name
TEST_F(XmppIfmapTest, CheckClientGraphCleanupTest) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_graph_cleanup_1.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->Has2Messages());
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    // verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    size_t cli_index = static_cast<size_t>(client->index());

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(client_name, cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);

    /////////////////////////////////////////////////////////
    // repeat the whole thing by creating one more connection
    /////////////////////////////////////////////////////////
 
    // create the mock client
    filename = "/tmp/" + GetUserName() + "_graph_cleanup_2.output";
    vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->Has2Messages());
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    // verify ifmap_server client creation
    client = ifmap_server_.FindClient(client_name);
    TASK_UTIL_EXPECT_TRUE(client != NULL);
    size_t cli_index1 = static_cast<size_t>(client->index());
    TASK_UTIL_EXPECT_EQ(cli_index1, cli_index);

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    vnsw_client->ConfigUpdate(new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, DISABLED_DeleteProperty) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection1.json");
    FeedEventsJson();

    // create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_delete_property.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    //subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->Has2Messages());
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    size_t cli_index = static_cast<size_t>(client->index());

    // Deleting one property
    // content = FileRead("controller/src/ifmap/testdata/vn_prop_del.xml");
    FeedEventsJson();
    TASK_UTIL_EXPECT_EQ(3, vnsw_client->Count());

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, VrVmSubUnsub) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_vr_vm_sub_unsub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(kDefaultClientName) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get all the nodes.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));


    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // After sending unsubscribe, client should get a delete for the vr-vm
    // link. The link should not have XMPP as origin anymore.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, VrVmSubUnsubTwice) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_vr_vm_sub_unsub_twice.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(kDefaultClientName);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // After sending unsubscribe, client should get a delete for the vr-vm
    // link. The link should not exist in the ctrl-node db anymore.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    // Send a subscribe-unsubscribe again.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    // We should download everything since Unsubscribe above would have reset
    // interest/advertised.
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// 3 consecutive subscribe requests with no unsubscribe in between
TEST_F(XmppIfmapTest, VrVmSubThrice) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_vr_vm_sub_thrice.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(kDefaultClientName);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;
    EXPECT_EQ(ifmap_channel_mgr_->get_duplicate_vmsub_messages(), 0);

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // 2nd spurious subscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    task_util::WaitForIdle();
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_duplicate_vmsub_messages(), 1);
    EXPECT_EQ(vnsw_client->HasNMessages(num_msgs), true);

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // 3rd spurious subscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_duplicate_vmsub_messages(), 2);
    EXPECT_EQ(vnsw_client->HasNMessages(num_msgs), true);

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // Unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// 1 subscribe followed by 3 consecutive unsubscribe requests
TEST_F(XmppIfmapTest, VrVmUnsubThrice) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_vr_vm_unsub_thrice.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(kDefaultClientName);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // Unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));
    EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novmsub_messages(), 0);

    // 2nd spurious unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs));
    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novmsub_messages(), 1);

    // 3rd spurious unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs));
    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novmsub_messages(), 2);

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// subscribe followed by connection close - no unsubscribe
TEST_F(XmppIfmapTest, VrVmSubConnClose) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    string unknown_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5baaa";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_vr_vm_sub_conn_close.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(kDefaultClientName);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    vnsw_client->OutputRecvBufferToFile();

    // unknown_vm_name does not exist in config.
    vnsw_client->SendVmConfigSubscribe(unknown_vm_name);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.vm_uuid_mapper()->PendingVmRegCount(), 1);
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.vm_uuid_mapper()->PendingVmRegCount(), 0);

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, RegBeforeConfig) {
    string host_vm_name = "aad4c946-9390-4a53-8bbd-09d346f5ba6c";
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_reg_before_config.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // The nodes should not exist since the config has not been read yet
    IFMapNode *vr = TableLookup("virtual-router", kDefaultClientName);
    EXPECT_TRUE(vr == NULL);
    IFMapNode *vm = TableLookup("virtual-machine", host_vm_name);
    EXPECT_TRUE(vm == NULL);

    // The parser has not been given the data yet and so nothing to download
    vnsw_client->SendVmConfigSubscribe(host_vm_name);
    usleep(1000);
    EXPECT_EQ(vnsw_client->Count(), 0);

    // The nodes should not exist since the config has not been read yet
    EXPECT_TRUE(TableLookup("virtual-router", kDefaultClientName) == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine", host_vm_name) == NULL);

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(kDefaultClientName);
    EXPECT_TRUE(client != NULL);
    size_t cli_index = static_cast<size_t>(client->index());

    // Give the read file to the parser
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    size_t num_msgs = vnsw_client->Count();
#endif
    FeedEventsJson();
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
#endif

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", kDefaultClientName)
                          != NULL);
    vr = TableLookup("virtual-router", kDefaultClientName);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine", host_vm_name) != NULL);
    vm = TableLookup("virtual-machine", host_vm_name);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm, "virtual-router-virtual-machine");
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    num_msgs = vnsw_client->Count();
#endif
    vnsw_client->SendVmConfigUnsubscribe(host_vm_name);
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->HasNMessages(num_msgs + 3));
#endif
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    TASK_UTIL_EXPECT_TRUE(
            LinkLookup(vr, vm, "virtual-router-virtual-machine") == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    vnsw_client->OutputRecvBufferToFile();

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, Cli1Vn1Vm3Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli1_vn1_vm3_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_cli1_vn1_vm3_add.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);

    // Allow sender to run and send all the config
    TASK_UTIL_EXPECT_EQ(34, vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    size_t cli_index = static_cast<size_t>(client->index());
    int walk_count = ClientGraphWalkVerify(client_name, cli_index, true, true);
    EXPECT_EQ(InterestConfigTrackerSize(client->index()), walk_count);
    EXPECT_EQ(InterestConfigTrackerSize(client->index()), 34);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, Cli1Vn2Np1Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli1_vn2_np1_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_cli1_vn2_np1_add.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    usleep(1000);

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe("ae85ef17-1bff-4303-b1a0-980e0e9b0705");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("fd6e78d3-a4fb-400f-94a7-c367c232a56c");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);

    // Allow sender to run and send all the config
    TASK_UTIL_EXPECT_EQ(36, vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());

    size_t cli_index = static_cast<size_t>(client->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn2_np1_add.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, Cli1Vn2Np2Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli1_vn2_np2_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_cli1_vn2_np2_add.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    usleep(1000);

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    vnsw_client->SendVmConfigSubscribe("695d391b-65e6-4091-bea5-78e5eae32e66");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("5f25dd5e-5442-4edf-89d1-6a318c0d213b");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);

    // Allow sender to run and send all the config
    TASK_UTIL_EXPECT_EQ(36, vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());

    size_t cli_index = static_cast<size_t>(client->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn2_np2_add.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, Cli2Vn2Np2Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli2_vn2_np2_add.json");
    FeedEventsJson();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename1("/tmp/" + GetUserName() + "_cli2_vn2_np2_add_s27.output");
    IFMapXmppClientMock *vnsw_cli1 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name1,
                                filename1);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    string filename2("/tmp/" + GetUserName() + "_cli2_vn2_np2_add_s28.output");
    IFMapXmppClientMock *vnsw_cli2 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name2,
                                filename2);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli2->IsEstablished());

    vnsw_cli1->RegisterWithXmpp();
    usleep(1000);
    vnsw_cli2->RegisterWithXmpp();
    usleep(1000);

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name1)
                          == true);
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name2)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) == NULL);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli2->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_cli1->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) != NULL);
    vnsw_cli1->SendVmConfigSubscribe("29fe5698-d04b-47ca-acf0-199b21c0a6ee");
    usleep(1000);
    vnsw_cli2->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) != NULL);
    vnsw_cli2->SendVmConfigSubscribe("39ed8f81-cf9c-4789-a118-e71f53abdf85");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_NE(0, vnsw_cli2->Count());
    IFMapClient *cli1 = ifmap_server_.FindClient(cli_name1);
    IFMapClient *cli2 = ifmap_server_.FindClient(cli_name2);
    EXPECT_TRUE(cli1 != NULL);
    EXPECT_TRUE(cli2 != NULL);

    // Allow senders to run and send all the config.
    TASK_UTIL_EXPECT_EQ(20, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(20, vnsw_cli2->Count());
    TASK_UTIL_EXPECT_EQ(cli2->msgs_sent(), vnsw_cli2->Count());

    size_t cli_index1 = static_cast<size_t>(cli1->index());
    size_t cli_index2 = static_cast<size_t>(cli2->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 2);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_cli1, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    ConfigUpdate(vnsw_cli2, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name1));
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name2));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_cli1->name(), cli_index1, false, false);
    CheckClientBits(vnsw_cli2->name(), cli_index2, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_cli1->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_np2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_np2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_cli1->UnRegisterWithXmpp();
    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
    vnsw_cli2->UnRegisterWithXmpp();
    vnsw_cli2->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli2);
    vnsw_cli2 = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(cli_name1);
    if (sconnection) {
        sconnection->Shutdown();
    }
    sconnection = xmpp_server_->FindConnection(cli_name2);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);

    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name1) == NULL);
    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name2) == NULL);
}

TEST_F(XmppIfmapTest, Cli2Vn2Vm2Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli2_vn2_vm2_add.json");
    FeedEventsJson();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename1("/tmp/" + GetUserName() + "_cli2_vn2_vm2_add_s27.output");
    IFMapXmppClientMock *vnsw_cli1 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name1,
                                filename1);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    string filename2("/tmp/" + GetUserName() + "_cli2_vn2_vm2_add_s28.output");
    IFMapXmppClientMock *vnsw_cli2 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name2,
                                filename2);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli2->IsEstablished());

    vnsw_cli1->RegisterWithXmpp();
    usleep(1000);
    vnsw_cli2->RegisterWithXmpp();
    usleep(1000);

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name1)
                          == true);
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name2)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) == NULL);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli2->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_cli1->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) != NULL);
    vnsw_cli1->SendVmConfigSubscribe("0af0866c-08c9-49ae-856b-0f4a58179920");
    usleep(1000);
    vnsw_cli2->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) != NULL);
    vnsw_cli2->SendVmConfigSubscribe("0d9dd007-b25a-4d86-bf68-dc0e85e317e3");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_NE(0, vnsw_cli2->Count());
    IFMapClient *cli1 = ifmap_server_.FindClient(cli_name1);
    IFMapClient *cli2 = ifmap_server_.FindClient(cli_name2);
    EXPECT_TRUE(cli1 != NULL);
    EXPECT_TRUE(cli2 != NULL);

    // Allow senders to run and send all the config
    TASK_UTIL_EXPECT_EQ(20, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(20, vnsw_cli2->Count());
    TASK_UTIL_EXPECT_EQ(cli2->msgs_sent(), vnsw_cli2->Count());

    size_t cli_index1 = static_cast<size_t>(cli1->index());
    size_t cli_index2 = static_cast<size_t>(cli2->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 2);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_cli1, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    ConfigUpdate(vnsw_cli2, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name1));
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name2));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_cli1->name(), cli_index1, false, false);
    CheckClientBits(vnsw_cli2->name(), cli_index2, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_cli1->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_vm2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_vm2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_cli1->UnRegisterWithXmpp();
    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
    vnsw_cli2->UnRegisterWithXmpp();
    vnsw_cli2->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli2);
    vnsw_cli2 = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(cli_name1);
    if (sconnection) {
        sconnection->Shutdown();
    }
    sconnection = xmpp_server_->FindConnection(cli_name2);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);

    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name1) == NULL);
    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name2) == NULL);
}

TEST_F(XmppIfmapTest, Cli2Vn3Vm6Np2Add) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add.json");
    FeedEventsJson();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename1("/tmp/" + GetUserName() +
                     "_cli2_vn3_vm6_np2_add_s27.output");
    IFMapXmppClientMock *vnsw_cli1 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name1,
                                filename1);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    string filename2("/tmp/" + GetUserName() +
                     "_cli2_vn3_vm6_np2_add_s28.output");
    IFMapXmppClientMock *vnsw_cli2 =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), cli_name2,
                                filename2);
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli2->IsEstablished());

    vnsw_cli1->RegisterWithXmpp();
    usleep(1000);
    vnsw_cli2->RegisterWithXmpp();
    usleep(1000);

    // server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name1)
                          == true);
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, cli_name2)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) == NULL);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) == NULL);
    // no config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(0, vnsw_cli2->Count());

    // Subscribe to config and wait until we receive atleast one msg before we
    // verify ifmap_server client creation
    vnsw_cli1->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name1) != NULL);

    vnsw_cli1->SendVmConfigSubscribe("7285b8b4-63e7-4251-8690-bbef70c2ccc1");
    usleep(1000);

    vnsw_cli1->SendVmConfigSubscribe("98e60d70-460a-4618-b334-1dbd6333e599");
    usleep(1000);

    vnsw_cli1->SendVmConfigSubscribe("7e87e01a-6847-4e24-b668-4a1ad24cef1c");
    usleep(1000);

    vnsw_cli1->SendVmConfigSubscribe("34a09a89-823a-4934-bf3d-f2cd9513e121");
    usleep(1000);

    vnsw_cli2->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(cli_name2) != NULL);

    vnsw_cli2->SendVmConfigSubscribe("2af8952f-ee66-444b-be63-67e8c6efaf74");
    usleep(1000);

    vnsw_cli2->SendVmConfigSubscribe("9afa046f-743c-42e0-ab63-2786a81d5731");
    usleep(1000);

    TASK_UTIL_EXPECT_NE(0, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_NE(0, vnsw_cli2->Count());
    IFMapClient *cli1 = ifmap_server_.FindClient(cli_name1);
    IFMapClient *cli2 = ifmap_server_.FindClient(cli_name2);
    EXPECT_TRUE(cli1 != NULL);
    EXPECT_TRUE(cli2 != NULL);

    // Allow senders to run and send all the config. GSC and NwIpam dups to cli1
    TASK_UTIL_EXPECT_EQ(52, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(28, vnsw_cli2->Count());
    TASK_UTIL_EXPECT_EQ(cli2->msgs_sent(), vnsw_cli2->Count());

    size_t cli_index1 = static_cast<size_t>(cli1->index());
    size_t cli_index2 = static_cast<size_t>(cli2->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 2);
    // client close generates a TcpClose event on server
    ConfigUpdate(vnsw_cli1, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    ConfigUpdate(vnsw_cli2, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name1));
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, cli_name2));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_cli1->name(), cli_index1, false, false);
    CheckClientBits(vnsw_cli2->name(), cli_index2, false, false);

    // Compare the contents of the received buffer with master_file_path
#ifdef IFMAP_XMPP_TEST_FLAKINESS_FIXED
    bool bresult = vnsw_cli1->OutputFileCompare(
      "controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
      "controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);
#endif

    vnsw_cli1->UnRegisterWithXmpp();
    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
    vnsw_cli2->UnRegisterWithXmpp();
    vnsw_cli2->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli2);
    vnsw_cli2 = NULL;

    //Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(cli_name1);
    if (sconnection) {
        sconnection->Shutdown();
    }
    sconnection = xmpp_server_->FindConnection(cli_name2);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);

    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name1) == NULL);
    EXPECT_TRUE(xmpp_server_->FindConnection(cli_name2) == NULL);
}

// Read config, then vm-sub followed by vm-unsub followed by close
TEST_F(XmppIfmapTest, CfgSubUnsub) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_cfg_reg_unreg.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run and send all the config
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    // Send unreg only for vm1 and vm2
    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);

    EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    // vm3 unreg is still pending. vr and vm3 should have xmpp origin.
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    size_t cli_index = static_cast<size_t>(client->index());

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Config-add followed by vm-reg followed by config-delete followed by vm-unreg
// followed by close
TEST_F(XmppIfmapTest, CfgAdd_Reg_CfgDel_Unreg) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() +
                    "_cfgadd_reg_cfgdel_unreg.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run and send all the config
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    // Delete the vr and all the vms via config
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_3vm_delete.xml"

    EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);;
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Vm-reg followed by config-add followed by config-delete followed by vm-unreg
// followed by close
TEST_F(XmppIfmapTest, Reg_CfgAdd_CfgDel_Unreg) {

    SetObjectsPerMessage(1);

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() +
                    "_reg_cfgadd_cfgdel_unreg.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run 
    usleep(1000);
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    EXPECT_EQ(vnsw_client->Count(), 0);

    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr == NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 == NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 == NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 == NULL);

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    EXPECT_EQ(0, vnsw_client->Count());
    EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    // Delete the vr and all the vms via config
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_3vm_delete.xml"

    EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);;
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Vm-reg followed by config-add followed by vm-unreg followed by config-delete
// followed by close
TEST_F(XmppIfmapTest, Reg_CfgAdd_Unreg_CfgDel) {

    SetObjectsPerMessage(1);

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() +
                    "_reg_cfgadd_unreg_cfgdel.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run 
    usleep(1000);
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    EXPECT_EQ(vnsw_client->Count(), 0);

    // Config not read yet
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr == NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 == NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 == NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 == NULL);

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    // Read the ifmap data from file and give it to the parser
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);;
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") == NULL);

    // Delete the vr and all the vms via config
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_3vm_delete.xml"

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Vm-reg followed by config-add followed by vm-unreg followed by close
TEST_F(XmppIfmapTest, Reg_CfgAdd_Unreg_Close) {

    SetObjectsPerMessage(1);

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_reg_cfgadd_unreg_close.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run 
    usleep(1000);
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    EXPECT_EQ(vnsw_client->Count(), 0);

    // Config not read yet
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr == NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    EXPECT_TRUE(vm1 == NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_TRUE(vm2 == NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_TRUE(vm3 == NULL);

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    // Read the ifmap data from file and give it to the parser
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm2, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") != NULL);
    link = LinkLookup(vr, vm3, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));

    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));

    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);;
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2, "virtual-router-virtual-machine") == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3, "virtual-router-virtual-machine") == NULL);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

//  vm-sub then cfg-add then vm-unsub then vm-sub then cfg-del then cfg-add and
//  vm-sub
TEST_F(XmppIfmapTest, CheckIFMapObjectSeqInList) {

    SetObjectsPerMessage(1);

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() +
                    "_check_ifmap_object_seq_in_list.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Allow sender to run 
    usleep(1000);
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    EXPECT_EQ(vnsw_client->Count(), 0);

    // Vm Subscribe
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() > 0);
    EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    EXPECT_EQ(vnsw_client->Count(), 0);
    vm_uuid_mapper_->PrintAllPendingVmRegEntries();

    // Read the ifmap data from file and give it to the parser
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add1.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 2);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_EQ(vm1->get_object_list_size(), 2);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();

    // Wait for the other 2 VMs just to sequence events
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_GE(3, vnsw_client->Count());

    // Vm Unsubscribe
    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vm1->get_object_list_size(), 1);
    EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // should receive vr-vm link delete and vm-delete since the link is gone
    TASK_UTIL_EXPECT_GE(5, vnsw_client->Count());

    // Vm Subscribe
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vr->get_object_list_size(), 2);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vm1->get_object_list_size(), 2);
    EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // should receive vr-vm link add and vm-add since the link was added
    TASK_UTIL_EXPECT_GE(7, vnsw_client->Count());

    // Delete the vr and all the vms via config
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_3vm_delete.xml"

    // Wait for the other 2 VMs just to sequence events
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_EQ(vm1->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // although the vr/vm are not 'marked' deleted, client will get updates for
    // them since the config-delete will trigger a change for the client.
    TASK_UTIL_EXPECT_GE(vnsw_client->Count(), 9);
    vm_uuid_mapper_->PrintAllUuidMapperEntries();
    vm_uuid_mapper_->PrintAllNodeUuidMappedEntries();

    // Vm Unsubscribe
    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") == NULL);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->UuidMapperCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->NodeUuidMapCount(), 0);
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    // Should get deletes for vr/vm and link(vr,vm)
    TASK_UTIL_EXPECT_GE(vnsw_client->Count(), 12);
    vm_uuid_mapper_->PrintAllUuidMapperEntries();
    vm_uuid_mapper_->PrintAllNodeUuidMappedEntries();

    // New cycle - the nodes do not exist right now
    
    // Read from config first
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_3vm_add.xml"

    // Wait for the other 2 VMs just to sequence events
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    vm1 = TableLookup("virtual-machine",
                      "2d308482-c7b3-4e05-af14-e732b7b50117");

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_EQ(vm1->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // Nothing should get downloaded until we receive the vm-sub
    EXPECT_EQ(vnsw_client->Count(), 12);
    vm_uuid_mapper_->PrintAllUuidMapperEntries();
    vm_uuid_mapper_->PrintAllNodeUuidMappedEntries();

    // Add the vm-sub 
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vr->get_object_list_size(), 2);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_EQ(vm1->get_object_list_size(), 2);
    EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // Should get adds for vr/vm and link(vr,vm)
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 15);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Get a READY and then a NOT_READY
TEST_F(XmppIfmapTest, ReadyNotready) {

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_ready_not_ready.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    // Wait until server is established and until we have an XmppChannel.
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    TASK_UTIL_EXPECT_TRUE(GetXmppChannel(xmpp_server_, client_name) != NULL);

    // Give a chance to others to run
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Give a chance to others to run
    usleep(1000);

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, Bug788) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    // Create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() + "_bug788.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    // Send vr-subscribe and wait until server processes it
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    usleep(1000);
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    EXPECT_EQ(vnsw_client->Count(), 0);

    // Send vm-subscribe for this vm. Object-list-size for vm1 should become 2
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);

    // vr
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_EQ(vr->get_object_list_size(), 2);
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));

    // vm1
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_EQ(vm1->get_object_list_size(), 2);
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));

    // link(vr, vm1)
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") != NULL);
    IFMapLink *link = LinkLookup(vr, vm1, "virtual-router-virtual-machine");
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::CASSANDRA));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    // No vm-config for this vm. Object-list-size should be 1 (config)
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_EQ(vm2->get_object_list_size(), 1);
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));

    // No vm-config for this vm. Object-list-size should be 1 (config)
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_EQ(vm3->get_object_list_size(), 1);
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::CASSANDRA));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    // We are sending one object/message. We will send vr, vm1 and link(vr,vm1)
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 3);

    // Simulate the condition where we get a link delete. Exporter has finished
    // processing it and before the sender sends the update, we process a 
    // client-delete
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    EXPECT_EQ(ifmap_server_.GetIndexMapSize(), 1);
    task_util::TaskSchedulerStop();
    TriggerLinkDeleteToExporter(link, vr, vm1);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1, "virtual-router-virtual-machine") == NULL);
    TriggerDeleteClient(client, client_name);
    task_util::TaskSchedulerStart();

    // Allow others to run
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetIndexMapSize(), 0);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// 2 consecutive vr subscribe requests with no unsubscribe in between
TEST_F(XmppIfmapTest, SpuriousVrSub) {
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() + "_spurious_vr_sub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();

    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);

    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    IFMapClient *client1 = ifmap_server_.FindClient(client_name);
    size_t index1 = client1->index();
    task_util::WaitForIdle();
    EXPECT_EQ(ifmap_channel_mgr_->get_duplicate_vrsub_messages(), 0);

    // Subscribe to config again to simulate a spurious vr-subscribe
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_duplicate_vrsub_messages(), 1);
    IFMapClient *client2 = ifmap_server_.FindClient(client_name);
    size_t index2 = client2->index();

    EXPECT_EQ(index1, index2);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, VmSubUnsubWithNoVrSub) {
    ParseEventsJson("controller/src/ifmap/testdata/two-vn-connection.json");
    FeedEventsJson();

    // Create the mock client
    string client_name(kDefaultClientName);
    string filename("/tmp/" + GetUserName() +
                    "_vm_subunsub_with_novrsub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();

    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);

    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendVmConfigSubscribe("aad4c946-9390-4a53-8bbd-09d346f5ba6c");
    usleep(1000);
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_vmsub_novrsub_messages(), 1);

    // Unsubscribe from config
    vnsw_client->SendVmConfigUnsubscribe(
            "aad4c946-9390-4a53-8bbd-09d346f5ba6c");
    usleep(1000);
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novrsub_messages(), 1);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Receive config and then VR-subscribe
TEST_F(XmppIfmapTest, ConfigVrsubVrUnsub) {
    ParseEventsJson("controller/src/ifmap/testdata/vr_gsc_config.json");
    FeedEventsJson();

    string client_name("vr1");
    string gsc_str("gsc1");

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc, "global-system-config-virtual-router");
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    // Create the mock client
    string filename("/tmp/" + GetUserName() + "_config_vrsub_vrunsub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Receive VR-subscribe and then config
TEST_F(XmppIfmapTest, VrsubConfigVrunsub) {

    string client_name("vr1");
    string gsc_str("gsc1");

    // Create the mock client
    string filename("/tmp/" + GetUserName() + "_vrsub_config_vrunsub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Read the ifmap data from file
    ParseEventsJson("controller/src/ifmap/testdata/vr_gsc_config.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc, "global-system-config-virtual-router");
    TASK_UTIL_EXPECT_TRUE(link != NULL);
    TASK_UTIL_EXPECT_GE(vnsw_client->Count(), 1);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Receive config where nodes have no properties and then VR-subscribe
// Then receive config where the nodes have properties
TEST_F(XmppIfmapTest, DISABLED_ConfignopropVrsub) {
    ParseEventsJson("controller/src/ifmap/testdata/vr_gsc_config_no_prop.json");
    FeedEventsJson();

    string client_name("vr1");
    string gsc_str("gsc1");

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, gsc,
            "global-system-config-virtual-router") != NULL);

    // Create the mock client
    string filename("/tmp/" + GetUserName() + "_config_noprop_vrsub.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->Count());

    // Now read the properties and another link update with no real change.
    // Client should receive one more message.
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_gsc_config.xml"
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

// Receive VR-subscribe and then config where nodes have no properties
// Then receive config where the nodes have properties
TEST_F(XmppIfmapTest, DISABLED_VrsubConfignoprop) {

    string client_name("vr1");
    string gsc_str("gsc1");

    // Create the mock client
    string filename("/tmp/" + GetUserName() + "_vrsub_config_noprop.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Read the ifmap data from file
    ParseEventsJson("controller/src/ifmap/testdata/vr_gsc_config_no_prop.json");
    FeedEventsJson();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc, "global-system-config-virtual-router");
    TASK_UTIL_EXPECT_TRUE(link != NULL);
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->Count());

    // Now read the properties and another link update with no real change.
    // Client should receive one more message.
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_gsc_config.xml"
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, DISABLED_NodePropertyChanges) {
    string client_name("vr1");

    // Create the mock client
    string filename("/tmp/" + GetUserName() + "_node_prop_changes.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());

    vnsw_client->RegisterWithXmpp();
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    // verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Read the ifmap data from file
    ParseEventsJson("controller/src/ifmap/testdata/vr_gsc_config_no_prop.json");
    FeedEventsJson();

    // subscribe to config
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vrnode = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vrnode != NULL);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 1);

    // Add the 'id-perms' property
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_with_1prop.xml"
    // Checks. Only 'id-perms' should be set.
    vrnode = TableLookup("virtual-router", client_name);
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    IFMapObject *obj = vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    autogen::VirtualRouter *vr = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vr !=NULL);
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 2);

    // Add 'id-perms' and 'display-name' to the vrnode
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_with_2prop.xml"
    // Checks. 'id-perms' and 'display-name' should be set.
    vrnode = TableLookup("virtual-router", client_name);
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    vr = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vr !=NULL);
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 3);

    // Remove 'display-name' from the vrnode
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_del_1prop.xml"
    // Checks. Only 'id-perms' should be set.
    vrnode = TableLookup("virtual-router", client_name);
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    vr = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vr !=NULL);
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 4);

    // Add 'id-perms' and 'display-name' to the vrnode
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_with_2prop.xml"
    db_.SetQueueDisable(false);
    task_util::WaitForIdle();
    // Checks. 'id-perms' and 'display-name' should be set.
    vrnode = TableLookup("virtual-router", client_name);
    ASSERT_TRUE(vrnode != NULL);
    EXPECT_TRUE(vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA)) != NULL);
    obj = vrnode->Find(IFMapOrigin(IFMapOrigin::CASSANDRA));
    ASSERT_TRUE(obj != NULL);
    vr = dynamic_cast<autogen::VirtualRouter *>(obj);
    ASSERT_TRUE(vr !=NULL);
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::ID_PERMS));
    EXPECT_TRUE(vr->IsPropertySet(autogen::VirtualRouter::DISPLAY_NAME));
    EXPECT_FALSE(vr->IsPropertySet(autogen::VirtualRouter::IP_ADDRESS));
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 5);

    // Remove both properties from the vrnode
    FeedEventsJson(); // "controller/src/ifmap/testdata/vr_del_2prop.xml"
    db_.SetQueueDisable(false);
    task_util::WaitForIdle();
    // Checks. The node should exist since it has a neighbor. But, the object
    // should be gone since all the properties are gone.
    vrnode = TableLookup("virtual-router", client_name);
    ASSERT_TRUE(vrnode != NULL);
    TASK_UTIL_ASSERT_TRUE(vrnode->GetObject() == NULL);
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 6);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);
    vnsw_client = NULL;

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }

    TASK_UTIL_EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);
}

TEST_F(XmppIfmapTest, DeleteClientPendingVmregCleanup) {
    SetObjectsPerMessage(1);
    ParseEventsJson("controller/src/ifmap/testdata/vr_3vm_add.json");
    FeedEventsJson();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    string filename("/tmp/" + GetUserName() +
                    "_cfgadd_reg_cfgdel_unreg.output");
    IFMapXmppClientMock *vnsw_client =
        new IFMapXmppClientMock(&evm_, xmpp_server_->GetPort(), client_name,
                                filename);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // verify ifmap_server client is not created until config subscribe
    EXPECT_TRUE(ifmap_server_.FindClient(client_name) == NULL);
    // no config messages sent until config subscribe
    EXPECT_EQ(0, vnsw_client->Count());

    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);

    // Send a VM subscribe for a VM that does not exist in the config. This
    // should create a pending vm-reg entry.
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
    vnsw_client->SendVmConfigSubscribe("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 1);

    // Cleanup the client. This should clean up the pending vm-reg list too.
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    vnsw_client->UnRegisterWithXmpp();
    vnsw_client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_client);

    // Delete xmpp-channel explicitly
    XmppConnection *sconnection = xmpp_server_->FindConnection(client_name);
    if (sconnection) {
        sconnection->Shutdown();
    }
    TASK_UTIL_EXPECT_EQ(xmpp_server_->ConnectionCount(), 0);
    EXPECT_TRUE(xmpp_server_->FindConnection(client_name) == NULL);

    // The pending vm-reg should be cleaned up when the client dies
    TASK_UTIL_EXPECT_EQ(vm_uuid_mapper_->PendingVmRegCount(), 0);
}

}

static void SetUp() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
    ConfigAmqpClient::set_disable(true);
    IFMapFactory::Register<ConfigCassandraClient>(
        boost::factory<ConfigCassandraClientTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
