/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_server.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/state_machine.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

class BgpSessionManagerCustom : public BgpSessionManager {
public:
    BgpSessionManagerCustom(EventManager *evm, BgpServer *server)
        : BgpSessionManager(evm, server), create_session_fail_(false) {
    }

    virtual TcpSession *CreateSession() {
        if (create_session_fail_)
            return NULL;
        return BgpSessionManager::CreateSession();
    }

    void set_create_session_fail(bool flag) { create_session_fail_ = flag; }

private:
    bool create_session_fail_;
};

//
// Fire state machine timers faster and reduce possible delay in this test
//
class StateMachineTest : public StateMachine {
public:
    static int hold_time_msec_;

    explicit StateMachineTest(BgpPeer *peer) : StateMachine(peer) { }
    ~StateMachineTest() { }

    void StartConnectTimer(int seconds) {
        connect_timer_->Start(10,
            boost::bind(&StateMachine::ConnectTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartOpenTimer(int seconds) {
        open_timer_->Start(10,
            boost::bind(&StateMachine::OpenTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartIdleHoldTimer() {
        if (idle_hold_time_ <= 0)
            return;

        idle_hold_timer_->Start(10,
            boost::bind(&StateMachine::IdleHoldTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    virtual int hold_time_msecs() const {
        if (hold_time_msec_)
            return hold_time_msec_;
        return StateMachine::hold_time_msecs();
    }
};

int StateMachineTest::hold_time_msec_ = 0;

class BgpServerUnitTest : public ::testing::Test {
public:
    void ASNUpdateCb(BgpServerTest *server, as_t old_asn) {
        if (server == a_.get()) {
            a_asn_update_notification_cnt_++;
            assert(old_asn == a_old_as_);
            a_old_as_ = server->autonomous_system();
        } else {
            b_asn_update_notification_cnt_++;
            assert(old_asn == b_old_as_);
            b_old_as_ = server->autonomous_system();
        }
        return;
    }

protected:
    static bool validate_done_;
    static void ValidateClearBgpNeighborResponse(Sandesh *sandesh, bool success);
    static void ValidateShowBgpServerResponse(Sandesh *sandesh);

    BgpServerUnitTest() {
        ControlNode::SetTestMode(true);
        a_asn_update_notification_cnt_ = 0;
        b_asn_update_notification_cnt_ = 0;
        a_old_as_ = 0;
        b_old_as_ = 0;
    }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_.reset(new BgpServerTest(evm_.get(), "A"));
        b_.reset(new BgpServerTest(evm_.get(), "B"));
        thread_.reset(new ServerThread(evm_.get()));

        a_session_manager_ =
            static_cast<BgpSessionManagerCustom *>(a_->session_manager());
        a_session_manager_->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            a_session_manager_->GetPort());

        b_session_manager_ =
            static_cast<BgpSessionManagerCustom *>(b_->session_manager());
        b_session_manager_->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " <<
            b_session_manager_->GetPort());
        thread_->Start();
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        a_->Shutdown();
        b_->Shutdown();
        task_util::WaitForIdle();
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        BgpPeerTest::verbose_name(false);
    }

    void PausePeerRibMembershipManager(BgpServer *server) {
        task_util::WaitForIdle();
        server->membership_mgr()->event_queue_->set_disable(true);
    }

    void ResumePeerRibMembershipManager(BgpServer *server) {
        server->membership_mgr()->event_queue_->set_disable(false);
        task_util::WaitForIdle();
    }

    const StateMachine *GetPeerStateMachine(BgpPeer *peer) {
        return peer->state_machine();
    }

    void SetSocketOpenFailure(BgpSessionManager *session_manager, bool flag) {
        session_manager->set_socket_open_failure(flag);
    }

    void SetupPeers(int peer_count, unsigned short port_a,
                    unsigned short port_b, bool verify_keepalives,
                    uint16_t as_num1 = BgpConfigManager::kDefaultAutonomousSystem,
                    uint16_t as_num2 = BgpConfigManager::kDefaultAutonomousSystem,
                    string peer_address1 = "127.0.0.1",
                    string peer_address2 = "127.0.0.1",
                    string bgp_identifier1 = "192.168.0.10",
                    string bgp_identifier2 = "192.168.0.11",
                    vector<string> families1 = vector<string>(),
                    vector<string> families2 = vector<string>(),
                    uint16_t hold_time1 = StateMachine::kHoldTime,
                    uint16_t hold_time2 = StateMachine::kHoldTime);
    void SetupPeers(BgpServerTest *server, int peer_count,
                    unsigned short port_a, unsigned short port_b,
                    bool verify_keepalives, as_t as_num1, as_t as_num2,
                    string peer_address1, string peer_address2,
                    string bgp_identifier1, string bgp_identifier2,
                    vector<string> families1, vector<string> families2,
                    bool delete_config = false);
    void VerifyPeers(int peer_count, int verify_keepalives_count = 0,
                     uint16_t as_num1 = BgpConfigManager::kDefaultAutonomousSystem,
                     uint16_t as_num2 = BgpConfigManager::kDefaultAutonomousSystem);
    string GetConfigStr(int peer_count,
                        unsigned short port_a, unsigned short port_b,
                        uint16_t as_num1, uint16_t as_num2,
                        string peer_address1, string peer_address2,
                        string bgp_identifier1, string bgp_identifier2,
                        vector<string> families1, vector<string> families2,
                        uint16_t hold_time1, uint16_t hold_time2,
                        bool delete_config);

    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    auto_ptr<BgpServerTest> a_;
    auto_ptr<BgpServerTest> b_;
    BgpSessionManagerCustom *a_session_manager_;
    BgpSessionManagerCustom *b_session_manager_;
    tbb::atomic<long> a_asn_update_notification_cnt_;
    tbb::atomic<long> b_asn_update_notification_cnt_;
    as_t a_old_as_;
    as_t b_old_as_;
};

bool BgpServerUnitTest::validate_done_;

void BgpServerUnitTest::ValidateClearBgpNeighborResponse(
    Sandesh *sandesh, bool success) {
    ClearBgpNeighborResp *resp = dynamic_cast<ClearBgpNeighborResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    EXPECT_EQ(success, resp->get_success());
    validate_done_ = true;
}

void BgpServerUnitTest::ValidateShowBgpServerResponse(Sandesh *sandesh) {
    ShowBgpServerResp *resp = dynamic_cast<ShowBgpServerResp *>(sandesh);
    EXPECT_TRUE(resp != NULL);
    const TcpServerSocketStats &rx_stats = resp->get_rx_socket_stats();
    EXPECT_NE(0, rx_stats.calls);
    EXPECT_NE(0, rx_stats.bytes);
    EXPECT_NE(0, rx_stats.average_bytes);
    const TcpServerSocketStats &tx_stats = resp->get_tx_socket_stats();
    EXPECT_NE(0, tx_stats.calls);
    EXPECT_NE(0, tx_stats.bytes);
    EXPECT_NE(0, tx_stats.average_bytes);
    validate_done_ = true;
}

string BgpServerUnitTest::GetConfigStr(int peer_count,
        unsigned short port_a, unsigned short port_b,
        as_t as_num1, as_t as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        uint16_t hold_time1, uint16_t hold_time2,
        bool delete_config) {
    ostringstream config;

    if (families1.empty()) families1.push_back("inet");
    if (families2.empty()) families2.push_back("inet");

    config << (!delete_config ? "<config>" : "<delete>");
    config << "<bgp-router name=\'A\'>"
        "<autonomous-system>" << as_num1 << "</autonomous-system>"
        "<identifier>" << bgp_identifier1 << "</identifier>"
        "<address>" << peer_address1 << "</address>"
        "<hold-time>" << hold_time1 << "</hold-time>"
        "<port>" << port_a << "</port>";
    config << "<address-families>";
    for (vector<string>::const_iterator it = families1.begin();
         it != families1.end(); ++it) {
            config << "<family>" << *it << "</family>";
    }
    config << "</address-families>";

    for (int i = 0; i < peer_count; i++) {
        config << "<session to='B'>";
        config << "<address-families>";
        for (vector<string>::const_iterator it = families2.begin();
             it != families2.end(); ++it) {
                config << "<family>" << *it << "</family>";
        }
        config << "</address-families>";
        config << "</session>";
    }
    config << "</bgp-router>";

    config << "<bgp-router name=\'B\'>"
        "<autonomous-system>" << as_num2 << "</autonomous-system>"
        "<identifier>" << bgp_identifier2 << "</identifier>"
        "<address>" << peer_address2 << "</address>"
        "<hold-time>" << hold_time2 << "</hold-time>"
        "<port>" << port_b << "</port>";
    config << "<address-families>";
    for (vector<string>::const_iterator it = families2.begin();
         it != families2.end(); ++it) {
            config << "<family>" << *it << "</family>";
    }
    config << "</address-families>";

    for (int i = 0; i < peer_count; i++) {
        config << "<session to='A'>";
        config << "<address-families>";
        for (vector<string>::const_iterator it = families1.begin();
             it != families1.end(); ++it) {
                config << "<family>" << *it << "</family>";
        }
        config << "</address-families>";
        config << "</session>";
    }
    config << "</bgp-router>";
    config << (!delete_config ? "</config>" : "</delete>");

    return config.str();
}

void BgpServerUnitTest::SetupPeers(BgpServerTest *server, int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool verify_keepalives, as_t as_num1, as_t as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        bool delete_config) {
    string config = GetConfigStr(peer_count, port_a, port_b, as_num1, as_num2,
                                 peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 delete_config);
    server->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool verify_keepalives, as_t as_num1, as_t as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        uint16_t hold_time1, uint16_t hold_time2) {
    string config = GetConfigStr(peer_count, port_a, port_b, as_num1, as_num2,
                                 peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 hold_time1, hold_time2,
                                 false);
    a_->Configure(config);
    task_util::WaitForIdle();
    b_->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::VerifyPeers(int peer_count,
        int verify_keepalives_count, as_t as_num1, as_t as_num2) {
    BgpProto::BgpPeerType peer_type =
        (as_num1 == as_num2) ? BgpProto::IBGP : BgpProto::EBGP;
    const int peers = peer_count;
    for (int j = 0; j < peers; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);

        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(as_num1, peer_a->local_as());
        TASK_UTIL_EXPECT_EQ(peer_type, peer_a->PeerType());
        BGP_WAIT_FOR_PEER_STATE(peer_a, StateMachine::ESTABLISHED);

        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(as_num2, peer_b->local_as());
        TASK_UTIL_EXPECT_EQ(peer_type, peer_b->PeerType());
        BGP_WAIT_FOR_PEER_STATE(peer_b, StateMachine::ESTABLISHED);

        if (verify_keepalives_count) {

            //
            // Make sure that a few keepalives are exchanged
            //
            TASK_UTIL_EXPECT_TRUE(peer_a->get_rx_keepalive() > verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_a->get_tr_keepalive() > verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_keepalive() > verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_b->get_tr_keepalive() > verify_keepalives_count);
        }
    }
}

static void PauseDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->PauseDelete();
    TaskScheduler::GetInstance()->Start();
}

static void ResumeDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->ResumeDelete();
    TaskScheduler::GetInstance()->Start();
}

TEST_F(BgpServerUnitTest, Connection) {
    int hold_time_orig = StateMachineTest::hold_time_msec_;
    StateMachineTest::hold_time_msec_ = 30;
    BgpPeerTest::verbose_name(true);
    SetupPeers(3, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 2);
    StateMachineTest::hold_time_msec_ = hold_time_orig;
}

TEST_F(BgpServerUnitTest, LotsOfKeepAlives) {
    int hold_time_orig = StateMachineTest::hold_time_msec_;
    StateMachineTest::hold_time_msec_ = 30;
    BgpPeerTest::verbose_name(true);
    SetupPeers(3, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 500);
    StateMachineTest::hold_time_msec_ = hold_time_orig;
}

TEST_F(BgpServerUnitTest, ChangeAsNumber1) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 1,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ChangeAsNumber2) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);

    }
}

TEST_F(BgpServerUnitTest, ChangeAsNumber3) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);

    }
}

TEST_F(BgpServerUnitTest, ASNUpdateRegUnreg) {
    for (int i = 0; i < 1024; i++) {
        int j = a_->RegisterASNUpdateCallback(
              boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1));
        assert(j == i);
    }
    for (int i = 0; i < 1024; i++) {
        a_->UnregisterASNUpdateCallback(i);
        int j = a_->RegisterASNUpdateCallback(
              boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1));
        assert(j == 0);
        a_->UnregisterASNUpdateCallback(j);
    }
}

TEST_F(BgpServerUnitTest, ASNUpdateNotification) {
    int peer_count = 3;

    int a_asn_listener_id = a_->RegisterASNUpdateCallback(
                        boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1));
    int b_asn_listener_id = b_->RegisterASNUpdateCallback(
                        boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, b_.get(), _1));
    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem);


    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 1,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    TASK_UTIL_EXPECT_EQ(a_asn_update_notification_cnt_, 2);
    TASK_UTIL_EXPECT_EQ(b_asn_update_notification_cnt_, 2);
    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
    a_->UnregisterASNUpdateCallback(a_asn_listener_id);
    b_->UnregisterASNUpdateCallback(b_asn_listener_id);
    a_asn_update_notification_cnt_ = 0;
    b_asn_update_notification_cnt_ = 0;

    //
    // Modify AS Number and apply AGAIN
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem + 2,
               BgpConfigManager::kDefaultAutonomousSystem + 2,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 2,
                BgpConfigManager::kDefaultAutonomousSystem + 2);

    TASK_UTIL_EXPECT_EQ(a_asn_update_notification_cnt_, 0);
    TASK_UTIL_EXPECT_EQ(b_asn_update_notification_cnt_, 0);
}

TEST_F(BgpServerUnitTest, ChangePeerAddress) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify Peer Address and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.2",
               "192.168.1.10", "192.168.1.11");
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ChangeBgpIdentifier) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify BGP Identifier and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.1.10", "192.168.1.11");
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ChangePeerAddressFamilies) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Modify peer families and apply
    //
    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, AdminDown) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }

    //
    // Set peers on A to be admin down
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a->SetAdminState(true);
    }

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 5);
    }

    //
    // Set peers on A to be admin up
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a->SetAdminState(false);
    }

    //
    // Make sure that the sessions come up
    //
    VerifyPeers(peer_count, 0);
}

//
// BGP Port change number change is not supported yet
//
TEST_F(BgpServerUnitTest, DISABLED_ChangeBgpPort) {
    int peer_count = 1;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;
    vector<BgpPeer *> peers_a;
    vector<BgpPeer *> peers_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
        peers_a.push_back(peer_a);
        peers_b.push_back(peer_b);
    }

    //
    // Remove the peers from 'B' as 'A's port shall be changed
    //
    vector<string> families_a;
    vector<string> families_b;
    string config = GetConfigStr(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(),
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b,
               StateMachine::kHoldTime, StateMachine::kHoldTime,
               true);
    b_->Configure(config);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        TASK_UTIL_EXPECT_EQ(static_cast<BgpPeer *>(NULL),
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
    }

    //
    // Modify BGP A's TCP Server Port number and apply.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort() + 2,
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    //
    // Make sure that peers did flap.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_EQ(peers_a[j], peer_a);
        TASK_UTIL_EXPECT_NE(peers_b[j], peer_b);
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation1) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation2) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);


    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation3) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");
    families_b.push_back("e-vpn");
    families_b.push_back("erm-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation4) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_a.push_back("e-vpn");
    families_a.push_back("erm-vpn");
    families_b.push_back("inet-vpn");
    families_b.push_back("e-vpn");
    families_b.push_back("erm-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation5) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_a.push_back("inet-vpn");
    families_a.push_back("e-vpn");
    families_a.push_back("erm-vpn");
    families_b.push_back("inet-vpn");
    families_b.push_back("e-vpn");
    families_b.push_back("erm-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_FALSE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_FALSE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation6) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_a.push_back("inet-vpn");
    families_a.push_back("e-vpn");
    families_a.push_back("erm-vpn");
    families_b.push_back("inet");
    families_b.push_back("inet-vpn");
    families_b.push_back("e-vpn");
    families_b.push_back("erm-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INET));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INETVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::ERMVPN));
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::EVPN));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::EVPN));
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation7) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE((peer_a->get_rx_notification() > 2) ||
                              (peer_b->get_rx_notification() > 2));
        TASK_UTIL_EXPECT_NE(peer_a->GetState(), StateMachine::ESTABLISHED);
        TASK_UTIL_EXPECT_NE(peer_b->GetState(), StateMachine::ESTABLISHED);
    }
}

TEST_F(BgpServerUnitTest, AddressFamilyNegotiation8) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_b.push_back("inet-vpn");
    families_b.push_back("e-vpn");
    families_b.push_back("erm-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE((peer_a->get_rx_notification() > 2) ||
                              (peer_b->get_rx_notification() > 2));
        TASK_UTIL_EXPECT_NE(peer_a->GetState(), StateMachine::ESTABLISHED);
        TASK_UTIL_EXPECT_NE(peer_b->GetState(), StateMachine::ESTABLISHED);
    }
}

TEST_F(BgpServerUnitTest, HoldTimeChange) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b,
               10, 10);
    VerifyPeers(peer_count);

    for (int idx = 2; idx <= 9; ++idx) {

        // Change the hold time and apply the new configuration.
        SetupPeers(peer_count, a_->session_manager()->GetPort(),
                   b_->session_manager()->GetPort(), false,
                   BgpConfigManager::kDefaultAutonomousSystem,
                   BgpConfigManager::kDefaultAutonomousSystem,
                   "127.0.0.1", "127.0.0.1",
                   "192.168.0.10", "192.168.0.11",
                   families_a, families_b,
                   10 * idx, 90);
        VerifyPeers(peer_count);

        // Established sessions should keep using the old hold time value.
        for (int j = 0; j < peer_count; j++) {
            string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
            BgpPeer *peer_a =
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            const StateMachine *sm_a = GetPeerStateMachine(peer_a);
            TASK_UTIL_EXPECT_EQ(10 * (idx - 1), sm_a->hold_time());

            BgpPeer *peer_b =
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            const StateMachine *sm_b = GetPeerStateMachine(peer_b);
            TASK_UTIL_EXPECT_EQ(10 * (idx - 1), sm_b->hold_time());
        }

        // Clear all the sessions.
        for (int j = 0; j < peer_count; j++) {
            string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
            BgpPeer *peer_a =
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            peer_a->Clear(BgpProto::Notification::AdminReset);
            task_util::WaitForIdle();

            BgpPeer *peer_b =
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            peer_b->Clear(BgpProto::Notification::AdminReset);
            task_util::WaitForIdle();
        }

        VerifyPeers(peer_count);

        // Re-established sessions should use the updated hold time value.
        for (int j = 0; j < peer_count; j++) {
            string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
            BgpPeer *peer_a =
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            const StateMachine *sm_a = GetPeerStateMachine(peer_a);
            TASK_UTIL_EXPECT_EQ(10 * idx, sm_a->hold_time());

            BgpPeer *peer_b =
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            const StateMachine *sm_b = GetPeerStateMachine(peer_b);
            TASK_UTIL_EXPECT_EQ(10 * idx, sm_b->hold_time());
        }
    }
}

// Apply bgp neighbor configuration on only one side of the session and
// verify that the session does not come up.
TEST_F(BgpServerUnitTest, MissingPeerConfig) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_rx_notification() > 50);
        TASK_UTIL_EXPECT_NE(peer_a->GetState(), StateMachine::ESTABLISHED);
    }
}

TEST_F(BgpServerUnitTest, DeleteInProgress) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    //
    // Build list of peers on A
    //
    vector<BgpPeer *> peer_a_list;
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a_list.push_back(peer_a);
    }

    //
    // Pause deletion of peers on A
    //
    for (int j = 0; j < peer_count; j++) {
        PauseDelete(peer_a_list[j]->deleter());
    }

    //
    // Trigger deletion of peer configuration on A
    // Peers will be marked deleted but won't get destroyed since deletion
    // has been paused
    //
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b, true);

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 5);
    }

    //
    // Resume deletion of peers on A
    //
    for (int j = 0; j < peer_count; j++) {
        ResumeDelete(peer_a_list[j]->deleter());
    }
}

TEST_F(BgpServerUnitTest, CloseInProgress) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    //
    // Pause peer membership manager on A
    //
    PausePeerRibMembershipManager(a_.get());

    //
    // Trigger close of peers on A
    // Peer close will be started but not completed since peer membership
    // manager has been paused
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a->Clear(BgpProto::Notification::AdminReset);
    }

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 5);
    }

    //
    // Resume peer membership manager on A
    //
    ResumePeerRibMembershipManager(a_.get());
}

TEST_F(BgpServerUnitTest, CloseDeferred) {
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");
    BgpPeerTest::verbose_name(true);

    //
    // Configure peers on A
    //
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    task_util::WaitForIdle();

    //
    // Pause peer membership manager on A
    //
    PausePeerRibMembershipManager(a_.get());

    //
    // Configure peers on B and make sure they are up
    //
    SetupPeers(b_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    VerifyPeers(peer_count);

    //
    // Trigger close of peers on A
    // Peer close will be deferred since peer membership manager was paused
    // before the peers got established
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a->Clear(BgpProto::Notification::AdminReset);
    }

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 5);
    }

    //
    // Resume peer membership manager on A
    //
    ResumePeerRibMembershipManager(a_.get());

    //
    // Make sure that the sessions come up
    //
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, CreateSessionFail) {
    int peer_count = 3;

    //
    // Force session creation on both servers to fail.
    //
    a_session_manager_->set_create_session_fail(true);
    b_session_manager_->set_create_session_fail(true);

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");

    //
    // Wait for A's and B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_a = GetPeerStateMachine(peer_a);
        TASK_UTIL_EXPECT_TRUE(sm_a->connect_attempts() >= 5);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_b = GetPeerStateMachine(peer_b);
        TASK_UTIL_EXPECT_TRUE(sm_b->connect_attempts() >= 5);
    }

    //
    // Allow session creation on both servers to succeed.
    //
    a_session_manager_->set_create_session_fail(false);
    b_session_manager_->set_create_session_fail(false);

    //
    // Make sure that the sessions come up
    //
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, SocketOpenFail) {
    int peer_count = 3;

    //
    // Force socket open on both servers to fail.
    //
    SetSocketOpenFailure(a_session_manager_, true);
    SetSocketOpenFailure(b_session_manager_, true);

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");

    //
    // Wait for A's and B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_a = GetPeerStateMachine(peer_a);
        TASK_UTIL_EXPECT_TRUE(sm_a->connect_attempts() >= 5);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_b = GetPeerStateMachine(peer_b);
        TASK_UTIL_EXPECT_TRUE(sm_b->connect_attempts() >= 5);
    }

    //
    // Allow socket open on both servers to succeed.
    //
    SetSocketOpenFailure(a_session_manager_, false);
    SetSocketOpenFailure(b_session_manager_, false);

    //
    // Make sure that the sessions come up
    //
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ClearNeighbor1) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }


    //
    // Clear neighbors via sandesh
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = a_.get();
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(ValidateClearBgpNeighborResponse, _1, true));
        ClearBgpNeighborReq *clear_req = new ClearBgpNeighborReq;
        validate_done_ = false;
        clear_req->set_name(peer_a->peer_name());
        clear_req->HandleRequest();
        clear_req->Release();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(validate_done_);
    }

    //
    // Make sure that the peers come up again
    //
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ClearNeighbor2) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 1,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }


    //
    // Clear neighbors via sandesh
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = a_.get();
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(ValidateClearBgpNeighborResponse, _1, true));
        ClearBgpNeighborReq *clear_req = new ClearBgpNeighborReq;
        validate_done_ = false;
        clear_req->set_name(peer_a->peer_name());
        clear_req->HandleRequest();
        clear_req->Release();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(validate_done_);
    }

    //
    // Make sure that the peers come up again
    //
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem,
                BgpConfigManager::kDefaultAutonomousSystem + 1);

    //
    // Make sure that the peers did flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ClearNeighbor3) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }


    //
    // Attempt to clear neighbors (without matching name) via sandesh
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpSandeshContext sandesh_context;
        sandesh_context.bgp_server = a_.get();
        Sandesh::set_client_context(&sandesh_context);
        Sandesh::set_response_callback(
            boost::bind(ValidateClearBgpNeighborResponse, _1, false));
        ClearBgpNeighborReq *clear_req = new ClearBgpNeighborReq;
        validate_done_ = false;
        clear_req->set_name(peer_a->peer_name() + "extra");
        clear_req->HandleRequest();
        clear_req->Release();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(validate_done_);
    }

    //
    // Make sure that the peers are still up
    //
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did not flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(peer_a->flap_count(), flap_count_a[j]);
        TASK_UTIL_EXPECT_EQ(peer_b->flap_count(), flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ClearNeighbor4) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");
    VerifyPeers(peer_count);

    vector<uint32_t> flap_count_a;
    vector<uint32_t> flap_count_b;

    //
    // Note down the current flap count
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        flap_count_a.push_back(peer_a->flap_count());
        flap_count_b.push_back(peer_b->flap_count());
    }


    //
    // Attempt to clear neighbor(s) with an empty name.
    //
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);
    Sandesh::set_response_callback(
        boost::bind(ValidateClearBgpNeighborResponse, _1, false));
    ClearBgpNeighborReq *clear_req = new ClearBgpNeighborReq;
    validate_done_ = false;
    clear_req->set_name("");
    clear_req->HandleRequest();
    clear_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    //
    // Make sure that the peers are still up
    //
    VerifyPeers(peer_count);

    //
    // Make sure that the peers did not flap
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(peer_a->flap_count(), flap_count_a[j]);
        TASK_UTIL_EXPECT_EQ(peer_b->flap_count(), flap_count_b[j]);
    }
}

TEST_F(BgpServerUnitTest, ShowBgpServer) {
    int hold_time_orig = StateMachineTest::hold_time_msec_;
    StateMachineTest::hold_time_msec_ = 30;
    BgpPeerTest::verbose_name(true);
    SetupPeers(3,a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 50);
    StateMachineTest::hold_time_msec_ = hold_time_orig;

    BgpSandeshContext sandesh_context;
    ShowBgpServerReq *show_req;

    sandesh_context.bgp_server = a_.get();
    Sandesh::set_client_context(&sandesh_context);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowBgpServerResponse, _1));
    show_req = new ShowBgpServerReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);

    sandesh_context.bgp_server = b_.get();
    Sandesh::set_client_context(&sandesh_context);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowBgpServerResponse, _1));
    show_req = new ShowBgpServerReq;
    validate_done_ = false;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(validate_done_);
}

TEST_F(BgpServerUnitTest, BasicAdvertiseWithdraw) {
    SetupPeers(1, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false);
    VerifyPeers(1);

    // Find the inet.0 table in A and B.
    DB *db_a = a_.get()->database();
    InetTable *table_a = static_cast<InetTable *>(db_a->FindTable("inet.0"));
    assert(table_a);
    DB *db_b = b_.get()->database();
    InetTable *table_b = static_cast<InetTable *>(db_b->FindTable("inet.0"));
    assert(table_b);

    // Create a BgpAttrSpec to mimic a eBGP learnt route with Origin, AS Path
    // NextHop and Local Pref.
    BgpAttrSpec attr_spec;

    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    attr_spec.push_back(&origin);

    AsPathSpec path_spec;
    AsPathSpec::PathSegment *path_seg = new AsPathSpec::PathSegment;
    path_seg->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    path_seg->path_segment.push_back(65534);
    path_spec.path_segments.push_back(path_seg);
    attr_spec.push_back(&path_spec);

    BgpAttrNextHop nexthop(0x7f00007f);
    attr_spec.push_back(&nexthop);

    BgpAttrLocalPref local_pref(100);
    attr_spec.push_back(&local_pref);

    BgpAttrPtr attr_ptr = a_.get()->attr_db()->Locate(attr_spec);

    // Create 3 IPv4 prefixes and the corresponding keys.
    const Ip4Prefix prefix1(Ip4Prefix::FromString("192.168.1.0/24"));
    const Ip4Prefix prefix2(Ip4Prefix::FromString("192.168.2.0/24"));
    const Ip4Prefix prefix3(Ip4Prefix::FromString("192.168.3.0/24"));

    const InetTable::RequestKey key1(prefix1, NULL);
    const InetTable::RequestKey key2(prefix2, NULL);
    const InetTable::RequestKey key3(prefix3, NULL);

    DBRequest req;

    // Add prefix1 to A and make sure it shows up at B.
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetTable::RequestKey(prefix1, NULL));
    req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
    table_a->Enqueue(&req);
    task_util::WaitForIdle();

    BGP_VERIFY_ROUTE_PRESENCE(table_a, &key1);
    BGP_VERIFY_ROUTE_PRESENCE(table_b, &key1);

    // Add prefix2 to A and make sure it shows up at B.
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetTable::RequestKey(prefix2, NULL));
    req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
    table_a->Enqueue(&req);
    task_util::WaitForIdle();

    BGP_VERIFY_ROUTE_COUNT(table_a, 2);
    BGP_VERIFY_ROUTE_COUNT(table_b, 2);
    BGP_VERIFY_ROUTE_PRESENCE(table_a, &key2);
    BGP_VERIFY_ROUTE_PRESENCE(table_b, &key2);

    // Delete prefix1 from A and make sure it's gone from B.
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new InetTable::RequestKey(prefix1, NULL));
    table_a->Enqueue(&req);
    task_util::WaitForIdle();

    BGP_VERIFY_ROUTE_COUNT(table_a, 1);
    BGP_VERIFY_ROUTE_COUNT(table_b, 1);
    BGP_VERIFY_ROUTE_ABSENCE(table_a, &key1);
    BGP_VERIFY_ROUTE_ABSENCE(table_b, &key1);

    // Add prefix1 and prefix3 to A and make sure they show up at B.
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetTable::RequestKey(prefix1, NULL));
    req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
    table_a->Enqueue(&req);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new InetTable::RequestKey(prefix3, NULL));
    req.data.reset(new InetTable::RequestData(attr_ptr, 0, 0));
    table_a->Enqueue(&req);
    task_util::WaitForIdle();

    BGP_VERIFY_ROUTE_COUNT(table_a, 3);
    BGP_VERIFY_ROUTE_COUNT(table_b, 3);
    BGP_VERIFY_ROUTE_PRESENCE(table_a, &key1);
    BGP_VERIFY_ROUTE_PRESENCE(table_a, &key2);
    BGP_VERIFY_ROUTE_PRESENCE(table_a, &key3);
    BGP_VERIFY_ROUTE_PRESENCE(table_b, &key1);
    BGP_VERIFY_ROUTE_PRESENCE(table_b, &key2);
    BGP_VERIFY_ROUTE_PRESENCE(table_b, &key3);

    // Delete all the prefixes from A and make sure they are gone from B.
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new InetTable::RequestKey(prefix3, NULL));
    table_a->Enqueue(&req);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new InetTable::RequestKey(prefix1, NULL));
    table_a->Enqueue(&req);
    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(new InetTable::RequestKey(prefix2, NULL));
    table_a->Enqueue(&req);
    task_util::WaitForIdle();

    BGP_VERIFY_ROUTE_COUNT(table_a, 0);
    BGP_VERIFY_ROUTE_COUNT(table_b, 0);
    BGP_VERIFY_ROUTE_ABSENCE(table_a, &key1);
    BGP_VERIFY_ROUTE_ABSENCE(table_a, &key2);
    BGP_VERIFY_ROUTE_ABSENCE(table_a, &key3);
    BGP_VERIFY_ROUTE_ABSENCE(table_b, &key1);
    BGP_VERIFY_ROUTE_ABSENCE(table_b, &key2);
    BGP_VERIFY_ROUTE_ABSENCE(table_b, &key3);
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<BgpSessionManager>(
        boost::factory<BgpSessionManagerCustom *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
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
