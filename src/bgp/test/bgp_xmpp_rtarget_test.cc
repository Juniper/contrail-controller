/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "bgp/rtarget/rtarget_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/test/bgp_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "ifmap/test/ifmap_test_util.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "xmpp/xmpp_factory.h"

using boost::assign::list_of;
using boost::assign::map_list_of;
using boost::system::error_code;
using std::cout;
using std::endl;
using std::multimap;
using std::string;
using std::vector;

static const char *config_template0 = "\
<config>\
    <bgp-router name=\'CN1\'>\
        <identifier>192.168.0.1</identifier>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <address-families>\
            <family>inet-vpn</family>\
            <family>route-target</family>\
        </address-families>\
    </bgp-router>\
</config>\
";

static const char *config_template1 = "\
<config>\
    <bgp-router name=\'CN1\'>\
        <identifier>192.168.1.1</identifier>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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
        <identifier>192.168.1.2</identifier>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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
        <identifier>192.168.1.3</identifier>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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
        <vrf-target>target:1:1001</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <vrf-target>target:1:1002</vrf-target>\
    </routing-instance>\
</config>\
";

class BgpXmppRTargetTest : public ::testing::Test {
protected:
    static const int kRouteCount = 8;
    static const int kRtGroupCount = 8;

    BgpXmppRTargetTest()
        : thread_(&evm_), cn1_xmpp_server_(NULL), cn2_xmpp_server_(NULL) {
    }

    virtual void SetUp() {
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
        Configure(64496, 64496, 64496);
        task_util::WaitForIdle();

        // Create XMPP Agent a1 connected to XMPP server CN1.
        agent_a_1_.reset(new test::NetworkAgentMock(&evm_, "agent-a1",
            cn1_xmpp_server_->GetPort(), "127.0.0.1"));

        // Create XMPP Agent a2 connected to XMPP server CN2.
        agent_a_2_.reset(new test::NetworkAgentMock(&evm_, "agent-a2",
            cn2_xmpp_server_->GetPort(), "127.0.0.1"));

        // Create XMPP Agent b1 connected to XMPP server CN1.
        agent_b_1_.reset(new test::NetworkAgentMock(&evm_, "agent-b1",
            cn1_xmpp_server_->GetPort(), "127.0.0.2"));


        // Create XMPP Agent b2 connected to XMPP server CN2.
        agent_b_2_.reset(new test::NetworkAgentMock(&evm_, "agent-b2",
            cn2_xmpp_server_->GetPort(), "127.0.0.2"));

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

        agent_a_1_->Delete();
        agent_b_1_->Delete();
        agent_a_2_->Delete();
        agent_b_2_->Delete();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    bool WalkCallback(DBTablePartBase *tpart, DBEntryBase *db_entry) {
        CHECK_CONCURRENCY("db::DBTable");
        BgpRoute *route = static_cast<BgpRoute *>(db_entry);
        std::cout << route->ToString() << std::endl;
        return true;
    }

    void WalkDoneCallback(DBTable::DBTableWalkRef ref,
                          DBTableBase *table, bool *complete) {
        if (complete)
            *complete = true;
    }

    void WalkTable(BgpTable *table) {
        bool complete = false;
        DBTable::DBTableWalkRef walk_ref = table->AllocWalker(
            boost::bind(&BgpXmppRTargetTest::WalkCallback, this, _1, _2),
            boost::bind(&BgpXmppRTargetTest::WalkDoneCallback, this, _1, _2,
                        &complete));
        std::cout << "Table " << table->name() << " walk start\n";
        table->WalkTable(walk_ref);
        TASK_UTIL_EXPECT_TRUE(complete);
        std::cout << "Table " << table->name() << " walk end\n";
    }

    void AddDeleteRTargetRoute(BgpServer *server, bool add_change,
        const string &rt_prefix_str) {
        RoutingInstanceMgr *instance_mgr = server->routing_instance_mgr();
        RoutingInstance *master =
            instance_mgr->GetRoutingInstance(BgpConfigManager::kMasterInstance);
        assert(master);
        BgpTable *rtarget_table = master->GetTable(Address::RTARGET);
        assert(rtarget_table);

        BgpAttrPtr attr;
        if (add_change) {
            BgpAttrSpec attr_spec;
            BgpAttrNextHop nexthop(server->bgp_identifier());
            attr_spec.push_back(&nexthop);
            BgpAttrOrigin origin(BgpAttrOrigin::IGP);
            attr_spec.push_back(&origin);
            attr = server->attr_db()->Locate(attr_spec);
        }
        DBRequest req;
        error_code ec;
        RTargetPrefix rt_prefix = RTargetPrefix::FromString(rt_prefix_str, &ec);
        assert(ec == 0);
        req.key.reset(new RTargetTable::RequestKey(rt_prefix, NULL));
        if (add_change) {
            req.data.reset(new RTargetTable::RequestData(attr, 0, 0));
            req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        } else {
            req.oper = DBRequest::DB_ENTRY_DELETE;
        }
        rtarget_table->Enqueue(&req);
        if (TaskScheduler::GetInstance()->GetRunStatus())
            task_util::WaitForIdle();
    }

    void AddRTargetRoute(BgpServer *server, const string &rt_prefix_str) {
        AddDeleteRTargetRoute(server, true, rt_prefix_str);
    }

    void DeleteRTargetRoute(BgpServer *server, const string &rt_prefix_str) {
        AddDeleteRTargetRoute(server, false, rt_prefix_str);
    }

    void BringupAgents() {
        TASK_UTIL_EXPECT_TRUE(agent_a_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_1_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_a_2_->IsEstablished());
        TASK_UTIL_EXPECT_TRUE(agent_b_2_->IsEstablished());
        TASK_UTIL_EXPECT_EQ(2U, bgp_channel_manager_cn1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(2U, bgp_channel_manager_cn2_->NumUpPeer());
        task_util::WaitForIdle();
    }

    void BringdownAgents() {
        agent_a_1_->SessionDown();
        agent_a_2_->SessionDown();
        agent_b_1_->SessionDown();
        agent_b_2_->SessionDown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_FALSE(agent_a_1_->IsEstablished());
        TASK_UTIL_EXPECT_FALSE(agent_b_1_->IsEstablished());
        TASK_UTIL_EXPECT_FALSE(agent_a_2_->IsEstablished());
        TASK_UTIL_EXPECT_FALSE(agent_b_2_->IsEstablished());
        TASK_UTIL_EXPECT_EQ(0U, bgp_channel_manager_cn1_->NumUpPeer());
        TASK_UTIL_EXPECT_EQ(0U, bgp_channel_manager_cn2_->NumUpPeer());
        task_util::WaitForIdle();
    }

    void SubscribeAgents() {
        agent_a_1_->Subscribe("blue", 1);
        agent_b_1_->Subscribe("blue", 1);
        agent_a_2_->Subscribe("blue", 1);
        agent_b_2_->Subscribe("blue", 1);

        agent_a_1_->Subscribe("pink", 2);
        agent_a_2_->Subscribe("pink", 2);
        agent_b_1_->Subscribe("pink", 2);
        agent_b_2_->Subscribe("pink", 2);

        agent_a_1_->Subscribe("red", 3);
        agent_a_2_->Subscribe("red", 3);
        agent_b_1_->Subscribe("red", 3);
        agent_b_2_->Subscribe("red", 3);

        agent_a_1_->Subscribe("purple", 4);
        agent_a_2_->Subscribe("purple", 4);
        agent_b_1_->Subscribe("purple", 4);
        agent_b_2_->Subscribe("purple", 4);

        agent_a_1_->Subscribe("yellow", 5);
        agent_a_2_->Subscribe("yellow", 5);
        agent_b_1_->Subscribe("yellow", 5);
        agent_b_2_->Subscribe("yellow", 5);
    }

    void SubscribeAgents(const string &network, int id) {
        agent_a_1_->Subscribe(network, id);
        agent_b_1_->Subscribe(network, id);
        agent_a_2_->Subscribe(network, id);
        agent_b_2_->Subscribe(network, id);
    }

    void UnsubscribeAgents(const string &network) {
        agent_a_1_->Unsubscribe(network, -1);
        agent_b_1_->Unsubscribe(network, -1);
        agent_a_2_->Unsubscribe(network, -1);
        agent_b_2_->Unsubscribe(network, -1);
    }

    void VerifyAllPeerUp(BgpServerTest *server, uint32_t num) {
        TASK_UTIL_EXPECT_EQ_MSG(num, server->num_bgp_peer(),
                                "Wait for all peers to get configured");
        TASK_UTIL_EXPECT_EQ_MSG(num, server->NumUpPeer(),
                                "Wait for all peers to come up");

        LOG(DEBUG, "All Peers are up: " << server->localname());
    }

    const BgpPeer *FindMatchingPeer(BgpServerTest *server, const string &name) {
        task_util::TaskSchedulerLock lock;
        const BgpPeer *peer =
            server->FindMatchingPeer(BgpConfigManager::kMasterInstance, name);
        return peer;
    }

    void Configure(as_t cn1_asn, as_t cn2_asn, as_t mx_asn,
                   as_t cn1_local_asn = 0, as_t cn2_local_asn = 0,
                   as_t mx_local_asn = 0) {
        if (cn1_local_asn == 0)
            cn1_local_asn = cn1_asn;
        if (cn2_local_asn == 0)
            cn2_local_asn = cn2_asn;
        if (mx_local_asn == 0)
            mx_local_asn = mx_asn;

        char config[4096];
        snprintf(config, sizeof(config), config_template0,
                 cn1_asn, cn1_local_asn, cn1_->session_manager()->GetPort(),
                 cn2_asn, cn2_local_asn, cn2_->session_manager()->GetPort(),
                 mx_asn, mx_local_asn, mx_->session_manager()->GetPort());
        cn1_->Configure(config);
        task_util::WaitForIdle();
        cn2_->Configure(config);
        task_util::WaitForIdle();
        mx_->Configure(config);
        task_util::WaitForIdle();

        VerifyAllPeerUp(cn1_.get(), 2);
        VerifyAllPeerUp(cn2_.get(), 2);
        VerifyAllPeerUp(mx_.get(), 2);

        TASK_UTIL_EXPECT_EQ(cn1_asn, cn1_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn2_asn, cn2_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(mx_asn, mx_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn1_local_asn, cn1_->local_autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn2_local_asn, cn2_->local_autonomous_system());
        TASK_UTIL_EXPECT_EQ(mx_local_asn, mx_->local_autonomous_system());

        vector<string> instance_names = {"blue", "pink", "red", "purple",
                                         "yellow"};
        multimap<string, string> connections;
        NetworkConfig(instance_names, connections);

        VerifyNetworkConfig(cn1_.get(), instance_names);
        VerifyNetworkConfig(cn2_.get(), instance_names);

        mx_->Configure(config_mx_vrf);
        TASK_UTIL_EXPECT_EQ(1U,
                            GetExportRouteTargetListSize(mx_.get(), "blue"));
        TASK_UTIL_EXPECT_EQ(1U,
                            GetExportRouteTargetListSize(mx_.get(), "pink"));
    }

    void NetworkConfig(const vector<string> &instance_names,
        const multimap<string, string> &connections) {
        bgp_util::NetworkConfigGenerate(cn1_->config_db(), instance_names,
                                        connections);
        bgp_util::NetworkConfigGenerate(cn2_->config_db(), instance_names,
                                        connections);
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

    void SetRoutingInstanceAlwaysSubscribe(BgpServerTest *server,
        const string &instance) {
        autogen::RoutingInstance::OolProperty *property =
            new autogen::RoutingInstance::OolProperty;
        property->data = true;
        ifmap_test_util::IFMapMsgPropertyAdd(server->config_db(),
            "routing-instance", instance, "routing-instance-has-pnf", property);
        task_util::WaitForIdle();
    }

    void ClearRoutingInstanceAlwaysSubscribe(BgpServerTest *server,
        const string &instance) {
        ifmap_test_util::IFMapMsgPropertyDelete(server->config_db(),
            "routing-instance", instance, "routing-instance-has-pnf");
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
        task_util::WaitForIdle();
    }

    void UpdateASN(as_t cn1_asn, as_t cn2_asn, as_t mx_asn,
                   as_t cn1_local_asn = 0, as_t cn2_local_asn = 0,
                   as_t mx_local_asn = 0) {
        if (cn1_local_asn == 0)
            cn1_local_asn = cn1_asn;
        if (cn2_local_asn == 0)
            cn2_local_asn = cn2_asn;
        if (mx_local_asn == 0)
            mx_local_asn = mx_asn;

        char config[4096];
        snprintf(config, sizeof(config), config_template0,
                 cn1_asn, cn1_local_asn, cn1_->session_manager()->GetPort(),
                 cn2_asn, cn2_local_asn, cn2_->session_manager()->GetPort(),
                 mx_asn, mx_local_asn, mx_->session_manager()->GetPort());
        cn1_->Configure(config);
        task_util::WaitForIdle();
        cn2_->Configure(config);
        task_util::WaitForIdle();
        mx_->Configure(config);
        task_util::WaitForIdle();

        VerifyAllPeerUp(cn1_.get(), 2);
        VerifyAllPeerUp(cn2_.get(), 2);
        VerifyAllPeerUp(mx_.get(), 2);

        TASK_UTIL_EXPECT_EQ(cn1_asn, cn1_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn2_asn, cn2_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(mx_asn, mx_->autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn1_local_asn, cn1_->local_autonomous_system());
        TASK_UTIL_EXPECT_EQ(cn2_local_asn, cn2_->local_autonomous_system());
        TASK_UTIL_EXPECT_EQ(mx_local_asn, mx_->local_autonomous_system());
    }

    void UpdateIdentifier(as_t cn1_asn, as_t cn2_asn, as_t mx_asn,
        as_t cn1_local_asn, as_t cn2_local_asn, as_t mx_local_asn) {
        char config[4096];
        snprintf(config, sizeof(config), config_template1,
                 cn1_asn, cn1_local_asn, cn1_->session_manager()->GetPort(),
                 cn2_asn, cn2_local_asn, cn2_->session_manager()->GetPort(),
                 mx_asn, mx_local_asn, mx_->session_manager()->GetPort());
        cn1_->Configure(config);
        task_util::WaitForIdle();
        cn2_->Configure(config);
        task_util::WaitForIdle();
        mx_->Configure(config);
        task_util::WaitForIdle();

        VerifyAllPeerUp(cn1_.get(), 2);
        VerifyAllPeerUp(cn2_.get(), 2);
        VerifyAllPeerUp(mx_.get(), 2);
    }

    void DisableRTargetRouteProcessing(BgpServerTest *server) {
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::DisableRTargetRouteProcessing, mgr),
            "bgp::Config");
    }

    void EnableRTargetRouteProcessing(BgpServerTest *server) {
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::EnableRTargetRouteProcessing, mgr),
            "bgp::Config");
    }

    bool IsRTargetRouteOnList(BgpServer *server, RTargetRoute *rt) const {
        task_util::WaitForIdle();
        return server->rtarget_group_mgr()->IsRTargetRouteOnList(rt);
    }

    void DisableRtGroupProcessing(BgpServerTest *server) {
        task_util::WaitForIdle();
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::DisableRtGroupProcessing, mgr),
            "bgp::Config");
    }

    void EnableRtGroupProcessing(BgpServerTest *server) {
        task_util::WaitForIdle();
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::EnableRtGroupProcessing, mgr),
            "bgp::Config");
    }

    bool IsRtGroupOnList(BgpServer *server, RtGroup *rtgroup) const {
        task_util::WaitForIdle();
        return server->rtarget_group_mgr()->IsRtGroupOnList(rtgroup);
    }

    RtGroup *RtGroupLookup(BgpServer *server, const string group) {
        task_util::WaitForIdle();
        RouteTarget rtarget = RouteTarget::FromString(group);
        return server->rtarget_group_mgr()->GetRtGroup(rtarget);
    }

    RtGroup *VerifyRtGroupExists(BgpServer *server, const string group) {
        task_util::WaitForIdle();
        TASK_UTIL_WAIT_NE_NO_MSG(RtGroupLookup(server, group),
            NULL, 1000, 10000, "Wait for RtGroup " << group);
        return RtGroupLookup(server, group);
    }

    void VerifyRtGroupNoExists(BgpServer *server, const string group) {
        task_util::WaitForIdle();
        TASK_UTIL_WAIT_EQ_NO_MSG(RtGroupLookup(server, group),
            NULL, 1000, 10000, "Wait for RtGroup " << group << "to go away");
    }

    void DisableRouteTargetProcessing(BgpServer *server) {
        task_util::WaitForIdle();
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::DisableRouteTargetProcessing, mgr),
            "bgp::Config");
    }

    void EnableRouteTargetProcessing(BgpServer *server) {
        task_util::WaitForIdle();
        RTargetGroupMgr *mgr = server->rtarget_group_mgr();
        task_util::TaskFire(
            boost::bind(&RTargetGroupMgr::EnableRouteTargetProcessing, mgr),
            "bgp::Config");
    }

    bool IsRouteTargetOnList(BgpServer *server,
        const string &rtarget_str) const {
        task_util::WaitForIdle();
        RouteTarget rtarget = RouteTarget::FromString(rtarget_str);
        return server->rtarget_group_mgr()->IsRouteTargetOnList(rtarget);
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
        if (TaskScheduler::GetInstance()->GetRunStatus())
            task_util::WaitForIdle();
    }

    void RemoveRouteTarget(BgpServerTest *server, string name, string target) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(name));

        ifmap_test_util::IFMapMsgUnlink(server->config_db(),
                                      "routing-instance", name,
                                      "route-target", target,
                                      "instance-target");
        if (TaskScheduler::GetInstance()->GetRunStatus())
            task_util::WaitForIdle();
    }

    RTargetRoute *RTargetRouteLookup(BgpServerTest *server,
        const string &prefix) {
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
        RTargetRoute *rt = static_cast<RTargetRoute *>(table->Find(&key));
        return rt;
    }

    RTargetRoute *VerifyRTargetRouteExists(BgpServerTest *server,
        const string &prefix) {
        TASK_UTIL_WAIT_NE_NO_MSG(RTargetRouteLookup(server, prefix),
            NULL, 1000, 10000, "Wait for rtarget route " << prefix);
        return RTargetRouteLookup(server, prefix);
    }

    bool CheckRTargetRouteNexthops(BgpServerTest *server,
        const string &prefix, const vector<string> &nexthops) {
        task_util::TaskSchedulerLock lock;

        BgpRoute *route = RTargetRouteLookup(server, prefix);
        if (!route || !route->IsUsable())
            return false;
        if (nexthops.size() != route->count())
            return false;
        for (Route::PathList::iterator it = route->GetPathList().begin();
             it != route->GetPathList().end(); ++it) {
            bool found = false;
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            if (!path)
                return false;
            const BgpAttr *attr = path->GetAttr();
            if (!attr)
                return false;
            BOOST_FOREACH(const string &nexthop, nexthops) {
                if (attr->nexthop().to_v4().to_string() == nexthop) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    }

    void VerifyRTargetRouteNexthops(BgpServerTest *server,
        const string &prefix, const vector<string> &nexthops) {
        TASK_UTIL_EXPECT_TRUE(
            CheckRTargetRouteNexthops(server, prefix, nexthops));
    }

    void VerifyRTargetRouteNoExists(BgpServerTest *server,
        const string &prefix) {
        task_util::WaitForIdle();
        TASK_UTIL_WAIT_EQ_NO_MSG(RTargetRouteLookup(server, prefix),
            NULL, 1000, 10000,
            "Wait for rtarget route " << prefix << " to go away");
    }

    void AddInetRoute(BgpServerTest *server, IPeer *peer,
        const string &instance_name, const string &prefix,
        int localpref = 100, string nexthop = "7.8.9.1",
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
        const string &instance_name, const string &prefix) {
        error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);

        DBRequest request;
        request.oper = DBRequest::DB_ENTRY_DELETE;
        request.key.reset(new InetTable::RequestKey(nlri, peer));

        string table_name = instance_name + ".inet.0";
        BgpTable *table =
            static_cast<BgpTable *>(server->database()->FindTable(table_name));
        ASSERT_TRUE(table != NULL);

        table->Enqueue(&request);
    }

    BgpRoute *InetRouteLookup(BgpServerTest *server,
        const string &instance_name, const string &prefix) {
        task_util::WaitForIdle();
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

    BgpRoute *VerifyInetRouteExists(BgpServerTest *server,
        const string &instance, const string &prefix) {
        task_util::WaitForIdle();
        TASK_UTIL_WAIT_NE_NO_MSG(InetRouteLookup(server, instance, prefix),
            NULL, 1000, 10000,
            "Wait for route " << prefix << " in " << instance);
        return InetRouteLookup(server, instance, prefix);
    }

    void VerifyInetRouteNoExists(BgpServerTest *server, const string &instance,
        const string &prefix) {
        task_util::WaitForIdle();
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
        task_util::WaitForIdle();
        BOOST_FOREACH(RouteTarget tgt, rti->GetExportList()) {
            target_list.push_back(tgt.ToString());
        }
        sort(target_list.begin(), target_list.end());
        return target_list;
    }

    size_t GetExportRouteTargetListSize(BgpServerTest *server,
        const string &instance) {
        task_util::WaitForIdle();
        return GetExportRouteTargetList(server, instance).size();
    }

    vector<string> GetImportRouteTargetList(BgpServerTest *server,
        const string &instance) {
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
            server->routing_instance_mgr()->GetRoutingInstance(instance));
        RoutingInstance *rti =
            server->routing_instance_mgr()->GetRoutingInstance(instance);
        vector<string> target_list;
        task_util::WaitForIdle();
        BOOST_FOREACH(RouteTarget tgt, rti->GetImportList()) {
            target_list.push_back(tgt.ToString());
        }
        sort(target_list.begin(), target_list.end());
        return target_list;
    }

    size_t GetImportRouteTargetListSize(BgpServerTest *server,
        const string &instance) {
        task_util::WaitForIdle();
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


    void ValidateRTGroupResponse(Sandesh *sandesh,
                                 const vector<string> &result) const {
        ShowRtGroupResp *resp =
                dynamic_cast<ShowRtGroupResp *>(sandesh);
        if (resp == NULL) {
            result_ = false;
            validate_done_ = true;
            return;
        }

        if (result.size() != resp->get_rtgroup_list().size()) {
            result_ = false;
            validate_done_ = true;
            return;
        }
        cout << "*****************************************************" << endl;
        int i = 0;
        BOOST_FOREACH(const ShowRtGroupInfo &info, resp->get_rtgroup_list()) {
            if (info.get_rtarget() != result[i]) {
                result_ = false;
                validate_done_ = true;
                return;
            }
            cout << info.log() << endl;
            i++;
        }
        cout << "*****************************************************" << endl;

        result_ = true;
        validate_done_ = true;
    }

    void ValidateRTGroupPeerResponse(Sandesh *sandesh,
                                     const vector<string> &result) const {
        ShowRtGroupPeerResp *resp =
                dynamic_cast<ShowRtGroupPeerResp *>(sandesh);
        if (resp == NULL) {
            result_ = false;
            validate_done_ = true;
            return;
        }

        if (result.size() != resp->get_rtgroup_list().size()) {
            result_ = false;
            validate_done_ = true;
            return;
        }

        cout << "*****************************************************" << endl;
        int i = 0;
        BOOST_FOREACH(const ShowRtGroupInfo &info, resp->get_rtgroup_list()) {
            if (info.get_rtarget() != result[i]) {
                result_ = false;
                validate_done_ = true;
                return;
            }
            cout << info.log() << endl;
            i++;
        }
        cout << "*****************************************************" << endl;

        result_ = true;
        validate_done_ = true;
    }

    void ValidateRTGroupSummaryResponse(Sandesh *sandesh,
                                        const vector<string> &result) const {
        ShowRtGroupSummaryResp *resp =
                dynamic_cast<ShowRtGroupSummaryResp *>(sandesh);
        if (resp == NULL) {
            result_ = false;
            validate_done_ = true;
            return;
        }

        if (result.size() != resp->get_rtgroup_list().size()) {
            result_ = false;
            validate_done_ = true;
            return;
        }

        cout << "*****************************************************" << endl;
        int i = 0;
        BOOST_FOREACH(const ShowRtGroupInfo &info, resp->get_rtgroup_list()) {
            if (info.get_rtarget() != result[i]) {
                result_ = false;
                validate_done_ = true;
                return;
            }
            cout << info.log() << endl;
            i++;
        }
        cout << "*****************************************************" << endl;

        result_ = true;
        validate_done_ = true;
    }

    bool VerifyRtGroupSandesh(BgpServerTest *server,
                              vector<string> result) const {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = server;
        sandesh_context.xmpp_peer_manager = NULL;
        RegisterSandeshShowXmppExtensions(&sandesh_context);
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(&BgpXmppRTargetTest::ValidateRTGroupResponse, this, _1,
                        result));
        ShowRtGroupReq *req = new ShowRtGroupReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
        return result_;
    }

    bool VerifyRtGroupSandesh(BgpServerTest *server, string rtarget,
                              vector<string> result) const {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = server;
        sandesh_context.xmpp_peer_manager = NULL;
        RegisterSandeshShowXmppExtensions(&sandesh_context);
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(&BgpXmppRTargetTest::ValidateRTGroupResponse, this, _1,
                        result));
        ShowRtGroupReq *req = new ShowRtGroupReq;
        req->set_search_string(rtarget);
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
        return result_;
    }

    bool VerifyRtGroupPeerSandesh(BgpServerTest *server, string peer,
                                  vector<string> result) const {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = server;
        sandesh_context.xmpp_peer_manager = NULL;
        RegisterSandeshShowXmppExtensions(&sandesh_context);
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(&BgpXmppRTargetTest::ValidateRTGroupPeerResponse, this,
                        _1, result));
        ShowRtGroupPeerReq *req = new ShowRtGroupPeerReq;
        req->set_search_string(peer);
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
        return result_;
    }

    bool VerifyRtGroupSummarySandesh(BgpServerTest *server,
                                     vector<string> result) const {
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = server;
        sandesh_context.xmpp_peer_manager = NULL;
        RegisterSandeshShowXmppExtensions(&sandesh_context);
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(&BgpXmppRTargetTest::ValidateRTGroupSummaryResponse,
                        this, _1, result));
        ShowRtGroupSummaryReq *req = new ShowRtGroupSummaryReq;
        validate_done_ = 0;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(1, validate_done_);
        return result_;
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> cn1_;
    boost::scoped_ptr<BgpServerTest> cn2_;
    boost::scoped_ptr<BgpServerTest> mx_;
    XmppServerTest *cn1_xmpp_server_;
    XmppServerTest *cn2_xmpp_server_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_2_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn1_;
    boost::scoped_ptr<BgpXmppChannelManager> bgp_channel_manager_cn2_;
    mutable bool validate_done_;
    mutable bool result_;
};

static string BuildPrefix(int idx = 1) {
    string prefix = string("10.1.1.") + integerToString(idx) + "/32";
    return prefix;
}

//
// Add and Delete RtGroup due to trigger from RoutePathReplicator.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRtGroup1) {
    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    VerifyRtGroupExists(mx_.get(), "target:1:99");
    RemoveRouteTarget(mx_.get(), "blue", "target:1:99");
    VerifyRtGroupNoExists(mx_.get(), "target:1:99");
}

//
// Add and Delete RtGroup due to trigger from VPN route.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRtGroup2) {
    SubscribeAgents("blue", 1);
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    VerifyRtGroupExists(cn1_.get(), "target:1:1001");
    VerifyRtGroupExists(cn2_.get(), "target:1:1001");

    RemoveRouteTarget(mx_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");
    VerifyRtGroupNoExists(cn1_.get(), "target:1:1001");
    VerifyRtGroupNoExists(cn2_.get(), "target:1:1001");

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete RtGroup due to trigger from RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRtGroup3) {
    SubscribeAgents("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    VerifyRtGroupExists(mx_.get(), "target:1:99");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");
    VerifyRtGroupNoExists(mx_.get(), "target:1:99");
}

//
// Add and Delete multiple RtGroups due to trigger from RoutePathReplicator.
//
TEST_F(BgpXmppRTargetTest, AddDeleteMultipleRtGroup1) {
    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        AddRouteTarget(mx_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupExists(mx_.get(), rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        RemoveRouteTarget(mx_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupNoExists(mx_.get(), rtarget_str);
    }
}

//
// Add and Delete multiple RtGroups due to trigger from VPN route.
//
TEST_F(BgpXmppRTargetTest, AddDeleteMultipleRtGroup2) {
    SubscribeAgents("blue", 1);
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        AddRouteTarget(mx_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupExists(cn1_.get(), rtarget_str);
        VerifyRtGroupExists(cn2_.get(), rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        RemoveRouteTarget(mx_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupNoExists(cn1_.get(), rtarget_str);
        VerifyRtGroupNoExists(cn2_.get(), rtarget_str);
    }

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete multiple RtGroups due to trigger from RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, AddDeleteMultipleRtGroup3) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        AddRouteTarget(cn1_.get(), "blue", rtarget_str);
        AddRouteTarget(cn2_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupExists(mx_.get(), rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        RemoveRouteTarget(cn1_.get(), "blue", rtarget_str);
        RemoveRouteTarget(cn2_.get(), "blue", rtarget_str);
    }

    for (int idx = 1; idx <= kRtGroupCount; ++idx) {
        string rtarget_str = string("target:1:90") + integerToString(idx);
        VerifyRtGroupNoExists(mx_.get(), rtarget_str);
    }
}

//
// Add, Delete and Add RtGroup due to trigger from RoutePathReplicator.
// RtGroup list processing is disabled after initial add.
//
TEST_F(BgpXmppRTargetTest, ResurrectRtGroup1) {
    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    RtGroup *rtgroup1 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(mx_.get(), rtgroup1));

    DisableRtGroupProcessing(mx_.get());

    RemoveRouteTarget(mx_.get(), "blue", "target:1:99");
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    RtGroup *rtgroup2 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup2);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(mx_.get(), rtgroup2));

    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    RtGroup *rtgroup3 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup3);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(mx_.get(), rtgroup3));

    EnableRtGroupProcessing(mx_.get());
    RtGroup *rtgroup4 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup4);
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(mx_.get(), rtgroup4));
}

//
// Add, Delete and Add RtGroup due to trigger from VPN route.
// RtGroup list processing is disabled after initial add.
//
TEST_F(BgpXmppRTargetTest, ResurrectRtGroup2) {
    agent_a_1_->Subscribe("blue", 1);
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    RtGroup *rtgroup1 = VerifyRtGroupExists(cn1_.get(), "target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(cn1_.get(), rtgroup1));

    DisableRtGroupProcessing(cn1_.get());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    RtGroup *rtgroup2 = VerifyRtGroupExists(cn1_.get(), "target:1:1001");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup2);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(cn1_.get(), rtgroup2));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    RtGroup *rtgroup3 = VerifyRtGroupExists(cn1_.get(), "target:1:1001");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup3);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(cn1_.get(), rtgroup3));

    EnableRtGroupProcessing(cn1_.get());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    RtGroup *rtgroup4 = VerifyRtGroupExists(cn1_.get(), "target:1:1001");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup4);
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(cn1_.get(), rtgroup4));

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add, Delete and Add RtGroup due to trigger from RTargetRoute.
// RtGroup list processing is disabled after initial add.
//
TEST_F(BgpXmppRTargetTest, ResurrectRtGroup3) {
    agent_a_1_->Subscribe("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:99");
    RtGroup *rtgroup1 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(mx_.get(), rtgroup1));

    DisableRtGroupProcessing(mx_.get());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:99");
    RtGroup *rtgroup2 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup2);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(mx_.get(), rtgroup2));

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:99");
    RtGroup *rtgroup3 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup3);
    TASK_UTIL_EXPECT_TRUE(IsRtGroupOnList(mx_.get(), rtgroup3));

    EnableRtGroupProcessing(mx_.get());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:99");
    RtGroup *rtgroup4 = VerifyRtGroupExists(mx_.get(), "target:1:99");
    TASK_UTIL_EXPECT_TRUE(rtgroup1 == rtgroup4);
    TASK_UTIL_EXPECT_FALSE(IsRtGroupOnList(mx_.get(), rtgroup4));
}

//
// Add and Delete route from MX and verify it's updated on CNs.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRoute) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
}

//
// Add and Delete multiple routes from MX and verify they are updated on CNs.
//
TEST_F(BgpXmppRTargetTest, AddDeleteMultipleRoute) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }
}

//
// Add and Delete route with multiple RTs on MX and verify it's imported into
// appropriate VRFs on CNs.
// CN1 and CN2 use different RTs to import the route.
// Agents on CN1 and CN2 subscribe to the VRFs after the RTs are added to the
// VRFs.
// Route is added after the agents subscribe to the VRFs.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRouteWithMultipleTargets1) {
    AddRouteTarget(mx_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    AddRouteTarget(cn2_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    SubscribeAgents("blue", 1);

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
}

//
// Add and Delete route with multiple RTs on MX and verify it's imported into
// appropriate VRFs on CNs.
// CN1 and CN2 use different RTs to import the route.
// Agents on CN1 and CN2 subscribe to the VRFs before the RTs are added to the
// VRFs.
// Route is added after the agents subscribe to the VRFs.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRouteWithMultipleTargets2) {
    AddRouteTarget(mx_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    SubscribeAgents("blue", 1);
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    AddRouteTarget(cn2_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete route with multiple RTs on MX and verify it's imported into
// appropriate VRFs on CNs.
// CN1 and CN2 use different RTs to import the route.
// Agents on CN1 and CN2 subscribe to the VRFs before the RTs are added to the
// VRFs.
// Route is added before the agents subscribe to the VRFs.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRouteWithMultipleTargets3) {
    AddRouteTarget(mx_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    AddRouteTarget(cn2_.get(), "blue", "target:1:2001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Subscribe and Unsubscribe from agents should trigger advertisement and
// withdrawal of RTargetRoutes from CNs and hence advertisement/withdrawal
// of VPN route from MX.
//
TEST_F(BgpXmppRTargetTest, SubscribeUnsubscribe1) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    agent_a_1_->Subscribe("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_2_->Subscribe("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_1_->Unsubscribe("blue", -1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_2_->Unsubscribe("blue", -1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Subscribe and Unsubscribe from agents should trigger advertisement and
// withdrawal of RTargetRoutes from CNs and hence advertisement/withdrawal
// of VPN route from MX.
// RTargetRoute must be withdrawn only when the last agent has unsubscribed.
//
TEST_F(BgpXmppRTargetTest, SubscribeUnsubscribe2) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_1_->Unsubscribe("blue", -1);
    agent_a_2_->Unsubscribe("blue", -1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_b_1_->Unsubscribe("blue", -1);
    agent_b_2_->Unsubscribe("blue", -1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// SessionDown from agents should trigger withdrawal of RTargetRoutes from
// CNs and hence withdrawal of VPN route from MX.
//
TEST_F(BgpXmppRTargetTest, SubscribeThenSessionDown1) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_1_->SessionDown();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_2_->SessionDown();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// SessionDown from agents should trigger withdrawal of RTargetRoutes from
// CNs and hence withdrawal of VPN route from MX.
// RTargetRoute must be withdrawn only when the last agent's session is down.
//
TEST_F(BgpXmppRTargetTest, SubscribeThenSessionDown2) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_a_1_->SessionDown();
    agent_a_2_->SessionDown();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    agent_b_1_->SessionDown();
    agent_b_2_->SessionDown();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Duplicate unsubscribe should not cause any problem.
//
TEST_F(BgpXmppRTargetTest, DuplicateUnsubscribe) {
    SubscribeAgents("blue", 1);
    UnsubscribeAgents("blue");
    UnsubscribeAgents("blue");
}

//
// Subscribe and Unsubscribe from agents should trigger advertisement and
// withdrawal of RTargetRoutes from CNs and hence advertisement/withdrawal
// of all VPN route from MX.
// RTargetRoute must be withdrawn only when the last agent has unsubscribed.
//
TEST_F(BgpXmppRTargetTest, SubscribeUnsubscribeMultipleRoute) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    agent_a_1_->Subscribe("blue", 1);
    agent_b_1_->Subscribe("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    agent_a_2_->Subscribe("blue", 1);
    agent_b_2_->Subscribe("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    agent_a_1_->Unsubscribe("blue", -1);
    agent_b_1_->Unsubscribe("blue", -1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    agent_a_2_->Unsubscribe("blue", -1);
    agent_b_2_->Unsubscribe("blue", -1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Add and Delete RT to route advertised from MX.
// That should cause the route to get imported/un-imported from VRFs on CNs.
// The 2 CNs use the same RT for the VRF.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToRoute1) {
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(mx_.get(), "blue", "target:64496:1");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete RTs to route advertised from MX.
// That should cause the route to get imported/unimported to/from VRFs on CNs.
// The 2 CNs use different RTs for the VRF.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToRoute2) {
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    AddRouteTarget(cn1_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    AddRouteTarget(cn2_.get(), "blue", "target:2:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:2:101");
    TASK_UTIL_EXPECT_EQ(3U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(mx_.get(), "blue", "target:1:101");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(mx_.get(), "blue", "target:2:101");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete RTs to route advertised from MX.
// Add one RT and delete another RT in one shot.
// That should cause the route to get imported/unimported to/from VRFs on CNs.
// The 2 CNs use different RTs for the VRF.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToRoute3) {
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    AddRouteTarget(cn1_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    AddRouteTarget(cn2_.get(), "blue", "target:2:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(mx_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    TaskScheduler::GetInstance()->Stop();
    RemoveRouteTarget(mx_.get(), "blue", "target:1:101");
    AddRouteTarget(mx_.get(), "blue", "target:2:101");
    TaskScheduler::GetInstance()->Start();
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToInstance) {
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete RT to VRFs on one CN.
// That should cause the corresponding RouteTarget routes to get advertised
// and withdrawn.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToInstance1) {
    SubscribeAgents("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "blue"));
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:101");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:101");
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(3U, GetImportRouteTargetListSize(cn1_.get(), "blue"));
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:101");
}

//
// Add and Delete multiple RTs to VRFs on one CN.
// That should cause the corresponding RouteTarget routes to get advertised
// and withdrawn.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToInstance2) {
    SubscribeAgents("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:101");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:102");

    AddRouteTarget(cn1_.get(), "blue", "target:1:102");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:102");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:101");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:102");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:102");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:102");
}

//
// Add and Delete multiple RTs to VRFs on one CN.
// That should cause the corresponding RouteTarget routes to get advertised
// and withdrawn.
// Add one RT and delete another RT in one shot.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetToInstance3) {
    SubscribeAgents("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:101");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:102");

    TaskScheduler::GetInstance()->Stop();
    RemoveRouteTarget(cn1_.get(), "blue", "target:1:101");
    AddRouteTarget(cn1_.get(), "blue", "target:1:102");
    TaskScheduler::GetInstance()->Start();

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:102");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:102");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:101");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:102");
}

//
// Add and Delete different RTargetRoutes for the same RT from a peer.
// VPN routes with the RT should be advertised to the peer if there's one or
// more RTargetRoutes received from it.
// Add two RTargetRoutes, then delete them one at a time.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetRoute1) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(mx_.get(), cn2_.get());
    task_util::WaitForIdle();
    SubscribeAgents("blue", 1);

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete different RTargetRoutes for the same RT from a peer.
// VPN routes with the RT should be advertised to the peer if there's one or
// more RTargetRoutes received from it.
// Add two RTargetRoutes one at a time, then delete them.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetRoute2) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(mx_.get(), cn2_.get());
    task_util::WaitForIdle();
    SubscribeAgents("blue", 1);

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete different RTargetRoutes for the same RT from a peer.
// VPN routes with the RT should be advertised to the peer if there's one or
// more RTargetRoutes received from it.
// Add one RTargetRoute, then delete it and add a different one.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetRoute3) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(mx_.get(), cn2_.get());
    task_util::WaitForIdle();
    SubscribeAgents("blue", 1);

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Add and Delete different RTargetRoutes for the same RT from a peer.
// VPN routes with the RT should be advertised to the peer if there's one or
// more RTargetRoutes received from it.
// Add one RTargetRoute, then delete it and add a different one in one shot.
//
TEST_F(BgpXmppRTargetTest, AddDeleteRTargetRoute4) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(mx_.get(), cn2_.get());
    task_util::WaitForIdle();
    SubscribeAgents("blue", 1);

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DisableRTargetRouteProcessing(mx_.get());

    RemoveRouteTarget(cn2_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");

    RTargetRoute *rtgt_rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rtgt_rt1));
    RTargetRoute *rtgt_rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rtgt_rt2));

    EnableRTargetRouteProcessing(mx_.get());

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:1:1001");

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// HTTP Introspect for RTGroups.
//
TEST_F(BgpXmppRTargetTest, HTTPIntrospect1) {
    SubscribeAgents();
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix());

    vector<string> cn2_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:0")
        ("target:192.168.0.1:1")("target:192.168.0.1:2")
        ("target:192.168.0.1:3")("target:192.168.0.1:4")
        ("target:192.168.0.1:5")("target:192.168.0.2:1")
        ("target:192.168.0.2:2")("target:192.168.0.2:3")
        ("target:192.168.0.2:4")("target:192.168.0.2:5")
        ("target:192.168.0.3:0");
    vector<string> cn1_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:1")
        ("target:192.168.0.1:2")("target:192.168.0.1:3")
        ("target:192.168.0.1:4")("target:192.168.0.1:5")
        ("target:192.168.0.2:0")("target:192.168.0.2:1")
        ("target:192.168.0.2:2")("target:192.168.0.2:3")
        ("target:192.168.0.2:4")("target:192.168.0.2:5")
        ("target:192.168.0.3:0");
    vector<string> mx_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:0")
        ("target:192.168.0.1:1")("target:192.168.0.1:2")
        ("target:192.168.0.1:3")("target:192.168.0.1:4")
        ("target:192.168.0.1:5")("target:192.168.0.2:0")
        ("target:192.168.0.2:1")("target:192.168.0.2:2")
        ("target:192.168.0.2:3")("target:192.168.0.2:4")
        ("target:192.168.0.2:5")("target:192.168.0.3:1")
        ("target:192.168.0.3:2");
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(mx_.get(), mx_result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn1_.get(), cn1_result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn2_.get(), cn2_result));

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
}

//
// HTTP Introspect for specific RTGroup.
//
TEST_F(BgpXmppRTargetTest, HTTPIntrospect2) {
    SubscribeAgents();
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix());

    vector<string> result = list_of("target:64496:1");
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(mx_.get(), "target:64496:1",
                                               result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn1_.get(), "target:64496:1",
                                               result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn2_.get(), "target:64496:1",
                                               result));

    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(mx_.get(), "target:64496:999",
                                               vector<string>()));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn1_.get(), "target:64496:999",
                                               vector<string>()));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSandesh(cn2_.get(), "target:64496:999",
                                               vector<string>()));

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
}

//
// HTTP Introspect for specific peer.
//
TEST_F(BgpXmppRTargetTest, HTTPIntrospect3) {
    SubscribeAgents();
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix());

    vector<string> result = list_of("target:64496:1")("target:64496:2")
        ("target:64496:3")("target:64496:4")("target:64496:5");
    vector<string> cn1_result = list_of("target:64496:1")("target:64496:2")
        ("target:64496:3")("target:64496:4")("target:64496:5")
        ("target:64496:7999999")
        ("target:192.168.0.1:0")("target:192.168.0.1:1")
        ("target:192.168.0.1:2")("target:192.168.0.1:3")
        ("target:192.168.0.1:4")("target:192.168.0.1:5");
    const BgpPeer *peer_cn1 = FindMatchingPeer(mx_.get(), "CN1");
    EXPECT_TRUE(peer_cn1 != NULL);
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(mx_.get(),
                                                   peer_cn1->peer_basename(),
                                                   cn1_result));
    const BgpPeer *peer_cn2 = FindMatchingPeer(mx_.get(), "CN2");
    EXPECT_TRUE(peer_cn2 != NULL);
    vector<string> cn2_result = list_of("target:64496:1")("target:64496:2")
        ("target:64496:3")("target:64496:4")("target:64496:5")
        ("target:64496:7999999")
        ("target:192.168.0.2:0")("target:192.168.0.2:1")
        ("target:192.168.0.2:2")("target:192.168.0.2:3")
        ("target:192.168.0.2:4")("target:192.168.0.2:5");
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(mx_.get(),
                                                   peer_cn2->peer_basename(),
                                                   cn2_result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(mx_.get(), string(),
                                                   vector<string>()));

    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(mx_.get(), "undefined",
                                                   vector<string>()));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(cn1_.get(), "undefined",
                                                   vector<string>()));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupPeerSandesh(cn2_.get(), "undefined",
                                                   vector<string>()));

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
}

//
// HTTP Introspect for RTGroups summary.
//
TEST_F(BgpXmppRTargetTest, HTTPIntrospect4) {
    SubscribeAgents();
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix());

    vector<string> cn2_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:0")
        ("target:192.168.0.1:1")("target:192.168.0.1:2")
        ("target:192.168.0.1:3")("target:192.168.0.1:4")
        ("target:192.168.0.1:5")("target:192.168.0.2:1")
        ("target:192.168.0.2:2")("target:192.168.0.2:3")
        ("target:192.168.0.2:4")("target:192.168.0.2:5")
        ("target:192.168.0.3:0");
    vector<string> cn1_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:1")
        ("target:192.168.0.1:2")("target:192.168.0.1:3")
        ("target:192.168.0.1:4")("target:192.168.0.1:5")
        ("target:192.168.0.2:0")("target:192.168.0.2:1")
        ("target:192.168.0.2:2")("target:192.168.0.2:3")
        ("target:192.168.0.2:4")("target:192.168.0.2:5")
        ("target:192.168.0.3:0");
    vector<string> mx_result = list_of("target:1:1001")("target:1:1002")
        ("target:64496:1")("target:64496:2")("target:64496:3")
        ("target:64496:4")("target:64496:5")("target:64496:7999999")
        ("target:192.168.0.1:0")
        ("target:192.168.0.1:1")("target:192.168.0.1:2")
        ("target:192.168.0.1:3")("target:192.168.0.1:4")
        ("target:192.168.0.1:5")("target:192.168.0.2:0")
        ("target:192.168.0.2:1")("target:192.168.0.2:2")
        ("target:192.168.0.2:3")("target:192.168.0.2:4")
        ("target:192.168.0.2:5")("target:192.168.0.3:1")
        ("target:192.168.0.3:2");
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSummarySandesh(mx_.get(), mx_result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSummarySandesh(cn1_.get(), cn1_result));
    TASK_UTIL_EXPECT_TRUE(VerifyRtGroupSummarySandesh(cn2_.get(), cn2_result));

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix());
}

TEST_F(BgpXmppRTargetTest, BulkRouteAdd) {
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SubscribeAgents("blue", 1);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    vector<BgpServerTest *> server_list =
        list_of(cn1_.get())(cn2_.get())(mx_.get());
    vector<string> rtarget_str_list = list_of("target:1:400")
        ("target:64496:100")("target:64496:200")("target:64496:300")
        ("target:64496:400")("target:64496:500")("target:64496:600")
        ("target:64496:7999999")
        ;

    BOOST_FOREACH(BgpServerTest *server, server_list) {
        BOOST_FOREACH(const string &rtarget_str, rtarget_str_list) {
            AddRouteTarget(server, "blue", rtarget_str);
        }
        task_util::WaitForIdle();
        BOOST_FOREACH(const string &rtarget_str, rtarget_str_list) {
            RemoveRouteTarget(server, "blue", rtarget_str);
        }
    }

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Update ASN on CNs and make sure that the ASN in the RTargetRoute prefix
// is updated.
// Local AS is same as global AS.
//
TEST_F(BgpXmppRTargetTest, ASNUpdate) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));

    SubscribeAgents("blue", 1);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));

    UpdateASN(64497, 64497, 64497);
    task_util::WaitForIdle();
    BringupAgents();

    SubscribeAgents("blue", 1);

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UnsubscribeAgents("blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

//
// Update local ASN on CNs and make sure that the ASN in the RTargetRoute
// prefix is updated.
// Local AS is different than global AS.
//
TEST_F(BgpXmppRTargetTest, LocalASNUpdate) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));

    SubscribeAgents("blue", 1);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));

    UpdateASN(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();
    BringupAgents();

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UnsubscribeAgents("blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

//
// Update Identifier on CNs and make sure that Nexthop in RTargetRoute prefix
// is updated.
// Local AS is same as global AS.
//
TEST_F(BgpXmppRTargetTest, IdentifierUpdate1) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("blue", 1);

    vector<string> nexthops0 = {"192.168.0.1", "192.168.0.2"};
    VerifyRTargetRouteNexthops(cn1_.get(), "64496:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(cn2_.get(), "64496:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(mx_.get(), "64496:target:64496:1", nexthops0);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateIdentifier(64496, 64496, 64496, 64496, 64496, 64496);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(true, agent_a_1_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(true, agent_a_2_->IsEstablished());
    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    vector<string> nexthops1 = list_of("192.168.1.1")("192.168.1.2");
    VerifyRTargetRouteNexthops(cn1_.get(), "64496:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(cn2_.get(), "64496:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(mx_.get(), "64496:target:64496:1", nexthops1);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    agent_a_1_->Unsubscribe("blue", -1);
    agent_a_2_->Unsubscribe("blue", -1);

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
}

//
// Update Identifier on CNs and make sure that Nexthop in RTargetRoute prefix
// is updated.
// Local AS is different than global AS.
//
TEST_F(BgpXmppRTargetTest, IdentifierUpdate2) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();
    BringupAgents();

    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("blue", 1);

    vector<string> nexthops0 = {"192.168.0.1", "192.168.0.2"};
    VerifyRTargetRouteNexthops(cn1_.get(), "64497:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(cn2_.get(), "64497:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(mx_.get(), "64497:target:64496:1", nexthops0);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateIdentifier(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(true, agent_a_1_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(true, agent_a_2_->IsEstablished());
    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    vector<string> nexthops1 = list_of("192.168.1.1")("192.168.1.2");
    VerifyRTargetRouteNexthops(cn1_.get(), "64497:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(cn2_.get(), "64497:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(mx_.get(), "64497:target:64496:1", nexthops1);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    agent_a_1_->Unsubscribe("blue", -1);
    agent_a_2_->Unsubscribe("blue", -1);

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

//
// Validate defer logic of BgpPeer deletion.
// BgpPeer will not be deleted till RTargetGroupManager stops referring to it
// in InterestedPeers list.
//
TEST_F(BgpXmppRTargetTest, DeletedPeer) {
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));

    // CN1 generates rtarget route for blue import rt
    agent_a_1_->Subscribe("blue", 1);
    agent_b_1_->Subscribe("blue", 1);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(6, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(6, RTargetRouteCount(cn2_.get()));

    // Stop the RTGroupMgr processing of RTargetRoute list.
    DisableRTargetRouteProcessing(cn2_.get());

    size_t count = cn2_->lifetime_manager()->GetQueueDeferCount();

    // Delete the bgp peering from both CNs.
    UnconfigureBgpPeering(cn1_.get(), cn2_.get());

    // Wait for the peer to go down in CN2
    TASK_UTIL_EXPECT_EQ_MSG(1U, cn2_->NumUpPeer(),
                            "Wait for control-node peer to go down");

    // Route on CN2 will not be deleted due to DBState from RTargetGroupMgr
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");

    // Ensure that the path from CN1 is deleted
    BgpRoute *rt_route = RTargetRouteLookup(cn2_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(rt_route->count(), 0U);

    // Make sure that lifetime manager does not delete this peer as there is
    // a reference to the peer from RTargetGroupMgr's work queue.
    TASK_UTIL_EXPECT_TRUE(
        cn2_->lifetime_manager()->GetQueueDeferCount() > count);

    // Enable the RTGroupMgr processing of RTargetRoute list.
    EnableRTargetRouteProcessing(cn2_.get());

    // Verify both RTargetGroupMgr's are empty.
    TASK_UTIL_EXPECT_TRUE(
        cn1_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    TASK_UTIL_EXPECT_TRUE(
        cn2_->rtarget_group_mgr()->IsRTargetRoutesProcessed());
    VerifyAllPeerUp(cn2_.get(), 1);

    // Route on CN2 should be deleted now.
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
}

//
// CNs have multiple VRFs with the same RT.
// The RTargetRoute should not be withdrawn till the RT is removed from all
// VRFs.
//
TEST_F(BgpXmppRTargetTest, SameTargetInMultipleInstances1) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 2);

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "pink", "target:1:99");
    AddRouteTarget(cn2_.get(), "pink", "target:1:99");

    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "pink", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "pink", "target:1:99");

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:1:99");
}

//
// CNs have multiple VRFs with the same RT.
// The RTargetRoute should not be withdrawn is the RT is not removed from
// all VRFs.
// The RTargetRoute should be withdrawn if all agents unsubscribe from VRF.
//
TEST_F(BgpXmppRTargetTest, SameTargetInMultipleInstances2) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 2);

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "pink", "target:1:99");
    AddRouteTarget(cn2_.get(), "pink", "target:1:99");

    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    UnsubscribeAgents("pink");

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:1:99");
}

//
// Two VRFs are connected to each other and CNs have agents that subscribe to
// both VRFs.
// One VRF has an extra RT added to it.
// If connection between the VRFs is removed, the RtargetRoute for the extra
// RT should not be withdrawn since it's still in the import list of one VRF.
//
TEST_F(BgpXmppRTargetTest, ConnectedInstances1) {
    SubscribeAgents();
    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "purple"));

    AddRouteTarget(cn1_.get(), "red", "target:1:99");
    AddRouteTarget(cn2_.get(), "red", "target:1:99");

    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn2_.get(), "purple"));
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveConnection(cn1_.get(), "red", "purple");
    RemoveConnection(cn2_.get(), "red", "purple");

    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(3U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(3U, GetImportRouteTargetListSize(cn2_.get(), "purple"));
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");

    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(5U, GetImportRouteTargetListSize(cn2_.get(), "purple"));
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");
}

//
// Two VRFs are connected to each other and CNs have agents that subscribe to
// both VRFs.
// If all agents unsubscribe from one VRF, RTargetRoute for that VRFs RT must
// not be withdrawn since it still in the import list of the other VRF.
//
TEST_F(BgpXmppRTargetTest, ConnectedInstances2) {
    SubscribeAgents();
    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "purple"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:4");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:4");

    UnsubscribeAgents("purple");
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:4");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:4");
}

//
// Two VRFs are connected to each other and CNs have agents that subscribe
// to both VRFs.
// If all agents' sessions go down, RTargetRoute for both VRFs RT must be
// withdrawn.
//
TEST_F(BgpXmppRTargetTest, ConnectedInstances3) {
    SubscribeAgents();
    AddConnection(cn1_.get(), "red", "purple");
    AddConnection(cn2_.get(), "red", "purple");
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "red"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn1_.get(), "purple"));
    TASK_UTIL_EXPECT_EQ(4U, GetImportRouteTargetListSize(cn2_.get(), "purple"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:4");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:3");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:4");

    BringdownAgents();
    task_util::WaitForIdle();

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:3");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:4");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:3");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:4");
}

//
// Disable RTargetRoute processing on MX and verify that a new RTargetRoute
// advertised by CN remains on the trigger list.  Routes with that RT are
// sent to the CN only after processing is enabled.
// There is 1 CN which sends the RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRTargetRouteProcessing1) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RTargetRoute *rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt1));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
    }

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt2));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RTargetRoute processing on MX and verify that a new RTargetRoute
// advertised by CN remains on the trigger list.  Routes with that RT are
// sent to the CN only after processing is enabled.
// There are 2 CNs which send the RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRTargetRouteProcessing2) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RTargetRoute *rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt1));

    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    RTargetRoute *rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt2));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt3 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt3));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RTargetRoute processing on MX and verify that a new RTargetRoute
// advertised by CN remains on the trigger list.  Routes with that RT are
// sent to the CN only after processing is enabled.
// There are 2 CNs which send the RTargetRoute.
// Processing is disabled and enabled separately when the CNs advertise the
// RTargetRoute i.e. the RTargetRoute needs to be processed twice on the MX.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRTargetRouteProcessing3) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RTargetRoute *rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt1));

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt2));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    RTargetRoute *rt3 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt3));

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt4 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt4));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RTargetRoute processing on MX and verify that new RTargetRoutes
// advertised by CN remain on the trigger list.  Routes with those RTs are
// sent to the CN only after processing is enabled.
// There is 1 CN which sends 2 RTargetRoutes, one per VRF.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRTargetRouteProcessing4) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 2);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RTargetRoute *rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt1));

    AddRouteTarget(cn1_.get(), "pink", "target:1:1002");
    RTargetRoute *rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1002");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt2));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "pink", BuildPrefix(idx));
    }

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt3 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt3));
    RTargetRoute *rt4 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1002");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt4));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }
}

//
// Disable RTargetRoute processing on MX and verify that new RTargetRoutes
// advertised by CNs remain on the trigger list.  Routes with those RTs are
// sent to the CNs only after processing is enabled.
// There are 2 CNs which send 1 RTargetRoute each.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRTargetRouteProcessing5) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 2);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx));
    }

    DisableRTargetRouteProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    RTargetRoute *rt1 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt1));

    AddRouteTarget(cn2_.get(), "pink", "target:1:1002");
    RTargetRoute *rt2 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1002");
    TASK_UTIL_EXPECT_TRUE(IsRTargetRouteOnList(mx_.get(), rt2));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "pink", BuildPrefix(idx));
    }

    EnableRTargetRouteProcessing(mx_.get());

    RTargetRoute *rt3 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt3));
    RTargetRoute *rt4 =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1002");
    TASK_UTIL_EXPECT_FALSE(IsRTargetRouteOnList(mx_.get(), rt4));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that the RT corresponding
// to the RTargetRoute advertised by CN remains on the trigger lists.  Routes
// with that RT are sent to the CN only after processing is enabled.
// There is 1 CN which sends the RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing1) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
    }

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that the RT corresponding
// to the RTargetRoute advertised by CNs remains on the trigger lists. Routes
// with that RT are sent to the CN only after processing is enabled.
// There are 2 CNs which send the RTargetRoute.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing2) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that the RT corresponding
// to the RTargetRoute advertised by CNs remains on the trigger lists. Routes
// with that RT are sent to the CN only after processing is enabled.
// There are 2 CNs which send the RTargetRoute.
// Processing is disabled and enabled separately when the CNs advertise the
// RTargetRoute i.e. the RouteTarget needs to be processed twice on the MX
// for all partitions.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing3) {
    SubscribeAgents("blue", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that the RTs corresponding
// to the RTargetRoutes advertised by CN remain on the trigger lists. Routes
// with those RTs are sent to the CN only after processing is enabled.
// There is 1 CN which sends 2 RTargetRoutes, one per VRF.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing4) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    AddRouteTarget(cn1_.get(), "pink", "target:1:1002");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1002"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "pink", BuildPrefix(idx));
    }

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:2:1002"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that the RTs corresponding
// to the RTargetRoutes advertised by CNs remain on the trigger lists. Routes
// with those RTs are sent to the CNs only after processing is enabled.
// There are 2 CNs which send 1 RTargetRoute each.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing5) {
    SubscribeAgents("blue", 1);
    SubscribeAgents("pink", 1);

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        AddInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));

    AddRouteTarget(cn2_.get(), "pink", "target:1:1002");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:1002"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn2_.get(), "pink", BuildPrefix(idx));
    }

    EnableRouteTargetProcessing(mx_.get());

    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:1001"));
    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:2:1002"));
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn2_.get(), "pink", BuildPrefix(idx));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
        DeleteInetRoute(mx_.get(), NULL, "pink", BuildPrefix(idx));
    }
}

//
// Disable RouteTarget processing on MX and verify that there are no issues
// even if the RtGroup corresponding to the RouteTarget has been deleted by
// the time processing is enabled.
//
TEST_F(BgpXmppRTargetTest, DisableEnableRouteTargetProcessing6) {
    SubscribeAgents("blue", 1);

    AddRouteTarget(mx_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "blue", "target:1:99");

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(idx));
    }

    DisableRouteTargetProcessing(mx_.get());

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:99"));

    RemoveRouteTarget(mx_.get(), "blue", "target:1:99");
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix(idx));
    }
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(idx));
    }
    TASK_UTIL_EXPECT_TRUE(IsRouteTargetOnList(mx_.get(), "target:1:99"));
    VerifyRtGroupNoExists(mx_.get(), "target:1:99");

    EnableRouteTargetProcessing(mx_.get());
    TASK_UTIL_EXPECT_FALSE(IsRouteTargetOnList(mx_.get(), "target:1:99"));
}

//
// If a RTargetRoute is received from EBGP peers that are in same AS, VPN
// routes should be sent only to the peer that sent the best path for the
// RTargetRoute i.e. the one with the lowest router id. The other peer will
// receive the VPN route via IBGP.
//
// This test was disabled when default behavior of route target filtering
// got modified via launchpad bug 1509945. It can be re-enabled when a knob
// to limit the number of external paths is added.
//
TEST_F(BgpXmppRTargetTest, DISABLED_PathSelection1a) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64497);
    SubscribeAgents("blue", 1);
    task_util::WaitForIdle();

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    RTargetRoute *rtgt_rt =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, rtgt_rt->count());

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    BgpRoute *rt1 = VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    uint32_t mx_id = mx_->bgp_identifier();
    TASK_UTIL_EXPECT_EQ(mx_id, rt1->BestPath()->GetPeer()->bgp_identifier());
    BgpRoute *rt2 = VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    uint32_t cn1_id = cn1_->bgp_identifier();
    TASK_UTIL_EXPECT_EQ(cn1_id, rt2->BestPath()->GetPeer()->bgp_identifier());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// If a RTargetRoute is received from EBGP peers that are in same AS, VPN
// routes should be sent to all the peers that sent the best path for the
// RTargetRoute, not just the one with the lowest router-id.
//
// This test was added when the default behavior of route target filtering
// got modified via launchpad bug 1509945.
//
TEST_F(BgpXmppRTargetTest, PathSelection1b) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64497);
    SubscribeAgents("blue", 1);
    task_util::WaitForIdle();

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    RTargetRoute *rtgt_rt =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, rtgt_rt->count());

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    uint32_t mx_id = mx_->bgp_identifier();
    BgpRoute *rt1 = VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    TASK_UTIL_EXPECT_EQ(mx_id, rt1->BestPath()->GetPeer()->bgp_identifier());
    BgpRoute *rt2 = VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());
    TASK_UTIL_EXPECT_EQ(mx_id, rt2->BestPath()->GetPeer()->bgp_identifier());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// If different RTargetRoutes for same RouteTarget are received from different
// EBGP peers, VPN routes should be sent to each peer.
//
TEST_F(BgpXmppRTargetTest, PathSelection2) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(cn1_.get(), cn2_.get());
    SubscribeAgents("blue", 1);
    task_util::WaitForIdle();

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// If RTargetRoute is received from an EBGP peer and an IBGP peer, VPN routes
// should be sent to the both the EBGP and IBGP peers.
//
TEST_F(BgpXmppRTargetTest, PathSelection3) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64497);
    agent_a_1_->Subscribe("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    RTargetRoute *rtgt_rt =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, rtgt_rt->count());

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
//
// If different RTargetRoutes for same RouteTarget are received from different
// EBGP and IBGP peers, VPN routes should be sent to each peer.
//
TEST_F(BgpXmppRTargetTest, PathSelection4) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64497);
    UnconfigureBgpPeering(cn1_.get(), cn2_.get());

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    SubscribeAgents("blue", 1);
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Propagation of RTargetRoutes and VPN routes across multiple ASes.
//
TEST_F(BgpXmppRTargetTest, PathSelection5) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    UnconfigureBgpPeering(mx_.get(), cn2_.get());
    task_util::WaitForIdle();
    SubscribeAgents("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");

    VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:1:1001");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// If RTargetRoute is received from EBGP peers in different ASes, VPN routes
// should be sent to peers in AS of the best EBGP path.
//
TEST_F(BgpXmppRTargetTest, PathSelection6) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64497, 64498);
    agent_a_1_->Subscribe("blue", 1);

    AddRouteTarget(cn1_.get(), "blue", "target:1:1001");
    AddRouteTarget(cn2_.get(), "blue", "target:1:1001");
    task_util::WaitForIdle();

    RTargetRoute *rtgt_rt =
        VerifyRTargetRouteExists(mx_.get(), "64496:target:1:1001");
    TASK_UTIL_EXPECT_EQ(2U, rtgt_rt->count());
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:1:1001");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    BgpRoute *inet_rt =
        VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    usleep(50000);
    TASK_UTIL_EXPECT_EQ(1U, inet_rt->count());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Local AS is different than the global AS.
// Verify that RTarget route prefixes use local AS while the route target
// itself uses the global AS.
// CN1, CN2 and MX have the same local AS.
//
TEST_F(BgpXmppRTargetTest, DifferentLocalAs1) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();
    BringupAgents();

    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("pink", 2);

    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:2");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:2");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:2");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:2");

    agent_a_1_->Unsubscribe("blue", -1);
    agent_a_2_->Unsubscribe("pink", -1);

    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:2");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:2");
}

//
// Local AS is different than the global AS.
// Verify that RTarget route prefixes use local AS while the route target
// itself uses the global AS.
// CN1, CN2 and MX have different local ASes.
//
TEST_F(BgpXmppRTargetTest, DifferentLocalAs2) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64496, 64497, 64498, 64499);
    task_util::WaitForIdle();
    BringupAgents();

    agent_a_1_->Subscribe("blue", 1);
    agent_a_2_->Subscribe("pink", 2);

    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64498:target:64496:2");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64498:target:64496:2");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64498:target:64496:2");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:2");

    agent_a_1_->Unsubscribe("blue", -1);
    agent_a_2_->Unsubscribe("pink", -1);

    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64498:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64498:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64498:target:64496:2");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:2");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:2");
}

//
// Default RTarget route is received/withdrawn at the CNs after the
// end of rib marker has been received from the MX.
// Single route on each CN.
//
TEST_F(BgpXmppRTargetTest, AddDeleteDefaultRTargetRoute1) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    AddInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(2));

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(2));

    AddRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteExists(cn1_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteExists(cn2_.get(), RTargetPrefix::kDefaultPrefixString);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(2));

    DeleteRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn1_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn2_.get(), RTargetPrefix::kDefaultPrefixString);

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(2));

    DeleteInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    DeleteInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(2));
}

//
// Default RTarget route is received/withdrawn at the CNs after the
// end of rib marker has been received from the MX.
// Multiple routes on each CN.
//
TEST_F(BgpXmppRTargetTest, AddDeleteDefaultRTargetRoute2) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "pink"));

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        AddInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(idx));
        AddInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(idx + 64));
        AddInetRoute(cn1_.get(), NULL, "pink", BuildPrefix(idx));
        AddInetRoute(cn2_.get(), NULL, "pink", BuildPrefix(idx + 64));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx + 64));
        VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(idx));
        VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(idx + 64));
    }

    AddRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(idx + 64));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx));
        VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(idx + 64));
    }

    DeleteRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    for (int idx = 1; idx <= kRouteCount; ++idx) {
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx));
        VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(idx + 64));
        VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(idx));
        VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(idx + 64));
    }

    for (int idx = 1; idx <= kRouteCount; ++idx) {
        DeleteInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(idx));
        DeleteInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(idx + 64));
        DeleteInetRoute(cn1_.get(), NULL, "pink", BuildPrefix(idx));
        DeleteInetRoute(cn2_.get(), NULL, "pink", BuildPrefix(idx + 64));
    }
}

//
// Default RTarget route is withdrawn at the CNs but a more specific
// RTarget route is still present.
//
TEST_F(BgpXmppRTargetTest, AddDeleteDefaultRTargetRoute3) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));

    AddRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    AddRTargetRoute(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    AddInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix(2));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(2));

    DeleteRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn1_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn2_.get(), RTargetPrefix::kDefaultPrefixString);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(2));

    DeleteRTargetRoute(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(2));

    DeleteInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    DeleteInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(2));
}

//
// Default RTarget route is withdrawn at the CNs but a more specific
// RTarget route is still present for one VRF, but not for another.
//
TEST_F(BgpXmppRTargetTest, AddDeleteDefaultRTargetRoute4) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddRouteTarget(mx_.get(), "pink", "target:64496:2");
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(mx_.get(), "pink"));

    AddRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    AddRTargetRoute(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    AddInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    AddInetRoute(cn1_.get(), NULL, "pink", BuildPrefix(1));
    AddInetRoute(cn2_.get(), NULL, "pink", BuildPrefix(2));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(2));
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "pink", BuildPrefix(2));

    DeleteRTargetRoute(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(mx_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn1_.get(), RTargetPrefix::kDefaultPrefixString);
    VerifyRTargetRouteNoExists(cn2_.get(), RTargetPrefix::kDefaultPrefixString);

    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteExists(mx_.get(), "blue", BuildPrefix(2));
    VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(2));

    DeleteRTargetRoute(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "blue", BuildPrefix(2));
    VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(1));
    VerifyInetRouteNoExists(mx_.get(), "pink", BuildPrefix(2));

    DeleteInetRoute(cn1_.get(), NULL, "blue", BuildPrefix(1));
    DeleteInetRoute(cn2_.get(), NULL, "blue", BuildPrefix(2));
    DeleteInetRoute(cn1_.get(), NULL, "pink", BuildPrefix(1));
    DeleteInetRoute(cn2_.get(), NULL, "pink", BuildPrefix(2));
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix(1));
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix(2));
    VerifyInetRouteNoExists(cn1_.get(), "pink", BuildPrefix(1));
    VerifyInetRouteNoExists(cn2_.get(), "pink", BuildPrefix(2));
}

//
// Setting and Clearing always subscribe on the routing instance without
// agents should result in RTarget route advertisement and withdrawal.
//
// Set always subscribe before inet route is added.
// Clear always subscribe before inet route is deleted.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribe1) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Setting and Clearing always subscribe on the routing instance without
// agents should result in RTarget route advertisement and withdrawal.
//
// Set always subscribe after inet route is added.
// Clear always subscribe after inet route is deleted.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribe2) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");
    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
}

//
// RTarget route should be advertised even if always subscribe is set only
// after an RTarget route is received from a peer.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribe3) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Routing instance has always subscribe as well as agent subscriptions.
// RTarget routes are withdrawn only after always subscribe is cleared
// and agents have unsubscribed.
//
// Agents subscribe before always subscribe is set.
// Agents unsubscribe before always subscribe is cleared.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithXmppSubscribe1) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SubscribeAgents("blue", 1);
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    UnsubscribeAgents("blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Routing instance has always subscribe as well as agent subscriptions.
// RTarget routes are withdrawn only after always subscribe is cleared
// and agents have unsubscribed.
//
// Agents subscribe before always subscribe is set.
// Agents unsubscribe after always subscribe is cleared.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithXmppSubscribe2) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SubscribeAgents("blue", 1);
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    UnsubscribeAgents("blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Routing instance has always subscribe as well as agent subscriptions.
// RTarget routes are withdrawn only after always subscribe is cleared
// and agents have unsubscribed.
//
// Agents subscribe after always subscribe is set.
// Agents unsubscribe before always subscribe is cleared.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithXmppSubscribe3) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    SubscribeAgents("blue", 1);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    UnsubscribeAgents("blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// Routing instance has always subscribe as well as agent subscriptions.
// RTarget routes are withdrawn only after always subscribe is cleared
// and agents have unsubscribed.
//
// Agents subscribe after always subscribe is set.
// Agents unsubscribe after always subscribe is cleared.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithXmppSubscribe4) {
    AddRouteTarget(mx_.get(), "blue", "target:64496:1");

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    SubscribeAgents("blue", 1);

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    AddInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
    VerifyInetRouteExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteExists(cn2_.get(), "blue", BuildPrefix());

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");

    UnsubscribeAgents("blue");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");

    VerifyInetRouteNoExists(cn1_.get(), "blue", BuildPrefix());
    VerifyInetRouteNoExists(cn2_.get(), "blue", BuildPrefix());
    DeleteInetRoute(mx_.get(), NULL, "blue", BuildPrefix());
}

//
// CNs have multiple VRFs with the same RT.
// The RTargetRoute should not be withdrawn till the RT is removed from all
// VRFs.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithSameTargetInMultipleInstances1) {
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "pink");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "pink");

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "pink", "target:1:99");
    AddRouteTarget(cn2_.get(), "pink", "target:1:99");

    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "blue", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "blue", "target:1:99");

    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn1_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn1_.get(), "pink"));
    TASK_UTIL_EXPECT_EQ(1U, GetExportRouteTargetListSize(cn2_.get(), "blue"));
    TASK_UTIL_EXPECT_EQ(2U, GetExportRouteTargetListSize(cn2_.get(), "pink"));
    task_util::WaitForIdle();

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    RemoveRouteTarget(cn1_.get(), "pink", "target:1:99");
    RemoveRouteTarget(cn2_.get(), "pink", "target:1:99");

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:1:99");
}

//
// CNs have multiple VRFs with the same RT.
// The RTargetRoute should be withdrawn till always subscribe is cleared from
// all VRFs.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeWithSameTargetInMultipleInstances2) {
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "pink");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "pink");

    AddRouteTarget(cn1_.get(), "blue", "target:1:99");
    AddRouteTarget(cn2_.get(), "blue", "target:1:99");
    AddRouteTarget(cn1_.get(), "pink", "target:1:99");
    AddRouteTarget(cn2_.get(), "pink", "target:1:99");

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");
    VerifyRTargetRouteExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:1:99");

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "pink");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "pink");
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:1:99");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:1:99");
}

//
// Update local ASN on CNs and make sure that the ASN in the RTargetRoute
// prefix is updated.
// Local AS is same as global AS.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeASNUpdate) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateASN(64497, 64497, 64497);
    task_util::WaitForIdle();

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

//
// Update local ASN on CNs and make sure that the ASN in the RTargetRoute
// prefix is updated.
// Local AS is different than global AS.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeLocalASNUpdate) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    VerifyRTargetRouteExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateASN(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();

    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

//
// Update Identifier on CNs and make sure that Nexthop in RTargetRoute prefix
// is updated.
// Local AS is same as global AS.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeIdentifierUpdate1) {
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    vector<string> nexthops0 = {"192.168.0.1", "192.168.0.2"};
    VerifyRTargetRouteNexthops(cn1_.get(), "64496:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(cn2_.get(), "64496:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(mx_.get(), "64496:target:64496:1", nexthops0);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateIdentifier(64496, 64496, 64496, 64496, 64496, 64496);
    task_util::WaitForIdle();

    vector<string> nexthops1 = list_of("192.168.1.1")("192.168.1.2");
    VerifyRTargetRouteNexthops(cn1_.get(), "64496:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(cn2_.get(), "64496:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(mx_.get(), "64496:target:64496:1", nexthops1);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64496:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64496:target:64496:1");
}

//
// Update Identifier on CNs and make sure that Nexthop in RTargetRoute prefix
// is updated.
// Local AS is different than global AS.
//
// Routing instances have always subscribe and there are no subscriptions
// from xmpp agents.
//
TEST_F(BgpXmppRTargetTest, AlwaysSubscribeIdentifierUpdate2) {
    Unconfigure();
    task_util::WaitForIdle();
    Configure(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();

    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));

    SetRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    SetRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    vector<string> nexthops0 = {"192.168.0.1", "192.168.0.2"};
    VerifyRTargetRouteNexthops(cn1_.get(), "64497:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(cn2_.get(), "64497:target:64496:1", nexthops0);
    VerifyRTargetRouteNexthops(mx_.get(), "64497:target:64496:1", nexthops0);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    UpdateIdentifier(64496, 64496, 64496, 64497, 64497, 64497);
    task_util::WaitForIdle();

    vector<string> nexthops1 = list_of("192.168.1.1")("192.168.1.2");
    VerifyRTargetRouteNexthops(cn1_.get(), "64497:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(cn2_.get(), "64497:target:64496:1", nexthops1);
    VerifyRTargetRouteNexthops(mx_.get(), "64497:target:64496:1", nexthops1);
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(7, RTargetRouteCount(mx_.get()));

    ClearRoutingInstanceAlwaysSubscribe(cn1_.get(), "blue");
    ClearRoutingInstanceAlwaysSubscribe(cn2_.get(), "blue");

    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn1_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(cn2_.get()));
    TASK_UTIL_EXPECT_EQ(3, RTargetRouteCount(mx_.get()));
    VerifyRTargetRouteNoExists(cn1_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(cn2_.get(), "64497:target:64496:1");
    VerifyRTargetRouteNoExists(mx_.get(), "64497:target:64496:1");
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
}

static void TearDown() {
    BgpServer::Terminate();
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
