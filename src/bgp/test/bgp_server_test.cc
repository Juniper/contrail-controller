/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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

using namespace boost::asio;
using namespace std;

class BgpSessionManagerCustom : public BgpSessionManager {
public:
    BgpSessionManagerCustom(EventManager *evm, BgpServer *server)
        : BgpSessionManager(evm, server),
          connect_session_fail_(false),
          create_session_fail_(false) {
    }

    void Connect(TcpSession *session, Endpoint remote) {
        if (connect_session_fail_)
            return;
        BgpSessionManager::Connect(session, remote);
    }

    virtual TcpSession *CreateSession() {
        if (create_session_fail_)
            return NULL;
        return BgpSessionManager::CreateSession();
    }

    void set_connect_session_fail(bool flag) { connect_session_fail_ = flag; }
    void set_create_session_fail(bool flag) { create_session_fail_ = flag; }

private:
    bool connect_session_fail_;
    bool create_session_fail_;
};

struct ConfigUTAuthKeyItem {
    ConfigUTAuthKeyItem(string id, string ikey, string time) :
        key_id(id), key(ikey), start_time(time) { }
    string key_id;
    string key;
    string start_time;
};

class BgpServerUnitTest : public ::testing::Test {
public:
    void ASNUpdateCb(BgpServerTest *server, as_t old_asn, as_t old_local_asn) {
        if (server == a_.get()) {
            a_asn_update_notification_cnt_++;
            assert(old_asn == a_old_as_);
            assert(old_local_asn == a_old_local_as_);
            a_old_as_ = server->autonomous_system();
            a_old_local_as_ = server->local_autonomous_system();
        } else {
            b_asn_update_notification_cnt_++;
            assert(old_asn == b_old_as_);
            assert(old_local_asn == b_old_local_as_);
            b_old_as_ = server->autonomous_system();
            b_old_local_as_ = server->local_autonomous_system();
        }
    }

protected:
    static bool validate_done_;
    static void ValidateClearBgpNeighborResponse(Sandesh *sandesh,
                                                 bool success);
    static void ValidateShowBgpServerResponse(Sandesh *sandesh);

    BgpServerUnitTest() : a_session_manager_(NULL), b_session_manager_(NULL) {
        a_asn_update_notification_cnt_ = 0;
        b_asn_update_notification_cnt_ = 0;
        a_old_as_ = 0;
        a_old_local_as_ = 0;
        b_old_as_ = 0;
        b_old_local_as_ = 0;
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
        TASK_UTIL_EXPECT_EQ(0, TcpServerManager::GetServerCount());

        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        BgpPeerTest::verbose_name(false);
    }

    size_t GetBgpPeerCount(BgpServer *server) {
        return server->peer_list_.size();
    }

    void PausePeerRibMembershipManager(BgpServer *server) {
        task_util::WaitForIdle();
        server->membership_mgr()->SetQueueDisable(true);
    }

    void ResumePeerRibMembershipManager(BgpServer *server) {
        server->membership_mgr()->SetQueueDisable(false);
        task_util::WaitForIdle();
    }

    const StateMachine *GetPeerStateMachine(BgpPeer *peer) {
        return peer->state_machine();
    }

    void SetSocketOpenFailure(BgpSessionManager *session_manager, bool flag) {
        session_manager->set_socket_open_failure(flag);
    }

    size_t GetSessionQueueSize(BgpSessionManager *session_manager) {
        return session_manager->GetSessionQueueSize();
    }

    void SetSessionQueueDisable(BgpSessionManager *session_manager, bool flag) {
        session_manager->SetSessionQueueDisable(flag);
    }

    void SetupPeers(int peer_count, unsigned short port_a,
                unsigned short port_b, bool verify_keepalives,
                bool admin_down1, bool admin_down2,
                bool nbr_admin_down1, bool nbr_admin_down2,
                bool nbr_passive1, bool nbr_passive2,
                uint16_t as_num1, uint16_t as_num2,
                uint16_t local_as_num1, uint16_t local_as_num2);
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
                uint16_t hold_time2 = StateMachine::kHoldTime,
                uint16_t nbr_hold_time1 = 0,
                uint16_t nbr_hold_time2 = 0);
    void SetupPeers(int peer_count, unsigned short port_a,
                unsigned short port_b, bool verify_keepalives,
                uint16_t as_num1, uint16_t as_num2,
                uint16_t local_as_num1, uint16_t local_as_num2);
    void SetupPeers(BgpServerTest *server, int peer_count,
                unsigned short port_a, unsigned short port_b,
                bool verify_keepalives,
                uint16_t as_num1 = BgpConfigManager::kDefaultAutonomousSystem,
                uint16_t as_num2 = BgpConfigManager::kDefaultAutonomousSystem,
                string peer_address1 = "127.0.0.1",
                string peer_address2 = "127.0.0.1",
                string bgp_identifier1 = "192.168.0.10",
                string bgp_identifier2 = "192.168.0.11",
                vector<string> families1 = vector<string>(),
                vector<string> families2 = vector<string>(),
                bool delete_config = false);
    // Setup peers with auth keys
    void SetupPeers(int peer_count, unsigned short port_a,
                unsigned short port_b, bool verify_keepalives,
                vector<ConfigUTAuthKeyItem> auth_keys,
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
    // Setup peer with auth keys
    void SetupPeers(BgpServerTest *server, int peer_count,
                unsigned short port_a, unsigned short port_b,
                bool verify_keepalives,
                vector<ConfigUTAuthKeyItem> auth_keys,
                uint16_t as_num1 = BgpConfigManager::kDefaultAutonomousSystem,
                uint16_t as_num2 = BgpConfigManager::kDefaultAutonomousSystem,
                string peer_address1 = "127.0.0.1",
                string peer_address2 = "127.0.0.1",
                string bgp_identifier1 = "192.168.0.10",
                string bgp_identifier2 = "192.168.0.11",
                vector<string> families1 = vector<string>(),
                vector<string> families2 = vector<string>(),
                bool delete_config = false);
    void VerifyPeers(int peer_count, size_t verify_keepalives_count = 0,
           uint16_t local_as_num1 = BgpConfigManager::kDefaultAutonomousSystem,
           uint16_t local_as_num2 = BgpConfigManager::kDefaultAutonomousSystem);
    string GetConfigStr(int peer_count,
                        unsigned short port_a, unsigned short port_b,
                        bool admin_down1, bool admin_down2,
                        bool nbr_admin_down1, bool nbr_admin_down2,
                        bool nbr_passive1, bool nbr_passive2,
                        uint16_t as_num1, uint16_t as_num2,
                        uint16_t local_as_num1, uint16_t local_as_num2,
                        string peer_address1, string peer_address2,
                        string bgp_identifier1, string bgp_identifier2,
                        vector<string> families1, vector<string> families2,
                        uint16_t hold_time1, uint16_t hold_time2,
                        uint16_t nbr_hold_time1, uint16_t nbr_hold_time2,
                        bool delete_config,
                        vector<ConfigUTAuthKeyItem> auth_keys =
                                vector<ConfigUTAuthKeyItem>());

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
    as_t a_old_local_as_;
    as_t b_old_local_as_;
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
    const SocketIOStats &rx_stats = resp->get_rx_socket_stats();
    EXPECT_NE(0, rx_stats.calls);
    EXPECT_NE(0, rx_stats.bytes);
    EXPECT_NE(0, rx_stats.average_bytes);
    const SocketIOStats &tx_stats = resp->get_tx_socket_stats();
    EXPECT_NE(0, tx_stats.calls);
    EXPECT_NE(0, tx_stats.bytes);
    EXPECT_NE(0, tx_stats.average_bytes);
    validate_done_ = true;
}

string BgpServerUnitTest::GetConfigStr(int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool admin_down1, bool admin_down2,
        bool nbr_admin_down1, bool nbr_admin_down2,
        bool nbr_passive1, bool nbr_passive2,
        as_t as_num1, as_t as_num2,
        as_t local_as_num1, as_t local_as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        uint16_t hold_time1, uint16_t hold_time2,
        uint16_t nbr_hold_time1, uint16_t nbr_hold_time2,
        bool delete_config, vector<ConfigUTAuthKeyItem> auth_keys) {
    ostringstream config;

    if (families1.empty()) families1.push_back("inet");
    if (families2.empty()) families2.push_back("inet");

    config << (!delete_config ? "<config>" : "<delete>");
    config << "<bgp-router name=\'A\'>"
        "<admin-down>" <<
            std::boolalpha << admin_down1 << std::noboolalpha <<
        "</admin-down>"
        "<autonomous-system>" << as_num1 << "</autonomous-system>"
        "<local-autonomous-system>" << local_as_num1
                                    << "</local-autonomous-system>"
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

    if (!auth_keys.empty()) {
        config << "<auth-data>";
        config << "<key-type>MD5</key-type>";
        config << "<key-items>";
    }
    for (vector<ConfigUTAuthKeyItem>::const_iterator it =
            auth_keys.begin(); it != auth_keys.end(); ++it) {
        ConfigUTAuthKeyItem item = *it;
        config << "<key-id>" << item.key_id << "</key-id>";
        config << "<key>" << item.key << "</key>";
        //config << "<start-time>" << "2001-11-12 18:31:01" << "</start-time>";
    }
    if (!auth_keys.empty()) {
        config << "</key-items>";
        config << "</auth-data>";
    }

    for (int i = 0; i < peer_count; i++) {
        config << "<session to='B'>";
        config << "<admin-down>";
        config << std::boolalpha << nbr_admin_down1 << std::noboolalpha;
        config << "</admin-down>";
        config << "<passive>";
        config << std::boolalpha << nbr_passive1 << std::noboolalpha;
        config << "</passive>";
        if (nbr_hold_time1)
            config << "<hold-time>" << nbr_hold_time1 << "</hold-time>";
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
        "<admin-down>" <<
            std::boolalpha << admin_down2 << std::noboolalpha <<
        "</admin-down>"
        "<autonomous-system>" << as_num2 << "</autonomous-system>"
        "<local-autonomous-system>" << local_as_num2
                                    << "</local-autonomous-system>"
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

    if (!auth_keys.empty()) {
        config << "<auth-data>";
        config << "<key-type>MD5</key-type>";
        config << "<key-items>";
    }
    for (vector<ConfigUTAuthKeyItem>::const_iterator it =
            auth_keys.begin(); it != auth_keys.end(); ++it) {
        ConfigUTAuthKeyItem item = *it;
        config << "<key-id>" << item.key_id << "</key-id>";
        config << "<key>" << item.key << "</key>";
        //config << "<start-time>" << "2001-11-12 18:31:01" << "</start-time>";
    }
    if (!auth_keys.empty()) {
        config << "</key-items>";
        config << "</auth-data>";
    }

    for (int i = 0; i < peer_count; i++) {
        config << "<session to='A'>";
        config << "<admin-down>";
        config << std::boolalpha << nbr_admin_down2 << std::noboolalpha;
        config << "</admin-down>";
        config << "<passive>";
        config << std::boolalpha << nbr_passive2 << std::noboolalpha;
        config << "</passive>";
        if (nbr_hold_time2)
            config << "<hold-time>" << nbr_hold_time2 << "</hold-time>";
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
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 false, false, false, false, false, false,
                                 as_num1, as_num2,
                                 as_num1, as_num2,
                                 peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 0, 0, delete_config);
    server->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(BgpServerTest *server, int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool verify_keepalives, vector<ConfigUTAuthKeyItem> auth_keys,
        as_t as_num1, as_t as_num2, string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        bool delete_config) {
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 false, false, false, false, false, false,
                                 as_num1, as_num2,
                                 as_num1, as_num2, peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 0, 0, delete_config, auth_keys);
    server->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(int peer_count,
        unsigned short port_a, unsigned short port_b, bool verify_keepalives,
        bool admin_down1, bool admin_down2,
        bool nbr_admin_down1, bool nbr_admin_down2,
        bool nbr_passive1, bool nbr_passive2,
        as_t as_num1, as_t as_num2,
        as_t local_as_num1, as_t local_as_num2) {
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 admin_down1, admin_down2,
                                 nbr_admin_down1, nbr_admin_down2,
                                 nbr_passive1, nbr_passive2,
                                 as_num1, as_num2,
                                 local_as_num1, local_as_num2,
                                 "127.0.0.1",
                                 "127.0.0.1",
                                 "192.168.0.10",
                                 "192.168.0.11",
                                 vector<string>(),
                                 vector<string>(),
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 0, 0, false);
    a_->Configure(config);
    task_util::WaitForIdle();
    b_->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool verify_keepalives, vector<ConfigUTAuthKeyItem> auth_keys,
        as_t as_num1, as_t as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        uint16_t hold_time1, uint16_t hold_time2) {
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 false, false, false, false, false, false,
                                 as_num1, as_num2,
                                 as_num1, as_num2, peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 0, 0, false, auth_keys);
    a_->Configure(config);
    task_util::WaitForIdle();
    b_->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(int peer_count,
        unsigned short port_a, unsigned short port_b,
        bool verify_keepalives, as_t as_num1, as_t as_num2,
        string peer_address1, string peer_address2,
        string bgp_identifier1, string bgp_identifier2,
        vector<string> families1, vector<string> families2,
        uint16_t hold_time1, uint16_t hold_time2,
        uint16_t nbr_hold_time1, uint16_t nbr_hold_time2) {
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 false, false, false, false, false, false,
                                 as_num1, as_num2,
                                 as_num1, as_num2,
                                 peer_address1, peer_address2,
                                 bgp_identifier1, bgp_identifier2,
                                 families1, families2,
                                 hold_time1, hold_time2,
                                 nbr_hold_time1, nbr_hold_time2,
                                 false);
    a_->Configure(config);
    task_util::WaitForIdle();
    b_->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::SetupPeers(int peer_count,
        unsigned short port_a, unsigned short port_b, bool verify_keepalives,
        as_t as_num1, as_t as_num2, as_t local_as_num1, as_t local_as_num2) {
    string config = GetConfigStr(peer_count, port_a, port_b,
                                 false, false, false, false, false, false,
                                 as_num1, as_num2,
                                 local_as_num1, local_as_num2,
                                 "127.0.0.1", "127.0.0.1",
                                 "192.168.0.10", "192.168.0.11",
                                 vector<string>(), vector<string>(),
                                 StateMachine::kHoldTime,
                                 StateMachine::kHoldTime,
                                 0, 0, false);
    a_->Configure(config);
    task_util::WaitForIdle();
    b_->Configure(config);
    task_util::WaitForIdle();
}

void BgpServerUnitTest::VerifyPeers(int peer_count,
    size_t verify_keepalives_count, as_t local_as_num1, as_t local_as_num2) {
    BgpProto::BgpPeerType peer_type =
        (local_as_num1 == local_as_num2) ? BgpProto::IBGP : BgpProto::EBGP;
    const int peers = peer_count;
    for (int j = 0; j < peers; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);

        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        ASSERT_TRUE(peer_a != NULL);
        TASK_UTIL_EXPECT_EQ(local_as_num1, peer_a->local_as());
        TASK_UTIL_EXPECT_EQ(peer_type, peer_a->PeerType());
        BGP_WAIT_FOR_PEER_STATE(peer_a, StateMachine::ESTABLISHED);

        TASK_UTIL_EXPECT_NE(static_cast<BgpPeer *>(NULL),
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid));
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        ASSERT_TRUE(peer_b != NULL);
        TASK_UTIL_EXPECT_EQ(local_as_num2, peer_b->local_as());
        TASK_UTIL_EXPECT_EQ(peer_type, peer_b->PeerType());
        BGP_WAIT_FOR_PEER_STATE(peer_b, StateMachine::ESTABLISHED);

        if (verify_keepalives_count) {

            //
            // Make sure that a few keepalives are exchanged
            //
            TASK_UTIL_EXPECT_TRUE(peer_a->get_rx_keepalive() >
                                  verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_a->get_tx_keepalive() >
                                  verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_keepalive() >
                                  verify_keepalives_count);
            TASK_UTIL_EXPECT_TRUE(peer_b->get_tx_keepalive() >
                                  verify_keepalives_count);
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
    StateMachineTest::set_keepalive_time_msecs(100);
    BgpPeerTest::verbose_name(true);
    SetupPeers(3, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 2);
    StateMachineTest::set_keepalive_time_msecs(0);
}

TEST_F(BgpServerUnitTest, LotsOfKeepAlives) {
    StateMachineTest::set_keepalive_time_msecs(100);
    BgpPeerTest::verbose_name(true);
    SetupPeers(3, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 30);
    StateMachineTest::set_keepalive_time_msecs(0);
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

TEST_F(BgpServerUnitTest, ChangeLocalAsNumber1) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 100,
               BgpConfigManager::kDefaultAutonomousSystem + 100);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 100,
                BgpConfigManager::kDefaultAutonomousSystem + 100);

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
    // Modify Local AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 101,
               BgpConfigManager::kDefaultAutonomousSystem + 101);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 101,
                BgpConfigManager::kDefaultAutonomousSystem + 101);

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

TEST_F(BgpServerUnitTest, ChangeLocalAsNumber2) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 100,
               BgpConfigManager::kDefaultAutonomousSystem + 100);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 100,
                BgpConfigManager::kDefaultAutonomousSystem + 100);

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
    // Modify Local AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 100,
               BgpConfigManager::kDefaultAutonomousSystem + 101);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 100,
                BgpConfigManager::kDefaultAutonomousSystem + 101);

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

TEST_F(BgpServerUnitTest, ChangeLocalAsNumber3) {
    int peer_count = 3;

    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 100,
               BgpConfigManager::kDefaultAutonomousSystem + 101);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 100,
                BgpConfigManager::kDefaultAutonomousSystem + 101);

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
    // Modify Local AS Number and apply
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem + 100,
               BgpConfigManager::kDefaultAutonomousSystem + 100);
    VerifyPeers(peer_count, 0,
                BgpConfigManager::kDefaultAutonomousSystem + 100,
                BgpConfigManager::kDefaultAutonomousSystem + 100);

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

//
// Note that RoutingInstanceMgr already has a callback registered - that will
// get id 0.
//
TEST_F(BgpServerUnitTest, ASNUpdateRegUnreg) {
    for (int i = 0; i < 1024; i++) {
        int j = a_->RegisterASNUpdateCallback(
          boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1, _2));
        assert(j == i + 1);
    }
    for (int i = 0; i < 1024; i++) {
        a_->UnregisterASNUpdateCallback(i + 1);
        int j = a_->RegisterASNUpdateCallback(
          boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1, _2));
        assert(j == 1);
        a_->UnregisterASNUpdateCallback(j);
    }
}

TEST_F(BgpServerUnitTest, ASNUpdateNotification) {
    int peer_count = 3;

    int a_asn_listener_id = a_->RegisterASNUpdateCallback(
        boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, a_.get(), _1, _2));
    int b_asn_listener_id = b_->RegisterASNUpdateCallback(
        boost::bind(&BgpServerUnitTest::ASNUpdateCb, this, b_.get(), _1, _2));
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
    ConcurrencyScope scope("bgp::Config");
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
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 3);
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
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown1) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set bgp-router on A and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               true, true, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers are admin down.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
    }

    //
    // Set bgp-router on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown2) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set bgp-router on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set bgp-router on A and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               true, true, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped and are admin down now.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
    }

    //
    // Set bgp-router on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown3) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set bgp-router on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set bgp-router on A to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               true, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped and are admin down now.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
    }

    //
    // Set bgp-router on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown4) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set neighbors on A and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, true, true, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers are admin down.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
    }

    //
    // Set neighbors on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown5) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set neighbors on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set neighbors on A and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, true, true, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped and are admin down now.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
    }

    //
    // Set neighbors on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, ConfigAdminDown6) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set neighbors on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set neighbor on A to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, true, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped.
    // Verify that peers on A are admin down and peers on B are not.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsAdminDown());
        TASK_UTIL_EXPECT_FALSE(peer_b->IsAdminDown());
    }

    //
    // Set neighbors on A and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
    VerifyPeers(peer_count);
}

TEST_F(BgpServerUnitTest, Passive1) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set neighbors on A and B to be passive.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, true, true,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers are passive.
    // Verify that state machine is in ACTIVE state.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsPassive());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsPassive());
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::ACTIVE);
        TASK_UTIL_EXPECT_EQ(peer_b->GetState(), StateMachine::ACTIVE);
    }

    //
    // Set neighbors on A and B to not be passive.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set neighbors on A and B to be passive.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, true, true,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped.
    // Make sure that the peers are passive.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsPassive());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsPassive());
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::ACTIVE);
        TASK_UTIL_EXPECT_EQ(peer_b->GetState(), StateMachine::ACTIVE);
    }
}

TEST_F(BgpServerUnitTest, Passive2) {
    int peer_count = 3;
    BgpPeerTest::verbose_name(true);

    //
    // Set neighbors on A to be passive and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, true, true, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers on A are passive.
    // Verify that state machine for peers on A is in ACTIVE state.
    // Make sure that the peers on B are admin down.
    // Verify that state machine for peers on B is in IDLE state.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsPassive());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::ACTIVE);
        TASK_UTIL_EXPECT_EQ(peer_b->GetState(), StateMachine::IDLE);
    }

    //
    // Set neighbors on A to be passive and B to not be admin down.
    // Verify that sessions come up.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, false, true, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);
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
    // Set neighbors on A to be passive and B to be admin down.
    //
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               false, false, false, true, true, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem);

    //
    // Make sure that the peers flapped.
    // Make sure that the peers on A are passive.
    // Verify that state machine for peers on A is in ACTIVE state.
    // Make sure that the peers on B are admin down.
    // Verify that state machine for peers on B is in IDLE state.
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->flap_count() > flap_count_a[j]);
        TASK_UTIL_EXPECT_TRUE(peer_b->flap_count() > flap_count_b[j]);
        TASK_UTIL_EXPECT_TRUE(peer_a->IsPassive());
        TASK_UTIL_EXPECT_TRUE(peer_b->IsAdminDown());
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::ACTIVE);
        TASK_UTIL_EXPECT_EQ(peer_b->GetState(), StateMachine::IDLE);
    }
}

TEST_F(BgpServerUnitTest, ResetStatsOnFlap) {
    ConcurrencyScope scope("bgp::Config");
    int peer_count = 3;

    StateMachineTest::set_keepalive_time_msecs(100);
    BgpPeerTest::verbose_name(true);
    SetupPeers(peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11");

    //
    // Make sure that a few keepalives have been exchanged
    //
    VerifyPeers(peer_count, 3);

    //
    // Set peers to be admin down
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_a->SetAdminState(true);
        peer_b->SetAdminState(true);
    }

    //
    // Verify that peer keepalive count is 0
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(0, peer_a->get_rx_keepalive());
        TASK_UTIL_EXPECT_EQ(0, peer_a->get_tx_keepalive());
        TASK_UTIL_EXPECT_EQ(0, peer_b->get_rx_keepalive());
        TASK_UTIL_EXPECT_EQ(0, peer_b->get_tx_keepalive());
    }

    StateMachineTest::set_keepalive_time_msecs(0);
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
               false, false, false, false, false, false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b,
               StateMachine::kHoldTime, StateMachine::kHoldTime,
               0, 0, true);
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
    families_a.push_back("inet6");
    families_a.push_back("inet-vpn");
    families_a.push_back("e-vpn");
    families_a.push_back("erm-vpn");
    families_b.push_back("inet");
    families_b.push_back("inet6");
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
        TASK_UTIL_EXPECT_TRUE(peer_a->IsFamilyNegotiated(Address::INET6));
        TASK_UTIL_EXPECT_TRUE(peer_b->IsFamilyNegotiated(Address::INET6));
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
        TASK_UTIL_EXPECT_TRUE((peer_a->get_open_error() >= 1) ||
                              (peer_b->get_open_error() >= 1));
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
        TASK_UTIL_EXPECT_TRUE((peer_a->get_open_error() >= 1) ||
                              (peer_b->get_open_error() >= 1));
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

TEST_F(BgpServerUnitTest, NeighborHoldTimeChange) {
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
               StateMachine::kHoldTime, StateMachine::kHoldTime,
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
                   StateMachine::kHoldTime, StateMachine::kHoldTime,
                   10 * idx, 90);
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
        TASK_UTIL_EXPECT_TRUE(peer_a->get_rx_notification() >= 3);
        TASK_UTIL_EXPECT_NE(peer_a->GetState(), StateMachine::ESTABLISHED);
    }
}

// Apply bgp neighbor configuration on only one side of the session, disable
// session queue processing on the other side and verify that the first side
// gets stuck in OpenSent.
TEST_F(BgpServerUnitTest, DisableSessionQueue1) {
    int peer_count = 3;

    SetSessionQueueDisable(b_session_manager_, true);

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::OPENSENT);
        TASK_UTIL_EXPECT_EQ(0, peer_a->get_hold_timer_expired());
    }
    TASK_UTIL_EXPECT_TRUE(GetSessionQueueSize(b_session_manager_) >= 3);

    usleep(50000);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_EQ(peer_a->GetState(), StateMachine::OPENSENT);
        TASK_UTIL_EXPECT_EQ(0, peer_a->get_hold_timer_expired());
    }

    SetSessionQueueDisable(b_session_manager_, false);
}

// Apply bgp neighbor configuration on only one side of the session, disable
// session queue processing on the other side, use very small hold timer and
// verify that the first side sees the hold timer expiring repeatedly.
TEST_F(BgpServerUnitTest, DisableSessionQueue2) {
    int peer_count = 3;

    SetSessionQueueDisable(b_session_manager_, true);
    StateMachineTest::set_hold_time_msecs(30);

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_hold_timer_expired() >= 3);
    }

    usleep(100000);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_hold_timer_expired() >= 6);
    }

    StateMachineTest::set_hold_time_msecs(0);
    SetSessionQueueDisable(b_session_manager_, false);
}

//
// Apply bgp neighbor configuration on only one side of the session, disable
// session queue processing on the other side, use very small hold timer and
// verify that the first side sees the hold timer expiring repeatedly.
//
// Then trigger deletion of the server and verify that the session queue is
// still non-empty.  Finally, enable session queue processing and make sure
// that the server gets deleted.
//
TEST_F(BgpServerUnitTest, DisableSessionQueue3) {
    int peer_count = 3;

    SetSessionQueueDisable(b_session_manager_, true);
    StateMachineTest::set_hold_time_msecs(30);

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_hold_timer_expired() >= 3);
    }

    vector<size_t> a_connect_error(peer_count);
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        a_connect_error[j] = peer_a->get_connect_error();
    }

    b_->Shutdown(false);
    usleep(50000);
    TASK_UTIL_EXPECT_TRUE(b_->session_manager() != NULL);
    size_t queue_size = GetSessionQueueSize(b_->session_manager());
    TASK_UTIL_EXPECT_NE(0, queue_size);

    usleep(50000);
    TASK_UTIL_EXPECT_EQ(queue_size, GetSessionQueueSize(b_->session_manager()));

    StateMachineTest::set_hold_time_msecs(0);
    SetSessionQueueDisable(b_session_manager_, false);
    b_->VerifyShutdown();

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(
            peer_a->get_connect_error() >= a_connect_error[j] + 3);
    }
}

// Apply bgp neighbor configuration on only one side of the session, close
// the listen socket on the other side and verify that the first side gets
// connect errors.
TEST_F(BgpServerUnitTest, ConnectError) {
    int peer_count = 3;

    int port_a = a_->session_manager()->GetPort();
    int port_b = b_->session_manager()->GetPort();
    b_session_manager_->TcpServer::Shutdown();

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, port_a, port_b, false);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_connect_error() >= 3);
    }
}

// Apply bgp neighbor configuration on only one side of the session, make
// it's attempts to connect fail and verify it gets connect timeouts.
TEST_F(BgpServerUnitTest, ConnectTimerExpired) {
    int peer_count = 1;

    int port_a = a_->session_manager()->GetPort();
    int port_b = b_->session_manager()->GetPort();
    a_session_manager_->set_connect_session_fail(true);

    BgpPeerTest::verbose_name(true);
    SetupPeers(a_.get(), peer_count, port_a, port_b, false);

    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_a = a_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(peer_a->get_connect_timer_expired() >= 3);
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
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 3);
    }

    //
    // Resume deletion of peers on A
    //
    for (int j = 0; j < peer_count; j++) {
        ResumeDelete(peer_a_list[j]->deleter());
    }
}

TEST_F(BgpServerUnitTest, CloseInProgress) {
    ConcurrencyScope scope("bgp::Config");
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
    // Trigger close of peers on A by making B send notifications
    // Peer close will be started but not completed since peer membership
    // manager has been paused
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_b->SetAdminState(true);
    }

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_b->SetAdminState(false);
        TASK_UTIL_EXPECT_TRUE(peer_b->get_rx_notification() >= 3);
    }

    //
    // Resume peer membership manager on A
    //
    ResumePeerRibMembershipManager(a_.get());
}

TEST_F(BgpServerUnitTest, CloseDeferred) {
    ConcurrencyScope scope("bgp::Config");
    int peer_count = 3;

    vector<string> families_a;
    vector<string> families_b;
    families_a.push_back("inet-vpn");
    families_b.push_back("inet-vpn");
    BgpPeerTest::verbose_name(true);

    //
    // Configure peers on B
    //
    SetupPeers(b_.get(), peer_count, a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), false,
               BgpConfigManager::kDefaultAutonomousSystem,
               BgpConfigManager::kDefaultAutonomousSystem,
               "127.0.0.1", "127.0.0.1",
               "192.168.0.10", "192.168.0.11",
               families_a, families_b);
    TASK_UTIL_EXPECT_EQ(3, GetBgpPeerCount(b_.get()));
    task_util::WaitForIdle();

    //
    // Pause peer membership manager on A
    //
    PausePeerRibMembershipManager(a_.get());

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
    TASK_UTIL_EXPECT_EQ(3, GetBgpPeerCount(a_.get()));
    task_util::WaitForIdle();


    //
    // Give the peers some time to come up.  We don't consider failure to
    // bring up peers as a hard failure since it's sometimes possible that
    // pausing the peer membership manager stops the close process for a
    // previous session bring up attempt. If this happens the test is not
    // effective but we don't want a hard failure since this is a timing
    // issue in the test itself and not an issue in the production code.
    //
    for (int idx = 0; idx < 1000; ++idx) {
        bool established = true;
        for (int j = 0; j < peer_count; j++) {
            string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
            BgpPeer *peer_a =
                a_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            BgpPeer *peer_b =
                b_->FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
            if ((peer_a->GetState() != StateMachine::ESTABLISHED) ||
                (peer_b->GetState() != StateMachine::ESTABLISHED)) {
                established = false;
                break;
            }
        }

        if (established)
            break;
        usleep(5000);
    }

    //
    // Trigger close of peers on A by making B send notifications
    // Peer close will be deferred since peer membership manager was paused
    // before the peers got established
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        peer_b->SetAdminState(true);
    }

    //
    // Note down notification counts and bring up peers on B
    //
    vector<size_t> b_rx_notification(peer_count);
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        b_rx_notification[j] = peer_b->get_rx_notification();
        peer_b->SetAdminState(false);
    }

    //
    // Wait for B's attempts to bring up the peers fail a few times
    //
    for (int j = 0; j < peer_count; j++) {
        string uuid = BgpConfigParser::session_uuid("A", "B", j + 1);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        TASK_UTIL_EXPECT_TRUE(
            peer_b->get_rx_notification() >= b_rx_notification[j] + 3);
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
        TASK_UTIL_EXPECT_TRUE(sm_a->connect_attempts() >= 3);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_b = GetPeerStateMachine(peer_b);
        TASK_UTIL_EXPECT_TRUE(sm_b->connect_attempts() >= 3);
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
        TASK_UTIL_EXPECT_TRUE(sm_a->connect_attempts() >= 3);
        BgpPeer *peer_b = b_->FindPeerByUuid(BgpConfigManager::kMasterInstance,
                                             uuid);
        const StateMachine *sm_b = GetPeerStateMachine(peer_b);
        TASK_UTIL_EXPECT_TRUE(sm_b->connect_attempts() >= 3);
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
        sandesh_context.set_test_mode(true);
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
        sandesh_context.set_test_mode(true);
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
        sandesh_context.set_test_mode(true);
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
    sandesh_context.set_test_mode(true);
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

TEST_F(BgpServerUnitTest, ClearNeighbor5) {
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
    // Attempt to clear neighbor(s) without enabling test mode
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
        clear_req->set_name(peer_a->peer_name());
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

TEST_F(BgpServerUnitTest, ShowBgpServer) {
    StateMachineTest::set_keepalive_time_msecs(100);
    BgpPeerTest::verbose_name(true);
    SetupPeers(3,a_->session_manager()->GetPort(),
               b_->session_manager()->GetPort(), true);
    VerifyPeers(3, 3);
    StateMachineTest::set_keepalive_time_msecs(0);

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
    StateMachineTest::set_keepalive_time_msecs(0);
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
