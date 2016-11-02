/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string/replace.hpp>
#include <boost/lexical_cast.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
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
        evm_.reset(new EventManager());
        server_.reset(new BgpServerTest(evm_.get(), "local"));
        vm1_.reset(new BgpServerTest(evm_.get(), "vm1"));
        vm2_.reset(new BgpServerTest(evm_.get(), "vm2"));
        thread_.reset(new ServerThread(evm_.get()));

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
        thread_->Start();
        BgpPeerTest::verbose_name(true);
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        server_->Shutdown();
        vm1_->Shutdown();
        task_util::WaitForIdle();
        vm2_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, TcpServerManager::GetServerCount());

        evm_->Shutdown();
        if (thread_.get() != NULL)
            thread_->Join();
        BgpPeerTest::verbose_name(false);
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

    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    auto_ptr<BgpServerTest> server_;
    auto_ptr<BgpServerTest> vm1_;
    auto_ptr<BgpServerTest> vm2_;
    BgpSessionManager *server_session_manager_;
    BgpSessionManager *vm1_session_manager_;
    BgpSessionManager *vm2_session_manager_;
};

TEST_F(BGPaaSTest, Basic) {
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
    cout << serverConfigStr << endl << endl << endl << endl;
    cout << clientsConfigStr << endl;
    vm1_->Configure(clientsConfigStr);
    vm2_->Configure(clientsConfigStr);
    task_util::WaitForIdle();

    server_->Configure(serverConfigStr);
    WaitForPeerToComeUp(vm1_.get(), "vm1");
    WaitForPeerToComeUp(vm2_.get(), "vm2");
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
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
