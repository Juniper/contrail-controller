/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"

#include <fstream>
#include <list>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/community.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/rtarget_group_types.h"
#include "bgp/rtarget/rtarget_prefix.h"
#include "bgp/rtarget/rtarget_table.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_table.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include <pugixml/pugixml.hpp>
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using boost::system::error_code;
using namespace pugi;
using namespace test;

#define VERIFY_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server, 
            BgpXmppChannelManager *manager) : 
        BgpXmppChannel(channel, server, manager), count_(0) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), count(0), channels(0) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         count++;
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_[channels] = new BgpXmppChannelMock(channel, bgp_server_, this);
        channels++;
        return channel_[channels-1];
    }

    int Count() {
        return count;
    }
    int count;
    int channels;
    BgpXmppChannelMock *channel_[2];
};


static const char *config_update= "\
<config>\
    <routing-instance name='default-domain:default-project:ip-fabric:__default__'>\
    <bgp-router name=\'CN1\'>\
        <autonomous-system>64510</autonomous-system>\
    </bgp-router>\
    <bgp-router name=\'CN2\'>\
        <autonomous-system>64510</autonomous-system>\
    </bgp-router>\
    <bgp-router name=\'MX\'>\
        <autonomous-system>64510</autonomous-system>\
    </bgp-router>\
    </routing-instance>\
</config>\
";
static const char *config_control_node= "\
<config>\
    <routing-instance name='default-domain:default-project:ip-fabric:__default__'>\
    <bgp-router name=\'CN1\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'CN2\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'CN2\'>\
        <identifier>192.168.0.2</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'MX\'>\
        <identifier>192.168.0.3</identifier>\
        <autonomous-system>64512</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
        </address-families>\
    </bgp-router>\
    </routing-instance>\
</config>\
";

static const char *config_delete = "\
<delete>\
    <bgp-router name=\'CN1\'>\
    </bgp-router>\
    <bgp-router name=\'CN2\'>\
    </bgp-router>\
    <bgp-router name=\'MX\'>\
    </bgp-router>\
</delete>\
";
static const char *config_mx_vrf = "\
<config>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:4</vrf-target>\
    </routing-instance>\
</config>\
";

class RTargetPeerTest : public ::testing::Test {
protected:
    RTargetPeerTest() : thread_(&evm_) { }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        bgp_schema_ParserInit(parser);
        vnc_cfg_ParserInit(parser);

        cn1_.reset(new BgpServerTest(&evm_, "CN1"));
        cn1_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 1 at port: " <<
            cn1_->session_manager()->GetPort());

        cn1_xmpp_server_ = 
            new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        cn1_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn1_xmpp_server_->GetPort());

        cn2_.reset(new BgpServerTest(&evm_, "CN2"));
        cn2_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 2 at port: " <<
            cn2_->session_manager()->GetPort());

        cn2_xmpp_server_ = 
            new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        cn2_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn2_xmpp_server_->GetPort());
        mx_.reset(new BgpServerTest(&evm_, "MX"));
        mx_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created MX at port: " << mx_->session_manager()->GetPort());

        bgp_channel_manager_cn1_.reset(
            new BgpXmppChannelManagerMock(cn1_xmpp_server_, cn1_.get()));

        bgp_channel_manager_cn2_.reset(
            new BgpXmppChannelManagerMock(cn2_xmpp_server_, cn2_.get()));

        task_util::WaitForIdle();

        thread_.Start();
        Configure();
        task_util::WaitForIdle();
        // Create XMPP Agent on compute node 1 connected to XMPP server 
        // Control-node-1
        agent_a_1_.reset(new test::NetworkAgentMock(&evm_, "agent-a", 
                                                    cn1_xmpp_server_->GetPort(),
                                                    "127.0.0.1"));

        // Create XMPP Agent on compute node 1 connected to XMPP server 
        // Control-node-2
        agent_a_2_.reset(new test::NetworkAgentMock(&evm_, "agent-a", 
                                                    cn2_xmpp_server_->GetPort(),
                                                    "127.0.0.1"));

        // Create XMPP Agent on compute node 2 connected to XMPP server 
        // Control-node-1 
        agent_b_1_.reset(new test::NetworkAgentMock(&evm_, "agent-b", 
                                                    cn1_xmpp_server_->GetPort(),
                                                    "127.0.0.2"));


        // Create XMPP Agent on compute node 2 connected to XMPP server Control-node-2 
        agent_b_2_.reset(new test::NetworkAgentMock(&evm_, "agent-b", 
                                                    cn2_xmpp_server_->GetPort(),
                                                    "127.0.0.2"));

        TASK_UTIL_EXPECT_TRUE(agent_a_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_a_2_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_2_->IsEstablished());
        TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_cn1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_cn2_->NumUpPeer());

        agent_a_1_->Subscribe("blue-i1", 1); 
        agent_a_2_->Subscribe("blue-i1", 1); 
        agent_b_1_->Subscribe("blue-i1", 1); 
        agent_b_2_->Subscribe("blue-i1", 1); 

        agent_a_1_->Subscribe("red", 3); 
        agent_a_2_->Subscribe("red", 3); 
        agent_b_1_->Subscribe("red", 3); 
        agent_b_2_->Subscribe("red", 3); 

        agent_a_1_->Subscribe("purple", 4); 
        agent_a_2_->Subscribe("purple", 4); 
        agent_b_1_->Subscribe("purple", 4); 
        agent_b_2_->Subscribe("purple", 4); 

        agent_a_1_->Subscribe("red-i2", 5); 
        agent_a_2_->Subscribe("red-i2", 5); 
        agent_b_1_->Subscribe("red-i2", 5); 
        agent_b_2_->Subscribe("red-i2", 5); 
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        // Close the sessions.
        agent_a_1_->SessionDown();
        agent_a_2_->SessionDown();
        agent_b_1_->SessionDown();
        agent_b_2_->SessionDown();

        Unconfigure();

        task_util::WaitForIdle();

        cn1_xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        cn1_->Shutdown();
        task_util::WaitForIdle();

        cn2_xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        cn2_->Shutdown();
        task_util::WaitForIdle();

        mx_->Shutdown();
        task_util::WaitForIdle();

        bgp_channel_manager_cn1_.reset();

        bgp_channel_manager_cn2_.reset();

        TcpServerManager::DeleteServer(cn1_xmpp_server_);
        cn1_xmpp_server_ = NULL;
        TcpServerManager::DeleteServer(cn2_xmpp_server_);
        cn2_xmpp_server_ = NULL;

        if (agent_a_1_) { agent_a_1_->Delete(); }
        if (agent_b_1_) { agent_b_1_->Delete(); }
        if (agent_a_2_) { agent_a_2_->Delete(); }
        if (agent_b_2_) { agent_b_2_->Delete(); }

        IFMapCleanUp();
        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }


    void IFMapCleanUp() {
        IFMapServerParser::GetInstance("vnc_cfg")->MetadataClear("vnc_cfg");
        IFMapServerParser::GetInstance("schema")->MetadataClear("schema");
    }

    void VerifyAllPeerUp(BgpServerTest *server, int num) {
        TASK_UTIL_EXPECT_EQ_MSG(num, server->num_bgp_peer(),
                                "Wait for all peers to get configured");
        TASK_UTIL_EXPECT_EQ_MSG(num, server->NumUpPeer(),
                                "Wait for all peers to come up");

        LOG(DEBUG, "All Peers are up: " << server->localname());
    }

    void EnableRtargetRouteProcessing(BgpServerTest *server) {
        server->rtarget_group_mgr()->EnableRtargetRouteProcessing();
    }

    void DisableRtargetRouteProcessing(BgpServerTest *server) {
        server->rtarget_group_mgr()->DisableRtargetRouteProcessing();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_control_node,
                 cn1_->session_manager()->GetPort(),
                 cn2_->session_manager()->GetPort(),
                 mx_->session_manager()->GetPort());
        cn1_->Configure(config);
        task_util::WaitForIdle();
        cn2_->Configure(config);
        task_util::WaitForIdle();
        mx_->Configure(config);
        task_util::WaitForIdle();

        VerifyAllPeerUp(cn1_.get(), 2);
        VerifyAllPeerUp(cn2_.get(), 2);
        VerifyAllPeerUp(mx_.get(), 2);

        vector<string> instance_names = 
            list_of("blue")("blue-i1")("red-i2")("red")("purple");
        multimap<string, string> connections = 
            map_list_of("red", "purple");
        NetworkConfig(instance_names, connections);

        VerifyNetworkConfig(cn1_.get(), instance_names);
        VerifyNetworkConfig(cn2_.get(), instance_names);

        mx_->Configure(config_mx_vrf);
    }

    void NetworkConfig(const vector<string> &instance_names,
                       const multimap<string, string> &connections) {
        string netconf(
            bgp_util::NetworkConfigGenerate(instance_names, connections));
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        parser->Receive(cn1_->config_db(), netconf.data(), netconf.length(), 0);
        task_util::WaitForIdle();
        parser->Receive(cn2_->config_db(), netconf.data(), netconf.length(), 0);
        task_util::WaitForIdle();
    }

    void VerifyNetworkConfig(BgpServerTest *server, 
                             const vector<string> &instance_names) {
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(
                     server->routing_instance_mgr()->GetRoutingInstance(*iter),
                     NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti = 
                server->routing_instance_mgr()->GetRoutingInstance(*iter);
            TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
                0, 1000, 10000, "Wait for vn index..");
        }
    }

    void Unconfigure() {
        char config[4096];
        snprintf(config, sizeof(config), "%s", config_delete);
        cn1_->Configure(config);
        cn2_->Configure(config);
        mx_->Configure(config);
    }

    void UpdateASN() {
        char config[4096];
        snprintf(config, sizeof(config), "%s", config_update);
        cn1_->Configure(config);
        cn2_->Configure(config);
        mx_->Configure(config);

        VerifyAllPeerUp(cn1_.get(), 2);
        VerifyAllPeerUp(cn2_.get(), 2);
        VerifyAllPeerUp(mx_.get(), 2);
    }

    int RouteCount(BgpServerTest *server, const string &instance_name) const {
        string tablename(instance_name);
        tablename.append(".inet.0");
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable(tablename));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    int RTargetRouteCount(BgpServerTest *server) const {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("bgp.rtarget.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    void AddRouteTarget(BgpServerTest *server, string name, string target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgLink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
    }

    void RemoveRouteTarget(BgpServerTest *server, string name, string target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgUnlink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
    }

    BgpRoute *RTargetRouteLookup(BgpServerTest *server, const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("bgp.rtarget.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        error_code error;
        RTargetPrefix nlri = RTargetPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        RTargetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    BgpRoute *InetRouteLookup(BgpServerTest *server, const string &instance_name,
                              const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable(instance_name + ".inet.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void AddInetRoute(BgpServerTest *server, IPeer *peer, 
                      const string &instance_name,
                      const string &prefix, int localpref, 
                      string nexthop="7.8.9.1", 
                      uint32_t flags=0, int label=303) {
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        BgpAttrSpec attr_spec;
        boost::scoped_ptr<BgpAttrLocalPref> 
            local_pref(new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        IpAddress chain_addr = Ip4Address::from_string(nexthop, error);
        boost::scoped_ptr<BgpAttrNextHop> 
            nexthop_attr(new BgpAttrNextHop(chain_addr.to_v4().to_ulong()));
        attr_spec.push_back(nexthop_attr.get());


        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = static_cast<BgpTable *>
            (server->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
    }

    void DeleteInetRoute(BgpServerTest *server, IPeer *peer, 
                         const string &instance_name,
                         const string &prefix) {
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        std::string table_name = instance_name + ".inet.0";
        BgpTable *table = 
            static_cast<BgpTable *>(server->database()->FindTable(table_name));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    vector<string> GetExportRouteTargetList(BgpServerTest *server, const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            server->routing_instance_mgr()->GetRoutingInstance(instance);
        vector<string> target_list;
        BOOST_FOREACH(RouteTarget tgt, rti->GetExportList()) {
            target_list.push_back(tgt.ToString());
        }
        sort(target_list.begin(), target_list.end());
        return target_list;
    }

    vector<string> GetImportRouteTargetList(BgpServerTest *server, const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            server->routing_instance_mgr()->GetRoutingInstance(instance);
        vector<string> target_list;
        BOOST_FOREACH(RouteTarget tgt, rti->GetImportList()) {
            target_list.push_back(tgt.ToString());
        }
        sort(target_list.begin(), target_list.end());
        return target_list;
    }



    void RemoveConnection(BgpServerTest *server, string src, string tgt) {
        ifmap_test_util::IFMapMsgUnlink(server->config_db(),
                                        "routing-instance", src,
                                        "routing-instance", tgt,
                                        "connection");

        task_util::WaitForIdle();
    }

    void AddConnection(BgpServerTest *server, string src, string tgt) {
        ifmap_test_util::IFMapMsgLink(server->config_db(),
                                      "routing-instance", src,
                                      "routing-instance", tgt,
                                      "connection");
        task_util::WaitForIdle();
    }


    static void ValidateRTGroupResponse(Sandesh *sandesh, 
                                        std::vector<string> &result) {
        ShowRtGroupResp *resp =
                dynamic_cast<ShowRtGroupResp *>(sandesh);
        TASK_UTIL_EXPECT_NE((ShowRtGroupResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_rtgroup_list().size());
        cout << "*******************************************************"<<endl;
        int i = 0;
        BOOST_FOREACH(const ShowRtGroupInfo &info, resp->get_rtgroup_list()) {
            TASK_UTIL_EXPECT_EQ(info.get_rtarget(), result[i]);
            cout << info.log() << endl;
            i++;
        }
        cout << "*******************************************************"<<endl;

        validate_done_ = 1;
    }

    void VerifyRtGroupSandesh(BgpServerTest *server, std::vector<string> result) {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = server;
        sandesh_context.xmpp_peer_manager = NULL;
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(boost::bind(ValidateRTGroupResponse, _1, 
                                                   result));
        ShowRtGroupReq *req = new ShowRtGroupReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
    }

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> cn1_;
    auto_ptr<BgpServerTest> cn2_;
    auto_ptr<BgpServerTest> mx_;
    XmppServer *cn1_xmpp_server_;
    XmppServer *cn2_xmpp_server_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_2_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_cn1_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_cn2_;
    static int validate_done_;
};
int RTargetPeerTest::validate_done_;

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

TEST_F(RTargetPeerTest, basic_route_add_delete) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);

    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");


    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 
}

TEST_F(RTargetPeerTest, basic_sub_unsub) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);

    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");


    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, AddRTargetToRoute) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    task_util::WaitForIdle();

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    RemoveRouteTarget(mx_.get(), "blue", "target:64496:1");
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, AddRTargetToInstance) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    task_util::WaitForIdle();

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    AddRouteTarget(cn1_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    AddRouteTarget(cn2_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    RemoveRouteTarget(cn1_.get(), "blue", "target:1:4");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:4");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}


TEST_F(RTargetPeerTest, HTTPIntro) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    std::vector<string> result = list_of("target:1:4")("target:64496:1")
     ("target:64496:2")("target:64496:3")("target:64496:4")("target:64496:5");
    VerifyRtGroupSandesh(mx_.get(), result);
    VerifyRtGroupSandesh(cn1_.get(), result);
    VerifyRtGroupSandesh(cn2_.get(), result);

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 

}

TEST_F(RTargetPeerTest, BulkAddRt) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(mx_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "1.1.2.3/32"),
                             NULL, 1000, 10000, 
                             "Wait for route in blue..");

    std::vector<BgpServerTest *> to_play = list_of(cn1_.get())(cn2_.get())(mx_.get());
    for (std::vector<BgpServerTest *>::iterator sit = to_play.begin(); sit != to_play.end(); sit++) {
        std::vector<string> to_add = list_of("target:1:400")("target:64496:100")
            ("target:64496:200")("target:64496:300")("target:64496:400")("target:64496:500");
        for (std::vector<string>::iterator it = to_add.begin(); it != to_add.end(); it++) {
            AddRouteTarget(*sit, "blue", *it);
        }

        for (std::vector<string>::iterator it = to_add.begin(); it != to_add.end(); it++) {
            RemoveRouteTarget(*sit, "blue", *it);
        }
    }

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, DuplicateUnsubscribe) {
    agent_a_1_->Subscribe("blue", 2); 
    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_a_1_->Unsubscribe("blue", -1, false); 
}

TEST_F(RTargetPeerTest, ASNUpdate) {
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    VERIFY_EQ(4, RTargetRouteCount(cn1_.get()));
    VERIFY_EQ(4, RTargetRouteCount(cn2_.get()));
    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    VERIFY_EQ(5, RTargetRouteCount(cn1_.get()));
    VERIFY_EQ(5, RTargetRouteCount(cn2_.get()));

    UpdateASN();    

    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64510:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64510:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");

    VERIFY_EQ(5, RTargetRouteCount(cn1_.get()));
    VERIFY_EQ(5, RTargetRouteCount(cn2_.get()));

    agent_a_1_->Unsubscribe("blue", -1, false); 
    agent_b_1_->Unsubscribe("blue", -1, false); 
    agent_a_2_->Unsubscribe("blue", -1, false); 
    agent_b_2_->Unsubscribe("blue", -1, false); 

    VERIFY_EQ(4, RTargetRouteCount(cn1_.get()));
    VERIFY_EQ(4, RTargetRouteCount(cn2_.get()));

    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn1_.get(), "64510:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(), "64510:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:64496:1"),
                             NULL, 1000, 10000, 
                             "Wait for route in rtarget table..");
}

//
// Test to validate the defer logic of peer deletion.
// Peer will not be deleted till RTGroupManager stops referring to it
// in InterestedPeers list
//
TEST_F(RTargetPeerTest, DeletedPeer) {
    // CN1 & CN2 removes all rtarget routes
    agent_a_2_->Unsubscribe("blue-i1", -1, false);
    agent_b_2_->Unsubscribe("blue-i1", -1, false);
    agent_a_2_->Unsubscribe("red-i2", -1, false);
    agent_b_2_->Unsubscribe("red-i2", -1, false);
    agent_a_2_->Unsubscribe("red", -1, false);
    agent_b_2_->Unsubscribe("red", -1, false);
    agent_a_2_->Unsubscribe("purple", -1, false);
    agent_b_2_->Unsubscribe("purple", -1, false);
    agent_a_1_->Unsubscribe("blue-i1", -1, false);
    agent_b_1_->Unsubscribe("blue-i1", -1, false);
    agent_a_1_->Unsubscribe("red-i2", -1, false);
    agent_b_1_->Unsubscribe("red-i2", -1, false);
    agent_a_1_->Unsubscribe("red", -1, false);
    agent_b_1_->Unsubscribe("red", -1, false);
    agent_a_1_->Unsubscribe("purple", -1, false);
    agent_b_1_->Unsubscribe("purple", -1, false);

    VERIFY_EQ(0, RTargetRouteCount(cn2_.get()));
    VERIFY_EQ(0, RTargetRouteCount(cn1_.get()));

    // CN1 generates rtarget route for blue import rt
    agent_a_1_->Subscribe("blue", 2);
    agent_b_1_->Subscribe("blue", 2);

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(),
                             "64512:target:64496:1"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(),
                             "64512:target:64496:1"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");

    VERIFY_EQ(1, RTargetRouteCount(cn1_.get()));
    VERIFY_EQ(1, RTargetRouteCount(cn2_.get()));

    // Stop the RTGroupMgr processing of RTargetRoute notification
    DisableRtargetRouteProcessing(cn2_.get());

    size_t count = cn2_->lifetime_manager()->GetQueueDeferCount();

    // Delete the peer from both CNs
    ifmap_test_util::IFMapMsgUnlink(cn1_->config_db(),
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN1",
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN2",
      "bgp-peering");
    ifmap_test_util::IFMapMsgUnlink(cn2_->config_db(),
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN1",
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN2",
      "bgp-peering");

    ifmap_test_util::IFMapMsgUnlink(cn1_->config_db(),
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN2",
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN1",
      "bgp-peering");
    ifmap_test_util::IFMapMsgUnlink(cn2_->config_db(),
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN2",
      "bgp-router", "default-domain:default-project:ip-fabric:__default__:CN1",
      "bgp-peering");

    // Wait for the peer to go down in CN2
    TASK_UTIL_EXPECT_EQ_MSG(1, cn2_->NumUpPeer(),
                            "Wait for control-node peer to go down");

    // Route on CN2 will not be deleted due to DBState from RTGroupMgr
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(),
                             "64512:target:64496:1"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    BgpRoute *rt_route = RTargetRouteLookup(cn2_.get(), "64512:target:64496:1");
    // Ensure that the path from CN1 is deleted
    VERIFY_EQ(rt_route->count(), 0);

    //
    // Make sure that life time manager does not delete this peer as there is
    // a reference to the peer from membership manager's queue
    //
    TASK_UTIL_EXPECT_TRUE(
        cn2_->lifetime_manager()->GetQueueDeferCount() > count);

    // Enable the RTGroupMgr processing
    EnableRtargetRouteProcessing(cn2_.get());

    TASK_UTIL_EXPECT_TRUE(cn1_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    TASK_UTIL_EXPECT_TRUE(cn2_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(),
                             "64512:target:64496:1"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    VerifyAllPeerUp(cn2_.get(), 1);
}

TEST_F(RTargetPeerTest, SameRTInMultipleVRF) {
    agent_a_1_->Subscribe("blue", 2);
    agent_b_1_->Subscribe("blue", 2);
    agent_a_2_->Subscribe("blue", 2);
    agent_b_2_->Subscribe("blue", 2);

    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn1_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn1_.get(), "blue-i1").size());
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn2_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn2_.get(), "blue-i1").size());

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "blue-i1", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue-i1", "target:1:99");

    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn1_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn1_.get(), "blue-i1").size());
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn2_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn2_.get(), "blue-i1").size());

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn1_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn1_.get(), "blue-i1").size());
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetList(cn2_.get(), "blue").size());
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetList(cn2_.get(), "blue-i1").size());

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    agent_a_1_->Unsubscribe("blue", -1, false);
    agent_a_2_->Unsubscribe("blue", -1, false);
    agent_b_1_->Unsubscribe("blue", -1, false);
    agent_b_2_->Unsubscribe("blue", -1, false);

    RemoveRouteTarget(cn1_.get(), "blue-i1", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue-i1", "target:1:99");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
}

TEST_F(RTargetPeerTest, ConnectedVRF) {
    // RED and PURPLE are connected
    AddRouteTarget(cn1_.get(), "red", "target:1:99");
    AddRouteTarget(cn2_.get(), "red", "target:1:99");

    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn1_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn2_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn1_.get(), "purple").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn2_.get(), "purple").size());

    RemoveConnection(cn1_.get(), "red", "purple");
    RemoveConnection(cn2_.get(), "red", "purple");
    TASK_UTIL_EXPECT_EQ(2, GetImportRouteTargetList(cn1_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(2, GetImportRouteTargetList(cn2_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(1, GetImportRouteTargetList(cn1_.get(), "purple").size());
    TASK_UTIL_EXPECT_EQ(1, GetImportRouteTargetList(cn2_.get(), "purple").size());

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");

    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn1_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn2_.get(), "red").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn1_.get(), "purple").size());
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetList(cn2_.get(), "purple").size());

    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn1_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
    TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(cn2_.get(), "64512:target:1:99"),
                             NULL, 1000, 10000,
                             "Wait for route in rtarget table..");
}


int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
