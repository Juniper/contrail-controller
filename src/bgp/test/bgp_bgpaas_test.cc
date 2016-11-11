/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>

#include "base/task_annotations.h"
#include "control-node/test/network_agent_mock.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using namespace std;

static string clientsConfigStr =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <bgp-router name='bgpaas-server'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n"
"       <session to='vm1'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n"
"       </session> \n"
"       <session to='vm2'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n"
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm1'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>65001</autonomous-system> \n"
"       <port>__vm1_port__</port> \n"
"       <identifier>10.0.0.1</identifier> \n"
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n"
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm2'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>65002</autonomous-system> \n"
"       <port>__vm2_port__</port> \n"
"       <identifier>10.0.0.2</identifier> \n"
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n"
"       </session> \n"
"   </bgp-router> \n"
"</config> \n"
;

static string serverConfigStr =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <bgp-router name='local'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n"
"   </bgp-router> \n"
"   <routing-instance name='test'> \n"
"       <vrf-target>target:64512:1</vrf-target> \n"
"       <bgp-router name='bgpaas-server'> \n"
"           <router-type>bgpaas-server</router-type> \n"
"           <autonomous-system>64512</autonomous-system> \n"
"           <port>__server_port__</port> \n"
"           <session to='vm1'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n"
"           </session> \n"
"           <session to='vm2'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n"
"           </session> \n"
"       </bgp-router> \n"
"       <bgp-router name='vm1'> \n"
"           <router-type>bgpaas-client</router-type> \n"
"           <autonomous-system>65001</autonomous-system> \n"
"           <address>127.0.0.1</address> \n"
"           <source-port>11024</source-port> \n"
"           <session to='bgpaas-server'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n"
"           </session> \n"
"       </bgp-router> \n"
"       <bgp-router name='vm2'> \n"
"           <router-type>bgpaas-client</router-type> \n"
"           <autonomous-system>65002</autonomous-system> \n"
"           <address>127.0.0.1</address> \n"
"           <source-port>11025</source-port> \n"
"           <session to='bgpaas-server'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n"
"           </session> \n"
"       </bgp-router> \n"
"   </routing-instance> \n"
"</config> \n"
;

class BGPaaSTest : public ::testing::Test {
protected:
    BGPaaSTest() :
            server_session_manager_(NULL), vm1_session_manager_(NULL),
            vm2_session_manager_(NULL) {
    }

    virtual void SetUp() {
        server_.reset(new BgpServerTest(&evm_, "local"));
        vm1_.reset(new BgpServerTest(&evm_, "vm1"));
        vm2_.reset(new BgpServerTest(&evm_, "vm2"));
        thread_.reset(new ServerThread(&evm_));

        server_session_manager_ = server_->session_manager();
        server_session_manager_->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            server_session_manager_->GetPort());

        vm1_session_manager_ = vm1_->session_manager();
        vm1_session_manager_->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            vm1_session_manager_->GetPort());

        vm2_session_manager_ = vm2_->session_manager();
        vm2_session_manager_->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            vm2_session_manager_->GetPort());
        xmpp_server_ = new XmppServerTest(&evm_, "bgp.contrail.com");
        channel_manager_.reset(new BgpXmppChannelManager(xmpp_server_,
                                                         server_.get()));
        xmpp_server_->Initialize(0, false);
        thread_->Start();
    }

    virtual void TearDown() {
        agent_->SessionDown();
        agent_->Delete();
        task_util::WaitForIdle();
        server_->Shutdown();
        xmpp_server_->Shutdown();
        vm1_->Shutdown();
        task_util::WaitForIdle();
        vm2_->Shutdown();
        task_util::WaitForIdle();
        XmppShutdown();

        TASK_UTIL_EXPECT_EQ(0, TcpServerManager::GetServerCount());


        evm_.Shutdown();
        if (thread_.get() != NULL)
            thread_->Join();
    }

    void SetUpAgent() {
        agent_ = new test::NetworkAgentMock(&evm_, "agent",
                                            xmpp_server_->GetPort());
        agent_->SessionUp();
        TASK_UTIL_EXPECT_TRUE(agent_->IsEstablished());
        agent_->SubscribeAll("test", 1);
    }

    void SetUpBGPaaSPeers() {
        server_->set_peer_lookup_disable(true);
        vm1_->set_source_port(11024);
        vm2_->set_source_port(11025);
        boost::replace_all(serverConfigStr, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(clientsConfigStr, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(clientsConfigStr, "__vm1_port__",
            boost::lexical_cast<string>(vm1_session_manager_->GetPort()));
        boost::replace_all(clientsConfigStr, "__vm2_port__",
            boost::lexical_cast<string>(vm2_session_manager_->GetPort()));
        vm1_->Configure(clientsConfigStr);
        vm2_->Configure(clientsConfigStr);
        task_util::WaitForIdle();

        server_->Configure(serverConfigStr);
        WaitForPeerToComeUp(vm1_.get(), "vm1");
        WaitForPeerToComeUp(vm2_.get(), "vm2");
    }

    void XmppShutdown() {
        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, xmpp_server_->ConnectionCount());
        channel_manager_.reset();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
    }

    BgpPeerTest *WaitForPeerToComeUp(BgpServerTest *server,
                                     const string &peerName) {
        string u = BgpConfigParser::session_uuid("bgpaas-server", peerName, 1);
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
            dynamic_cast<BgpPeerTest *>(
                server->FindPeerByUuid(BgpConfigManager::kMasterInstance, u)));
        BgpPeerTest *peer = dynamic_cast<BgpPeerTest *>(
                server->FindPeerByUuid(BgpConfigManager::kMasterInstance, u));
        BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
        return peer;
    }

    void AddBgpInetRoute(std::string prefix_str, std::string nexthop_str) {
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));

        int plen;
        Ip4Address nexthop_addr;
        Ip4PrefixParse(nexthop_str + "/32", &nexthop_addr, &plen);
        BgpAttrNextHop nexthop(nexthop_addr);

        BgpAttrSpec attr_spec;
        attr_spec.push_back(&nexthop);

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new InetTable::RequestKey(prefix, NULL));
        BgpAttrPtr attr = vm1_->attr_db()->Locate(attr_spec);
        req.data.reset(new InetTable::RequestData(attr, 0, 0));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            vm1_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void DeleteBgpInetRoute(std::string prefix_str) {
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new InetTable::RequestKey(prefix, NULL));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            vm1_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void AddBgpInet6Route(std::string prefix_str, std::string nexthop_str) {
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));

        int plen;
        Ip6Address nexthop_addr;
        Inet6PrefixParse(nexthop_str + "/128", &nexthop_addr, &plen);
        BgpAttrNextHop nexthop(nexthop_addr);

        BgpAttrSpec attr_spec;
        attr_spec.push_back(&nexthop);

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(new Inet6Table::RequestKey(prefix, NULL));
        BgpAttrPtr attr = vm1_->attr_db()->Locate(attr_spec);
        req.data.reset(new Inet6Table::RequestData(attr, 0, 0));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            vm1_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void DeleteBgpInet6Route(std::string prefix_str) {
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new Inet6Table::RequestKey(prefix, NULL));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            vm1_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    EventManager evm_;
    boost::scoped_ptr<ServerThread> thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    boost::scoped_ptr<BgpServerTest> vm1_;
    boost::scoped_ptr<BgpServerTest> vm2_;
    BgpSessionManager *server_session_manager_;
    BgpSessionManager *vm1_session_manager_;
    BgpSessionManager *vm2_session_manager_;
    XmppServerTest *xmpp_server_;
    test::NetworkAgentMock *agent_;
    boost::scoped_ptr<BgpXmppChannelManager> channel_manager_;
};

TEST_F(BGPaaSTest, Basic) {
    SetUpBGPaaSPeers();
    SetUpAgent();

    // Add routes with shared nexthop and also a unique one.
    AddBgpInetRoute("20.20.20.1/32", "1.1.1.1");
    AddBgpInet6Route("dead:1::beaf/128", "::ffff:1.1.1.1");

    AddBgpInetRoute("20.20.20.2/32", "1.1.1.2");
    AddBgpInet6Route("dead:2::beaf/128", "::ffff:1.1.1.3");
    task_util::WaitForIdle();

    // Verify that unresolved bgp route is not received by the agent.
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(0, agent_->inet6_route_mgr_->Count());

    // Add a route to make bgp route's nexthop resolvable.
    agent_->AddRoute("test", "1.1.1.1/32", "10.10.10.10");

    // Verify that now resolved bgp route is indeed received by the agent.
    TASK_UTIL_EXPECT_EQ(2, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(1, agent_->inet6_route_mgr_->Count());

    agent_->AddRoute("test", "1.1.1.3/32", "10.10.10.10");

    // Verify that now resolved bgp route is indeed received by the agent.
    TASK_UTIL_EXPECT_EQ(3, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(2, agent_->inet6_route_mgr_->Count());

    // Delete agent route to make bgp route's nexthop unresolvable.
    agent_->DeleteRoute("test", "1.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(1, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(1, agent_->inet6_route_mgr_->Count());

    agent_->DeleteRoute("test", "1.1.1.3/32");
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(0, agent_->inet6_route_mgr_->Count());

    // Delete BGPaaS route.
    DeleteBgpInetRoute("20.20.20.1/32");
    DeleteBgpInetRoute("20.20.20.2/32");
    DeleteBgpInet6Route("dead:1::beaf/128");
    DeleteBgpInet6Route("dead:2::beaf/128");

    // Verify that agent still has no route.
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(0, agent_->inet6_route_mgr_->Count());
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    BgpServerTest::GlobalSetUp();
}

static void TearDown() {
    BgpServer::Terminate();
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
