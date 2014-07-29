/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap/ifmap_exporter.h"

#include "base/util.h"
#include "base/logging.h"
#include "base/bitset.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_server.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "db/db.h"
#include "db/db_graph.h"
#include "io/event_manager.h"
#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_link.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server_table.h"
#include "ifmap/ifmap_update.h"
#include "ifmap/ifmap_update_queue.h"
#include "ifmap/ifmap_update_sender.h"
#include "ifmap/ifmap_uuid_mapper.h"
#include "ifmap/ifmap_xmpp.h"
#include "schema/vnc_cfg_types.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"

#include <pugixml/pugixml.hpp>
#include "xml/xml_pugi.h"

#include "xmpp/xmpp_state_machine.h"
#include "xmpp/xmpp_channel.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_proto.h"
#include "xml/xml_pugi.cc"

#include "testing/gunit.h"
#include "xmpp/test/xmpp_test_util.h"

#include <iostream>
#include <fstream>
using namespace boost::asio;
using namespace std;

#define SRV_ADDR                "127.0.0.1"
#define CLI_ADDR                "phys-host-1"
#define XMPP_CONTROL_SERV       "bgp.contrail.com"
#define XMPP_CONTROL_SERV_CFG   "bgp.contrail.com/config"
#define DEFAULT_OUTPUT_FILE     "/tmp/output.txt"
#define HOST_VMI_NAME           "aad4c946-9390-4a53-8bbd-09d346f5ba6c:323b7882-9bcf-4dc9-9460-48ea68b60ea2"
#define HOST_VM_NAME            "aad4c946-9390-4a53-8bbd-09d346f5ba6c"
#define HOST_VM_NAME1           "aad4c946-9390-4a53-8bbd-09d346f5ba6d"

class XmppVnswMockPeer : public XmppClient {
public:
    typedef set<string> ObjectSet;

    explicit XmppVnswMockPeer(EventManager *evm, int port, const string &name,
                 string laddr = string(), string filepath = string())
        : XmppClient(evm), count_(0), os_(&fb_), name_(name) {
        if (laddr.size()) {
            laddr_ = laddr;
        } else {
            laddr_ = string("127.0.0.1");
        }
        XmppChannelConfig *channel_config = 
            CreateXmppChannelCfg(SRV_ADDR, laddr_.c_str(), port,
                                 XMPP_CONTROL_SERV, name_);
        XmppConfigData *config = new XmppConfigData();
        config->AddXmppChannelConfig(channel_config);
        ConfigUpdate(config);
        if (filepath.size()) {
            fb_.open(filepath.c_str(), ios::out);
        } else {
            fb_.open(DEFAULT_OUTPUT_FILE, ios::out);
        }
    }

    ~XmppVnswMockPeer() {
        fb_.close();
    }

    void RegisterWithXmpp() {
        XmppChannel *channel = FindChannel(XMPP_CONTROL_SERV);
        assert(channel);
        channel->RegisterReceive(xmps::CONFIG, 
                                 boost::bind(&XmppVnswMockPeer::ReceiveUpdate,
                                             this, _1));
    }

    bool IsEstablished() {
        XmppConnection *cconnection = FindConnection(XMPP_CONTROL_SERV);
        if (cconnection == NULL) {
            return false;
        }
        return (cconnection->GetStateMcState() == xmsm::ESTABLISHED); 
    }

    static XmppChannelConfig *CreateXmppChannelCfg(
            const char *saddr, const char *laddr, int sport,
            const string &to, const string &from) {
        XmppChannelConfig *cfg = new XmppChannelConfig(true);

        cfg->endpoint.address(ip::address::from_string(saddr));
        cfg->endpoint.port(sport);
        cfg->local_endpoint.address(ip::address::from_string(laddr));
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        return cfg;
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        ++count_;

        ASSERT_TRUE(msg->type == XmppStanza::IQ_STANZA);
        XmlBase *impl = msg->dom.get();
        XmlPugi *pugi = reinterpret_cast<XmlPugi *>(impl);

        // Append the received message into the buffer
        stringstream ss;
        pugi->PrintDocFormatted(ss);
        recv_buffer_ += ss.str();
    }

    void SendDocument(const pugi::xml_document &xdoc) {
        ostringstream oss;
        xdoc.save(oss);
        string msg = oss.str();

        XmppChannel *channel = FindChannel(XMPP_CONTROL_SERV);
        assert(channel);
        if (channel->GetPeerState() == xmps::READY) {
            channel->Send(reinterpret_cast<const uint8_t *>(msg.data()),
                          msg.length(), xmps::CONFIG, NULL);
        }
    }

    pugi::xml_node PubSubHeader(pugi::xml_document *xdoc) {
        pugi::xml_node iq = xdoc->append_child("iq");
        iq.append_attribute("type") = "set";
        iq.append_attribute("from") = name_.c_str();
        iq.append_attribute("to") = XMPP_CONTROL_SERV_CFG;
        pugi::xml_node pubsub = iq.append_child("pubsub");
        pubsub.append_attribute("xmlns") = XmppInit::kPubSubNS;
        return pubsub;
    }

    void SendConfigSubscribe() {
        pugi::xml_document xdoc;
        pugi::xml_node pubsub = PubSubHeader(&xdoc);
        pugi::xml_node subscribe = pubsub.append_child("subscribe");
        string iqnode = std::string("virtual-router:") + std::string(name_);
        subscribe.append_attribute("node") = iqnode.c_str();
        SendDocument(xdoc);
    }

    uint64_t Count() const { return count_; }

    void SendVmConfigSubscribe(string vm_name) {
        pugi::xml_document xdoc;
        pugi::xml_node pubsub = PubSubHeader(&xdoc);
        pugi::xml_node subscribe = pubsub.append_child("subscribe");
        string iqnode = std::string("virtual-machine:") + std::string(vm_name);
        subscribe.append_attribute("node") = iqnode.c_str();
        SendDocument(xdoc);
    }

    void SendVmConfigUnsubscribe(string vm_name) {
        pugi::xml_document xdoc;
        pugi::xml_node pubsub = PubSubHeader(&xdoc);
        pugi::xml_node subscribe = pubsub.append_child("unsubscribe");
        string iqnode = std::string("virtual-machine:") + std::string(vm_name);
        subscribe.append_attribute("node") = iqnode.c_str();
        SendDocument(xdoc);
    }

    void ResetCount() { count_ = 0; }

    bool HasMessages() const {
        return count_ > 0;
    }

    bool Has2Messages() const {
        return count_ == 2;
    }

    bool HasNMessages(uint64_t n) const {
        return count_ == n;
    }

    string& name() { return name_; }

    void ProcessNodeTag(pugi::xml_node xnode, ObjectSet *oset) {
        string ntype;

        // EG: <node type="virtual-router">
        //        <name>a1s27</name>

        // Search for the 'type' attribute of the 'node' tag
        for (pugi::xml_attribute attr = xnode.first_attribute(); attr;
             attr = attr.next_attribute()) {
            string attr_name = attr.name();
            if (attr_name.compare("type") == 0) {
                ntype = attr.value();
                break;
            }
        }
        assert(ntype.size() != 0);

        // Find the child with the 'name' tag. Get the child of that child.
        // The value of this grand-child is the name of 'xnode'
        for (pugi::xml_node child = xnode.first_child(); child;
             child = child.next_sibling()) {
            string child_name = child.name();
            if (child_name.compare("name") == 0) {
                pugi::xml_node gchild = child.first_child();
                // Concatenate type and value to form unique string
                oset->insert(ntype.append(gchild.value()));
                break;
            }
        }
    }

    void ProcessLinkTag(pugi::xml_node xnode) {
        return;
    }

    // This logic works since we have SetObjectsPerMessage as 1. Otherwise, we
    // will need another loop after accessing 'config'.
    void XmlDocWalk(pugi::xml_node xnode, ObjectSet *oset) {
        string node_name = xnode.name();
        assert(node_name.compare("iq") == 0);

        pugi::xml_node cnode = xnode.first_child();
        node_name = cnode.name();
        assert(node_name.compare("config") == 0);

        cnode = cnode.first_child();
        node_name = cnode.name();
        assert((node_name.compare("update") == 0) ||
               (node_name.compare("delete") == 0));

        cnode = cnode.first_child();
        node_name = cnode.name();
        if (node_name.compare("node") == 0) {
            ProcessNodeTag(cnode, oset);
        } else if (node_name.compare("link") == 0) {
            ProcessLinkTag(cnode);
        } else {
            assert(0);
        }
    }

    void OutputRecvBufferToFile() {
        os_ << recv_buffer_;
    }

    // Compare the contents of the received buffer with master_file_path
    bool OutputFileCompare(string master_file_path) {
        pugi::xml_document doc1, doc2;
        ObjectSet set1, set2;

        // Save the received contents in the user's file. This file is just for
        // reference.
        os_ << recv_buffer_;

        // Decode the contents of the received buffer into set1
        pugi::xml_parse_result result =
            doc1.load_buffer(recv_buffer_.c_str(), recv_buffer_.size());
        if (result) {
            for (pugi::xml_node child = doc1.first_child(); child;
                child = child.next_sibling()) {
                XmlDocWalk(child, &set1);
            }
        } else {
            LOG(DEBUG, "Error loading received buffer. Buffer size is "
                << recv_buffer_.size());
            return false;
        }
        cout << "Set size is " << set1.size() << endl;
        PrintSet(set1);

        // Decode the contents of the master file into set2
        result = doc2.load_file(master_file_path.c_str());
        if (result) {
            for (pugi::xml_node child = doc2.first_child(); child;
                child = child.next_sibling()) {
                XmlDocWalk(child, &set2);
            }
        } else {
            LOG(DEBUG, "Error loading " << master_file_path);
            return false;
        }

        // == compares size and contents. Note, sets are sorted.
        if (set1 == set2) {
            return true;
        } else {
            LOG(DEBUG, "Error: File compare mismatch. set1 is " 
                << set1.size() << " set2 is " << set2.size() << endl);
            return false;
        }
    }

    void PrintSet(ObjectSet &oset) {
        int i = 0;
        cout << "Set size is " << oset.size() << endl;
        for (ObjectSet::iterator it = oset.begin(); it != oset.end(); ++it) {
            cout << i++ << ") " << *it << endl;
        }
    }

private:
    uint64_t count_;
    ostream os_;
    filebuf fb_;
    string name_;
    string laddr_;
    string recv_buffer_;
};

class XmppIfmapTest : public ::testing::Test {
protected:
    XmppIfmapTest()
         : ifmap_server_(&db_, &graph_, evm_.io_service()),
           exporter_(ifmap_server_.exporter()),
           parser_(NULL) {
    }

    virtual void SetUp() {
        IFMap_Initialize();

        xmpp_server_ = new XmppServer(&evm_, XMPP_CONTROL_SERV);
        thread_.reset(new ServerThread(&evm_));
        xmpp_server_->Initialize(0, false);

        LOG(DEBUG, "Created Xmpp Server at port " << xmpp_server_->GetPort());
        ifmap_channel_mgr_.reset(new IFMapChannelManager(xmpp_server_,
                                                         &ifmap_server_));
        ifmap_server_.set_ifmap_channel_manager(ifmap_channel_mgr_.get());
        thread_->Start();
    }

    virtual void TearDown() {
        ifmap_server_.Shutdown();
        task_util::WaitForIdle();

        IFMapLinkTable_Clear(&db_);
        IFMapTable::ClearTables(&db_);
        task_util::WaitForIdle();

        db_.Clear();
        DB::ClearFactoryRegistry();
        parser_->MetadataClear("vnc_cfg");

        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
        evm_.Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    void IFMap_Initialize() {
        IFMapLinkTable_Init(ifmap_server_.database(), ifmap_server_.graph());
        parser_ = IFMapServerParser::GetInstance("vnc_cfg");
        vnc_cfg_ParserInit(parser_);
        vnc_cfg_Server_ModuleInit(ifmap_server_.database(),
                                  ifmap_server_.graph());
        ifmap_server_.Initialize();
        vm_uuid_mapper_ = ifmap_server_.vm_uuid_mapper();
    }

    IFMapNode *TableLookup(const string &type, const string &name) {
        IFMapTable *tbl = IFMapTable::FindTable(&db_, type);
        if (tbl == NULL) {
            return NULL;
        }
        return tbl->FindNode(name);
    }

    IFMapLink *LinkLookup(IFMapNode *lhs, IFMapNode *rhs) {
        IFMapLink *link = static_cast<IFMapLink *>(graph_.GetEdge(lhs, rhs));
        return link;
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
        boost::asio::monotonic_deadline_timer timer(*evm_.io_service());
        timer.expires_from_now(boost::posix_time::seconds(timeout));
        timer.async_wait(boost::bind(&XmppIfmapTest::on_timeout, 
                         boost::asio::placeholders::error, &is_expired));
        while (!is_expired) {
            evm_.RunOnce();
            task_util::WaitForIdle();
            if ((condition)()) {
                timer.cancel();
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
        link->MarkDelete();
        graph_.Unlink(left, right);
        IFMapLinkTable *link_table = static_cast<IFMapLinkTable *>(
            db_.FindTable("__ifmap_metadata__.0"));
        DBTablePartBase *partition = link_table->GetTablePartition(0);
        exporter_->LinkTableExport(partition, link);
    }

    DB db_;
    DBGraph graph_;
    EventManager evm_;
    IFMapServer ifmap_server_;
    IFMapExporter *exporter_;
    IFMapServerParser *parser_;

    auto_ptr<ServerThread> thread_;
    XmppServer *xmpp_server_;
    auto_ptr<IFMapChannelManager> ifmap_channel_mgr_;
    IFMapVmUuidMapper *vm_uuid_mapper_;
};

namespace {

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

static string FileRead(const string &filename) {
    ifstream file(filename.c_str());
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

bool IsIFMapClientUnregistered(IFMapServer *ifmap_server,
                               const string &client_name) {
    if (ifmap_server->FindClient(client_name) == NULL) {
        return true;
    }
    return false;
}

TEST_F(XmppIfmapTest, Connection) {

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/connection.output"));
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
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/graph_cleanup_1.output"));
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
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
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
    vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/graph_cleanup_2.output"));
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
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
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

TEST_F(XmppIfmapTest, DeleteProperty) {

    // Read the ifmap data from file and give it to the parser
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/delete_property.output"));
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
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->Has2Messages());
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);
    size_t cli_index = static_cast<size_t>(client->index());

    // Deleting one property
    content = FileRead("controller/src/ifmap/testdata/vn_prop_del.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
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

    // Give the read file to the parser
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/vr_vm_sub_unsub.output"));
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    usleep(1000);
    // Server connection
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);

    // Verify ifmap_server client is not created until config subscribe
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(CLI_ADDR) == NULL);
    // No config messages sent until config subscribe
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());

    // Subscribe to config
    vnsw_client->SendConfigSubscribe();
    TASK_UTIL_EXPECT_TRUE(ifmap_server_.FindClient(client_name) != NULL);
    TASK_UTIL_EXPECT_EQ(0, vnsw_client->Count());
    IFMapClient *client = ifmap_server_.FindClient(client_name);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get all the nodes.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));


    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // After sending unsubscribe, client should get a delete for the vr-vm
    // link. The link should not have XMPP as origin anymore.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
            string("127.0.0.1"), string("/tmp/vr_vm_sub_unsub_twice.output"));
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
    IFMapClient *client = ifmap_server_.FindClient(CLI_ADDR);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // After sending unsubscribe, client should get a delete for the vr-vm
    // link. The link should not exist in the ctrl-node db anymore.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    // Send a subscribe-unsubscribe again.
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    // We should download everything since Unsubscribe above would have reset
    // interest/advertised.
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/vr_vm_sub_thrice.output"));
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
    IFMapClient *client = ifmap_server_.FindClient(CLI_ADDR);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;
    EXPECT_EQ(ifmap_channel_mgr_->get_dupicate_vmsub_messages(), 0);

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // 2nd spurious subscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    task_util::WaitForIdle();
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_dupicate_vmsub_messages(), 1);
    EXPECT_EQ(vnsw_client->HasNMessages(num_msgs), true);

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // 3rd spurious subscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_dupicate_vmsub_messages(), 2);
    EXPECT_EQ(vnsw_client->HasNMessages(num_msgs), true);

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // Unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/vr_vm_unsub_thrice.output"));
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
    IFMapClient *client = ifmap_server_.FindClient(CLI_ADDR);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    // Unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    // Should get a delete for the link
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));
    EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novmsub_messages(), 0);

    // 2nd spurious unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs));
    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_vmunsub_novmsub_messages(), 1);

    // 3rd spurious unsubscribe
    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs));
    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
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

    // Give the read file to the parser
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
               string("127.0.0.1"), string("/tmp/vr_vm_sub_conn_close.output"));
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
    IFMapClient *client = ifmap_server_.FindClient(CLI_ADDR);
    EXPECT_TRUE(client != NULL);

    // The link between VR-VM should not exist
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr != NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);

    // After sending subscribe, client should get an add for the vr-vm link.
    // The link should not exist before the subscribe and should exist after.
    size_t num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(client->msgs_sent(), vnsw_client->Count());
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link != NULL);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    size_t cli_index = static_cast<size_t>(client->index());
    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    vnsw_client->OutputRecvBufferToFile();

    // HOST_VM_NAME1 does not exist in config.
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME1);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.vm_uuid_mapper()->PendingVmRegCount(), 1);
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);
    TASK_UTIL_EXPECT_EQ(ifmap_server_.vm_uuid_mapper()->PendingVmRegCount(), 0);

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Interest and advertised must be false since the client is gone
    CheckClientBits(vnsw_client->name(), cli_index, false, false);

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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/reg_before_config.output"));
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
    IFMapNode *vr = TableLookup("virtual-router", CLI_ADDR);
    EXPECT_TRUE(vr == NULL);
    IFMapNode *vm = TableLookup("virtual-machine", HOST_VM_NAME);
    EXPECT_TRUE(vm == NULL);

    // The parser has not been given the data yet and so nothing to download
    vnsw_client->SendVmConfigSubscribe(HOST_VM_NAME);
    usleep(1000);
    EXPECT_EQ(vnsw_client->Count(), 0);

    // The nodes should not exist since the config has not been read yet
    EXPECT_TRUE(TableLookup("virtual-router", CLI_ADDR) == NULL);
    EXPECT_TRUE(TableLookup("virtual-machine", HOST_VM_NAME) == NULL);

    // Verify ifmap_server client creation
    IFMapClient *client = ifmap_server_.FindClient(CLI_ADDR);
    EXPECT_TRUE(client != NULL);
    size_t cli_index = static_cast<size_t>(client->index());

    // Give the read file to the parser
    size_t num_msgs = vnsw_client->Count();
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 2));

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", CLI_ADDR) != NULL);
    vr = TableLookup("virtual-router", CLI_ADDR);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine", HOST_VM_NAME) != NULL);
    vm = TableLookup("virtual-machine", HOST_VM_NAME);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm) != NULL);
    IFMapLink *link = LinkLookup(vr, vm);
    bool link_origin = LinkOriginLookup(link, IFMapOrigin::XMPP);
    EXPECT_TRUE(link_origin);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::XMPP));

    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    CheckLinkBits(link, cli_index, true, true);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, true, true);

    num_msgs = vnsw_client->Count();
    vnsw_client->SendVmConfigUnsubscribe(HOST_VM_NAME);
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->HasNMessages(num_msgs + 3));
    cout << "Rx msgs " << vnsw_client->Count() << endl;
    cout << "Sent msgs " << GetSentMsgs(xmpp_server_, client_name) << endl;

    link = LinkLookup(vr, vm);
    EXPECT_TRUE(link == NULL);
    CheckNodeBits(vr, cli_index, true, true);
    CheckNodeBits(vm, cli_index, false, false);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm, IFMapOrigin::MAP_SERVER));
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

    // Read the ifmap data from file and give it to the parser
    string content =
        FileRead("controller/src/ifmap/testdata/cli1_vn1_vm3_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/cli1_vn1_vm3_add.output"));
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
    TASK_UTIL_EXPECT_EQ(32, vnsw_client->Count());
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
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn1_vm3_add.master_output");
    EXPECT_EQ(true, bresult);

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

    // Read the ifmap data from file and give it to the parser
    string content =
        FileRead("controller/src/ifmap/testdata/cli1_vn2_np1_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"),
                string("/tmp/cli1_vn2_np1_add.output"));
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
    TASK_UTIL_EXPECT_EQ(32, vnsw_client->Count());
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
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn2_np1_add.master_output");
    EXPECT_EQ(true, bresult);

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

    // Read the ifmap data from file and give it to the parser
    string content = 
        FileRead("controller/src/ifmap/testdata/cli1_vn2_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/cli1_vn2_np2_add.output"));
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
    TASK_UTIL_EXPECT_EQ(32, vnsw_client->Count());
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
    bool bresult = vnsw_client->OutputFileCompare(
        "controller/src/ifmap/testdata/cli1_vn2_np2_add.master_output");
    EXPECT_EQ(true, bresult);

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

    // Read the ifmap data from file and give it to the parser
    string content = 
        FileRead("controller/src/ifmap/testdata/cli2_vn2_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli1 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name1,
                string("127.0.0.1"),
                string("/tmp/cli2_vn2_np2_add_a1s27.output"));
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli2 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name2,
                string("127.0.0.2"),
                string("/tmp/cli2_vn2_np2_add_a1s28.output"));
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
    TASK_UTIL_EXPECT_EQ(18, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(18, vnsw_cli2->Count());
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
    bool bresult = vnsw_cli1->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_np2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_np2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);

    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
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

    // Read the ifmap data from file and give it to the parser
    string content=
        FileRead("controller/src/ifmap/testdata/cli2_vn2_vm2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli1 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name1,
                string("127.0.0.1"),
                string("/tmp/cli2_vn2_vm2_add_a1s27.output"));
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli2 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name2,
                string("127.0.0.2"),
                string("/tmp/cli2_vn2_vm2_add_a1s28.output"));
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
    TASK_UTIL_EXPECT_EQ(18, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(18, vnsw_cli2->Count());
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
    bool bresult = vnsw_cli1->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_vm2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
        "controller/src/ifmap/testdata/cli2_vn2_vm2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);

    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
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

    // Read the ifmap data from file and give it to the parser
    string content =
        FileRead("controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Establish client a1s27
    string cli_name1 =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli1 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name1,
                string("127.0.0.1"),
                string("/tmp/cli2_vn3_vm6_np2_add_a1s27.output"));
    TASK_UTIL_EXPECT_EQ(true, vnsw_cli1->IsEstablished());

    // Establish client a1s28
    string cli_name2 =
        string("default-global-system-config:a1s28.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_cli2 =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), cli_name2,
                string("127.0.0.2"),
                string("/tmp/cli2_vn3_vm6_np2_add_a1s28.output"));
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
    TASK_UTIL_EXPECT_EQ(48, vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(cli1->msgs_sent(), vnsw_cli1->Count());
    TASK_UTIL_EXPECT_EQ(26, vnsw_cli2->Count());
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
    bool bresult = vnsw_cli1->OutputFileCompare(
      "controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add_a1s27.master_output");
    EXPECT_EQ(true, bresult);
    bresult = vnsw_cli2->OutputFileCompare(
      "controller/src/ifmap/testdata/cli2_vn3_vm6_np2_add_a1s28.master_output");
    EXPECT_EQ(true, bresult);

    vnsw_cli1->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(vnsw_cli1);
    vnsw_cli1 = NULL;
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

    // Read the ifmap data from file and give it to the parser
    string content =
        FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/CfgRegUnreg.output"));
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

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
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
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
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

    // Read the ifmap data from file and give it to the parser
    string content(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
            string("127.0.0.1"), string("/tmp/CfgAdd_Reg_CfgDel_Unreg.output"));
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

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);
    vnsw_client->SendVmConfigSubscribe("43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);
    TASK_UTIL_EXPECT_NE(0, vnsw_client->Count());

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    // Delete the vr and all the vms via config
    string content1 = 
        FileRead("controller/src/ifmap/testdata/vr_3vm_delete.xml");
    assert(content1.size() != 0);
    parser_->Receive(&db_, content1.data(), content1.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

    EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
            string("127.0.0.1"), string("/tmp/Reg_CfgAdd_CfgDel_Unreg.output"));
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

    // Read the ifmap data from file and give it to the parser
    string content(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

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

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    // Delete the vr and all the vms via config
    string content1 = 
        FileRead("controller/src/ifmap/testdata/vr_3vm_delete.xml");
    assert(content1.size() != 0);
    parser_->Receive(&db_, content1.data(), content1.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

    EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    EXPECT_TRUE(TableLookup("virtual-machine",
                "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
            string("127.0.0.1"), string("/tmp/Reg_CfgAdd_Unreg_CfgDel.output"));
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
    string content =
        FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

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

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
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

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) == NULL);

    // Delete the vr and all the vms via config
    string content1 = 
        FileRead("controller/src/ifmap/testdata/vr_3vm_delete.xml");
    assert(content1.size() != 0);
    parser_->Receive(&db_, content1.data(), content1.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
            string("127.0.0.1"), string("/tmp/Reg_CfgAdd_Unreg_Close.output"));
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
    string content(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

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

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) != NULL);
    link = LinkLookup(vr, vm2);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) != NULL);
    link = LinkLookup(vr, vm3);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));

    vnsw_client->SendVmConfigUnsubscribe(
            "93e76278-1990-4905-a472-8e9188f41b2c");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));

    vnsw_client->SendVmConfigUnsubscribe(
            "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    usleep(1000);

    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);;
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);

    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm2) == NULL);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm3) == NULL);

    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
        string("127.0.0.1"), string("/tmp/CheckIFMapObjectSeqInList.output"));
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
    string content(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 2);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
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

    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 3);

    // Vm Unsubscribe
    vnsw_client->SendVmConfigUnsubscribe(
            "2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_EQ(vm1->get_object_list_size(), 1);
    EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // should receive vr-vm link delete and vm-delete since the link is gone
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 5);

    // Vm Subscribe
    vnsw_client->SendVmConfigSubscribe("2d308482-c7b3-4e05-af14-e732b7b50117");
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_EQ(vr->get_object_list_size(), 2);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_EQ(vm1->get_object_list_size(), 2);
    EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 3);
    EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // should receive vr-vm link add and vm-add since the link was added
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 7);

    // Delete the vr and all the vms via config
    string content1 =
        FileRead("controller/src/ifmap/testdata/vr_3vm_delete.xml");
    assert(content1.size() != 0);
    parser_->Receive(&db_, content1.data(), content1.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

    // Wait for the other 2 VMs just to sequence events
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") == NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") == NULL);

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_FALSE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_EQ(vm1->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->UuidMapperCount() == 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->NodeUuidMapCount() == 1);
    TASK_UTIL_EXPECT_TRUE(vm_uuid_mapper_->PendingVmRegCount() == 0);
    vr->PrintAllObjects();
    vm1->PrintAllObjects();
    // although the vr/vm are not 'marked' deleted, client will get updates for
    // them since the config-delete will trigger a change for the client.
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 9);
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
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 12);
    vm_uuid_mapper_->PrintAllUuidMapperEntries();
    vm_uuid_mapper_->PrintAllNodeUuidMappedEntries();

    // New cycle - the nodes do not exist right now
    
    // Read from config first
    content1 = string(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content1.size() != 0);
    parser_->Receive(&db_, content1.data(), content1.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

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

    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vr, IFMapOrigin::XMPP));
    EXPECT_EQ(vr->get_object_list_size(), 1);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
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
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_EQ(vr->get_object_list_size(), 2);
    TASK_UTIL_EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/ReadyNotready.output"));
    TASK_UTIL_EXPECT_EQ(true, vnsw_client->IsEstablished());
    vnsw_client->RegisterWithXmpp();

    // Wait until server is established and until we have an XmppChannel.
    usleep(1000);
    TASK_UTIL_EXPECT_TRUE(ServerIsEstablished(xmpp_server_, client_name)
                          == true);
    TASK_UTIL_EXPECT_TRUE(GetXmppChannel(xmpp_server_, client_name) != NULL);

    // Give a chance to others to run
    usleep(1000);

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

    // Read the ifmap data from file and give it to the parser
    string content(FileRead("controller/src/ifmap/testdata/vr_3vm_add.xml"));
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();
    usleep(1000);

    // Create the mock client
    string client_name =
        string("default-global-system-config:a1s27.contrail.juniper.net");
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/Bug788.output"));
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
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vr, IFMapOrigin::XMPP));

    // vm1
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "2d308482-c7b3-4e05-af14-e732b7b50117") != NULL);
    IFMapNode *vm1 = TableLookup("virtual-machine",
                                 "2d308482-c7b3-4e05-af14-e732b7b50117");
    TASK_UTIL_EXPECT_EQ(vm1->get_object_list_size(), 2);
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(NodeOriginLookup(vm1, IFMapOrigin::XMPP));

    // link(vr, vm1)
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) != NULL);
    IFMapLink *link = LinkLookup(vr, vm1);
    EXPECT_FALSE(LinkOriginLookup(link, IFMapOrigin::MAP_SERVER));
    EXPECT_TRUE(LinkOriginLookup(link, IFMapOrigin::XMPP));

    // No vm-config for this vm. Object-list-size should be 1 (config)
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "93e76278-1990-4905-a472-8e9188f41b2c") != NULL);
    IFMapNode *vm2 = TableLookup("virtual-machine",
                                 "93e76278-1990-4905-a472-8e9188f41b2c");
    EXPECT_EQ(vm2->get_object_list_size(), 1);
    EXPECT_TRUE(NodeOriginLookup(vm2, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm2, IFMapOrigin::XMPP));

    // No vm-config for this vm. Object-list-size should be 1 (config)
    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-machine",
                          "43d086ab-52c4-4a1f-8c3d-63b321e36e8a") != NULL);
    IFMapNode *vm3 = TableLookup("virtual-machine",
                                 "43d086ab-52c4-4a1f-8c3d-63b321e36e8a");
    EXPECT_EQ(vm3->get_object_list_size(), 1);
    EXPECT_TRUE(NodeOriginLookup(vm3, IFMapOrigin::MAP_SERVER));
    EXPECT_FALSE(NodeOriginLookup(vm3, IFMapOrigin::XMPP));

    // We are sending one object/message. We will send vr, vm1 and link(vr,vm1)
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(vnsw_client->Count(), 3);

    // Simulate the condition where we get a link delete. Exporter has finished
    // processing it and before the sender sends the update, we process a 
    // client-delete
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 1);
    task_util::TaskSchedulerStop();
    TriggerLinkDeleteToExporter(link, vr, vm1);
    TASK_UTIL_EXPECT_TRUE(LinkLookup(vr, vm1) == NULL);
    TriggerDeleteClient(client, client_name);
    task_util::TaskSchedulerStart();

    // Allow others to run
    usleep(1000);
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Verify ifmap_server client cleanup
    EXPECT_EQ(true, IsIFMapClientUnregistered(&ifmap_server_, client_name));

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/connection.output"));
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
    EXPECT_EQ(ifmap_channel_mgr_->get_dupicate_vrsub_messages(), 0);

    // Subscribe to config again to simulate a spurious vr-subscribe
    vnsw_client->SendConfigSubscribe();
    usleep(1000);
    TASK_UTIL_EXPECT_EQ(ifmap_channel_mgr_->get_dupicate_vrsub_messages(), 1);
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/two-vn-connection"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    // Create the mock client
    string client_name(CLI_ADDR);
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/connection.output"));
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

    // Read the ifmap data from file
    string content(FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    string client_name("vr1");
    string gsc_str("gsc1");

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc);
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    // Create the mock client
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/ConfigVrsubVrUnsub.output"));
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
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/VrsubConfigVrunsub.output"));
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
    string content(FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc);
    TASK_UTIL_EXPECT_TRUE(link != NULL);
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

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
TEST_F(XmppIfmapTest, ConfignopropVrsub) {

    // Read the ifmap data from file
    string content(FileRead(
        "controller/src/ifmap/testdata/vr_gsc_config_no_prop.xml"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    string client_name("vr1");
    string gsc_str("gsc1");

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc);
    TASK_UTIL_EXPECT_TRUE(link != NULL);

    // Create the mock client
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
           string("127.0.0.1"), string("/tmp/ConfignopropVrsub.output"));
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
    content = FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    usleep(10000);
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

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
TEST_F(XmppIfmapTest, VrsubConfignoprop) {

    string client_name("vr1");
    string gsc_str("gsc1");

    // Create the mock client
    XmppVnswMockPeer *vnsw_client =
        new XmppVnswMockPeer(&evm_, xmpp_server_->GetPort(), client_name,
                string("127.0.0.1"), string("/tmp/VrsubConfignoprop.output"));
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
    string content(FileRead(
        "controller/src/ifmap/testdata/vr_gsc_config_no_prop.xml"));
    assert(content.size() != 0);

    // Give the read file to the parser
    parser_->Receive(&db_, content.data(), content.size(), 0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(TableLookup("virtual-router", client_name) != NULL);
    IFMapNode *vr = TableLookup("virtual-router", client_name);
    EXPECT_TRUE(vr != NULL);
    TASK_UTIL_EXPECT_TRUE(TableLookup("global-system-config", gsc_str) != NULL);
    IFMapNode *gsc = TableLookup("global-system-config", gsc_str);
    EXPECT_TRUE(gsc != NULL);
    IFMapLink *link = LinkLookup(vr, gsc);
    TASK_UTIL_EXPECT_TRUE(link != NULL);
    TASK_UTIL_EXPECT_EQ(1, vnsw_client->Count());

    // Now read the properties and another link update with no real change.
    // Client should receive one more message.
    content = FileRead("controller/src/ifmap/testdata/vr_gsc_config.xml");
    assert(content.size() != 0);
    parser_->Receive(&db_, content.data(), content.size(), 0);
    usleep(10000);
    TASK_UTIL_EXPECT_EQ(2, vnsw_client->Count());

    // Client close generates a TcpClose event on server
    ConfigUpdate(vnsw_client, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(ifmap_server_.GetClientMapSize(), 0);

    // Give a chance for the xmpp channel to get deleted
    usleep(1000);

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

}

static void SetUp() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
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
