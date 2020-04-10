/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_BGP_TEST_SVC_STATIC_ROUTE_INTEGRATION_TEST_H_
#define SRC_BGP_TEST_SVC_STATIC_ROUTE_INTEGRATION_TEST_H_


#include "bgp/routing-instance/iservice_chain_mgr.h"
#include "bgp/routing-instance/istatic_route_mgr.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/routepath_replicator.h"

#include <fstream>
#include <list>
#include <algorithm>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/program_options.hpp>

#include "base/regex.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/community.h"
#include "bgp/bgp_factory.h"
#include "bgp/extended-community/site_of_origin.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_route.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "db/db_graph.h"
#include "db/test/db_test_util.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_node.h"
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

using namespace boost::program_options;
using namespace std;
using boost::assign::list_of;
using boost::assign::map_list_of;
using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;
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
            <family>inet6-vpn</family>\
            <family>route-target</family>\
            <family>erm-vpn</family>\
        </address-families>\
        <session to=\'CN2\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
            </address-families>\
        </session>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
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
            <family>inet6-vpn</family>\
            <family>erm-vpn</family>\
            <family>route-target</family>\
        </address-families>\
        <session to=\'MX\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
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
            <family>inet6-vpn</family>\
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

class ServiceChainIntegrationTestGlobals {
public:
    static string connected_table_;
    static bool transparent_;
    static bool aggregate_enable_;
    static bool mx_push_connected_;
    static bool single_si_;
    static bool left_to_right_;
};

string ServiceChainIntegrationTestGlobals::connected_table_;
bool ServiceChainIntegrationTestGlobals::transparent_;
bool ServiceChainIntegrationTestGlobals::aggregate_enable_;
bool ServiceChainIntegrationTestGlobals::mx_push_connected_;
bool ServiceChainIntegrationTestGlobals::single_si_;
bool ServiceChainIntegrationTestGlobals::left_to_right_;

struct PathVerify {
    string path_id;
    string path_src;
    string nexthop;
    set<string> encaps;
    string origin_vn;
    SiteOfOrigin soo;
    PathVerify(string path_id, string path_src, string nexthop,
               set<string> encaps, string origin_vn)
        : path_id(path_id), path_src(path_src), nexthop(nexthop),
          encaps(encaps), origin_vn(origin_vn) {
    }
    PathVerify(string path_id, string path_src, string nexthop,
               set<string> encaps, string origin_vn,
               const SiteOfOrigin &soo)
        : path_id(path_id), path_src(path_src), nexthop(nexthop),
          encaps(encaps), origin_vn(origin_vn), soo(soo) {
    }
};

//
// Template structure to pass to fixture class template. Needed because
// gtest fixture class template can accept only one template parameter.
//
template
<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
struct TypeDefinition {
  typedef T1 TableT;
  typedef T2 PrefixT;
  typedef T3 RouteT;
  typedef T4 VpnTableT;
  typedef T5 VpnPrefixT;
  typedef T6 VpnRouteT;
};

// TypeDefinitions that we want to test.
typedef TypeDefinition<InetTable, Ip4Prefix, InetRoute, InetVpnTable,
                       InetVpnPrefix, InetVpnRoute> InetDefinition;
typedef TypeDefinition<Inet6Table, Inet6Prefix, Inet6Route, Inet6VpnTable,
                       Inet6VpnPrefix, Inet6VpnRoute> Inet6Definition;

// Fixture class template - instantiated later for each TypeDefinition.
template <typename T>
class ServiceChainIntegrationTest : public ::testing::Test {
protected:
    typedef typename T::TableT TableT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnTableT VpnTableT;
    typedef typename T::VpnPrefixT VpnPrefixT;
    typedef typename T::VpnRouteT VpnRouteT;

    ServiceChainIntegrationTest()
            : thread_(&evm_), cn1_xmpp_server_(NULL), cn2_xmpp_server_(NULL),
              family_(GetFamily()), ipv6_prefix_("::ffff:") {
    }

    virtual void SetUp() {
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

        if (ServiceChainIntegrationTestGlobals::aggregate_enable_) {
            EnableServiceChainAggregation(cn1_.get());
            EnableServiceChainAggregation(cn2_.get());
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
        TASK_UTIL_EXPECT_EQ(2U, bgp_channel_manager_cn1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2U, bgp_channel_manager_cn2_->NumUpPeer());

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

        if (ServiceChainIntegrationTestGlobals::single_si_)
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

        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void VerifyAllPeerUp(BgpServerTest *server) {
        TASK_UTIL_EXPECT_EQ_MSG(2U, server->num_bgp_peer(),
                                "Wait for all peers to get configured");
        TASK_UTIL_EXPECT_EQ_MSG(2U, server->NumUpPeer(),
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
        std::vector<string> instance_names;
        multimap<string, string> connections;
        std::vector<string> networks;
        std::vector<int> network_ids;

        if (ServiceChainIntegrationTestGlobals::single_si_) {
            instance_names = list_of
                ("blue")("blue-i1")("red-i2")("red")("purple")
                    .convert_to_container<vector<string> >();
            networks = list_of("blue")("blue")("red")("red")("purple")
                .convert_to_container<vector<string> >();
            network_ids = list_of(1)(1)(2)(2)(3)
                .convert_to_container<vector<int> >();
            connections = map_list_of
                ("blue","blue-i1")("red-i2","red")("red","purple")
                    .convert_to_container<multimap<string, string> >();
        } else {
            instance_names = list_of
                ("blue")("blue-i1")("red-i2")("blue-i3")("red-i4")("red")
                    .convert_to_container<vector<string> >();
            networks = list_of("blue")("blue")("red")("blue")("red")("red")
                .convert_to_container<vector<string> >();
            network_ids = list_of(1)(1)(2)(1)(2)(2)
                .convert_to_container<vector<int> >();
            connections = map_list_of
                ("blue","blue-i1")("red-i2","blue-i3")("red-i4","red")
                    .convert_to_container<multimap<string, string> >();
        }
        NetworkConfig(instance_names, connections, networks, network_ids);

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

        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_6.xml");
        SetServiceChainProperty(cn1_.get(), "red-i2", params);
        task_util::WaitForIdle();

        params =
            GetChainConfig("controller/src/bgp/testdata/service_chain_6.xml");
        SetServiceChainProperty(cn2_.get(), "red-i2", params);
        task_util::WaitForIdle();

        if (ServiceChainIntegrationTestGlobals::single_si_)
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

    void NetworkConfig(const std::vector<string> &instance_names,
                       const multimap<string, string> &connections,
                       const std::vector<string> &networks,
                       const std::vector<int> &network_ids) {
        bgp_util::NetworkConfigGenerate(cn1_->config_db(), instance_names,
            connections, networks, network_ids);
        bgp_util::NetworkConfigGenerate(cn2_->config_db(), instance_names,
            connections, networks, network_ids);
    }

    void VerifyNetworkConfig(BgpServerTest *server,
        const std::vector<string> &instance_names) {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        for (std::vector<string>::const_iterator iter = instance_names.begin();
             iter != instance_names.end(); ++iter) {
            TASK_UTIL_WAIT_NE_NO_MSG(rim->GetRoutingInstance(*iter),
                NULL, 1000, 10000, "Wait for routing instance..");
            const RoutingInstance *rti = rim->GetRoutingInstance(*iter);
            TASK_UTIL_WAIT_NE_NO_MSG(rti->virtual_network_index(),
                0, 1000, 10000, "Wait for vn index..");
        }
    }

    void ToggleAllowTransit(BgpServerTest *server, const string &network) {
        autogen::VirtualNetworkType *property;
        {
            task_util::TaskSchedulerLock lock;
            IFMapNode *node = ifmap_test_util::IFMapNodeLookup(
                server->config_db(), "virtual-network", network);
            EXPECT_TRUE(node != NULL);
            IFMapObject *obj = node->Find(IFMapOrigin(IFMapOrigin::MAP_SERVER));
            EXPECT_TRUE(obj != NULL);
            autogen::VirtualNetwork *vn =
                dynamic_cast<autogen::VirtualNetwork *>(obj);
            EXPECT_TRUE(vn != NULL);
            property = new autogen::VirtualNetworkType;
            property->Copy(vn->properties());
            property->allow_transit = !property->allow_transit;
        }
        ifmap_test_util::IFMapMsgPropertyAdd(server->config_db(),
            "virtual-network", network,
            "virtual-network-properties", property, 0);
        task_util::WaitForIdle();
    }

    void VerifyInstanceIsTransit(BgpServer *server, const string &instance) {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        const RoutingInstance *rti = rim->GetRoutingInstance(instance);
        TASK_UTIL_EXPECT_TRUE(rti->virtual_network_allow_transit());
    }

    void Unconfigure() {
        char config[4096];
        snprintf(config, sizeof(config), "%s", config_delete);
        cn1_->Configure(config);
        cn2_->Configure(config);
        mx_->Configure(config);
    }

    SCAddress::Family GetSCFamily(Address::Family family) {
        SCAddress sc_addr;
        return (sc_addr.AddressFamilyToSCFamily(family));
    }

    bool IsServiceChainQEmpty(BgpServerTest *server) {
        return server->service_chain_mgr(GetSCFamily(family_))->IsQueueEmpty();
    }

    void DisableServiceChainQ(BgpServerTest *server) {
        server->service_chain_mgr(GetSCFamily(family_))->DisableQueue();
    }

    void EnableServiceChainQ(BgpServerTest *server) {
        server->service_chain_mgr(GetSCFamily(family_))->EnableQueue();
    }

    void DisableServiceChainAggregation(BgpServerTest *server) {
        IServiceChainMgr *imgr = server->
            service_chain_mgr(GetSCFamily(family_));
        imgr->set_aggregate_host_route(false);
    }

    void EnableServiceChainAggregation(BgpServerTest *server) {
        IServiceChainMgr *imgr = server->
            service_chain_mgr(GetSCFamily(family_));
        imgr->set_aggregate_host_route(true);
    }

    int RouteCount(BgpServerTest *server, const string &instance_name) const {
        BgpTable *table = GetTable(server, instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return 0;
        }
        return table->Size();
    }

    BgpRoute *RouteLookup(BgpServerTest *server,
                              const string &instance_name,
                              const string &prefix) {
        BgpTable *table = GetTable(server, instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL) {
            return NULL;
        }
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename TableT::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void AddRoute(BgpServerTest *server, IPeer *peer,
                  const string &instance_name,
                  const string &prefix, int localpref,
                  std::vector<uint32_t> sglist = std::vector<uint32_t>(),
                  set<string> encap = set<string>(),
                  const SiteOfOrigin &soo = SiteOfOrigin(),
                  string nexthop="",
                  uint32_t flags=0, int label=303) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        if (nexthop.empty())
            nexthop = BuildNextHopAddress("7.8.9.1");

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
        if (!soo.IsNull())
            ext_comm.communities.push_back(soo.GetExtCommunityValue());
        attr_spec.push_back(&ext_comm);

        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);

        request.data.reset(new BgpTable::RequestData(attr, flags, label));
        BgpTable *table = GetTable(server, instance_name);
        ASSERT_TRUE(table != NULL);
        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    void AddRoute(BgpServerTest *server, IPeer *peer,
                  const string &instance_name,
                  const string &prefix, int localpref,
                  const SiteOfOrigin &soo) {
        AddRoute(server, peer, instance_name, prefix, localpref,
            std::vector<uint32_t>(), set<string>(), soo);
    }

    void DeleteRoute(BgpServerTest *server, IPeer *peer,
                     const string &instance_name,
                     const string &prefix) {
        boost::system::error_code error;
        PrefixT nlri = PrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new typename TableT::RequestKey(nlri, peer));

        BgpTable *table = GetTable(server, instance_name);
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
        task_util::WaitForIdle();
    }

    BgpRoute *VpnRouteLookup(BgpServerTest *server, const string &prefix) {
        BgpTable *table = GetVpnTable(server);
        EXPECT_TRUE(table != NULL);
        boost::system::error_code error;
        VpnPrefixT nlri = VpnPrefixT::FromString(prefix, &error);
        EXPECT_FALSE(error);
        typename VpnTableT::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return rt;
    }

    void VerifyVpnRouteExists(BgpServerTest *server, string prefix) {
        TASK_UTIL_WAIT_NE_NO_MSG(VpnRouteLookup(server, prefix),
                                 NULL, 1000, 10000,
                                 "Wait for route in bgp.l3vpn.0..");
    }

    void VerifyVpnRouteNoExists(BgpServerTest *server, string prefix) {
        TASK_UTIL_WAIT_EQ_NO_MSG(VpnRouteLookup(server, prefix),
                                 NULL, 1000, 10000,
                                 "Wait for route in bgp.l3vpn.0 to go away..");
    }

    void AddConnectedRoute(bool ecmp = false, string prefix = "",
                           string nexthop1 = "", string nexthop2 = "") {
        if (prefix.empty())
            prefix = BuildPrefix("1.1.2.3", 32);
        if (nexthop1.empty())
            nexthop1 = BuildNextHopAddress("88.88.88.88");
        if (nexthop2.empty())
            nexthop2 = BuildNextHopAddress("99.99.99.99");

        if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
            AddRoute(mx_.get(), NULL, "blue", prefix, 100);
            if (ecmp)
                AddRoute(mx_.get(), NULL, "ecmp", prefix, 100,
                         std::vector<uint32_t>(), set<string>(),
                         SiteOfOrigin(), BuildNextHopAddress("1.2.2.1"));
        } else {
            if (family_ == Address::INET) {
                agent_a_1_->AddRoute(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix, nexthop1, 100);
                agent_a_2_->AddRoute(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix, nexthop1, 100);
                if (ecmp) {
                    agent_b_1_->AddRoute(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix, nexthop2, 100);
                    agent_b_2_->AddRoute(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix, nexthop2, 100);
                }
            } else if (family_ == Address::INET6) {
                agent_a_1_->AddInet6Route(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix, nexthop1, 100);
                agent_a_2_->AddInet6Route(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix, nexthop1, 100);
                if (ecmp) {
                    agent_b_1_->AddInet6Route(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix, nexthop2, 100);
                    agent_b_2_->AddInet6Route(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix, nexthop2, 100);
                }
            } else {
                assert(false);
            }
        }
        task_util::WaitForIdle();
    }

    void AddTableConnectedRoute(const string &table, bool ecmp,
        const string &prefix,
        const string &nexthop1, const string &nexthop2 = "") {
        if (family_ == Address::INET) {
            agent_a_1_->AddRoute(table, prefix, nexthop1, 100);
            agent_a_2_->AddRoute(table, prefix, nexthop1, 100);
            if (ecmp) {
                EXPECT_FALSE(nexthop2.empty());
                agent_b_1_->AddRoute(table, prefix, nexthop2, 100);
                agent_b_2_->AddRoute(table, prefix, nexthop2, 100);
            }
        } else if (family_ == Address::INET6) {
            agent_a_1_->AddInet6Route(table, prefix, nexthop1, 100);
            agent_a_2_->AddInet6Route(table, prefix, nexthop1, 100);
            if (ecmp) {
                EXPECT_FALSE(nexthop2.empty());
                agent_b_1_->AddInet6Route(table, prefix, nexthop2, 100);
                agent_b_2_->AddInet6Route(table, prefix, nexthop2, 100);
            }
        } else {
            assert(false);
        }
        task_util::WaitForIdle();
    }

    void DeleteConnectedRoute(bool ecmp = false, string prefix = "") {
        if (prefix.empty())
            prefix = BuildPrefix("1.1.2.3", 32);
        // Add Connected route
        if (ServiceChainIntegrationTestGlobals::mx_push_connected_) {
            DeleteRoute(mx_.get(), NULL, "blue", prefix);
            if (ecmp)
                DeleteRoute(mx_.get(), NULL, "ecmp", prefix);
        } else {
            if (family_ == Address::INET) {
                agent_a_1_->DeleteRoute(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix);
                agent_a_2_->DeleteRoute(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix);
                if (ecmp) {
                    agent_b_1_->DeleteRoute(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix);
                    agent_b_2_->DeleteRoute(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix);
                }
            } else if (family_ == Address::INET6) {
                agent_a_1_->DeleteInet6Route(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix);
                agent_a_2_->DeleteInet6Route(
                    ServiceChainIntegrationTestGlobals::connected_table_,
                    prefix);
                if (ecmp) {
                    agent_b_1_->DeleteInet6Route(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix);
                    agent_b_2_->DeleteInet6Route(
                        ServiceChainIntegrationTestGlobals::connected_table_,
                        prefix);
                }
            } else {
                assert(false);
            }
        }
        task_util::WaitForIdle();
    }

    void DeleteTableConnectedRoute(const string &table, bool ecmp,
        const string &prefix) {
        if (family_ == Address::INET) {
            agent_a_1_->DeleteRoute(table, prefix);
            agent_a_2_->DeleteRoute(table, prefix);
            if (ecmp) {
                agent_b_1_->DeleteRoute(table, prefix);
                agent_b_2_->DeleteRoute(table, prefix);
            }
        } else if (family_ == Address::INET6) {
            agent_a_1_->DeleteInet6Route(table, prefix);
            agent_a_2_->DeleteInet6Route(table, prefix);
            if (ecmp) {
                agent_b_1_->DeleteInet6Route(table, prefix);
                agent_b_2_->DeleteInet6Route(table, prefix);
            }
        } else {
            assert(false);
        }
        task_util::WaitForIdle();
    }

    bool MatchResult(BgpServerTest *server, const string &instance,
        const string &prefix, const std::vector<PathVerify> &verify) {
        task_util::TaskSchedulerLock lock;

        // Verify number of paths
        BgpRoute *svc_route = RouteLookup(server, instance, prefix);
        if (!svc_route || svc_route->count() != verify.size()) {
            return false;
        }
        typename std::vector<PathVerify>::const_iterator vit;
        Route::PathList::const_iterator it;
        for (it = svc_route->GetPathList().begin(),
             vit = verify.begin();
             it != svc_route->GetPathList().end(); it++, vit++) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            const BgpAttr *attr = path->GetAttr();
            set<string> list = GetTunnelEncapListFromRoute(path);
            if (BgpPath::PathIdString(path->GetPathId()) != vit->path_id)
                return false;
            if (path->GetSourceString(true) != vit->path_src)
                return false;
            if (attr->nexthop().to_v4().to_string() != vit->nexthop)
                return false;
            if (list != vit->encaps)
                return false;
            if (GetOriginVnFromRoute(server, path) != vit->origin_vn)
                return false;
            if (!vit->soo.IsNull() &&
                GetSiteOfOriginFromRoute(path) != vit->soo)
                return false;
        }

        return true;
    }

    void VerifyServiceChainRoute(BgpServerTest *server, string prefix,
                                 std::vector<PathVerify> verify) {
        // First wait for the route.
        TASK_UTIL_WAIT_NE_NO_MSG(RouteLookup(server, "blue", prefix),
                                 NULL, 1000, 10000,
                                 "Wait for route in blue..");

        // Verify each path for specific attribute as per the PathVerify list
        BgpRoute *rt = RouteLookup(server, "blue", prefix);
        TASK_UTIL_WAIT_EQ_NO_MSG(MatchResult(server, "blue", prefix, verify),
                                 true, 1000, 10000,
                                 "Wait for correct route in blue..");
        LOG(DEBUG, "Prefix " << prefix << "has " << rt->GetPathList().size());
    }

    void VerifyServiceChainRoute(BgpServerTest *server, const string &instance,
        const string &prefix, const std::vector<PathVerify> verify) {
        // First wait for the route.
        TASK_UTIL_EXPECT_TRUE(
            RouteLookup(server, instance, prefix) != NULL);

        // Verify each path for specific attribute as per the PathVerify list
        TASK_UTIL_EXPECT_TRUE(MatchResult(server, instance, prefix, verify));
    }

    bool CheckServiceChainRouteOriginVnPath(BgpServerTest *server,
        const string &instance, const string &prefix,
        const std::vector<string> &origin_vn_path) {
        task_util::TaskSchedulerLock lock;
        BgpRoute *route = RouteLookup(server, instance, prefix);
        if (!route)
            return false;
        for (Route::PathList::const_iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (GetOriginVnPathFromRoute(server, path) != origin_vn_path)
                return false;
        }
        return true;
    }

    void VerifyServiceChainRouteOriginVnPath(BgpServerTest *server,
        const string &instance, const string &prefix,
        const std::vector<string> &origin_vn_path) {
        TASK_UTIL_EXPECT_TRUE(CheckServiceChainRouteOriginVnPath(
            server, instance, prefix, origin_vn_path));
    }

    string FileRead(const string filename) {
        string content;

        // Convert IPv4 Prefix to IPv6 for IPv6 tests
        if (family_ == Address::INET6) {
            std::ifstream input(filename.c_str());
            regex e1 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)\\/(\\d+)(.*$)");
            regex e2 ("(^.*?)(\\d+\\.\\d+\\.\\d+\\.\\d+)(.*$)");
            for (string line; getline(input, line);) {
                boost::cmatch cm;
                if (regex_match(line.c_str(), cm, e1)) {
                    const string prefix(cm[2].first, cm[2].second);
                    content += string(cm[1].first, cm[1].second) +
                        BuildPrefix(prefix, atoi(string(cm[3].first,
                                                    cm[3].second).c_str())) +
                        string(cm[4].first, cm[4].second);
                } else if (regex_match(line.c_str(), cm, e2)) {
                    content += string(cm[1].first, cm[1].second) +
                        BuildHostAddress(string(cm[2].first, cm[2].second)) +
                        string(cm[3].first, cm[3].second);
                } else {
                    content += line;
                }
            }
        } else {
            ifstream file(filename.c_str());
            content = string((istreambuf_iterator<char>(file)),
                              istreambuf_iterator<char>());
        }

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
            family_ == Address::INET ?
                "service-chain-information" : "ipv6-service-chain-information",
            params.release(), 0);
        task_util::WaitForIdle();
    }

    void RemoveServiceChainProperty(BgpServerTest *server,
        const string &instance) {
        ifmap_test_util::IFMapMsgPropertyDelete(server->config_db(),
            "routing-instance", instance,
            family_ == Address::INET ?
                "service-chain-information" : "ipv6-service-chain-information");
        task_util::WaitForIdle();
    }

    std::vector<uint32_t> GetSGIDListFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        std::vector<uint32_t> list;
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
            list.insert(encap.ToXmppString());
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

    std::vector<string> GetOriginVnPathFromRoute(BgpServerTest *server,
        const BgpPath *path) {
        const OriginVnPath *ovnpath = path->GetAttr()->origin_vn_path();
        assert(ovnpath);
        std::vector<string> result;
        RoutingInstanceMgr *ri_mgr = server->routing_instance_mgr();
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ovnpath->origin_vns()) {
            assert(ExtCommunity::is_origin_vn(comm));
            OriginVn origin_vn(comm);
            string vn_name =
                ri_mgr->GetVirtualNetworkByVnIndex(origin_vn.vn_index());
            result.push_back(vn_name);
        }
        return result;
    }

    SiteOfOrigin GetSiteOfOriginFromRoute(const BgpPath *path) {
        const ExtCommunity *ext_comm = path->GetAttr()->ext_community();
        assert(ext_comm);
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_comm->communities()) {
            if (!ExtCommunity::is_site_of_origin(comm))
                continue;
            SiteOfOrigin soo(comm);
            return soo;
        }
        return SiteOfOrigin();
    }

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    string GetTableName(const string &instance) const {
        if (family_ == Address::INET) {
            return instance + ".inet.0";
        }
        if (family_ == Address::INET6) {
            return instance + ".inet6.0";
        }
        assert(false);
        return "";
    }

    string GetVpnTableName() const {
        if (family_ == Address::INET) {
            return "bgp.l3vpn.0";
        }
        if (family_ == Address::INET6) {
            return "bgp.l3vpn-inet6.0";
        }
        assert(false);
        return "";
    }

    BgpTable *GetTable(BgpServerTest *server,
                       const std::string &instance_name) {
        return static_cast<BgpTable *>(server->database()->FindTable(
                    GetTableName(instance_name)));
    }

    BgpTable *GetVpnTable(BgpServerTest *server) {
        return static_cast<BgpTable *>(server->database()->FindTable(
                    GetVpnTableName()));
    }

    string BuildHostAddress(const string &ipv4_addr) const {
        if (family_ == Address::INET) {
            return ipv4_addr;// + "/32";
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_addr;// + "/128";
        }
        assert(false);
        return "";
    }

    string BuildPrefix(const string &ipv4_prefix, uint8_t ipv4_plen) const {
        if (family_ == Address::INET) {
            return ipv4_prefix + "/" + integerToString(ipv4_plen);
        }
        if (family_ == Address::INET6) {
            return ipv6_prefix_ + ipv4_prefix + "/" +
                integerToString(96 + ipv4_plen);
        }
        assert(false);
        return "";
    }

    string BuildNextHopAddress(const string &ipv4_addr) const {
        return ipv4_addr;
    }

    string GetNextHopAddress(BgpAttrPtr attr) {
        return attr->nexthop().to_v4().to_string();
    }

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> cn1_;
    auto_ptr<BgpServerTest> cn2_;
    auto_ptr<BgpServerTest> mx_;
    XmppServer *cn1_xmpp_server_;
    XmppServer *cn2_xmpp_server_;
    Address::Family family_;
    string ipv6_prefix_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_2_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn1_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn2_;
};

// Specialization of GetFamily for INET.
template<> Address::Family
ServiceChainIntegrationTest<InetDefinition>::GetFamily() const {
    return Address::INET;
}

// Specialization of GetFamily for INET6.
template<> Address::Family
ServiceChainIntegrationTest<Inet6Definition>::GetFamily() const {
    return Address::INET6;
}

// Instantiate fixture class template for each TypeDefinition.
typedef ::testing::Types <InetDefinition, Inet6Definition> TypeDefinitionList;
TYPED_TEST_CASE(ServiceChainIntegrationTest, TypeDefinitionList);

#endif // SRC_BGP_TEST_SVC_STATIC_ROUTE_INTEGRATION_TEST_H_
