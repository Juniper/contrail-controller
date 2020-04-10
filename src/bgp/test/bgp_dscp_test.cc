/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
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
#include "bgp/bgp_session.h"

using namespace std;
using std::string;

static const char *bgp_dscp_config_template = "\
<config>\
    <global-qos-config>\
    <control-traffic-dscp>\
          <control>%d</control>\
    </control-traffic-dscp>\
    </global-qos-config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>1</autonomous-system>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

class BgpDscpTest : public ::testing::Test {
public:
    // Allow IBGP bgpaas sessions for this test.
    bool ProcessSession() const { return true; }

    // Disable IBGP Split Horizon check for this test.
    bool CheckSplitHorizon() const { return false; }

protected:
    BgpDscpTest() :
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

    void UpdateTemplates() {
        server_->set_peer_lookup_disable(true);
        vm1_->set_source_port(11024);
        vm2_->set_source_port(11025);
        boost::replace_all(server_config_, "__server_port__",
            boost::lexical_cast<string>(server_session_manager_->GetPort()));

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

    void ConfigureDscp(uint8_t dscp);

    void InitializeTemplates(const string &vm1_server_as,
                             const string &vm2_server_as);
    void RunTest(uint8_t test_dscp_value);

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
    string vm1_client_config_;
    string vm2_client_config_;
    string server_config_;
    string vm1_server_as_;
    string vm2_server_as_;
    string vm1_as_;
    string vm2_as_;
};


void BgpDscpTest::ConfigureDscp(uint8_t dscp) {
        char config[8192];
        snprintf(config, sizeof(config), bgp_dscp_config_template, dscp,
                 server_->session_manager()->GetPort());
        server_->Configure(config);
}

void BgpDscpTest::InitializeTemplates(const string &vm1_server_as,
                                      const string &vm2_server_as) {
    vm1_server_as_ = vm1_server_as;
    vm2_server_as_ = vm2_server_as;

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
"       <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
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
"       <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
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
"       <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
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
"       <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
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

    string server_vm1_as;
    string server_vm2_as;
    server_vm1_as = "<local-autonomous-system>" + vm1_server_as_ +
        "</local-autonomous-system>";
    server_vm2_as = "<local-autonomous-system>" + vm2_server_as_ +
        "</local-autonomous-system>";
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
"   <global-qos-config>\n"
"   <control-traffic-dscp>\n"
"         <control>56</control>\n"
"   </control-traffic-dscp>\n"
"   </global-qos-config>\n"
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
"               </family-attributes> \n" + server_vm1_as +
"           </session> \n"
"           <session to='vm2'> \n"
"               <family-attributes> \n"
"                   <address-family>inet</address-family> \n"
"               </family-attributes> \n"
"               <family-attributes> \n"
"                   <address-family>inet6</address-family> \n"
"               </family-attributes> \n" + server_vm2_as +
"           </session> \n"
"       </bgp-router> \n"
"       <bgp-router name='vm1'> \n"
"           <router-type>bgpaas-client</router-type> \n"
"           <autonomous-system>" + vm1_as_ + "</autonomous-system> \n"
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
"           <autonomous-system>" + vm2_as_ + "</autonomous-system> \n"
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
    UpdateTemplates();
}

void BgpDscpTest::RunTest(uint8_t test_dscp_value) {
    BgpPeerTest *peer_vm1 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm1", 1));
    BgpSession *peer_vm1_session = peer_vm1->session();
    uint8_t   dscp_value;
    if (peer_vm1_session) {
        dscp_value   = peer_vm1_session->GetDscpValue();
        TASK_UTIL_EXPECT_EQ((test_dscp_value << 2), dscp_value);
     }
}

TEST_F(BgpDscpTest, Basic) {
    vm1_as_ = "700";
    vm2_as_ = "800";
    InitializeTemplates("700", "800");
    vm1_->Configure(vm1_client_config_);
    vm2_->Configure(vm2_client_config_);
    server_->Configure(server_config_);
    task_util::WaitForIdle();
    SetUpAgent();
    // Testing DSCP initialization
    TASK_UTIL_EXPECT_EQ(56, server_->global_qos()->control_dscp());
    BgpPeerTest *peer_vm1 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm1", 1));
    peer_vm1->set_process_session_fnc(
        boost::bind(&BgpDscpTest::ProcessSession, this));
    peer_vm1->set_check_split_horizon_fnc(
        boost::bind(&BgpDscpTest::CheckSplitHorizon, this));

    BgpPeerTest *peer_vm2 = FindPeer(server_.get(), "test",
            BgpConfigParser::session_uuid("bgpaas-server", "vm2", 1));
    peer_vm2->set_process_session_fnc(
        boost::bind(&BgpDscpTest::ProcessSession, this));
    peer_vm2->set_check_split_horizon_fnc(
        boost::bind(&BgpDscpTest::CheckSplitHorizon, this));

    BgpPeerTest *vm1_peer = WaitForPeerToComeUp(vm1_.get(), "vm1");
    EXPECT_EQ(BgpProto::IBGP, vm1_peer->PeerType());
    RunTest(56);

    // Testing the DSCP update Callback fn.
    ConfigureDscp(60);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(60, server_->global_qos()->control_dscp());
    RunTest(60);

    ConfigureDscp(0);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, server_->global_qos()->control_dscp());
    RunTest(0);
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
