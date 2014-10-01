
/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/service_chaining.h"
#include "bgp/routing-instance/static_route.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"

#include <fstream>
#include <list>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/community.h"
#include "bgp/bgp_factory.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
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
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"

using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using namespace pugi;
using namespace test;

static const char *config_control_node= "\
<config>\
    <routing-instance name='default-domain:default-project:ip-fabric:__default__'>\
    <bgp-router name=\'CN1\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
            <family>erm-vpn</family>\
        </address-families>\
        <session to=\'CN2\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'CN2\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'MX\'>\
        <identifier>192.168.0.3</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>erm-vpn</family>\
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
        <vrf-target>target:64496:1</vrf-target>\
        <vrf-target>target:1:4</vrf-target>\
    </routing-instance>\
    <routing-instance name='ecmp'>\
        <vrf-target>target:64496:1</vrf-target>\
        <vrf-target>target:1:4</vrf-target>\
    </routing-instance>\
    <routing-instance name='public'>\
        <vrf-target>target:1:1</vrf-target>\
        <vrf-target>\
            target:64496:4\
            <import-export>export</import-export>\
        </vrf-target>\
        <vrf-target>\
            target:64496:5\
            <import-export>export</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='public-i1'>\
        <vrf-target>target:1:1</vrf-target>\
        <vrf-target>\
            target:64496:5\
            <import-export>export</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";

class ServiceChainTest : public ::testing::Test {
protected:
    ServiceChainTest()
        : thread_(&evm_),
          cn1_xmpp_server_(NULL),
          cn2_xmpp_server_(NULL),
          connected_table_(NULL),
          aggregate_enable_(false),
          mx_push_connected_(false),
          single_si_(true),
          left_to_right_(true) {
    }

    virtual void SetUp() {
        IFMapServerParser *parser = IFMapServerParser::GetInstance("schema");
        bgp_schema_ParserInit(parser);
        vnc_cfg_ParserInit(parser);

        cn1_.reset(new BgpServerTest(&evm_, "CN1"));
        cn1_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 1 at port: " <<
            cn1_->session_manager()->GetPort());

        cn1_xmpp_server_ = new XmppServer(&evm_,
            test::XmppDocumentMock::kControlNodeJID);
        cn1_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn1_xmpp_server_->GetPort());

        cn2_.reset(new BgpServerTest(&evm_, "CN2"));
        cn2_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created Control-Node 2 at port: " <<
            cn2_->session_manager()->GetPort());

        cn2_xmpp_server_ = new XmppServer(&evm_,
            test::XmppDocumentMock::kControlNodeJID);
        cn2_xmpp_server_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            cn2_xmpp_server_->GetPort());
        mx_.reset(new BgpServerTest(&evm_, "MX"));
        mx_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created MX at port: " << mx_->session_manager()->GetPort());

        if (aggregate_enable_) {
            cn1_->service_chain_mgr()->set_aggregate_host_route(true);
            cn2_->service_chain_mgr()->set_aggregate_host_route(true);
        }

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
            cn1_xmpp_server_->GetPort(), "127.0.0.1"));

        // Create XMPP Agent on compute node 1 connected to XMPP server 
        // Control-node-2
        agent_a_2_.reset(new test::NetworkAgentMock(&evm_, "agent-a", 
            cn2_xmpp_server_->GetPort(), "127.0.0.1"));

        // Create XMPP Agent on compute node 2 connected to XMPP server 
        // Control-node-1 
        agent_b_1_.reset(new test::NetworkAgentMock(&evm_, "agent-b", 
            cn1_xmpp_server_->GetPort(), "127.0.0.2"));

        // Create XMPP Agent on compute node 2 connected to XMPP server
        // Control-node-2
        agent_b_2_.reset(new test::NetworkAgentMock(&evm_, "agent-b", 
            cn2_xmpp_server_->GetPort(), "127.0.0.2"));

        TASK_UTIL_EXPECT_TRUE(agent_a_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_a_2_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_2_->IsEstablished());
        TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_cn1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_cn2_->NumUpPeer());

        SubscribeAgentsToInstances();
        task_util::WaitForIdle();
    }

    void SubscribeAgents(const string &instance_name, int vrf_id) {
        agent_a_1_->Subscribe(instance_name, vrf_id);
        agent_a_2_->Subscribe(instance_name, vrf_id);
        agent_b_1_->Subscribe(instance_name, vrf_id);
        agent_b_2_->Subscribe(instance_name, vrf_id);
        task_util::WaitForIdle();
    }

    void UnsubscribeAgents(const string &instance_name) {
        agent_a_1_->Unsubscribe(instance_name);
        agent_a_2_->Unsubscribe(instance_name);
        agent_b_1_->Unsubscribe(instance_name);
        agent_b_2_->Unsubscribe(instance_name);
        task_util::WaitForIdle();
    }

    void SubscribeAgentsToInstances() {
        SubscribeAgents("blue-i1", 1);
        SubscribeAgents("blue", 2);
        SubscribeAgents("red", 3);
        SubscribeAgents("purple", 4);
        SubscribeAgents("red-i2", 5);

        if (single_si_)
            return;

        SubscribeAgents("blue-i3", 6);
        SubscribeAgents("red-i4", 7);
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


    void VerifyAllPeerUp(BgpServerTest *server) {
        TASK_UTIL_EXPECT_EQ_MSG(2, server->num_bgp_peer(),
            "Wait for all peers to get configured");
        TASK_UTIL_EXPECT_EQ_MSG(2, server->NumUpPeer(),
            "Wait for all peers to come up");

        LOG(DEBUG, "All Peers are up: " << server->localname());
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
        mx_->Configure(config_mx_vrf);
        task_util::WaitForIdle();

        VerifyAllPeerUp(cn1_.get());
        VerifyAllPeerUp(cn2_.get());
        VerifyAllPeerUp(mx_.get());
        task_util::WaitForIdle();

        ConfigureInstancesAndConnections();
        task_util::WaitForIdle();
    }

    void ConfigureInstancesAndConnections() {
        vector<string> instance_names;
        multimap<string, string> connections;

        if (single_si_) {
            instance_names = list_of
                ("blue")("blue-i1")("red-i2")("red")("purple");
            connections = map_list_of
                ("blue","blue-i1")("red-i2","red")("red","purple");
        } else {
            instance_names = list_of
                ("blue")("blue-i1")("red-i2")("blue-i3")("red-i4")("red");
            connections = map_list_of
                ("blue","blue-i1")("red-i2","blue-i3")("red-i4","red");
        }
        NetworkConfig(instance_names, connections);

        VerifyNetworkConfig(cn1_.get(), instance_names);
        VerifyNetworkConfig(cn2_.get(), instance_names);

        auto_ptr<autogen::ServiceChainInfo> params;
        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");
        SetServiceChainProperty(cn1_.get(), "blue-i1", params);
        task_util::WaitForIdle();
        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_1.xml");
        SetServiceChainProperty(cn2_.get(), "blue-i1", params);
        task_util::WaitForIdle();

        if (single_si_)
            return;

        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_5.xml");
        SetServiceChainProperty(cn1_.get(), "blue-i3", params);
        task_util::WaitForIdle();
        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_5.xml");
        SetServiceChainProperty(cn2_.get(), "blue-i3", params);
        task_util::WaitForIdle();
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
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        for (vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(rim->GetRoutingInstance(*iter),
                NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti = rim->GetRoutingInstance(*iter);
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

    void DisableServiceChainQ(BgpServerTest *server) {
        server->service_chain_mgr()->DisableQueue();
    }

    void EnableServiceChainQ(BgpServerTest *server) {
        server->service_chain_mgr()->EnableQueue();
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

    BgpRoute *InetRouteLookup(BgpServerTest *server, const string &instance_name,
                              const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable(instance_name + ".inet.0"));
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void AddInetRoute(BgpServerTest *server, IPeer *peer,
                      const string &instance_name,
                      const string &prefix, int localpref, 
                      vector<uint32_t> sglist = vector<uint32_t>(),
                      set<string> encap = set<string>(),
                      string nexthop="7.8.9.1", 
                      uint32_t flags=0, int label=303) {
        boost::system::error_code error;
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

        ExtCommunitySpec ext_comm;
        for (vector<uint32_t>::iterator it = sglist.begin();
            it != sglist.end(); it++) {
            SecurityGroup sgid(0, *it);
            ext_comm.communities.push_back(sgid.GetExtCommunityValue());
        }
        for (set<string>::iterator it = encap.begin();
            it != encap.end(); it++) {
            TunnelEncap tunnel_encap(*it);
            ext_comm.communities.push_back(tunnel_encap.GetExtCommunityValue());
        }
        attr_spec.push_back(&ext_comm);

        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = static_cast<BgpTable *>
            (server->database()->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void DeleteInetRoute(BgpServerTest *server, IPeer *peer,
                         const string &instance_name,
                         const string &prefix) {
        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        DB *db = server->database();
        BgpTable *table =
            static_cast<BgpTable *>(db->FindTable(instance_name + ".inet.0"));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    BgpRoute *InetVpnRouteLookup(BgpServerTest *server, const string &prefix) {
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("bgp.l3vpn.0"));
        EXPECT_TRUE(table != NULL);
        boost::system::error_code error;
        InetVpnPrefix nlri = InetVpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetVpnTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyInetVpnRouteExists(BgpServerTest *server, string prefix) {
        TASK_UTIL_WAIT_NE_NO_MSG(InetVpnRouteLookup(server, prefix),
                                 NULL, 1000, 10000,
                                 "Wait for route in bgp.l3vpn.0..");
    }

    void VerifyInetVpnRouteNoExists(BgpServerTest *server, string prefix) {
        TASK_UTIL_WAIT_EQ_NO_MSG(InetVpnRouteLookup(server, prefix),
                                 NULL, 1000, 10000,
                                 "Wait for route in bgp.l3vpn.0 to go away..");
    }

    void AddConnectedRoute(bool ecmp = false,
        const string &prefix = "1.1.2.3/32",
        const string &nexthop1 = "88.88.88.88",
        const string &nexthop2 = "99.99.99.99") {
        if (mx_push_connected_) {
            AddInetRoute(mx_.get(), NULL, "blue", prefix, 100);
            if (ecmp)
                AddInetRoute(mx_.get(), NULL, "ecmp", prefix, 100,
                             vector<uint32_t>(), set<string>(),
                             "1.2.2.1");
        } else {
            agent_a_1_->AddRoute(connected_table_, prefix, nexthop1, 100);
            agent_a_2_->AddRoute(connected_table_, prefix, nexthop1, 100);
            if (ecmp) {
                agent_b_1_->AddRoute(connected_table_, prefix, nexthop2, 100);
                agent_b_2_->AddRoute(connected_table_, prefix, nexthop2, 100);
            }
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(bool ecmp = false,
        const string &prefix = "1.1.2.3/32") {
        // Add Connected route
        if (mx_push_connected_) {
            DeleteInetRoute(mx_.get(), NULL, "blue", prefix);
            if (ecmp)
                DeleteInetRoute(mx_.get(), NULL, "ecmp", prefix);
        } else {
            agent_a_1_->DeleteRoute(connected_table_, prefix);
            agent_a_2_->DeleteRoute(connected_table_, prefix);
            if (ecmp) {
                agent_b_1_->DeleteRoute(connected_table_, prefix);
                agent_b_2_->DeleteRoute(connected_table_, prefix);
            }
        }
        task_util::WaitForIdle();
    }

    struct PathVerify {
        string path_id;
        string path_src;
        string nexthop;
        set<string> encaps;
        string origin_vn;
        PathVerify(string path_id, string path_src, string nexthop, 
                   set<string> encaps, string origin_vn)
            : path_id(path_id), path_src(path_src), nexthop(nexthop),
              encaps(encaps), origin_vn(origin_vn) {
        }
    };

    bool MatchResult(BgpServerTest *server, string prefix, 
                     vector<PathVerify> verify) {
        task_util::TaskSchedulerLock lock;

        // Verify number of paths
        BgpRoute *svc_route = InetRouteLookup(server, "blue", prefix);
        if (!svc_route || svc_route->count() != verify.size()) {
            return false;
        }
        vector<PathVerify>::iterator vit;
        Route::PathList::iterator it;
        for (it = svc_route->GetPathList().begin(),
             vit = verify.begin();
             it != svc_route->GetPathList().end(); it++, vit++) {
            BgpPath *path = static_cast<BgpPath *>(it.operator->());
            BgpAttrPtr attr = path->GetAttr();
            set<string> list = GetTunnelEncapListFromRoute(path);
            if (BgpPath::PathIdString(path->GetPathId()) != vit->path_id) return false;
            if (BgpPath::PathSourceString(path->GetSource()) != vit->path_src) return false;
            if (attr->nexthop().to_v4().to_string() != vit->nexthop) return false;
            if (list != vit->encaps) return false;
            if (GetOriginVnFromRoute(server, path) != vit->origin_vn) return false;
        }

        return true;
    }

    void VerifyServiceChainRoute(BgpServerTest *server, string prefix, 
                                 vector<PathVerify> verify) {
        // First wait for the route.
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(server, "blue", prefix),
                                 NULL, 1000, 10000, 
                                 "Wait for route in blue..");

        // Verify each path for specific attribute as per the PathVerify list
        BgpRoute *rt = InetRouteLookup(server, "blue", prefix);
        TASK_UTIL_WAIT_EQ_NO_MSG(MatchResult(server, prefix, verify),
                                 true, 1000, 10000, 
                                 "Wait for correct route in blue..");
        LOG(DEBUG, "Prefix " << prefix << "has " << rt->GetPathList().size());
    }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    auto_ptr<autogen::ServiceChainInfo> GetChainConfig(const string &filename) {
        auto_ptr<autogen::ServiceChainInfo> params(
            new autogen::ServiceChainInfo());
        string content = FileRead(filename);
        istringstream sstream(content);
        xml_document xdoc;
        xml_parse_result result = xdoc.load(sstream);
        if (!result) {
            BGP_WARN_UT("Unable to load XML document. (status="
                << result.status << ", offset=" << result.offset << ")");
            assert(0);
        }
        xml_node node = xdoc.first_child(); 
        params->XmlParse(node);
        return params;
    }

    auto_ptr<autogen::StaticRouteEntriesType> GetStaticRouteConfig(
        const string &filename) {
        auto_ptr<autogen::StaticRouteEntriesType> params(
            new autogen::StaticRouteEntriesType());
        string content = FileRead(filename);
        istringstream sstream(content);
        xml_document xdoc;
        xml_parse_result result = xdoc.load(sstream);
        if (!result) {
            BGP_WARN_UT("Unable to load XML document. (status="
                << result.status << ", offset=" << result.offset << ")");
            assert(0);
        }
        xml_node node = xdoc.first_child(); 
        params->XmlParse(node);
        return params;
    }

    void SetServiceChainProperty(BgpServerTest *server, const string &instance,
        auto_ptr<autogen::ServiceChainInfo> params) {
        ifmap_test_util::IFMapMsgPropertyAdd(server->config_db(),
            "routing-instance", instance,
            "service-chain-information", params.release(), 0);
        task_util::WaitForIdle();
    }

    void RemoveServiceChainProperty(BgpServerTest *server,
        const string &instance) {
        ifmap_test_util::IFMapMsgPropertyDelete(server->config_db(),
            "routing-instance", instance, "service-chain-information");
        task_util::WaitForIdle();
    }

    vector<uint32_t> GetSGIDListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        vector<uint32_t> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_security_group(comm))
                continue;
            SecurityGroup security_group(comm);

            list.push_back(security_group.security_group_id());
        }
        sort(list.begin(), list.end());
        return list;
    }

    set<string> GetTunnelEncapListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        set<string> list;
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_tunnel_encap(comm))
                continue;
            TunnelEncap encap(comm);
            list.insert(TunnelEncapType::TunnelEncapToString(
                encap.tunnel_encap()));
        }
        return list;
    }

    string GetOriginVnFromRoute(BgpServerTest *server,
        const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_origin_vn(comm))
                continue;
            OriginVn origin_vn(comm);
            return server->routing_instance_mgr()->GetVirtualNetworkByVnIndex(
                origin_vn.vn_index());
        }
        return "unresolved";
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
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn1_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn2_;
    const char *connected_table_;
    bool aggregate_enable_;
    bool mx_push_connected_;
    bool single_si_;
    bool left_to_right_;
};

typedef tr1::tuple<bool, bool, bool> TestParams;

class ServiceIntergrationParamTest :
    public ServiceChainTest,
    public ::testing::WithParamInterface<TestParams> {
    virtual void SetUp() {
        connected_table_ =
            tr1::get<0>(GetParam()) ? "blue-i1" : "blue";
        aggregate_enable_ = tr1::get<1>(GetParam());
        mx_push_connected_ = tr1::get<2>(GetParam());
        single_si_ = true;
        ServiceChainTest::SetUp();
    }

    virtual void TearDown() {
        ServiceChainTest::TearDown();
    }
};

TEST_P(ServiceIntergrationParamTest, Basic) {
    agent_a_1_->AddRoute("red", "192.168.1.1/32");
    agent_a_2_->AddRoute("red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    vector<PathVerify> path_list;
    if (mx_push_connected_) {
        PathVerify verify_1("7.8.9.1", "SericeChain",  "7.8.9.1", set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "BGP_XMPP",  "7.8.9.1", set<string>(), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
    } else {
        PathVerify verify_1("88.88.88.88", "SericeChain",  "88.88.88.88", list_of("gre"), "red");
        PathVerify verify_2("88.88.88.88", "BGP_XMPP",  "88.88.88.88", list_of("gre"), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
    }

    if (aggregate_enable_) {
        // Check for aggregated route
        VerifyServiceChainRoute(cn1_.get(), "192.168.1.0/24", path_list);
        VerifyServiceChainRoute(cn2_.get(), "192.168.1.0/24", path_list);
    } else {
        // Check for aggregated route
        VerifyServiceChainRoute(cn1_.get(), "192.168.1.1/32", path_list);
        VerifyServiceChainRoute(cn2_.get(), "192.168.1.1/32", path_list);
    }

    agent_a_1_->DeleteRoute("red", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("red", "192.168.1.1/32");

    DeleteConnectedRoute();
}

// 
// Test verify the ECMP for service instance. Connected routes can be pushed by 
// MX or from Agent
//
TEST_P(ServiceIntergrationParamTest, ECMP) {
    agent_a_1_->AddRoute("red", "192.168.1.1/32");
    agent_a_2_->AddRoute("red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    vector<PathVerify> path_list;
    if (mx_push_connected_) { 
        PathVerify verify_1("1.2.2.1", "SericeChain", "1.2.2.1", set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "SericeChain", "7.8.9.1", set<string>(), "red");
        PathVerify verify_3("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "red");
        PathVerify verify_4("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "SericeChain", "88.88.88.88", list_of  ("gre"), "red");
        PathVerify verify_2("99.99.99.99", "SericeChain", "99.99.99.99", list_of  ("gre"), "red");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("gre"), "red");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("gre"), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    if (aggregate_enable_) {
        // Check for aggregated route
        VerifyServiceChainRoute(cn1_.get(), "192.168.1.0/24", path_list);
        VerifyServiceChainRoute(cn2_.get(), "192.168.1.0/24", path_list);
    } else {
        // Check for aggregated route
        VerifyServiceChainRoute(cn1_.get(), "192.168.1.1/32", path_list);
        VerifyServiceChainRoute(cn2_.get(), "192.168.1.1/32", path_list);
    }
 
    agent_a_1_->DeleteRoute("red", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("red", "192.168.1.1/32");

    DeleteConnectedRoute(true);
}

//
// Verify ecmp for ext connected route
//
TEST_P(ServiceIntergrationParamTest, ExtRoute) {
    // Not applicable
    aggregate_enable_ = false;

    // Add external route from MX to dest routing instance
    AddInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    vector<PathVerify> path_list;

    if (mx_push_connected_) { 
        PathVerify verify_1("1.2.2.1", "SericeChain", "1.2.2.1", set<string>(), "red");
        PathVerify verify_2("7.8.9.1", "SericeChain", "7.8.9.1", set<string>(), "red");
        PathVerify verify_3("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "red");
        PathVerify verify_4("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "SericeChain", "88.88.88.88", list_of  ("gre"), "red");
        PathVerify verify_2("99.99.99.99", "SericeChain", "99.99.99.99", list_of  ("gre"), "red");
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("gre"), "red");
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("gre"), "red");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    VerifyServiceChainRoute(cn1_.get(), "10.1.1.0/24", path_list);
    VerifyServiceChainRoute(cn2_.get(), "10.1.1.0/24", path_list);

    DeleteInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24");

    DeleteConnectedRoute(true);
}

//
// For ext connected route pushed by MX contains route target which in export list of dest routing instance
// Service chain route will not be generated as origin VN is not correct
//
TEST_P(ServiceIntergrationParamTest, RouteTarget) {
    // Not applicable
    aggregate_enable_ = false;

    // Add external route from MX to a VN which is connected to dest routing instance
    AddInetRoute(mx_.get(), NULL, "public-i1", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "red", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Replicated route in red..");
    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "red", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for Replicated route in red..");

    // Check for ServiceChainRoute
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ServiceChain route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "10.1.1.0/24"),
                             NULL, 1000, 10000, 
                             "Wait for ServiceChain route in blue..");

    DeleteInetRoute(mx_.get(), NULL, "public-i1", "10.1.1.0/24");

    DeleteConnectedRoute(true);
}

//
// Verify that service chain route is not created when origin VN of the source route
// doesn't contain correct origin vn
//
TEST_P(ServiceIntergrationParamTest, OriginVn) {
    // Not applicable
    aggregate_enable_ = false;

    agent_a_1_->AddRoute("purple", "192.168.1.1/32");
    agent_a_2_->AddRoute("purple", "192.168.1.1/32");

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn1_.get(), "red", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for Replicated route in red..");
    // Check for Replicated
    TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(cn2_.get(), "red", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for Replicated route in red..");

    // Check for aggregated route
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn1_.get(), "blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
    TASK_UTIL_WAIT_EQ_NO_MSG(InetRouteLookup(cn2_.get(), "blue", "192.168.1.1/32"),
                             NULL, 1000, 10000, 
                             "Wait for Aggregate route in blue..");
 

    agent_a_1_->DeleteRoute("purple", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("purple", "192.168.1.1/32");

    DeleteConnectedRoute(true);
}

//
// Verify when externally connected route is available as static route.
//
TEST_P(ServiceIntergrationParamTest, StaticRoute) {
    // Not applicable
    aggregate_enable_ = false;
    auto_ptr<autogen::StaticRouteEntriesType> params;

    params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn1_->config_db(), "routing-instance",
        "blue-i1", "static-route-entries", params.release(), 0);

    params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn2_->config_db(), "routing-instance",
        "blue-i1", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    vector<PathVerify> path_list;
    if (mx_push_connected_) {
        PathVerify verify_1("1.2.2.1", "StaticRoute", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_2("7.8.9.1", "StaticRoute", "7.8.9.1", set<string>(), "blue");
        PathVerify verify_3("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_4("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    } else {
        PathVerify verify_1("88.88.88.88", "StaticRoute", "88.88.88.88", list_of  ("gre"), connected_table_);
        PathVerify verify_2("99.99.99.99", "StaticRoute", "99.99.99.99", list_of  ("gre"), connected_table_);
        PathVerify verify_3("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("gre"), connected_table_);
        PathVerify verify_4("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("gre"), connected_table_);
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
    }

    // Check for ServiceChain route
    VerifyServiceChainRoute(cn1_.get(), "10.1.1.0/24", path_list);
    VerifyServiceChainRoute(cn2_.get(), "10.1.1.0/24", path_list);

    DeleteConnectedRoute(true);
}

//
// Verify when externally connected route is available as both static route
// and service chain route
//
TEST_P(ServiceIntergrationParamTest, DISABLED_SvcStaticRoute) {
    // Not applicable
    aggregate_enable_ = false;
    auto_ptr<autogen::StaticRouteEntriesType> params;

    params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn1_->config_db(), "routing-instance", 
        "blue-i1", "static-route-entries", params.release(), 0);

    params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn2_->config_db(), "routing-instance", 
        "blue-i1", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();


    // Add external route from MX to a VN which is connected to dest routing instance
    AddInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected route
    AddConnectedRoute(true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    vector<PathVerify> path_list;
    if (mx_push_connected_) { 
        PathVerify verify_1("1.2.2.1", "StaticRoute", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_2("7.8.9.1", "StaticRoute", "7.8.9.1", set<string>(), "blue");
        PathVerify verify_3("1.2.2.1", "SericeChain", "1.2.2.1", set<string>(), "red");
        PathVerify verify_4("7.8.9.1", "SericeChain", "7.8.9.1", set<string>(), "red");
        PathVerify verify_5("1.2.2.1", "BGP_XMPP", "1.2.2.1", set<string>(), "blue");
        PathVerify verify_6("7.8.9.1", "BGP_XMPP", "7.8.9.1", set<string>(), "blue");
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
        path_list.push_back(verify_5);
        path_list.push_back(verify_6);
    } else {
        PathVerify verify_1("88.88.88.88", "StaticRoute", "88.88.88.88", list_of  ("gre"), connected_table_);
        PathVerify verify_2("99.99.99.99", "StaticRoute", "99.99.99.99", list_of  ("gre"), connected_table_);
        PathVerify verify_3("88.88.88.88", "SericeChain", "88.88.88.88", list_of  ("gre"), "red");
        PathVerify verify_4("99.99.99.99", "SericeChain", "99.99.99.99", list_of  ("gre"), "red");
        PathVerify verify_5("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("gre"), connected_table_);
        PathVerify verify_6("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("gre"), connected_table_);
        path_list.push_back(verify_1);
        path_list.push_back(verify_2);
        path_list.push_back(verify_3);
        path_list.push_back(verify_4);
        path_list.push_back(verify_5);
        path_list.push_back(verify_6);
    }

    // Check for ServiceChain route
    VerifyServiceChainRoute(cn1_.get(), "10.1.1.0/24", path_list);
    VerifyServiceChainRoute(cn2_.get(), "10.1.1.0/24", path_list);

    DeleteInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24");

    DeleteConnectedRoute(true);
}

// 
// Each Agent has multiple l3 interfaces and instance for static routes is
// created on both compute nodes.
//
TEST_P(ServiceIntergrationParamTest, MultipleL3Intf) {
    // Not applicable
    aggregate_enable_ = false;
    mx_push_connected_ = false;

    auto_ptr<autogen::StaticRouteEntriesType> params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(cn1_->config_db(), "routing-instance", 
                         "blue-i1", "static-route-entries", params.release(), 0);

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn2_->config_db(), "routing-instance", 
                         "blue-i1", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();

    // Add Connected route
    NextHops nexthops;
    nexthops.push_back(NextHop("88.88.88.88", 6, "udp"));
    nexthops.push_back(NextHop("99.99.99.99", 7, "udp"));

    agent_a_1_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    agent_a_2_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);

    nexthops.clear();
    nexthops.push_back(NextHop("66.66.66.66", 6, "udp"));
    nexthops.push_back(NextHop("77.77.77.77", 7, "udp"));
    agent_b_1_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    agent_b_2_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Check for aggregated route
    vector<PathVerify> path_list;
    PathVerify verify_1("66.66.66.66", "StaticRoute", "66.66.66.66", list_of  ("udp"), connected_table_);
    PathVerify verify_2("77.77.77.77", "StaticRoute", "77.77.77.77", list_of  ("udp"), connected_table_);
    PathVerify verify_3("88.88.88.88", "StaticRoute", "88.88.88.88", list_of  ("udp"), connected_table_);
    PathVerify verify_4("99.99.99.99", "StaticRoute", "99.99.99.99", list_of  ("udp"), connected_table_);
    PathVerify verify_5("66.66.66.66", "BGP_XMPP", "66.66.66.66", list_of  ("udp"), connected_table_);
    PathVerify verify_6("77.77.77.77", "BGP_XMPP", "77.77.77.77", list_of  ("udp"), connected_table_);
    PathVerify verify_7("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("udp"), connected_table_);
    PathVerify verify_8("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("udp"), connected_table_);
    path_list.push_back(verify_1);
    path_list.push_back(verify_2);
    path_list.push_back(verify_3);
    path_list.push_back(verify_4);
    path_list.push_back(verify_5);
    path_list.push_back(verify_6);
    path_list.push_back(verify_7);
    path_list.push_back(verify_8);

    // Check for ServiceChain route
    VerifyServiceChainRoute(cn1_.get(), "10.1.1.0/24", path_list);
    VerifyServiceChainRoute(cn2_.get(), "10.1.1.0/24", path_list);

    DeleteConnectedRoute(true);
}

//
// Each Agent has multiple l3 interfaces and service instance is created on
// both compute nodes.
// Tests both static route and service chain functionality.
//
TEST_P(ServiceIntergrationParamTest, DISABLED_MultipleL3Intf) {
    // Not applicable
    aggregate_enable_ = false;
    mx_push_connected_ = false;

    auto_ptr<autogen::StaticRouteEntriesType> params =
        GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");

    ifmap_test_util::IFMapMsgPropertyAdd(cn1_->config_db(), "routing-instance",
                         "blue-i1", "static-route-entries", params.release(), 0);

    params = GetStaticRouteConfig("controller/src/bgp/testdata/static_route_8.xml");
    ifmap_test_util::IFMapMsgPropertyAdd(cn2_->config_db(), "routing-instance",
                         "blue-i1", "static-route-entries", params.release(), 0);
    task_util::WaitForIdle();


    // Add external route from MX to a VN which is connected to dest routing instance
    AddInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24", 100);
    task_util::WaitForIdle();

    // Add Connected route
    NextHops nexthops;
    nexthops.push_back(NextHop("88.88.88.88", 6, "udp"));
    nexthops.push_back(NextHop("99.99.99.99", 7, "udp"));

    agent_a_1_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    agent_a_2_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);

    nexthops.clear();
    nexthops.push_back(NextHop("66.66.66.66", 6, "udp"));
    nexthops.push_back(NextHop("77.77.77.77", 7, "udp"));
    agent_b_1_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    agent_b_2_->AddRoute(connected_table_, "1.1.2.3/32", nexthops);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Check for aggregated route
    vector<PathVerify> path_list;
    PathVerify verify_1("66.66.66.66", "StaticRoute", "66.66.66.66", list_of  ("udp"), connected_table_);
    PathVerify verify_2("77.77.77.77", "StaticRoute", "77.77.77.77", list_of  ("udp"), connected_table_);
    PathVerify verify_3("88.88.88.88", "StaticRoute", "88.88.88.88", list_of  ("udp"), connected_table_);
    PathVerify verify_4("99.99.99.99", "StaticRoute", "99.99.99.99", list_of  ("udp"), connected_table_);
    PathVerify verify_5("66.66.66.66", "SericeChain", "66.66.66.66", list_of  ("udp"), "red");
    PathVerify verify_6("77.77.77.77", "SericeChain", "77.77.77.77", list_of  ("udp"), "red");
    PathVerify verify_7("88.88.88.88", "SericeChain", "88.88.88.88", list_of  ("udp"), "red");
    PathVerify verify_8("99.99.99.99", "SericeChain", "99.99.99.99", list_of  ("udp"), "red");
    PathVerify verify_9("66.66.66.66", "BGP_XMPP", "66.66.66.66", list_of  ("udp"), connected_table_);
    PathVerify verify_10("77.77.77.77", "BGP_XMPP", "77.77.77.77", list_of  ("udp"), connected_table_);
    PathVerify verify_11("88.88.88.88", "BGP_XMPP", "88.88.88.88", list_of  ("udp"), connected_table_);
    PathVerify verify_12("99.99.99.99", "BGP_XMPP", "99.99.99.99", list_of  ("udp"), connected_table_);
    path_list.push_back(verify_1);
    path_list.push_back(verify_2);
    path_list.push_back(verify_3);
    path_list.push_back(verify_4);
    path_list.push_back(verify_5);
    path_list.push_back(verify_6);
    path_list.push_back(verify_7);
    path_list.push_back(verify_8);
    path_list.push_back(verify_9);
    path_list.push_back(verify_10);
    path_list.push_back(verify_11);
    path_list.push_back(verify_12);

    // Check for ServiceChain route
    VerifyServiceChainRoute(cn1_.get(), "10.1.1.0/24", path_list);
    VerifyServiceChainRoute(cn2_.get(), "10.1.1.0/24", path_list);

    DeleteInetRoute(mx_.get(), NULL, "public", "10.1.1.0/24");

    DeleteConnectedRoute(true);
}

typedef tr1::tuple<bool, bool> TestParams2;

class ServiceIntergrationParamTest2 :
    public ServiceChainTest,
    public ::testing::WithParamInterface<TestParams2> {
    virtual void SetUp() {
        connected_table_ = "blue";
        mx_push_connected_ = false;
        single_si_ = false;
        aggregate_enable_ = tr1::get<0>(GetParam());
        left_to_right_ = tr1::get<1>(GetParam());
        ServiceChainTest::SetUp();
    }

    virtual void TearDown() {
        ServiceChainTest::TearDown();
    }
};

//
// Verify 2 in-network services on the same compute node.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TEST_P(ServiceIntergrationParamTest2, MultipleInNetwork) {
    // Add VM routes in red.
    agent_a_1_->AddRoute("red", "192.168.1.1/32");
    agent_a_2_->AddRoute("red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (left_to_right_) {
        AddConnectedRoute(false, "1.1.2.3/32", "8.8.8.8");
        AddConnectedRoute(false, "1.1.2.4/32", "8.8.8.8");
    } else {
        AddConnectedRoute(false, "1.1.2.4/32", "8.8.8.8");
        AddConnectedRoute(false, "1.1.2.3/32", "8.8.8.8");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (aggregate_enable_) {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.0/24");
    } else {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.1/32");
    }

    // Delete Connected routes for both service instances.
    if (left_to_right_) {
        DeleteConnectedRoute(false, "1.1.2.3/32");
        DeleteConnectedRoute(false, "1.1.2.4/32");
    } else {
        DeleteConnectedRoute(false, "1.1.2.4/32");
        DeleteConnectedRoute(false, "1.1.2.3/32");
    }

    // Delete VM routes in red.
    agent_a_1_->DeleteRoute("red", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("red", "192.168.1.1/32");
}

//
// Verify 2 in-network services scaled on 2 computes nodes each.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TEST_P(ServiceIntergrationParamTest2, MultipleInNetworkECMP) {
    // Add VM routes in red.
    agent_a_1_->AddRoute("red", "192.168.1.1/32");
    agent_a_2_->AddRoute("red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (left_to_right_) {
        AddConnectedRoute(true, "1.1.2.3/32", "8.8.8.8", "9.9.9.9");
        AddConnectedRoute(true, "1.1.2.4/32", "8.8.8.8", "9.9.9.9");
    } else {
        AddConnectedRoute(true, "1.1.2.4/32", "8.8.8.8", "9.9.9.9");
        AddConnectedRoute(true, "1.1.2.3/32", "8.8.8.8", "9.9.9.9");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (aggregate_enable_) {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "9.9.9.9:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "9.9.9.9:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "9.9.9.9:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "9.9.9.9:6:192.168.1.0/24");
    } else {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "9.9.9.9:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "9.9.9.9:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "9.9.9.9:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "9.9.9.9:6:192.168.1.1/32");
    }

    // Delete Connected routes for both service instances.
    if (left_to_right_) {
        DeleteConnectedRoute(true, "1.1.2.3/32");
        DeleteConnectedRoute(true, "1.1.2.4/32");
    } else {
        DeleteConnectedRoute(true, "1.1.2.4/32");
        DeleteConnectedRoute(true, "1.1.2.3/32");
    }

    // Delete VM routes in red.
    agent_a_1_->DeleteRoute("red", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("red", "192.168.1.1/32");
}

//
// Verify 2 in-network services on the same compute node, in cases where
// subscribe for the service routing instances happens later i.e. after
// the connected route is processed.
//
// The RDs used when advertising service chain routes for the 2 service
// instances should be different. The RD is based on the compute node's
// registration id for the service routing instances, rather then the
// connected routing instance.
//
TEST_P(ServiceIntergrationParamTest2, MultipleInNetworkUnsubscribeSubscribe) {
    // Add VM routes in red.
    agent_a_1_->AddRoute("red", "192.168.1.1/32");
    agent_a_2_->AddRoute("red", "192.168.1.1/32");
    task_util::WaitForIdle();

    // Add Connected routes for both service instances.
    if (left_to_right_) {
        AddConnectedRoute(false, "1.1.2.3/32", "8.8.8.8");
        AddConnectedRoute(false, "1.1.2.4/32", "8.8.8.8");
    } else {
        AddConnectedRoute(false, "1.1.2.4/32", "8.8.8.8");
        AddConnectedRoute(false, "1.1.2.3/32", "8.8.8.8");
    }

    // Wait for everything to be processed.
    TASK_UTIL_EXPECT_TRUE(cn1_.get()->service_chain_mgr()->IsQueueEmpty());
    TASK_UTIL_EXPECT_TRUE(cn2_.get()->service_chain_mgr()->IsQueueEmpty());

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (aggregate_enable_) {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.0/24");
    } else {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.1/32");
    }

    // Unsubscribe agents from blue-i1 and blue-i3.
    UnsubscribeAgents("blue-i1");
    UnsubscribeAgents("blue-i3");

    // Verify service chain routes are no longer in bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (aggregate_enable_) {
        VerifyInetVpnRouteNoExists(cn1_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteNoExists(cn1_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteNoExists(cn2_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteNoExists(cn2_.get(), "8.8.8.8:6:192.168.1.0/24");
    } else {
        VerifyInetVpnRouteNoExists(cn1_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteNoExists(cn1_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteNoExists(cn2_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteNoExists(cn2_.get(), "8.8.8.8:6:192.168.1.1/32");
    }

    // Subscribe agents to blue-i1 and blue-i3.
    SubscribeAgents("blue-i1", 1);
    SubscribeAgents("blue-i3", 6);

    // Verify service chain routes are replicated to bgp.l3vpn.0.
    // Note that agents are registered to blue-i1 with instance id 1.
    // Note that agents are registered to blue-i3 with instance id 6.
    if (aggregate_enable_) {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.0/24");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.0/24");
    } else {
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn1_.get(), "8.8.8.8:6:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:1:192.168.1.1/32");
        VerifyInetVpnRouteExists(cn2_.get(), "8.8.8.8:6:192.168.1.1/32");
    }

    // Delete Connected routes for both service instances.
    if (left_to_right_) {
        DeleteConnectedRoute(false, "1.1.2.3/32");
        DeleteConnectedRoute(false, "1.1.2.4/32");
    } else {
        DeleteConnectedRoute(false, "1.1.2.4/32");
        DeleteConnectedRoute(false, "1.1.2.3/32");
    }

    // Delete VM routes in red.
    agent_a_1_->DeleteRoute("red", "192.168.1.1/32");
    agent_a_2_->DeleteRoute("red", "192.168.1.1/32");
}

INSTANTIATE_TEST_CASE_P(Instance, ServiceIntergrationParamTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool(), ::testing::Bool()));

INSTANTIATE_TEST_CASE_P(Instance, ServiceIntergrationParamTest2,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
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
