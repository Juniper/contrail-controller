/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "sandesh/xmpp_server_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_sandesh.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

using namespace boost::asio::ip;
using boost::assign::list_of;
using boost::system::error_code;
using std::cout;
using std::endl;
using std::string;
using std::vector;

static const char *bgp_config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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

static const char *bgp_config_template2 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>%s</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>64512</autonomous-system>\
        <local-autonomous-system>64512</local-autonomous-system>\
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

static const char *bgp_admin_down_config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <admin-down>true</admin-down>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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

static const char *bgp_unconfig = "\
<delete>\
    <bgp-router name=\'X\'>\
    </bgp-router>\
</delete>\
";

static const char *cluster_seed_config_template = "\
<config>\
    <global-system-config>\
        <rd-cluster-seed>%d</rd-cluster-seed>\
    </global-system-config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>%d</autonomous-system>\
        <local-autonomous-system>%d</local-autonomous-system>\
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

//
// 1 BGP and XMPP server X.
// Agents A, B, C.
//
class BgpXmppBasicTest : public ::testing::Test {
protected:
    static bool validate_done_;

    BgpXmppBasicTest() :
        thread_(&evm_),
        xs_x_(NULL),
        xltm_x_(NULL),
        auth_enabled_(false) {
    }

    virtual void SetUp(bool xmpp_listener) {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        Configure(bgp_config_template, 64512, 64512);
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());

        if (auth_enabled_) {
            XmppChannelConfig xs_cfg(false);
            xs_cfg.auth_enabled = true;
            xs_cfg.path_to_server_cert =
                "controller/src/xmpp/testdata/server-build02.pem";
            xs_cfg.path_to_server_priv_key =
                 "controller/src/xmpp/testdata/server-build02.key";
            xs_x_ = new XmppServer(&evm_,
                        test::XmppDocumentMock::kControlNodeJID, &xs_cfg);
        } else {
            xs_x_ = new XmppServer(&evm_,
                        test::XmppDocumentMock::kControlNodeJID);
        }

        xs_addr_to_connect_ = "0.0.0.0";
        if (!xmpp_listener) {
            xs_x_->Initialize(0, false);
            LOG(DEBUG, "Created XMPP server at port: " <<
                xs_x_->GetPort());
        } else {
            xs_addr_to_connect_ = "127.0.0.44";
            error_code ec;
            IpAddress xmpp_ip_address = address::from_string(xs_addr_to_connect_, ec);
            xs_x_->Initialize(0, false, xmpp_ip_address);
            LOG(DEBUG, "Created XMPP server at " << xs_addr_to_connect_ << ": " <<
                xs_x_->GetPort());
        }
        xltm_x_ =
            dynamic_cast<XmppLifetimeManagerTest *>(xs_x_->lifetime_manager());
        assert(xltm_x_ != NULL);
        cm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));
        thread_.Start();
    }

    virtual void TearDown() {
        XmppStateMachineTest::set_notify_fn(NULL);
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
        cm_x_.reset();
        task_util::WaitForIdle();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;
        bs_x_->Shutdown();
        task_util::WaitForIdle();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void CreateAgents() {

        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                agent_a_addr_, xs_addr_to_connect_, auth_enabled_));
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
                agent_b_addr_, xs_addr_to_connect_, auth_enabled_));
        agent_c_.reset(
            new test::NetworkAgentMock(&evm_, "agent-c", xs_x_->GetPort(),
                agent_c_addr_, xs_addr_to_connect_, auth_enabled_));
    }

    void DestroyAgents() {
        agent_a_->Delete();
        agent_b_->Delete();
        agent_c_->Delete();
    }

    void DisableAgents() {
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        agent_c_->SessionDown();
    }

    void Configure(const char *cfg_template, uint16_t cluster_seed,
                   uint32_t asn, uint32_t local_asn) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 cluster_seed, asn, local_asn,
                 bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    void Configure(const char *cfg_template, uint32_t asn, uint32_t local_asn) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 asn, local_asn, bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    void Configure(const char *cfg_template, string router_id) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 router_id.c_str(), bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    void Unconfigure() {
        bs_x_->Configure(bgp_unconfig);
    }

    uint32_t GetXmppConnectionFlapCount(const string &hostname) {
        TASK_UTIL_EXPECT_TRUE(xs_x_->FindConnectionEndpoint(hostname) != NULL);
        const XmppConnectionEndpoint *conn_endpoint =
            xs_x_->FindConnectionEndpoint(hostname);
        return conn_endpoint->flap_count();
    }

    size_t GetConnectionQueueSize(XmppServer *xs) {
        return xs->GetConnectionQueueSize();
    }

    void SetConnectionQueueDisable(XmppServer *xs, bool flag) {
        xs->SetConnectionQueueDisable(flag);
    }

    void SetLifetimeManagerDestroyDisable(bool disabled) {
        xltm_x_->set_destroy_not_ok(disabled);
    }

    void SetLifetimeManagerQueueDisable(bool disabled) {
        xltm_x_->SetQueueDisable(disabled);
    }

    static void ValidateShowXmppConnectionResponse(Sandesh *sandesh,
        const vector<string> &result, bool deleted) {
        ShowXmppConnectionResp *resp =
            dynamic_cast<ShowXmppConnectionResp *>(sandesh);
        TASK_UTIL_EXPECT_TRUE(resp != NULL);
        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_connections().size());

        cout << "*****************************************************" << endl;
        BOOST_FOREACH(const ShowXmppConnection &info, resp->get_connections()) {
            cout << info.log() << endl;
        }
        cout << "*****************************************************" << endl;

        BOOST_FOREACH(const ShowXmppConnection &info, resp->get_connections()) {
            bool found = false;
            BOOST_FOREACH(const string &name, result) {
                if (info.get_name() == name) {
                    found = true;
                    if (deleted) {
                        EXPECT_TRUE(info.get_deleted());
                        EXPECT_EQ("Idle", info.get_state());
                    } else {
                        EXPECT_FALSE(info.get_deleted());
                        EXPECT_EQ("Established", info.get_state());
                        EXPECT_EQ("BGP", info.get_receivers().at(0));
                    }
                    break;
                }
            }
            EXPECT_TRUE(found);
        }

        validate_done_ = true;
    }

    void VerifyShowXmppConnectionSandesh(const vector<string> &result,
        bool deleted) {
        XmppSandeshContext xmpp_sandesh_context;
        xmpp_sandesh_context.xmpp_server = xs_x_;
        Sandesh::set_module_context("XMPP", &xmpp_sandesh_context);
        Sandesh::set_response_callback(boost::bind(
            ValidateShowXmppConnectionResponse, _1, result, deleted));
        ShowXmppConnectionReq *req = new ShowXmppConnectionReq;
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }

    static void ValidateClearXmppConnectionResponse(Sandesh *sandesh,
        bool success) {
        ClearXmppConnectionResp *resp =
            dynamic_cast<ClearXmppConnectionResp *>(sandesh);
        EXPECT_TRUE(resp != NULL);
        EXPECT_EQ(success, resp->get_success());
        validate_done_ = true;
    }

    void VerifyClearXmppConnectionSandesh(bool test_mode, const string &name,
        bool success) {
        XmppSandeshContext xmpp_sandesh_context;
        xmpp_sandesh_context.xmpp_server = xs_x_;
        xmpp_sandesh_context.test_mode = test_mode;
        Sandesh::set_module_context("XMPP", &xmpp_sandesh_context);
        Sandesh::set_response_callback(boost::bind(
            ValidateClearXmppConnectionResponse, _1, success));
        ClearXmppConnectionReq *req = new ClearXmppConnectionReq;
        req->set_hostname_or_all(name);
        validate_done_ = false;
        req->HandleRequest();
        req->Release();
        TASK_UTIL_EXPECT_EQ(true, validate_done_);
    }

public:
    void XmppStateMachineNotify(XmppStateMachineTest *sm, bool create,
                                bool queue_disable) {
        if (sm->IsActiveChannel())
            return;
        tbb::mutex::scoped_lock lock(mutex_);
        if (create) {
            xmpp_state_machines_.insert(sm);
            sm->set_queue_disable(queue_disable);
        } else {
            xmpp_state_machines_.erase(sm);
        }
    }

protected:
    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> bs_x_;
    XmppServer *xs_x_;
    XmppLifetimeManagerTest *xltm_x_;
    bool auth_enabled_;
    string xs_addr_to_connect_;
    string agent_a_addr_;
    string agent_b_addr_;
    string agent_c_addr_;
    string agent_x1_addr_;
    string agent_x2_addr_;
    string agent_x3_addr_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    test::NetworkAgentMockPtr agent_c_;
    test::NetworkAgentMockPtr agent_x1_;
    test::NetworkAgentMockPtr agent_x2_;
    test::NetworkAgentMockPtr agent_x3_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    std::set<XmppStateMachineTest *> xmpp_state_machines_;
    mutable tbb::mutex mutex_;
};

bool BgpXmppBasicTest::validate_done_ = false;

// Parameterize shared vs unique IP for each agent.
// Parameterize third parameter for xmpp listener ip.
typedef std::tr1::tuple<bool, bool, bool> TestParams3;

class BgpXmppBasicParamTest : public BgpXmppBasicTest,
    public ::testing::WithParamInterface<TestParams3> {
protected:
    virtual void SetUp() {
        bool agent_address_same_ = std::tr1::get<0>(GetParam());
        auth_enabled_ = std::tr1::get<1>(GetParam());
        LOG(DEBUG, "BgpXmppBasicParamTest Agent Address: " <<
                   ((agent_address_same_)? "Same" : "Unique") <<
                   "Xmpp Authentication: " <<
                   ((auth_enabled_)? "Enabled": "Disabled"));
        if (agent_address_same_) {
            agent_a_addr_ = "127.0.0.1";
            agent_b_addr_ = "127.0.0.1";
            agent_c_addr_ = "127.0.0.1";
        } else {
            agent_a_addr_ = "127.0.0.11";
            agent_b_addr_ = "127.0.0.12";
            agent_c_addr_ = "127.0.0.13";
        }
        BgpXmppBasicTest::SetUp(std::tr1::get<2>(GetParam()));
    }

    virtual void TearDown() {
        BgpXmppBasicTest::TearDown();
    }
};

TEST_P(BgpXmppBasicParamTest, ClearAllConnections) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ClearConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    EXPECT_TRUE(xs_x_->ClearConnection("agent-b"));
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ClearNonExistentConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    EXPECT_FALSE(xs_x_->ClearConnection("agent-bx"));
    EXPECT_FALSE(xs_x_->ClearConnection("agent-"));
    EXPECT_FALSE(xs_x_->ClearConnection("all"));
    EXPECT_FALSE(xs_x_->ClearConnection("*"));
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ChangeAsNumber) {
    Configure(bgp_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->autonomous_system());
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->local_autonomous_system());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(bgp_config_template, 64513, 64513);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64513, bs_x_->autonomous_system());
    TASK_UTIL_EXPECT_EQ(64513, bs_x_->local_autonomous_system());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ChangeLocalAsNumber) {
    Configure(bgp_config_template, 64512, 64513);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->autonomous_system());
    TASK_UTIL_EXPECT_EQ(64513, bs_x_->local_autonomous_system());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(bgp_config_template, 64512, 64514);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->autonomous_system());
    TASK_UTIL_EXPECT_EQ(64514, bs_x_->local_autonomous_system());
    usleep(5000);

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    Configure(bgp_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ChangeRouterId) {
    Configure(bgp_config_template2, "192.168.0.1");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ("192.168.0.1", bs_x_->bgp_identifier_string());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(bgp_config_template2, "192.168.0.2");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ("192.168.0.2", bs_x_->bgp_identifier_string());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ChangeClusterSeed) {
    Configure(cluster_seed_config_template, 100, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(100, bs_x_->global_config()->rd_cluster_seed());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(cluster_seed_config_template, 200, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(200, bs_x_->global_config()->rd_cluster_seed());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, NoSelfBgpConfiguration1) {
    Unconfigure();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(bs_x_->HasSelfConfiguration());

    CreateAgents();

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c + 3);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c + 3);

    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, NoSelfBgpConfiguration2) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Unconfigure();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(bs_x_->HasSelfConfiguration());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c + 3);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c + 3);

    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    Configure(bgp_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, BgpAdminDown1) {
    Configure(bgp_admin_down_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());
    TASK_UTIL_EXPECT_TRUE(bs_x_->admin_down());

    CreateAgents();

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c + 3);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c + 3);

    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, BgpAdminDown2) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(bgp_admin_down_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());
    TASK_UTIL_EXPECT_TRUE(bs_x_->admin_down());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c + 3);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c + 3);

    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    Configure(bgp_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());
    TASK_UTIL_EXPECT_FALSE(bs_x_->admin_down());

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ClientConnectionBackoff) {
    XmppStateMachineTest::set_hold_time_msecs(300);
    Unconfigure();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(bs_x_->HasSelfConfiguration());

    CreateAgents();

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    size_t client_conn_attempts_a = agent_a_->get_sm_connect_attempts();
    size_t client_conn_attempts_b = agent_b_->get_sm_connect_attempts();
    size_t client_conn_attempts_c = agent_c_->get_sm_connect_attempts();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a + 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b + 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c + 3);

    TASK_UTIL_EXPECT_TRUE(
        agent_a_->get_sm_connect_attempts() > client_conn_attempts_a + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_b_->get_sm_connect_attempts() > client_conn_attempts_b + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_c_->get_sm_connect_attempts() > client_conn_attempts_c + 3);

    TASK_UTIL_EXPECT_TRUE(agent_a_->get_sm_keepalive_count() <= 1);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_sm_keepalive_count() <= 1);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_sm_keepalive_count() <= 1);

    Configure(bgp_config_template, 64512, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    TASK_UTIL_EXPECT_TRUE(agent_a_->get_sm_keepalive_count() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_sm_keepalive_count() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_sm_keepalive_count() >= 3);

    TASK_UTIL_EXPECT_EQ(0U, agent_a_->get_sm_connect_attempts());
    TASK_UTIL_EXPECT_EQ(0U, agent_b_->get_sm_connect_attempts());
    TASK_UTIL_EXPECT_EQ(0U, agent_c_->get_sm_connect_attempts());

    DestroyAgents();
    XmppStateMachineTest::set_hold_time_msecs(0);
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer1) {

    // Create agents and wait for them to come up.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    // Shutdown the server.
    xs_x_->Shutdown();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer2) {

    // Shutdown the server and create agents.
    xs_x_->Shutdown();
    CreateAgents();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer3) {

    // Create agents, wait for a little bit and shutdown the server.
    // Idea is that agents may or may not have come up, sessions and
    // connections may have been queued etc.
    CreateAgents();
    usleep(15000);
    xs_x_->Shutdown();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue1) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Wait for queue to get drained and all agents to come up.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue2) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Bounce the agents a few times and verify that the queue builds up.
    for (int idx = 0; idx < 3; ++idx) {
        size_t queue_size = GetConnectionQueueSize(xs_x_);
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        agent_c_->SessionDown();
        agent_a_->SessionUp();
        agent_b_->SessionUp();
        agent_c_->SessionUp();
        TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= queue_size + 3);
    }

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Wait for queue to get drained and all agents to come up.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue3) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Shutdown the server and verify that the queue and connections
    // don't go away.
    xs_x_->Shutdown();
    size_t queue_size = GetConnectionQueueSize(xs_x_);
    TASK_UTIL_EXPECT_TRUE(xs_x_->deleter()->HasDependents());
    usleep(50000);
    TASK_UTIL_EXPECT_EQ(queue_size, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(xs_x_->deleter()->HasDependents());

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Verify that the queue gets drained and all connections are gone.
    TASK_UTIL_EXPECT_EQ(0U, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionDestroy) {

    // Create and bring up agents.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    // Disable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(true);

    // Clear all connections.
    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();

    // Verify that the connection count goes up.  The still to be destroyed
    // connections should be in the set, but new connections should come up.
    TASK_UTIL_EXPECT_TRUE(xs_x_->ConnectionCount() >= 6);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    // Enable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(false);

    // Verify that there are no connections waiting to be destroyed.
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionShutdown) {

    // Create and bring up agents.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    // Disable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(true);

    // Clear all connections.
    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();

    // Verify that the connection count goes up.  The still to be shutdown
    // connections should be in the map, and should prevent new connections
    // from the same endpoint.
    TASK_UTIL_EXPECT_TRUE(xs_x_->ConnectionCount() >= 9);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Enable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(false);

    // Verify that there are no connections waiting to be shutdown.
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShowConnections) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    vector<string> result = {"agent-a", "agent-b", "agent-c"};
    VerifyShowXmppConnectionSandesh(result, false);

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShowDeletedConnections1) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    // Disable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(true);

    // Disable agents and verify that server still has connections.
    DisableAgents();
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    vector<string> result = {"agent-a", "agent-b", "agent-c"};
    VerifyShowXmppConnectionSandesh(result, true);

    // Enable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(false);

    // Verify that server doesn't have connections.
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    VerifyShowXmppConnectionSandesh(vector<string>(), false);

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShowDeletedConnections2) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    // Disable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(true);

    // Disable agents and verify that server still has connections.
    DisableAgents();
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);
    TASK_UTIL_EXPECT_EQ(3U, xs_x_->ConnectionCount());

    vector<string> result = {"agent-a", "agent-b", "agent-c"};
    VerifyShowXmppConnectionSandesh(result, true);

    // Enable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(false);

    // Verify that server doesn't have connections.
    TASK_UTIL_EXPECT_EQ(0U, xs_x_->ConnectionCount());
    VerifyShowXmppConnectionSandesh(vector<string>(), false);

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, IntrospectClearAllConnections) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    // Clear should fail when test mode is disabled.
    VerifyClearXmppConnectionSandesh(false, "all", false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    // Clear should succeed when test mode is enabled.
    VerifyClearXmppConnectionSandesh(true, "all", true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, IntrospectClearConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    // Clear should fail when test mode is enabled.
    VerifyClearXmppConnectionSandesh(false, "agent-b", false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    // Clear should succeed when test mode is enabled.
    VerifyClearXmppConnectionSandesh(true, "agent-b", true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, IntrospectClearNonExistentConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    uint32_t client_flap_a = agent_a_->flap_count();
    uint32_t client_flap_b = agent_b_->flap_count();
    uint32_t client_flap_c = agent_c_->flap_count();

    uint32_t server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    uint32_t server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    uint32_t server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    VerifyClearXmppConnectionSandesh(false, "*", false);
    task_util::WaitForIdle();
    VerifyClearXmppConnectionSandesh(true, "*", false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

// Disable XmppStateMachine queue processing in the server and send back to
// back connections from the client. We expect the server to correctly detect
// duplicate connections and drop them until existing connection cleanup is
// complete. This shall happen only later, when the state machine queue
// processing is enabled.
TEST_P(BgpXmppBasicParamTest, BackToBackOpen) {
    XmppStateMachineTest::set_notify_fn(
        boost::bind(&BgpXmppBasicParamTest::XmppStateMachineNotify, this, _1,
                    _2, true));
    agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            agent_a_addr_, xs_addr_to_connect_, auth_enabled_));
    TASK_UTIL_EXPECT_EQ(1U, xmpp_state_machines_.size());
    agent_a_->Delete();

    agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            agent_a_addr_, xs_addr_to_connect_, auth_enabled_));
    TASK_UTIL_EXPECT_EQ(2U, xmpp_state_machines_.size());
    agent_a_->Delete();

    BOOST_FOREACH(XmppStateMachineTest *sm, xmpp_state_machines_) {
        TASK_UTIL_EXPECT_EQ(2U, sm->get_queue_length());
    }

    agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            agent_a_addr_, xs_addr_to_connect_, auth_enabled_));
    TASK_UTIL_EXPECT_EQ(3U, xmpp_state_machines_.size());

    // Enable the state machine queue now.
    BOOST_FOREACH(XmppStateMachineTest *sm, xmpp_state_machines_) {
        sm->set_queue_disable(false);
    }

    XmppStateMachineTest::set_notify_fn(
        boost::bind(&BgpXmppBasicParamTest::XmppStateMachineNotify, this, _1,
                    _2, false));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    agent_a_->Delete();
}

// Parameterize shared vs unique IP for each agent.

class BgpXmppBasicParamTest2 : public BgpXmppBasicTest,
    public ::testing::WithParamInterface<TestParams3> {
protected:
    virtual void SetUp() {
        agent_address_same_ = std::tr1::get<0>(GetParam());
        auth_enabled_ = std::tr1::get<1>(GetParam());

        LOG(DEBUG, "BgpXmppBasicParamTest Agent Address: " <<
                   ((agent_address_same_)? "Same" : "Unique") <<
                   " Xmpp Authentication: " <<
                   ((auth_enabled_)? "Enabled": "Disabled"));
        if (agent_address_same_) {
            agent_x1_addr_ = "127.0.0.1";
            agent_x2_addr_ = "127.0.0.1";
            agent_x3_addr_ = "127.0.0.1";
        } else {
            agent_x1_addr_ = "127.0.0.21";
            agent_x2_addr_ = "127.0.0.22";
            agent_x3_addr_ = "127.0.0.23";
        }
        BgpXmppBasicTest::SetUp(std::tr1::get<2>(GetParam()));
    }

    virtual void TearDown() {
        BgpXmppBasicTest::TearDown();
    }

    void CreateAgents() {
        agent_x1_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x1_addr_, xs_addr_to_connect_, auth_enabled_));
        agent_x1_->SessionDown();
        agent_x2_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x2_addr_, xs_addr_to_connect_, auth_enabled_));
        agent_x2_->SessionDown();
        agent_x3_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x3_addr_, xs_addr_to_connect_, auth_enabled_));
        agent_x3_->SessionDown();
    }

    void DestroyAgents() {
        agent_x1_->SessionDown();
        agent_x2_->SessionDown();
        agent_x3_->SessionDown();

        agent_x1_->Delete();
        agent_x2_->Delete();
        agent_x3_->Delete();
    }

    bool agent_address_same_;
};

TEST_P(BgpXmppBasicParamTest2, DuplicateEndpointName1) {
    CreateAgents();

    // Bring up one agent with given name.
    agent_x1_->SessionUp();
    TASK_UTIL_EXPECT_TRUE(agent_x1_->IsEstablished());

    uint32_t client_x1_session_close = agent_x1_->get_session_close();
    uint32_t client_x2_session_close = agent_x2_->get_session_close();
    uint32_t client_x3_session_close = agent_x3_->get_session_close();

    // Attempt to bring up two more agents with the same name.
    agent_x2_->SessionUp();
    agent_x3_->SessionUp();

    // Make sure that latter two agents see sessions getting closed
    TASK_UTIL_EXPECT_TRUE(
        agent_x2_->get_session_close() >= client_x2_session_close + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_x3_->get_session_close() >= client_x3_session_close + 3);

    // Session which was up to begin with should remain up and not flap.
    TASK_UTIL_EXPECT_TRUE(
        agent_x1_->get_session_close() == client_x1_session_close);
    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest2, DuplicateEndpointName2) {
    CreateAgents();

    // Bring up one agent with given name.
    agent_x1_->SessionUp();
    TASK_UTIL_EXPECT_TRUE(agent_x1_->IsEstablished());

    uint32_t client_x1_session_close = agent_x1_->get_session_close();
    uint32_t client_x2_session_close = agent_x2_->get_session_close();

    // Attempt to bring up another agent with the same name.
    agent_x2_->SessionUp();

    // Make sure that second agent sees sessions getting closed.
    TASK_UTIL_EXPECT_TRUE(
        agent_x2_->get_session_close() >= client_x2_session_close + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_x1_->get_session_close() == client_x1_session_close);

    // Bring down the first agent and make sure that second comes up.
    agent_x1_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(agent_x2_->IsEstablished());

    DestroyAgents();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppBasicParamTest,
     ::testing::Combine(::testing::Bool(), ::testing::Bool(), ::testing::Bool()));

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppBasicParamTest2,
     ::testing::Combine(::testing::Bool(), ::testing::Bool(), ::testing::Bool()));

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppLifetimeManager>(
        boost::factory<XmppLifetimeManagerTest *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
}

static void TearDown() {
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
