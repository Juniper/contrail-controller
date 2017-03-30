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
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

using namespace std;

class BgpPeerCloseTest : public BgpPeerClose {
public:
    explicit BgpPeerCloseTest(BgpPeer *peer) :
            BgpPeerClose(peer), state_machine_restart_(true) { }
    virtual void RestartStateMachine() {
        if (state_machine_restart_)
            BgpPeerClose::RestartStateMachine();
    }

    void set_state_machine_restart(bool flag) { state_machine_restart_ = flag; }

private:
    bool state_machine_restart_;
};

static string clientsConfigStr =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>5</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"   </global-system-config>\n"
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
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>5</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"   </global-system-config>\n"
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
"           <gateway-address>100.0.0.1</gateway-address>\n"
"           <ipv6-gateway-address>::ffff:100.0.0.2</ipv6-gateway-address>\n"
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
"           <gateway-address>200.0.0.1</gateway-address>\n"
"           <ipv6-gateway-address>beef:beef::1</ipv6-gateway-address>\n"
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
        agent_.reset(new test::NetworkAgentMock(&evm_, "agent",
                                                xmpp_server_->GetPort()));
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

    BgpPeerTest *FindPeer(BgpServerTest *server, const char *instance_name,
                          const string &uuid) {
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
            dynamic_cast<BgpPeerTest *>(server->FindPeerByUuid(instance_name,
                                                               uuid)));
        BgpPeerTest *peer = dynamic_cast<BgpPeerTest *>(
                server->FindPeerByUuid(instance_name, uuid));
        return peer;
    }

    BgpPeerTest *WaitForPeerToComeUp(BgpServerTest *server,
                                     const string &peer_name) {
        string uuid =
            BgpConfigParser::session_uuid("bgpaas-server", peer_name, 1);
        BgpPeerTest *peer = FindPeer(server, BgpConfigManager::kMasterInstance,
                                     uuid);
        BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
        return peer;
    }

    void AddBgpInetRoute(BgpServerTest *server,
                         std::string prefix_str, std::string nexthop_str) {
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
        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);
        req.data.reset(new InetTable::RequestData(attr, 0, 0));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void DeleteBgpInetRoute(BgpServer *server, std::string prefix_str) {
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new InetTable::RequestKey(prefix, NULL));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void AddBgpInet6Route(BgpServerTest *server, std::string prefix_str,
                          std::string nexthop_str) {
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
        BgpAttrPtr attr = server->attr_db()->Locate(attr_spec);
        req.data.reset(new Inet6Table::RequestData(attr, 0, 0));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void DeleteBgpInet6Route(BgpServerTest *server, std::string prefix_str) {
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(new Inet6Table::RequestKey(prefix, NULL));

        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        table->Enqueue(&req);
        task_util::WaitForIdle();
    }

    void VerifyInetRouteCount(BgpServer *server, size_t expected) {
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        TASK_UTIL_EXPECT_EQ(expected, table->Size());
    }

    bool VerifyInetRoutePresenceActual(BgpServer *server,
                                       const std::string &prefix_str,
                                       const std::string &nexthop_str) {
        task_util::TaskSchedulerLock lock;
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        const InetTable::RequestKey key(prefix, NULL);
        if (!table->Find(&key))
            return false;
        const BgpRoute *rt = dynamic_cast<const BgpRoute *>(table->Find(&key));
        const BgpPath *path = rt->FindPath(BgpPath::BGP_XMPP);
        const IpAddress nexthop = path->GetAttr()->nexthop();
        return (nexthop_str == nexthop.to_string());
    }

    void VerifyInetRoutePresence(BgpServer *server,
                                 const std::string &prefix_str,
                                 const std::string &nexthop_str) {
        TASK_UTIL_EXPECT_TRUE(VerifyInetRoutePresenceActual(
            server, prefix_str, nexthop_str));
    }

    void VerifyInetRouteAbsence(BgpServer *server,
                                const std::string &prefix_str) {
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        const InetTable::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_EQ(static_cast<const BgpRoute *>(NULL),
                            table->Find(&key));
    }

    void VerifyInet6RouteCount(BgpServer *server, size_t expected) {
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        TASK_UTIL_EXPECT_EQ(expected, table->Size());
    }

    bool VerifyInet6RoutePresenceActual(BgpServer *server,
                                        const std::string &prefix_str,
                                        const std::string &nexthop_str) {
        task_util::TaskSchedulerLock lock;
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        const Inet6Table::RequestKey key(prefix, NULL);
        if (!table->Find(&key))
            return false;
        const BgpRoute *rt = dynamic_cast<const BgpRoute *>(table->Find(&key));
        const BgpPath *path = rt->FindPath(BgpPath::BGP_XMPP);
        const IpAddress nexthop = path->GetAttr()->nexthop();
        return (nexthop_str == nexthop.to_string());
    }

    void VerifyInet6RoutePresence(BgpServer *server,
                                  const std::string &prefix_str,
                                  const std::string &nexthop_str) {
        TASK_UTIL_EXPECT_TRUE(VerifyInet6RoutePresenceActual(
            server, prefix_str, nexthop_str));
    }

    void VerifyInet6RouteAbsence(BgpServer *server,
                                 const std::string &prefix_str) {
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        const Inet6Table::RequestKey key(prefix, NULL);
        TASK_UTIL_EXPECT_EQ(static_cast<const BgpRoute *>(NULL),
                            table->Find(&key));
    }

    bool VerifyInetRoutePresenceActual(test::NetworkAgentMock *agent,
                                 std::string prefix, std::string nexthop1,
                                 std::string nexthop2 = "") {
        task_util::TaskSchedulerLock lock;
        if (!agent->RouteLookup("test", prefix))
            return false;
        const test::NetworkAgentMock::RouteEntry *item =
            agent->RouteLookup("test", prefix);
        size_t nexthop_count = nexthop2.empty() ? 1 : 2;
        if (nexthop_count != item->entry.next_hops.next_hop.size())
            return false;
        if (nexthop1 != item->entry.next_hops.next_hop[0].address)
            return false;
        if (!nexthop2.empty()) {
            if (nexthop2 != item->entry.next_hops.next_hop[1].address)
                return false;
        }
        return true;
    }

    void VerifyInetRoutePresence(test::NetworkAgentMock *agent,
                                 std::string prefix, std::string nexthop1,
                                 std::string nexthop2 = "") {
        TASK_UTIL_EXPECT_TRUE(VerifyInetRoutePresenceActual(agent, prefix,
                                  nexthop1, nexthop2));
    }

    bool VerifyInet6RoutePresenceActual(test::NetworkAgentMock *agent,
                                        std::string prefix,
                                        std::string nexthop1,
                                        std::string nexthop2 = "") {
        task_util::TaskSchedulerLock lock;
        if (!agent->Inet6RouteLookup("test", prefix))
            return false;
        const test::NetworkAgentMock::RouteEntry *item =
            agent->Inet6RouteLookup("test", prefix);
        size_t nexthop_count = nexthop2.empty() ? 1 : 2;
        if ((nexthop_count != item->entry.next_hops.next_hop.size()))
            return false;
        if (nexthop1 != item->entry.next_hops.next_hop[0].address)
            return false;
        if (!nexthop2.empty()) {
            if (nexthop2 != item->entry.next_hops.next_hop[1].address)
                return false;
        }
        return true;
    }

    void VerifyInet6RoutePresence(test::NetworkAgentMock *agent,
                                  std::string prefix, std::string nexthop1,
                                  std::string nexthop2 = "") {
        TASK_UTIL_EXPECT_TRUE(VerifyInet6RoutePresenceActual(agent, prefix,
                                   nexthop1, nexthop2));
    }

    void VerifyInetRouteAbsence(test::NetworkAgentMock *agent,
                                std::string prefix) {
        TASK_UTIL_EXPECT_EQ(
            static_cast<const test::NetworkAgentMock::RouteEntry *>(NULL),
            agent->RouteLookup("test", prefix));
    }

    void VerifyInet6RouteAbsence(test::NetworkAgentMock *agent,
                                 std::string prefix) {
        TASK_UTIL_EXPECT_EQ(
            static_cast<const test::NetworkAgentMock::RouteEntry *>(NULL),
            agent->Inet6RouteLookup("test", prefix));
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
    boost::scoped_ptr<test::NetworkAgentMock> agent_;
    boost::scoped_ptr<BgpXmppChannelManager> channel_manager_;
};

TEST_F(BGPaaSTest, Basic) {
    SetUpBGPaaSPeers();
    SetUpAgent();

    // Add routes with shared nexthop and also a unique one to vm1.
    AddBgpInetRoute(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    AddBgpInet6Route(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");

    AddBgpInetRoute(vm1_.get(), "20.20.20.2/32", "1.1.1.2");
    AddBgpInet6Route(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");
    task_util::WaitForIdle();

    // Verify that unresolved bgp route is not received by the agent.
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.3/32");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.1/32");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.2/32");

    TASK_UTIL_EXPECT_EQ(0, agent_.get()->inet6_route_mgr_->Count());
    VerifyInet6RouteAbsence(agent_.get(), "dead:1::beef/128");
    VerifyInet6RouteAbsence(agent_.get(), "dead:2::beef/128");

    // Verify that unresolved bgp route is not received by the bgpass peer vm2
    VerifyInetRouteCount(vm2_.get(), 0);
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.3/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.2/32");

    VerifyInet6RouteCount(vm2_.get(), 0);
    VerifyInet6RouteAbsence(vm2_.get(), "dead:1::beef/128");
    VerifyInet6RouteAbsence(vm2_.get(), "dead:2::beef/128");

    // Verify that unresolved bgp route is not received by the bgpass peer vm2
    // But locally added routes vm1 must be still present in its tables.
    VerifyInetRouteCount(vm1_.get(), 2);
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.3/32");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");

    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RoutePresence(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");
    VerifyInet6RoutePresence(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");

    // Add a route to make bgp route's nexthop resolvable.
    agent_->AddRoute("test", "1.1.1.1/32", "10.10.10.1");

    // Verify that now resolved inet bgp route is indeed received by the agent.
    TASK_UTIL_EXPECT_EQ(2, agent_->route_mgr_->Count());
    VerifyInetRoutePresence(agent_.get(), "1.1.1.1/32", "10.10.10.1");
    VerifyInetRoutePresence(agent_.get(), "20.20.20.1/32", "10.10.10.1");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.2/32");

    // Verify that now resolved inet bgp route is indeed received by vm2 with
    // correct gateway-address as specified in the configuration.
    VerifyInetRouteCount(vm2_.get(), 2);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.1/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.1/32", "200.0.0.1");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.2/32");

    // Verify that now resolved inet bgp route is indeed received by vm1.
    VerifyInetRouteCount(vm1_.get(), 3);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.1/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");

    // Verify that now resolved inet6 bgp route is indeed received by agent.
    TASK_UTIL_EXPECT_EQ(1, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "dead:1::beef/128", "10.10.10.1");
    VerifyInet6RouteAbsence(agent_.get(), "dead:2::beef/128");

    // Verify that now resolved inet6 bgp route is indeed received by vm2.
    VerifyInet6RouteCount(vm2_.get(), 1);
    VerifyInet6RoutePresence(vm2_.get(), "dead:1::beef/128", "beef:beef::1");
    VerifyInet6RouteAbsence(vm2_.get(), "dead:2::beef/128");

    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RoutePresence(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");
    VerifyInet6RoutePresence(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");

    // Make other vm1 routes also now resolvable.
    agent_->AddRoute("test", "1.1.1.2/32", "10.10.10.2");
    agent_->AddRoute("test", "1.1.1.3/32", "10.10.10.3");

    // Verify that now resolved inet bgp route is indeed received by the agent.
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRoutePresence(agent_.get(), "1.1.1.1/32", "10.10.10.1");
    VerifyInetRoutePresence(agent_.get(), "1.1.1.2/32", "10.10.10.2");
    VerifyInetRoutePresence(agent_.get(), "1.1.1.3/32", "10.10.10.3");
    VerifyInetRoutePresence(agent_.get(), "20.20.20.1/32", "10.10.10.1");
    VerifyInetRoutePresence(agent_.get(), "20.20.20.2/32", "10.10.10.2");

    // Verify that now resolved inet bgp route is indeed received by vm2 with
    // correct gateway-address as specified in the configuration.
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRouteCount(vm2_.get(), 5);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.1/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.2/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.3/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.1/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.2/32", "200.0.0.1");

    // Verify that now resolved inet bgp route is indeed received by vm1.
    VerifyInetRouteCount(vm1_.get(), 5);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.1/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.2/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.3/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");

    // Verify that now resolved inet6 bgp route is indeed received by agent.
    TASK_UTIL_EXPECT_EQ(2, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "dead:1::beef/128", "10.10.10.1");
    VerifyInet6RoutePresence(agent_.get(), "dead:2::beef/128", "10.10.10.3");

    // Verify that now resolved inet6 bgp route is indeed received by vm2.
    VerifyInet6RouteCount(vm2_.get(), 2);
    VerifyInet6RoutePresence(vm2_.get(), "dead:1::beef/128", "beef:beef::1");
    VerifyInet6RoutePresence(vm2_.get(), "dead:2::beef/128", "beef:beef::1");

    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RoutePresence(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");
    VerifyInet6RoutePresence(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");

    // Add an inet6 route to vm2 and verify that vm1 receives it with correct
    // ipv4-in-ipv6 next-hop.
    AddBgpInet6Route(vm2_.get(), "feed:1::feed/128", "::ffff:1.1.1.1");
    VerifyInet6RouteCount(vm2_.get(), 3);
    VerifyInet6RoutePresence(vm2_.get(), "feed:1::feed/128", "::ffff:1.1.1.1");
    VerifyInet6RouteCount(vm1_.get(), 3);
    VerifyInet6RoutePresence(vm1_.get(), "feed:1::feed/128", "100.0.0.2");

    // Close dut === vm1 session gracefully and verify that vm2 and agent still
    // have all the routes because routes are retained as 'stale' in dut even
    // when the session to BGPaaS client vm1 is closed
    BgpPeerTest *peer_vm1 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm1", 1));
    static_cast<BgpPeerCloseTest *>(peer_vm1->peer_close())->
        set_state_machine_restart(false);

    BgpNeighborResp resp;
    TASK_UTIL_EXPECT_EQ(0, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_stale());
    TASK_UTIL_EXPECT_EQ(0, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_gr_timer());
    TASK_UTIL_EXPECT_EQ(0, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_llgr_timer());
    TASK_UTIL_EXPECT_EQ(0, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_sweep());

    // Trigger graceful session closure bby faking HoldTimerExpiry.
    peer_vm1->state_machine()->HoldTimerExpired();
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRouteCount(vm2_.get(), 5);
    TASK_UTIL_EXPECT_EQ(3, agent_->inet6_route_mgr_->Count());
    VerifyInet6RouteCount(vm2_.get(), 3);

    // Wait until peer enters LLGR state.
    TASK_UTIL_EXPECT_EQ(1, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_stale());
    TASK_UTIL_EXPECT_EQ(1, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_gr_timer());
    TASK_UTIL_EXPECT_EQ(1, peer_vm1->close_manager()->FillCloseInfo(&resp)->
            get_peer_close_info().get_llgr_timer());

    // Restart BGP state machine.
    static_cast<BgpPeerCloseTest *>(peer_vm1->peer_close())->
        set_state_machine_restart(true);
    static_cast<BgpPeerCloseTest *>(peer_vm1->peer_close())->
        RestartStateMachine();
    BGP_WAIT_FOR_PEER_STATE(peer_vm1, StateMachine::ESTABLISHED);

    // GR session would have either got refreshed or delted, based on the timer
    // expiry and when the session comes up. Most of time time, we expect the
    // session to be swept. But add delete check to make test more stable.
    TASK_UTIL_EXPECT_TRUE(peer_vm1->close_manager()->FillCloseInfo(&resp)->
                              get_peer_close_info().get_sweep() ||
                          peer_vm1->close_manager()->FillCloseInfo(&resp)->
                              get_peer_close_info().get_deletes());

    // Verify that all routes remain or get re-advertised.
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRouteCount(vm2_.get(), 5);
    TASK_UTIL_EXPECT_EQ(3, agent_->inet6_route_mgr_->Count());
    VerifyInet6RouteCount(vm2_.get(), 3);
    VerifyInetRouteCount(vm1_.get(), 5);
    VerifyInet6RouteCount(vm1_.get(), 3);

    // Delete the route just added above and verify their absence.
    DeleteBgpInet6Route(vm2_.get(), "feed:1::feed/128");
    VerifyInet6RouteCount(vm2_.get(), 2);
    VerifyInet6RouteAbsence(vm2_.get(), "feed:1::feed/128");
    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RouteAbsence(vm1_.get(), "feed:1::feed/128");

    // Add same inet and inet6 prefix to both bgpaas and verify ecmp nexthop
    // in the agent.
    AddBgpInetRoute(vm1_.get(), "30.20.20.1/32", "3.1.1.1");
    AddBgpInet6Route(vm1_.get(), "feed:2::feed/128", "::ffff:3.1.1.1");
    AddBgpInetRoute(vm2_.get(), "30.20.20.1/32", "4.1.1.1");
    AddBgpInet6Route(vm2_.get(), "feed:2::feed/128", "::ffff:4.1.1.1");
    agent_->AddRoute("test", "3.1.1.1/32", "30.10.10.2");
    agent_->AddRoute("test", "4.1.1.1/32", "40.10.10.2");

    // Verify new inet routes added above along with ecmp next-hop.
    TASK_UTIL_EXPECT_EQ(8, agent_->route_mgr_->Count());
    VerifyInetRoutePresence(agent_.get(), "3.1.1.1/32", "30.10.10.2");
    VerifyInetRoutePresence(agent_.get(), "4.1.1.1/32", "40.10.10.2");
    VerifyInetRoutePresence(agent_.get(), "30.20.20.1/32",
                            "30.10.10.2", "40.10.10.2");

    // Verify new inet6 route added above along with ecmp next-hop.
    TASK_UTIL_EXPECT_EQ(3, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "feed:2::feed/128",
                             "30.10.10.2", "40.10.10.2");

    // Delete the ecmp routes added above and verify for their absence.
    DeleteBgpInetRoute(vm1_.get(), "30.20.20.1/32");
    DeleteBgpInet6Route(vm1_.get(), "feed:2::feed/128");
    DeleteBgpInetRoute(vm2_.get(), "30.20.20.1/32");
    DeleteBgpInet6Route(vm2_.get(), "feed:2::feed/128");
    agent_->DeleteRoute("test", "3.1.1.1/32");
    agent_->DeleteRoute("test", "4.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRouteAbsence(agent_.get(), "3.1.1.1/32");
    VerifyInetRouteAbsence(agent_.get(), "4.1.1.1/32");
    VerifyInetRouteAbsence(agent_.get(), "30.20.20.1/32");

    TASK_UTIL_EXPECT_EQ(2, agent_->inet6_route_mgr_->Count());
    VerifyInet6RouteAbsence(agent_.get(), "feed:2::feed/128");

    // Delete agent route to make bgp route's nexthop not resolvable any more.
    agent_->DeleteRoute("test", "1.1.1.1/32");

    // Verify that some routes are now deleted as nexthop is not resolvable.
    TASK_UTIL_EXPECT_EQ(3, agent_->route_mgr_->Count());
    VerifyInetRoutePresence(agent_.get(), "1.1.1.2/32", "10.10.10.2");
    VerifyInetRoutePresence(agent_.get(), "1.1.1.3/32", "10.10.10.3");
    VerifyInetRoutePresence(agent_.get(), "20.20.20.2/32", "10.10.10.2");
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.1/32");

    VerifyInetRouteCount(vm2_.get(), 3);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.2/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.3/32", "200.0.0.1");
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.2/32", "200.0.0.1");
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.1/32");

    VerifyInetRouteCount(vm1_.get(), 4);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.2/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.3/32", "100.0.0.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.1/32");

    TASK_UTIL_EXPECT_EQ(1, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "dead:2::beef/128", "10.10.10.3");
    VerifyInet6RouteAbsence(agent_.get(), "dead:1::beef/128");

    VerifyInet6RouteCount(vm2_.get(), 1);
    VerifyInet6RoutePresence(vm2_.get(), "dead:2::beef/128", "beef:beef::1");
    VerifyInet6RouteAbsence(vm2_.get(), "dead:1::beef/128");

    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RoutePresence(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");
    VerifyInet6RoutePresence(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");

    // Delete other routes as well to make all bgp routes not resolvable any
    // more. Now agent should have no route at all.
    agent_->DeleteRoute("test", "1.1.1.2/32");
    agent_->DeleteRoute("test", "1.1.1.3/32");
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(agent_.get(), "1.1.1.3/32");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.1/32");
    VerifyInetRouteAbsence(agent_.get(), "20.20.20.2/32");

    TASK_UTIL_EXPECT_EQ(0, agent_->inet6_route_mgr_->Count());
    VerifyInet6RouteAbsence(agent_.get(), "dead:1::beef/128");
    VerifyInet6RouteAbsence(agent_.get(), "dead:2::beef/128");

    VerifyInetRouteCount(vm2_.get(), 0);
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.3/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.2/32");

    VerifyInet6RouteCount(vm2_.get(), 0);
    VerifyInet6RouteAbsence(vm2_.get(), "dead:1::beef/128");
    VerifyInet6RouteAbsence(vm2_.get(), "dead:2::beef/128");

    // vm1 should still have the routes added to its tables at the beginning.
    VerifyInetRouteCount(vm1_.get(), 2);
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.2/32");
    VerifyInetRouteAbsence(vm1_.get(), "1.1.1.3/32");

    VerifyInet6RouteCount(vm1_.get(), 2);
    VerifyInet6RoutePresence(vm1_.get(), "dead:1::beef/128", "::ffff:1.1.1.1");
    VerifyInet6RoutePresence(vm1_.get(), "dead:2::beef/128", "::ffff:1.1.1.3");

    // Delete all BGPaaS routes addded earlier.
    DeleteBgpInetRoute(vm1_.get(), "20.20.20.1/32");
    DeleteBgpInetRoute(vm1_.get(), "20.20.20.2/32");
    DeleteBgpInet6Route(vm1_.get(), "dead:1::beef/128");
    DeleteBgpInet6Route(vm1_.get(), "dead:2::beef/128");

    // Verify that agent still has no route.
    TASK_UTIL_EXPECT_EQ(0, agent_->route_mgr_->Count());
    TASK_UTIL_EXPECT_EQ(0, agent_->inet6_route_mgr_->Count());

    // Verify that BGPaaS servers also have no routes.
    VerifyInetRouteCount(vm2_.get(), 0);
    VerifyInet6RouteCount(vm2_.get(), 0);
    VerifyInetRouteCount(vm1_.get(), 0);
    VerifyInet6RouteCount(vm1_.get(), 0);
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    BgpObjectFactory::Register<BgpPeerClose>(
        boost::factory<BgpPeerCloseTest *>());
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
