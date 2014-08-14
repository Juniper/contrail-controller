/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/rtarget/rtarget_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_factory.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using boost::system::error_code;

static const char *config_update= "\
<config>\
    <bgp-router name=\'CN1\'>\
        <autonomous-system>64497</autonomous-system>\
    </bgp-router>\
    <bgp-router name=\'CN2\'>\
        <autonomous-system>64497</autonomous-system>\
    </bgp-router>\
    <bgp-router name=\'MX\'>\
        <autonomous-system>64497</autonomous-system>\
    </bgp-router>\
</config>\
";

static const char *config_control_node= "\
<config>\
    <bgp-router name=\'CN1\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>64496</autonomous-system>\
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
        <autonomous-system>64496</autonomous-system>\
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
        <autonomous-system>64496</autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
        </address-families>\
    </bgp-router>\
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
    RTargetPeerTest()
        : thread_(&evm_), cn1_xmpp_server_(NULL), cn2_xmpp_server_(NULL) {
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        bgp_schema_ParserInit(parser);
        vnc_cfg_ParserInit(parser);

        cn1_.reset(new BgpServerTest(&evm_, "CN1"));
        cn1_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 1 at port: " <<
            cn1_->session_manager()->GetPort());

        cn1_xmpp_server_ = 
            new XmppServerTest(&evm_, test::XmppDocumentMock::kControlNodeJID);
        cn1_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn1_xmpp_server_->GetPort());

        cn2_.reset(new BgpServerTest(&evm_, "CN2"));
        cn2_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 2 at port: " <<
            cn2_->session_manager()->GetPort());

        cn2_xmpp_server_ = 
            new XmppServerTest(&evm_, test::XmppDocumentMock::kControlNodeJID);
        cn2_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn2_xmpp_server_->GetPort());

        mx_.reset(new BgpServerTest(&evm_, "MX"));
        mx_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created MX at port: " << mx_->session_manager()->GetPort());

        bgp_channel_manager_cn1_.reset(
            new BgpXmppChannelManager(cn1_xmpp_server_, cn1_.get()));
        bgp_channel_manager_cn2_.reset(
            new BgpXmppChannelManager(cn2_xmpp_server_, cn2_.get()));

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


        // Create XMPP Agent on compute node 2 connected to XMPP server
        // Control-node-2
        agent_b_2_.reset(new test::NetworkAgentMock(&evm_, "agent-b", 
                                                    cn2_xmpp_server_->GetPort(),
                                                    "127.0.0.2"));

        BringupAgents();
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

    void BringupAgents() {
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

    void UnconfigureBgpPeering(BgpServerTest *server1, BgpServerTest *server2) {
        string master_instance(BgpConfigManager::kMasterInstance);
        string router1 = master_instance + ":" + server1->localname();
        string router2 = master_instance + ":" + server2->localname();

        ifmap_test_util::IFMapMsgUnlink(server1->config_db(),
            "bgp-router", router1, "bgp-router", router2, "bgp-peering");
        ifmap_test_util::IFMapMsgUnlink(server2->config_db(),
            "bgp-router", router1, "bgp-router", router2, "bgp-peering");

        ifmap_test_util::IFMapMsgUnlink(server1->config_db(),
            "bgp-router", router2, "bgp-router", router1, "bgp-peering");
        ifmap_test_util::IFMapMsgUnlink(server2->config_db(),
            "bgp-router", router2, "bgp-router", router1, "bgp-peering");
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

    void VerifyRTargetRouteExists(BgpServerTest *server,
        const string &prefix) {
        TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(server, prefix),
            NULL, 1000, 10000, "Wait for rtarget route " << prefix);
    }

    void VerifyRTargetRouteNoExists(BgpServerTest *server,
        const string &prefix) {
        TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(server, prefix),
            NULL, 1000, 10000,
            "Wait for rtarget route " << prefix << " to go away");
    }

    void AddInetRoute(BgpServerTest *server, IPeer *peer, 
                      const string &instance_name,
                      const string &prefix, int localpref = 100,
                      string nexthop = "7.8.9.1",
                      uint32_t flags = 0, int label = 303) {
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

        IpAddress nhaddr = Ip4Address::from_string(nexthop, error);
        boost::scoped_ptr<BgpAttrNextHop> 
            nexthop_attr(new BgpAttrNextHop(nhaddr.to_v4().to_ulong()));
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

    BgpRoute *InetRouteLookup(BgpServerTest *server, const string &instance_name,
        const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable(instance_name + ".inet.0"));
        EXPECT_TRUE(table != NULL);
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyInetRouteExists(BgpServerTest *server, const string &instance,
        const string &prefix) {
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(server, instance, prefix),
            NULL, 1000, 10000,
            "Wait for route " << prefix << " in " << instance);
    }

    void VerifyInetRouteNoExists(BgpServerTest *server, const string &instance,
        const string &prefix) {
        TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(server, instance, prefix),
            NULL, 1000, 10000,
            "Wait for route " << prefix << " in " << instance " to go away");
    }


    vector<string> GetExportRouteTargetList(BgpServerTest *server,
        const string &instance) {
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

    size_t GetExportRouteTargetListSize(BgpServerTest *server,
        const string &instance) {
        return GetExportRouteTargetList(server, instance).size();
    }

    vector<string> GetImportRouteTargetList(BgpServerTest *server,
        const string &instance) {
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

    size_t GetImportRouteTargetListSize(BgpServerTest *server,
        const string &instance) {
        return GetImportRouteTargetList(server, instance).size();
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
    XmppServerTest *cn1_xmpp_server_;
    XmppServerTest *cn2_xmpp_server_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_2_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn1_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn2_;
    static int validate_done_;
};
int RTargetPeerTest::validate_done_;

TEST_F(RTargetPeerTest, BasicRouteAddDelete) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");

    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");

    VerifyInetRouteNoExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");
}

TEST_F(RTargetPeerTest, BasicSubscribeUnsubscribe) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");

    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

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

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    RemoveRouteTarget(mx_.get(), "blue", "target:64496:1");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, AddRTargetToInstance) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    task_util::WaitForIdle();

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    AddRouteTarget(cn1_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();
    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    AddRouteTarget(cn2_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:4");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteNoExists(cn2_.get(), "blue", "1.1.2.3/32");

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, HTTPIntrospect) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

    std::vector<string> result = list_of("target:1:4")("target:64496:1")
     ("target:64496:2")("target:64496:3")("target:64496:4")("target:64496:5");
    VerifyRtGroupSandesh(mx_.get(), result);
    VerifyRtGroupSandesh(cn1_.get(), result);
    VerifyRtGroupSandesh(cn2_.get(), result);

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");
}

TEST_F(RTargetPeerTest, BulkRouteAdd) {
    AddInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32", 100);
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyInetRouteExists(mx_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn1_.get(), "blue", "1.1.2.3/32");
    VerifyInetRouteExists(cn2_.get(), "blue", "1.1.2.3/32");

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

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    DeleteInetRoute(mx_.get(), NULL, "blue", "1.1.2.3/32");
}

TEST_F(RTargetPeerTest, DuplicateUnsubscribe) {
    agent_a_1_->Subscribe("blue", 2); 
    agent_a_1_->Unsubscribe("blue");
    agent_a_1_->Unsubscribe("blue");
}

TEST_F(RTargetPeerTest, ASNUpdate) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(4, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(4, RTargetRouteCount(cn2_.get()));

    agent_a_1_->Subscribe("blue", 2); 
    agent_b_1_->Subscribe("blue", 2); 
    agent_a_2_->Subscribe("blue", 2); 
    agent_b_2_->Subscribe("blue", 2); 

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(5, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(5, RTargetRouteCount(cn2_.get()));

    UpdateASN();
    task_util::WaitForIdle();

    BringupAgents();
    agent_a_1_->Subscribe("blue", 2);
    agent_b_1_->Subscribe("blue", 2);
    agent_a_2_->Subscribe("blue", 2);
    agent_b_2_->Subscribe("blue", 2);

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(5, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(5, RTargetRouteCount(cn2_.get()));

    agent_a_1_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    TASK_UTIL_EXPECT_EQ(4, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(4, RTargetRouteCount(cn2_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
}

//
// Test to validate the defer logic of peer deletion.
// Peer will not be deleted till RTGroupManager stops referring to it
// in InterestedPeers list
//
TEST_F(RTargetPeerTest, DeletedPeer) {
    // CN1 & CN2 removes all rtarget routes
    agent_a_2_->Unsubscribe("blue-i1");
    agent_b_2_->Unsubscribe("blue-i1");
    agent_a_2_->Unsubscribe("red-i2");
    agent_b_2_->Unsubscribe("red-i2");
    agent_a_2_->Unsubscribe("red");
    agent_b_2_->Unsubscribe("red");
    agent_a_2_->Unsubscribe("purple");
    agent_b_2_->Unsubscribe("purple");
    agent_a_1_->Unsubscribe("blue-i1");
    agent_b_1_->Unsubscribe("blue-i1");
    agent_a_1_->Unsubscribe("red-i2");
    agent_b_1_->Unsubscribe("red-i2");
    agent_a_1_->Unsubscribe("red");
    agent_b_1_->Unsubscribe("red");
    agent_a_1_->Unsubscribe("purple");
    agent_b_1_->Unsubscribe("purple");

    TASK_UTIL_EXPECT_EQ(0, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(0, RTargetRouteCount(cn1_.get()));

    // CN1 generates rtarget route for blue import rt
    agent_a_1_->Subscribe("blue", 2);
    agent_b_1_->Subscribe("blue", 2);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(1, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(1, RTargetRouteCount(cn2_.get()));

    // Stop the RTGroupMgr processing of RTargetRoute notification
    DisableRtargetRouteProcessing(cn2_.get());

    size_t count = cn2_->lifetime_manager()->GetQueueDeferCount();

    // Delete the bgp peering from both CNs.
    UnconfigureBgpPeering(cn1_.get(), cn2_.get());

    // Wait for the peer to go down in CN2
    TASK_UTIL_EXPECT_EQ_MSG(1, cn2_->NumUpPeer(),
                            "Wait for control-node peer to go down");

    // Route on CN2 will not be deleted due to DBState from RTargetGroupMgr
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");

    // Ensure that the path from CN1 is deleted
    BgpRoute *rt_route = RTargetRouteLookup(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(rt_route->count(), 0);

    // Make sure that lifetime manager does not delete this peer as there is
    // a reference to the peer from RTargetGroupMgr's work queue.
    TASK_UTIL_EXPECT_TRUE(
        cn2_->lifetime_manager()->GetQueueDeferCount() > count);

    // Enable the RTGroupMgr processing
    EnableRtargetRouteProcessing(cn2_.get());

    // Verify both RTargetGroupMgr's are empty.
    TASK_UTIL_EXPECT_TRUE(cn1_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    TASK_UTIL_EXPECT_TRUE(cn2_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    VerifyAllPeerUp(cn2_.get(), 1);

    // Route on CN2 should be deleted now.
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
}

TEST_F(RTargetPeerTest, SameRTInMultipleVRF) {
    agent_a_1_->Subscribe("blue", 2);
    agent_b_1_->Subscribe("blue", 2);
    agent_a_2_->Subscribe("blue", 2);
    agent_b_2_->Subscribe("blue", 2);

    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn1_.get(), "blue-i1"));
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn2_.get(), "blue-i1"));

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "blue-i1", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue-i1", "target:1:99");

    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn1_.get(), "blue-i1"));
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn2_.get(), "blue-i1"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");

    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn1_.get(), "blue-i1"));
    TASK_UTIL_EXPECT_EQ(1, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2, GetExportRouteTargetListSize(cn2_.get(), "blue-i1"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    agent_a_1_->Unsubscribe("blue");
    agent_a_2_->Unsubscribe("blue");
    agent_b_1_->Unsubscribe("blue");
    agent_b_2_->Unsubscribe("blue");

    RemoveRouteTarget(cn1_.get(), "blue-i1", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue-i1", "target:1:99");

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:1:99");
}

TEST_F(RTargetPeerTest, ConnectedVRF) {
    // RED and PURPLE are connected
    AddRouteTarget(cn1_.get(), "red", "target:1:99");
    AddRouteTarget(cn2_.get(), "red", "target:1:99");

    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn2_.get(), "purple"));

    RemoveConnection(cn1_.get(), "red", "purple");
    RemoveConnection(cn2_.get(), "red", "purple");

    TASK_UTIL_EXPECT_EQ(2, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(2, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(1, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(1, GetImportRouteTargetListSize(cn2_.get(), "purple"));
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");

    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(3, GetImportRouteTargetListSize(cn2_.get(), "purple"));
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
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
