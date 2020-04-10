/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>

#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
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
#include "rtarget/rtarget_route.h"
#include "rtarget/rtarget_table.h"

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

typedef std::tr1::tuple<bool, bool, bool, bool> TestParams;
class BGPaaSTest : public ::testing::TestWithParam<TestParams>{
public:
    // Allow IBGP bgpaas sessions for this test.
    bool ProcessSession() const { return true; }

    // Disable IBGP Split Horizon check for this test.
    virtual bool CheckSplitHorizon(uint32_t cluster_id = 0,
            uint32_t ribout_cid = 0) const { return false; }

protected:
    BGPaaSTest() :
            server_session_manager_(NULL), vm1_session_manager_(NULL),
            vm2_session_manager_(NULL) {
    }

    virtual void SetUp() {
        set_local_as_ = std::tr1::get<0>(GetParam());
        ebgp_ = std::tr1::get<1>(GetParam());
        set_auth_ = std::tr1::get<2>(GetParam());
        server_as4_supported_ = std::tr1::get<3>(GetParam());
        server_.reset(new BgpServerTest(&evm_, "local"));
        server2_.reset(new BgpServerTest(&evm_, "remote"));
        vm1_.reset(new BgpServerTest(&evm_, "vm1"));
        vm2_.reset(new BgpServerTest(&evm_, "vm2"));
        if (server_as4_supported_) {
            server_->set_enable_4byte_as(true);
            server2_->set_enable_4byte_as(true);
            vm1_->set_enable_4byte_as(true);
            vm2_->set_enable_4byte_as(true);
        }
        thread_.reset(new ServerThread(&evm_));

        server_session_manager_ = server_->session_manager();
        server2_session_manager_ = server2_->session_manager();
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
        vm1_->Shutdown();
        task_util::WaitForIdle();
        vm2_->Shutdown();
        task_util::WaitForIdle();
        BgpPeerTest *peer_vm1 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm1", 1));
        VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm1, false);
        BgpPeerTest *peer_vm2 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm2", 1));
        VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm2, false);
        agent_->SessionDown();
        agent_->Delete();
        task_util::WaitForIdle();
        xmpp_server_->Shutdown();
        server_->Shutdown();
        server2_->Shutdown();
        XmppShutdown();

        TASK_UTIL_EXPECT_EQ(0U, TcpServerManager::GetServerCount());

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

    bool WalkCallback(const DBTablePartBase *tpart,
                      const DBEntryBase *db_entry) const {
        CHECK_CONCURRENCY("db::DBTable");
        const BgpRoute *route = static_cast<const BgpRoute *>(db_entry);
        std::cout << route->ToString() << "(" << route->count() << ") 0x";
        std::cout << std::hex << (uint64_t) route << std::endl;
        return true;
    }

    void WalkDoneCallback(DBTable::DBTableWalkRef ref,
                          const DBTableBase *table, bool *complete) const {
        if (complete)
            *complete = true;
    }

    void PrintTable(const BgpTable *table) const {
        bool complete = false;
        DBTable::DBTableWalkRef walk_ref =
            const_cast<BgpTable *>(table)->AllocWalker(
                boost::bind(&BGPaaSTest::WalkCallback, this, _1, _2),
                boost::bind(&BGPaaSTest::WalkDoneCallback, this, _1, _2,
                            &complete));
        std::cout << "Table " << table->name() << " walk start\n";
        const_cast<BgpTable *>(table)->WalkTable(walk_ref);
        TASK_UTIL_EXPECT_TRUE(complete);
        std::cout << "Table " << table->name() << " walk end\n";
    }

    void CheckBGPaaSRTargetRoutePresence(const BgpTable *table,
            const BgpPeerTest *peer, bool *result) const {
        CHECK_CONCURRENCY("bgp::Config");
        const RTargetTable::RequestKey key(
            RTargetPrefix::FromString("64512:target:64512:100", NULL), peer);
        const BgpRoute *rt = static_cast<const BgpRoute *>(table->Find(&key));
        if (!rt) {
            *result = false;
            return;
        }
        const BgpPath *path = rt->FindPath(peer);
        *result = path != NULL;
    }

    bool CheckBGPaaSRTargetRoutePresence(const BgpTable *table,
                                         const BgpPeerTest *peer) const {
        bool result = false;
        task_util::TaskFire(
            boost::bind(&BGPaaSTest::CheckBGPaaSRTargetRoutePresence, this,
                        table, peer, &result), "bgp::Config");
        return result;
    }

    void CheckBGPaaSRTargetRouteAbsence(const BgpTable *table,
            const BgpPeerTest *peer, bool *result) const {
        CHECK_CONCURRENCY("bgp::Config");
        const RTargetTable::RequestKey key(
            RTargetPrefix::FromString("64512:target:64512:100", NULL), peer);
        const BgpRoute *rt = static_cast<const BgpRoute *>(table->Find(&key));
        if (!rt) {
            *result = true;
            return;
        }
        const BgpPath *path = rt->FindPath(peer);
        *result = path == NULL;
    }

    bool CheckBGPaaSRTargetRouteAbsence(const BgpTable *table,
                                        const BgpPeerTest *peer) const {
        bool result = false;
        task_util::TaskFire(
            boost::bind(&BGPaaSTest::CheckBGPaaSRTargetRouteAbsence, this,
                        table, peer, &result), "bgp::Config");
        return result;
    }

    void VerifyBGPaaSRTargetRoutes(const BgpServerTest *server,
            const BgpPeerTest *peer, bool presence) const {
        const RoutingInstance *rtinstance= static_cast<const RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        const BgpTable *table = rtinstance->GetTable(Address::RTARGET);
        if (presence) {
            BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
            TASK_UTIL_EXPECT_EQ(true,
                CheckBGPaaSRTargetRoutePresence(table, peer));
        } else {
            BGP_WAIT_FOR_PEER_STATE_NE(peer, StateMachine::ESTABLISHED);
            TASK_UTIL_EXPECT_EQ(true,
                CheckBGPaaSRTargetRouteAbsence(table, peer));
        }
    }

    void UpdateTemplates() {
        // server_->set_peer_lookup_disable(true);
        // server2_->set_peer_lookup_disable(true);
        vm1_->set_source_port(11024);
        vm2_->set_source_port(11025);
        boost::replace_all(server_config_, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(server_config_, "__server2_port__",
            boost::lexical_cast<string>(server2_session_manager_->GetPort()));
        boost::replace_all(server2_config_, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(server2_config_, "__server2_port__",
            boost::lexical_cast<string>(server2_session_manager_->GetPort()));

        boost::replace_all(vm1_client_config_, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(vm1_client_config_, "__vm1_port__",
            boost::lexical_cast<string>(vm1_session_manager_->GetPort()));
        boost::replace_all(vm1_client_config_, "__vm2_port__",
            boost::lexical_cast<string>(vm2_session_manager_->GetPort()));

        boost::replace_all(vm2_client_config_, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));
        boost::replace_all(vm2_client_config_, "__vm1_port__",
            boost::lexical_cast<string>(vm1_session_manager_->GetPort()));
        boost::replace_all(vm2_client_config_, "__vm2_port__",
            boost::lexical_cast<string>(vm2_session_manager_->GetPort()));
    }

    void XmppShutdown() {
        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0U, xmpp_server_->ConnectionCount());
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
                                       const std::string &nexthop_str,
                                       const std::string &as_path = "") {
        task_util::TaskSchedulerLock lock;
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET);
        Ip4Prefix prefix(Ip4Prefix::FromString(prefix_str));
        const InetTable::RequestKey key(prefix, NULL);
        if (!table->Find(&key))
            return false;

        // BGPaaS is _not_ supported for ibgp. Just for completeness, this test
        // also tests ibgp bgpaas sessions bringup part. As-path is not modified
        // for ibgp either. Also, nexthop is not modified and resolved. All the
        // test parameters passed in here are only applicable to ebgp.
        if (!ebgp_)
            return true;
        const BgpRoute *rt = dynamic_cast<const BgpRoute *>(table->Find(&key));
        const BgpPath *path = rt->FindPath(BgpPath::BGP_XMPP);
        const IpAddress nexthop = path->GetAttr()->nexthop();
        if (nexthop_str != nexthop.to_string())
            return false;
        if (!as_path.empty()) {
            if (!server->enable_4byte_as()) {
                if (!path->GetAttr()->as_path())
                    return false;
                if (as_path != path->GetAttr()->as_path()->path().ToString())
                    return false;
            } else {
                if (!path->GetAttr()->aspath_4byte())
                    return false;
                if (as_path != path->GetAttr()->aspath_4byte()->
                               path().ToString()) {
                    return false;
                }
            }
        }
        return true;
    }

    void VerifyInetRoutePresence(BgpServer *server,
                                 const std::string &prefix_str,
                                 const std::string &nexthop_str,
                                 const std::string &as_path = "") {
        TASK_UTIL_EXPECT_TRUE(VerifyInetRoutePresenceActual(
            server, prefix_str, nexthop_str, as_path));
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
                                        const std::string &nexthop_str,
                                        const std::string &as_path = "") {
        task_util::TaskSchedulerLock lock;
        RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
            server->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        BgpTable *table = rtinstance->GetTable(Address::INET6);
        Inet6Prefix prefix(Inet6Prefix::FromString(prefix_str));
        const Inet6Table::RequestKey key(prefix, NULL);
        if (!table->Find(&key))
            return false;
        if (!ebgp_)
            return true;
        const BgpRoute *rt = dynamic_cast<const BgpRoute *>(table->Find(&key));
        const BgpPath *path = rt->FindPath(BgpPath::BGP_XMPP);
        const IpAddress nexthop = path->GetAttr()->nexthop();
        if (nexthop_str != nexthop.to_string())
            return false;
        if (!as_path.empty()) {
            if (!server->enable_4byte_as()) {
                if (!path->GetAttr()->as_path())
                    return false;
                if (as_path != path->GetAttr()->as_path()->path().ToString())
                    return false;
            } else {
                if (!path->GetAttr()->aspath_4byte())
                    return false;
                if (as_path != path->GetAttr()->aspath_4byte()->
                               path().ToString()) {
                    return false;
                }
            }
        }
        return true;
    }

    void VerifyInet6RoutePresence(BgpServer *server,
                                  const std::string &prefix_str,
                                  const std::string &nexthop_str,
                                  const std::string &as_path = "") {
        TASK_UTIL_EXPECT_TRUE(VerifyInet6RoutePresenceActual(
            server, prefix_str, nexthop_str, as_path));
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

    void InitializeTemplates(bool set_local_as, const string &vm1_server_as,
                             const string &vm2_server_as, bool set_auth);
    void RunTest();
    void SetUpControlNodes();

    EventManager evm_;
    boost::scoped_ptr<ServerThread> thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    boost::scoped_ptr<BgpServerTest> server2_;
    boost::scoped_ptr<BgpServerTest> vm1_;
    boost::scoped_ptr<BgpServerTest> vm2_;
    BgpSessionManager *server_session_manager_;
    BgpSessionManager *server2_session_manager_;
    BgpSessionManager *vm1_session_manager_;
    BgpSessionManager *vm2_session_manager_;
    XmppServerTest *xmpp_server_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_;
    boost::scoped_ptr<BgpXmppChannelManager> channel_manager_;
    bool set_local_as_;
    bool ebgp_;
    string vm1_client_config_;
    string vm2_client_config_;
    string server_config_;
    string server2_config_;
    string vm1_server_as_;
    string vm2_server_as_;
    string vm1_as_;
    string vm2_as_;
    bool set_auth_;
    bool server_as4_supported_;
    string auth_config_;
};

void BGPaaSTest::InitializeTemplates(bool set_local_as,
                                     const string &vm1_server_as,
                                     const string &vm2_server_as,
                                     bool set_auth) {
    vm1_server_as_ = vm1_server_as;
    vm2_server_as_ = vm2_server_as;
    if (set_auth ) {
        auth_config_ =
"           <auth-data> \n"
"               <key-type>MD5</key-type> \n"
"               <key-items> \n"
"                   <key-id> 0 </key-id> \n"
"                   <key>juniper</key> \n"
"               </key-items> \n"
"           </auth-data> \n";
    }

    vm1_client_config_ =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>2</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"       <bgpaas-parameters>\n"
"           <port-start>0</port-start>\n"
"           <port-end>0</port-end>\n"
"       </bgpaas-parameters>\n"
"   </global-system-config>\n"
"   <bgp-router name='bgpaas-server'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm1_server_as_ + "</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n" + auth_config_ +
"       <session to='vm1'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"       <session to='vm2'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm1'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
"       <port>__vm1_port__</port> \n"
"       <identifier>10.0.0.1</identifier> \n" + auth_config_ +
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm2'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
"       <port>__vm2_port__</port> \n"
"       <identifier>10.0.0.2</identifier> \n" + auth_config_ +
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"</config> \n"
;

    vm2_client_config_ =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>2</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"       <bgpaas-parameters>\n"
"           <port-start>0</port-start>\n"
"           <port-end>0</port-end>\n"
"       </bgpaas-parameters>\n"
"   </global-system-config>\n"
"   <bgp-router name='bgpaas-server'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm2_server_as_ + "</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n" + auth_config_ +
"       <session to='vm1'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"       <session to='vm2'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm1'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
"       <port>__vm1_port__</port> \n"
"       <identifier>10.0.0.1</identifier> \n" + auth_config_ +
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='vm2'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
"       <port>__vm2_port__</port> \n"
"       <identifier>10.0.0.2</identifier> \n" + auth_config_ +
"       <session to='bgpaas-server'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"           </family-attributes> \n"
"           <family-attributes> \n"
"               <address-family>inet6</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"</config> \n"
;

    string server_vm1_as;
    string server_vm2_as;
    if (set_local_as) {
        server_vm1_as = "<local-autonomous-system>" + vm1_server_as_ +
            "</local-autonomous-system>";
        server_vm2_as = "<local-autonomous-system>" + vm2_server_as_ +
            "</local-autonomous-system>";
    }
    server_config_ =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>2</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"       <bgpaas-parameters>\n"
"           <port-start>0</port-start>\n"
"           <port-end>0</port-end>\n"
"       </bgpaas-parameters>\n"
"   </global-system-config>\n"
"   <bgp-router name='local'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n" + auth_config_ +
"       <address-families> \n"
"           <family>inet</family> \n"
"           <family>inet-vpn</family> \n"
"           <family>inet6-vpn</family> \n"
"           <family>route-target</family> \n"
"       </address-families> \n"
"       <session to='remote'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"               <address-family>inet-vpn</address-family> \n"
"               <address-family>inet6-vpn</address-family> \n"
"               <address-family>route-target</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='remote'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.2</identifier> \n"
"       <port>__server2_port__</port> \n" + auth_config_ +
"       <address-families> \n"
"           <family>inet</family> \n"
"           <family>inet-vpn</family> \n"
"           <family>inet6-vpn</family> \n"
"           <family>route-target</family> \n"
"       </address-families> \n"
"       <session to='local'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"               <address-family>inet-vpn</address-family> \n"
"               <address-family>inet6-vpn</address-family> \n"
"               <address-family>route-target</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <routing-instance name='test'> \n"
"       <vrf-target>target:64512:100</vrf-target> \n"
"       <bgp-router name='bgpaas-server'> \n"
"           <router-type>bgpaas-server</router-type> \n"
"           <autonomous-system>64512</autonomous-system> \n"
"           <port>__server_port__</port> \n" + auth_config_ +
"           <session to='vm1'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n" + server_vm1_as + auth_config_ +
"           </session> \n"
"           <session to='vm2'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n" + server_vm2_as + auth_config_ +
"           </session> \n"
"       </bgp-router> \n"
"       <bgp-router name='vm1'> \n"
"           <router-type>bgpaas-client</router-type> \n"
"           <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
"           <address>127.0.0.1</address> \n"
"           <source-port>11024</source-port> \n"
"           <gateway-address>100.0.0.1</gateway-address>\n"
"           <ipv6-gateway-address>::ffff:100.0.0.2</ipv6-gateway-address>\n"
            + auth_config_ +
"           <session to='bgpaas-server'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n" + auth_config_ +
"           </session> \n"
"       </bgp-router> \n"
"       <bgp-router name='vm2'> \n"
"           <router-type>bgpaas-client</router-type> \n"
"           <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
"           <address>127.0.0.1</address> \n"
"           <source-port>11025</source-port> \n"
"           <gateway-address>200.0.0.1</gateway-address>\n"
"           <ipv6-gateway-address>beef:beef::1</ipv6-gateway-address>\n"
            + auth_config_ +
"           <session to='bgpaas-server'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n" + auth_config_ +
"           </session> \n"
"       </bgp-router> \n"
"   </routing-instance> \n"
"</config> \n"
;
    server2_config_ =
"<?xml version='1.0' encoding='utf-8'?> \n"
"<config> \n"
"   <global-system-config>\n"
"      <graceful-restart-parameters>\n"
"         <enable>true</enable>\n"
"         <restart-time>1</restart-time>\n"
"         <long-lived-restart-time>2</long-lived-restart-time>\n"
"         <end-of-rib-timeout>1</end-of-rib-timeout>\n"
"         <bgp-helper-enable>true</bgp-helper-enable>\n"
"         <xmpp-helper-enable>true</xmpp-helper-enable>\n"
"      </graceful-restart-parameters>\n"
"       <bgpaas-parameters>\n"
"           <port-start>0</port-start>\n"
"           <port-end>0</port-end>\n"
"       </bgpaas-parameters>\n"
"   </global-system-config>\n"
"   <bgp-router name='local'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.1</identifier> \n"
"       <port>__server_port__</port> \n" + auth_config_ +
"       <address-families> \n"
"           <family>inet</family> \n"
"           <family>inet-vpn</family> \n"
"           <family>inet6-vpn</family> \n"
"           <family>route-target</family> \n"
"       </address-families> \n"
"       <session to='remote'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"               <address-family>inet-vpn</address-family> \n"
"               <address-family>inet6-vpn</address-family> \n"
"               <address-family>route-target</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"   <bgp-router name='remote'> \n"
"       <address>127.0.0.1</address> \n"
"       <autonomous-system>64512</autonomous-system> \n"
"       <identifier>192.168.1.2</identifier> \n"
"       <port>__server2_port__</port> \n" + auth_config_ +
"       <address-families> \n"
"           <family>inet</family> \n"
"           <family>inet-vpn</family> \n"
"           <family>inet6-vpn</family> \n"
"           <family>route-target</family> \n"
"       </address-families> \n"
"       <session to='local'> \n"
"           <family-attributes> \n"
"               <address-family>inet</address-family> \n"
"               <address-family>inet-vpn</address-family> \n"
"               <address-family>inet6-vpn</address-family> \n"
"               <address-family>route-target</address-family> \n"
"           </family-attributes> \n" + auth_config_ +
"       </session> \n"
"   </bgp-router> \n"
"</config> \n"
;
    UpdateTemplates();
}

void BGPaaSTest::SetUpControlNodes() {
    server_->Configure(server_config_);
    server2_->Configure(server2_config_);
    task_util::WaitForIdle();

    string uuid = BgpConfigParser::session_uuid("local", "remote", 1);
    TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
        FindPeer(server_.get(), BgpConfigManager::kMasterInstance, uuid));
    TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
        FindPeer(server2_.get(), BgpConfigManager::kMasterInstance, uuid));
    BgpPeerTest *peer = FindPeer(server_.get(),
            BgpConfigManager::kMasterInstance, uuid);
    BgpPeerTest *peer2 = FindPeer(server2_.get(),
            BgpConfigManager::kMasterInstance, uuid);
    BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
    BGP_WAIT_FOR_PEER_STATE(peer2, StateMachine::ESTABLISHED);
}

void BGPaaSTest::RunTest() {
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
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.1/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.1/32", "200.0.0.1",
                            vm2_server_as_ + " " + vm1_as_);
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.2/32");

    // Verify that now resolved inet bgp route is indeed received by vm1.
    VerifyInetRouteCount(vm1_.get(), 3);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.1/32", "100.0.0.1",
                            vm1_server_as_);
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");

    // Verify that now resolved inet6 bgp route is indeed received by agent.
    TASK_UTIL_EXPECT_EQ(1, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "dead:1::beef/128", "10.10.10.1");
    VerifyInet6RouteAbsence(agent_.get(), "dead:2::beef/128");

    // Verify that now resolved inet6 bgp route is indeed received by vm2.
    VerifyInet6RouteCount(vm2_.get(), 1);
    VerifyInet6RoutePresence(vm2_.get(), "dead:1::beef/128", "beef:beef::1",
                             vm2_server_as_ + " " + vm1_as_);
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
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.1/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.2/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.3/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.1/32", "200.0.0.1",
                            vm2_server_as_ + " " + vm1_as_);
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.2/32", "200.0.0.1");

    // Verify that now resolved inet bgp route is indeed received by vm1.
    VerifyInetRouteCount(vm1_.get(), 5);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.1/32", "100.0.0.1",
                            vm1_server_as_);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.2/32", "100.0.0.1",
                            vm1_server_as_);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.3/32", "100.0.0.1",
                            vm1_server_as_);
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.1/32", "1.1.1.1");
    VerifyInetRoutePresence(vm1_.get(), "20.20.20.2/32", "1.1.1.2");

    // Verify that now resolved inet6 bgp route is indeed received by agent.
    TASK_UTIL_EXPECT_EQ(2, agent_->inet6_route_mgr_->Count());
    VerifyInet6RoutePresence(agent_.get(), "dead:1::beef/128", "10.10.10.1");
    VerifyInet6RoutePresence(agent_.get(), "dead:2::beef/128", "10.10.10.3");

    // Verify that now resolved inet6 bgp route is indeed received by vm2.
    VerifyInet6RouteCount(vm2_.get(), 2);
    VerifyInet6RoutePresence(vm2_.get(), "dead:1::beef/128", "beef:beef::1",
                             vm2_server_as_ + " " + vm1_as_);
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
    size_t orig_stale_count = peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_stale();
    size_t orig_gr_count = peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_gr_timer();
    size_t orig_llgr_count = peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_llgr_timer();
    size_t orig_sweep_count = peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_sweep();
    size_t orig_delete_count = peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_deletes();

    // Trigger graceful session closure by faking HoldTimerExpiry.
    peer_vm1->state_machine()->HoldTimerExpired();
    TASK_UTIL_EXPECT_EQ(5, agent_->route_mgr_->Count());
    VerifyInetRouteCount(vm2_.get(), 5);
    TASK_UTIL_EXPECT_EQ(3, agent_->inet6_route_mgr_->Count());
    VerifyInet6RouteCount(vm2_.get(), 3);

    // Wait until peer enters LLGR state.
    TASK_UTIL_EXPECT_EQ(orig_stale_count + 1,
                        peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_stale());
    TASK_UTIL_EXPECT_EQ(orig_gr_count + 1,
                        peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_gr_timer());
    TASK_UTIL_EXPECT_EQ(orig_llgr_count + 1,
                        peer_vm1->close_manager()->FillCloseInfo(&resp)->
                            get_peer_close_info().get_llgr_timer());

    // Restart BGP state machine.
    static_cast<BgpPeerCloseTest *>(peer_vm1->peer_close())->
        set_state_machine_restart(true);
    static_cast<BgpPeerCloseTest *>(peer_vm1->peer_close())->
        RestartStateMachine();
    VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm1, true);

    // GR session would have either got refreshed or deleted, based on the timer
    // expiry and when the session comes up. Most of time time, we expect the
    // session to be swept. But add delete check to make test more stable.
    TASK_UTIL_EXPECT_TRUE(orig_sweep_count + 1 ==
                              peer_vm1->close_manager()->FillCloseInfo(&resp)->
                                  get_peer_close_info().get_sweep() ||
                          orig_delete_count + 1 ==
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
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.2/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "1.1.1.3/32", "200.0.0.1",
                            vm2_server_as_);
    VerifyInetRoutePresence(vm2_.get(), "20.20.20.2/32", "200.0.0.1",
                            vm2_server_as_ + " " + vm1_as_);
    VerifyInetRouteAbsence(vm2_.get(), "1.1.1.1/32");
    VerifyInetRouteAbsence(vm2_.get(), "20.20.20.1/32");

    VerifyInetRouteCount(vm1_.get(), 4);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.2/32", "100.0.0.1",
                            vm1_server_as_);
    VerifyInetRoutePresence(vm1_.get(), "1.1.1.3/32", "100.0.0.1",
                            vm1_server_as_);
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

TEST_P(BGPaaSTest, Basic) {
    if (!ebgp_) {
        if (set_local_as_) {
            vm1_as_ = "700";
            vm2_as_ = "800";
        } else {
            vm1_as_ = "64512";
            vm2_as_ = "64512";
        }
    } else {
        vm1_as_ = "65001";
        vm2_as_ = "65002";
    }

    InitializeTemplates(set_local_as_, set_local_as_ ? "700" : "64512",
                        set_local_as_ ? "800" : "64512", set_auth_);
    vm1_->Configure(vm1_client_config_);
    vm2_->Configure(vm2_client_config_);
    SetUpControlNodes();

    SetUpAgent();

    BgpPeerTest *peer_vm1 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm1", 1));
    peer_vm1->set_process_session_fnc(
        boost::bind(&BGPaaSTest::ProcessSession, this));
    peer_vm1->set_check_split_horizon_fnc(
        boost::bind(&BGPaaSTest::CheckSplitHorizon, this, 0, 0));

    BgpPeerTest *peer_vm2 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm2", 1));
    peer_vm2->set_process_session_fnc(
        boost::bind(&BGPaaSTest::ProcessSession, this));
    peer_vm2->set_check_split_horizon_fnc(
        boost::bind(&BGPaaSTest::CheckSplitHorizon, this, 0, 0));

    BgpPeerTest *vm1_peer = WaitForPeerToComeUp(vm1_.get(), "vm1");
    auto vm1_flap_count = vm1_peer->flap_count();
    BgpPeerTest *vm2_peer = WaitForPeerToComeUp(vm2_.get(), "vm2");
    auto vm2_flap_count = vm2_peer->flap_count();
    EXPECT_EQ(ebgp_ ? BgpProto::EBGP : BgpProto::IBGP, vm1_peer->PeerType());

    // Verify that route-target routes are advertised from server_ to server2_
    // for target:64512:100, one for each bgpaas peer (only the best path gets
    // advertised to server2_ though). Hence check in rtarget table of server1_
    VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm1, true);
    VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm2, true);

    RunTest();

    // Set/Clear local-as and rerun the test.
    if (!ebgp_) {
        if (!set_local_as_) {
            vm1_as_ = "700";
            vm2_as_ = "800";
        } else {
            vm1_as_ = "64512";
            vm2_as_ = "64512";
        }
    } else {
        vm1_as_ = "65001";
        vm2_as_ = "65002";
    }
    InitializeTemplates(!set_local_as_, !set_local_as_ ? "700" : "64512",
                        !set_local_as_ ? "800" : "64512", set_auth_);
    vm1_->Configure(vm1_client_config_);
    vm2_->Configure(vm2_client_config_);
    server_->Configure(server_config_);
    server2_->Configure(server2_config_);
    task_util::WaitForIdle();

    // Peers should flap due to change in local-as configuration.
    EXPECT_GT(vm1_peer->flap_count(), vm1_flap_count);
    EXPECT_GT(vm2_peer->flap_count(), vm2_flap_count);
    WaitForPeerToComeUp(vm1_.get(), "vm1");
    WaitForPeerToComeUp(vm2_.get(), "vm2");
    EXPECT_EQ(ebgp_ ? BgpProto::EBGP : BgpProto::IBGP, vm1_peer->PeerType());
    VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm1, true);
    VerifyBGPaaSRTargetRoutes(server_.get(), peer_vm2, true);
    RunTest();
}

INSTANTIATE_TEST_CASE_P(BGPaaSTestWithParam, BGPaaSTest,
                        testing::Combine(::testing::Bool(), ::testing::Bool(),
                                         ::testing::Bool(), ::testing::Bool()));

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
