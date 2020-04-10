/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/asio/ip/address.hpp>

#include "base/regex.h"
#include "bfd/bfd_client.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_session.h"

typedef contrail::regex regex_t;
#include "bfd/test/bfd_test_utils.h"

#include "io/test/event_manager_test.h"
#include "base/regex.h"
#include "base/test/task_test_util.h"

typedef contrail::regex regex_t;
#include <testing/gunit.h>

using namespace BFD;
using namespace std;

using std::pair;
using std::size_t;

class Communicator : public Connection {
public:
    struct LinksKey {
        LinksKey(const boost::asio::ip::address &remote_address,
                 const boost::asio::ip::address &local_address,
                 const SessionIndex &session_index = SessionIndex()) :
                remote_address(remote_address), local_address(local_address),
                session_index(session_index) {
        }
        bool operator<(const LinksKey &other) const {
            BOOL_KEY_COMPARE(remote_address, other.remote_address);
            BOOL_KEY_COMPARE(local_address, other.local_address);
            BOOL_KEY_COMPARE(session_index, other.session_index);
            return false;
        }
        boost::asio::ip::address remote_address;
        boost::asio::ip::address local_address;
        SessionIndex             session_index;
    };

    typedef map<LinksKey, Connection *> Links;
    Communicator() { }
    virtual ~Communicator() { }
    virtual void SendPacket(
            const boost::asio::ip::udp::endpoint &local_endpoint,
            const boost::asio::ip::udp::endpoint &remote_endpoint,
            const SessionIndex &session_index,
            const boost::asio::mutable_buffer &buffer, int pktSize) {
        // Find other end-point from the links map.
        Links::const_iterator it = links_.find(
            LinksKey(remote_endpoint.address(), local_endpoint.address(),
                     session_index));
        if (it != links_.end()) {
            boost::system::error_code error;
            it->second->HandleReceive(buffer, remote_endpoint,
                local_endpoint, session_index, pktSize, error);
        }
    }
    virtual void NotifyStateChange(const SessionKey &key, const bool &up) { }
    virtual Server *GetServer() const { return server_; }
    virtual void SetServer(Server *server) { server_ = server; }
    Links *links() { return &links_; }

private:
    Server *server_;
    Links links_;
};

class ClientTest : public ::testing::Test {
 protected:
    ClientTest() :
        server_(&evm_, &cm_), client_(&cm_),
        server_test_(&evm_, &cm_test_), client_test_(&cm_test_) {
    }

    virtual void SetUp() {
        thread_.reset(new ServerThread(&evm_));
        thread_->Start();
    }

    virtual void TearDown() {
        server_.DeleteClientSessions();
        server_test_.DeleteClientSessions();
        TASK_UTIL_EXPECT_TRUE(server_.event_queue()->IsQueueEmpty());
        TASK_UTIL_EXPECT_TRUE(server_test_.event_queue()->IsQueueEmpty());
        task_util::WaitForIdle();
        server_.event_queue()->Shutdown();
        server_test_.event_queue()->Shutdown();
        evm_.Shutdown();
        thread_->Join();
    }

    void GetSessionCb(const Server *server, const SessionKey &key,
                      Session **session) const {
        *session = server->SessionByKey(key);
    }

    Session *GetSession(const Server &server, const SessionKey &key) const {
        Session *session;
        task_util::TaskFire(boost::bind(&ClientTest::GetSessionCb, this,
                                        &server, key, &session), "BFD");
        return session;
    }

    void UpCb(const Client *client, const SessionKey &key, bool *up) const {
        *up = client->Up(key);
    }

    bool Up(const Client &client, const SessionKey &key) const {
        bool up;
        task_util::TaskFire(boost::bind(&ClientTest::UpCb, this, &client, key,
                                        &up), "BFD");
        return up;
    }

    EventManager evm_;
    auto_ptr<ServerThread> thread_;
    Communicator cm_;
    Server server_;
    Client client_;

    // Test BFD end-points
    Communicator cm_test_;
    Server server_test_;
    Client client_test_;
};

TEST_F(ClientTest, BasicSingleHop1) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()),&cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));
    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
}

TEST_F(ClientTest, BasicSendTimerTest1) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()),&cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(100);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(100);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(60);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(200);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    // Read the pkt stats
    Server *server_test = client_test_.GetConnection()->GetServer();
    Session *session_test =  server_test->SessionByKey(client_test_key);
    int old_rx_count = session_test->Stats().rx_count;
    int old_tx_count = session_test->Stats().tx_count;
    boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
    // Read the Stats and validate with expected
    EXPECT_GE((session_test->Stats().rx_count - old_rx_count), 4);
    EXPECT_GE((session_test->Stats().tx_count - old_tx_count), 9);

    // Bring the remote session down, and check if the
    // local tx rate set to default value.
    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));
    old_tx_count = session_test->Stats().tx_count;
    boost::this_thread::sleep(boost::posix_time::seconds(2));
    // Detection time = 600mSec, test wait time = 2sec.
    // so the expected Tx packet ~6+2 = 8. And taking some jitter into
    // account shouldn't exceed 10 pkts.
    EXPECT_LE((session_test->Stats().tx_count - old_tx_count), 10);

    // Bringing the remote session UP and check
    // for new negotiated values being set for Tx rate.
    client_.AddSession(client_key, sc);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));
    old_rx_count = session_test->Stats().rx_count;
    old_tx_count = session_test->Stats().tx_count;
    boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
    // Read the Stats and validate with expected
    EXPECT_GE((session_test->Stats().rx_count - old_rx_count), 4);
    EXPECT_GE((session_test->Stats().tx_count - old_tx_count), 9);
    
    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));
}

// Multiple sessions with same IPs (but with different ifindex)
TEST_F(ClientTest, BasicSingleHop2) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               client_key.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         client_test_key.index), &cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc.detectionTimeMultiplier = 3;

    client_.AddSession(client_key, sc);
    client_test_.AddSession(client_test_key, sc);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    SessionKey client_key2 = SessionKey(client_address, SessionIndex(1),
                                        kSingleHop, client_test_address);
    SessionKey client_test_key2 = SessionKey(client_test_address,
                                             SessionIndex(1), kSingleHop,
                                             client_address);
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               client_key2.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         client_test_key2.index), &cm_));

    client_.AddSession(client_key2, sc);
    client_test_.AddSession(client_test_key2, sc);

    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
                        GetSession(server_, client_key));
    TASK_UTIL_EXPECT_NE(GetSession(server_, client_key),
                        GetSession(server_, client_key2));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key2));

    TASK_UTIL_EXPECT_NE(GetSession(server_, client_key),
              GetSession(server_, client_key2));
    TASK_UTIL_EXPECT_NE(GetSession(server_test_, client_test_key),
              GetSession(server_test_, client_test_key2));

    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key2));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key2));

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));

    client_.DeleteSession(client_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key2));

    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
    client_test_.DeleteSession(client_test_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key2));
}

// Multiple sessions with same same ifindex connected to different IPs
TEST_F(ClientTest, BasicSingleHop3) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.66");
    boost::asio::ip::address client_test_address1 =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address2 =
        boost::asio::ip::address::from_string("10.10.10.2");


    SessionKey client_key1 = SessionKey(client_address, SessionIndex(2),
                                        kSingleHop, client_test_address1);
    SessionKey client_key2 = SessionKey(client_address, SessionIndex(2),
                                        kSingleHop, client_test_address2);

    SessionKey client_test_key1 = SessionKey(client_test_address1,
                                             SessionIndex(2), kSingleHop,
                                             client_address);
    SessionKey client_test_key2 = SessionKey(client_test_address2,
                                             SessionIndex(2), kSingleHop,
                                             client_address);

    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address1,
                               client_key1.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address1, client_address,
                                         client_test_key1.index), &cm_));


    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc.detectionTimeMultiplier = 3;

    client_.AddSession(client_key1, sc);
    client_test_.AddSession(client_test_key1, sc);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key1));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key1));

    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address2,
                               client_key2.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address2, client_address,
                                         client_test_key2.index), &cm_));

    client_.AddSession(client_key2, sc);
    client_test_.AddSession(client_test_key2, sc);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key2));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key2));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
                        GetSession(server_, client_key1));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
                        GetSession(server_, client_key2));
    TASK_UTIL_EXPECT_NE(GetSession(server_, client_key1),
                        GetSession(server_, client_key2));

    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key1));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key2));
    TASK_UTIL_EXPECT_NE(GetSession(server_test_, client_test_key1),
                        GetSession(server_test_, client_test_key2));

    task_util::WaitForIdle();
    // Check state of all sessions
    Server *server = client_.GetConnection()->GetServer();
    Sessions *client_sessions = server->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 2U);
    stringstream ss;
    for (Sessions::iterator it = client_sessions->begin(), next ;
         it != client_sessions->end(); it = next) {
        SessionKey key = *it;
        next = ++it;
        Session *session =  server->SessionByKey(key);
        TASK_UTIL_EXPECT_GE(session->Stats().rx_count, 2);
        TASK_UTIL_EXPECT_GE(session->Stats().tx_count, 2);
        ss.str("");
        ss << session->local_state();
        TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss.str().c_str()));
    }

    task_util::WaitForIdle();
    Server *server_test = client_test_.GetConnection()->GetServer();
    Sessions *client_test_sessions = server_test->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 2U);
    stringstream ss2;
    for (Sessions::iterator it = client_test_sessions->begin(), next ;
         it != client_test_sessions->end(); it = next) {
        SessionKey key = *it;
        next = ++it;
        Session *session =  server_test->SessionByKey(key);
        TASK_UTIL_EXPECT_GE(session->Stats().rx_count, 2);
        TASK_UTIL_EXPECT_GE(session->Stats().tx_count, 2);

        ss2.str("");
        ss2 << session->local_state();
        TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss2.str().c_str()));
    }

    client_.DeleteSession(client_key1);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key1));

    client_.DeleteSession(client_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key2));

    client_test_.DeleteSession(client_test_key1);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key1));

    client_test_.DeleteSession(client_test_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key2));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 0U);
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 0U);
}


TEST_F(ClientTest, BasicMultiHop1) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("20.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kMultiHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kMultiHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()), &cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(client_.Up(client_key));
    TASK_UTIL_EXPECT_TRUE(client_test_.Up(client_test_key));

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));
    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
}

// Multihop sessions with same IPs (but with different vrf index)
TEST_F(ClientTest, BasicMultipleHop2) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("20.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kMultiHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kMultiHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               client_key.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         client_test_key.index), &cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(30);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(10000);
    sc.detectionTimeMultiplier = 3;

    client_.AddSession(client_key, sc);
    client_test_.AddSession(client_test_key, sc);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    SessionKey client_key2 = SessionKey(client_address, SessionIndex(0, 2),
                                       kMultiHop, client_test_address);
    SessionKey client_test_key2 = SessionKey(client_test_address,
                                            SessionIndex(0, 2), kMultiHop,
                                            client_address);
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               client_key2.index), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         client_test_key2.index), &cm_));

    client_.AddSession(client_key2, sc);
    client_test_.AddSession(client_test_key2, sc);

    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
                        GetSession(server_, client_key));
    TASK_UTIL_EXPECT_NE(GetSession(server_, client_key),
                        GetSession(server_, client_key2));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key));
    TASK_UTIL_EXPECT_NE(static_cast<Session *>(NULL),
              GetSession(server_test_, client_test_key2));

    TASK_UTIL_EXPECT_NE(GetSession(server_, client_key),
              GetSession(server_, client_key2));
    TASK_UTIL_EXPECT_NE(GetSession(server_test_, client_test_key),
              GetSession(server_test_, client_test_key2));
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key2));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key2));

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));
    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));

    client_.DeleteSession(client_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key2));
    client_test_.DeleteSession(client_test_key2);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key2));
}

TEST_F(ClientTest, BasicSingleHop_CfgChange_MinRxInterval) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()),&cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    task_util::WaitForIdle();
    Server *server = client_.GetConnection()->GetServer();
    Sessions *client_sessions = server->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    stringstream ss;
    Session *session =  server->SessionByKey(client_key);
    TASK_UTIL_EXPECT_GE(session->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session->Stats().tx_count, 2);
    ss << session->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss.str().c_str()));

    task_util::WaitForIdle();
    Server *server_test = client_test_.GetConnection()->GetServer();
    Sessions *client_test_sessions = server_test->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);
    stringstream ss2;
    Session *session_test =  server_test->SessionByKey(client_test_key);
    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session_test->Stats().tx_count, 2);
    ss2 << session_test->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss2.str().c_str()));

    // Config update bfd.RequiredMinRxInterval
    SessionConfig sc_updt;
    sc_updt.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc_updt.requiredMinRxInterval = boost::posix_time::milliseconds(500);
    sc_updt.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc_updt);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);

    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 4);
    TASK_UTIL_EXPECT_EQ(sc_updt.requiredMinRxInterval,
                        session_test->remote_state().minRxInterval);
    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count*2 ,
                        session->Stats().rx_count);

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));

    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
}

TEST_F(ClientTest, BasicSingleHop_CfgChange_DesiredMinTxInterval) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()),&cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    task_util::WaitForIdle();
    Server *server = client_.GetConnection()->GetServer();
    Sessions *client_sessions = server->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    stringstream ss;
    Session *session =  server->SessionByKey(client_key);
    TASK_UTIL_EXPECT_GE(session->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session->Stats().tx_count, 2);
    ss << session->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss.str().c_str()));

    task_util::WaitForIdle();
    Server *server_test = client_test_.GetConnection()->GetServer();
    Sessions *client_test_sessions = server_test->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);
    stringstream ss2;
    Session *session_test =  server_test->SessionByKey(client_test_key);
    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session_test->Stats().tx_count, 2);
    ss2 << session->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss2.str().c_str()));

    // Config update bfd.DesiredMinTxInterval
    SessionConfig sc_updt;
    sc_updt.desiredMinTxInterval = boost::posix_time::milliseconds(2000);
    sc_updt.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc_updt.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc_updt);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);

    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 4);
    TASK_UTIL_EXPECT_EQ(sc_updt.desiredMinTxInterval, session_test->remote_state().minTxInterval);
    TASK_UTIL_EXPECT_GE(session->Stats().rx_count*2, session_test->Stats().rx_count);

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));

    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
}

TEST_F(ClientTest, BasicSingleHop_CfgChange_DetectionTime) {
    boost::asio::ip::address client_address =
        boost::asio::ip::address::from_string("10.10.10.1");
    boost::asio::ip::address client_test_address =
        boost::asio::ip::address::from_string("10.10.10.2");
    SessionKey client_key = SessionKey(client_address, SessionIndex(),
                                       kSingleHop, client_test_address);
    SessionKey client_test_key = SessionKey(client_test_address,
                                            SessionIndex(), kSingleHop,
                                            client_address);
    // Connect two bfd links
    cm_.links()->insert(make_pair(
        Communicator::LinksKey(client_address, client_test_address,
                               SessionIndex()), &cm_test_));
    cm_test_.links()->insert(
        make_pair(Communicator::LinksKey(client_test_address, client_address,
                                         SessionIndex()),&cm_));
    SessionConfig sc;
    sc.desiredMinTxInterval = boost::posix_time::milliseconds(1000);
    sc.requiredMinRxInterval = boost::posix_time::milliseconds(1000);
    sc.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc);

    SessionConfig sc_t;
    sc_t.desiredMinTxInterval = boost::posix_time::milliseconds(200);
    sc_t.requiredMinRxInterval = boost::posix_time::milliseconds(200);
    sc_t.detectionTimeMultiplier = 3;
    client_test_.AddSession(client_test_key, sc_t);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));

    task_util::WaitForIdle();
    Server *server = client_.GetConnection()->GetServer();
    Sessions *client_sessions = server->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    stringstream ss;
    Session *session =  server->SessionByKey(client_key);
    TASK_UTIL_EXPECT_GE(session->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session->Stats().tx_count, 2);
    ss << session->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss.str().c_str()));

    task_util::WaitForIdle();
    Server *server_test = client_test_.GetConnection()->GetServer();
    Sessions *client_test_sessions = server_test->GetSessions();
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);
    stringstream ss2;
    Session *session_test =  server_test->SessionByKey(client_test_key);
    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 2);
    TASK_UTIL_EXPECT_GE(session_test->Stats().tx_count, 2);
    ss2 << session_test->local_state();
    TASK_UTIL_EXPECT_EQ(kUp, BFDStateFromString(ss2.str().c_str()));

    ASSERT_EQ(session->detection_time(), boost::posix_time::milliseconds(3000));
    ASSERT_EQ(session_test->detection_time(),
              boost::posix_time::milliseconds(3000));
    // Config update bfd.desiredMinTxInterval for computing new detection time
    SessionConfig sc_updt;
    sc_updt.desiredMinTxInterval = boost::posix_time::milliseconds(200);
    sc_updt.requiredMinRxInterval = boost::posix_time::milliseconds(200);
    sc_updt.detectionTimeMultiplier = 3;
    client_.AddSession(client_key, sc_updt);
    TASK_UTIL_EXPECT_TRUE(Up(client_, client_key));
    TASK_UTIL_EXPECT_TRUE(Up(client_test_, client_test_key));
    TASK_UTIL_EXPECT_GE(session_test->Stats().rx_count, 4);
    ASSERT_EQ(session->detection_time(), boost::posix_time::milliseconds(600));
    ASSERT_EQ(session_test->detection_time(),
              boost::posix_time::milliseconds(600));
    TASK_UTIL_EXPECT_EQ(client_sessions->size(), 1U);
    TASK_UTIL_EXPECT_EQ(client_test_sessions->size(), 1U);

    client_.DeleteSession(client_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_, client_key));

    client_test_.DeleteSession(client_test_key);
    TASK_UTIL_EXPECT_FALSE(Up(client_test_, client_test_key));
}
int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
