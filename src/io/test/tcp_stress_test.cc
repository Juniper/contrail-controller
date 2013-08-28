/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <boost/asio/placeholders.hpp>
#include <boost/assign/std/vector.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <tbb/mutex.h>

#include <algorithm> 

#include "testing/gunit.h"

#include "base/logging.h"
#include "base/parse_object.h"
#include "base/task.h"
#include "base/timer.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "io/test/event_manager_test.h"
#include "io/io_log.h"

using namespace std;
using namespace tbb;

//
// Number of Server
// Number of Connection

using namespace boost::program_options;
using ::testing::TestWithParam;
using ::testing::ValuesIn;
using ::testing::Combine;

typedef std::tr1::tuple<int, int, int, bool> TestParams;
static char **gargv;
static int    gargc;

#define MAX_BUF_SIZE 8094
static char msg[MAX_BUF_SIZE];

//
// Count to track total number of tcp sessions created
//
static int session_count_;

namespace {

class EchoServer;
class EchoSession : public TcpSession {
public:
    EchoSession(EchoServer *server, Socket *socket);
    ~EchoSession() {

        //
        // Track the session count
        //
        session_count_--;
    }

    void increment_sent(uint32_t sent) {
        total_tx_ += sent;
    }

    void reset_total_rx()  {
        total_rx_ = 0;
    }

    void reset_total_tx()  {
        total_tx_ += 0;
    }

    uint32_t total_rx() {
        return total_rx_;
    }

    uint32_t total_tx() {
        return total_tx_;
    }

protected:
    virtual void OnRead(Buffer buffer) {
        const size_t len = BufferSize(buffer);
        TCP_SESSION_LOG_UT_DEBUG(this, TCP_DIR_IN, "Read " << len);
        total_rx_ += len;
    }

private:
    uint32_t total_tx_;
    uint32_t total_rx_;
    
};

class EchoServer : public TcpServer {
public:
    explicit EchoServer(EventManager *evm) 
        : TcpServer(evm), num_sessions_(0), num_req_(0), num_reject_accept_(0) {
    }

    virtual TcpSession *AllocSession(Socket *socket) {
        TcpSession *session = new EchoSession(this, socket);
        return session;
    }

    virtual bool AcceptSession(TcpSession *session) {
        num_req_++;
        if ((num_req_ % ((rand() % 8) + 1)) == 0) {
            boost::asio::ip::tcp::endpoint peer = session->remote_endpoint();
            string str(peer.address().to_string());
            char cstr[12];
            snprintf(cstr, sizeof(cstr), ":%d:", peer.port());
            str.append(cstr);
            peer = session->local_endpoint();
            str.append(peer.address().to_string());
            snprintf(cstr, sizeof(cstr), ":%d", peer.port());
            str.append(cstr);
            TCP_SESSION_LOG_DEBUG(session, TCP_DIR_IN,
                                  "Connect rejectd : " << str);
            num_reject_accept_++;
            return false;
        }
        num_sessions_++;
        return true;
    }

    TcpSession *CreateClientSession(bool blocking) {
        TcpSession *session = TcpServer::CreateSession();
        Socket *socket = session->socket();

        boost::system::error_code err;
        socket->open(boost::asio::ip::tcp::v4(), err);
        if (err) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                                  "open failed: " << err.message());
        }   
        if (!blocking) {
            err = session->SetSocketOptions();
        }
        return session;
    }

    TcpSession *ConnectToServer(TcpServer *to_server, TcpSession *session) {
        connect_matrix_[to_server] = session;
        boost::asio::ip::tcp::endpoint target;
        boost::system::error_code ec;
        target.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
        target.port(to_server->GetPort());
        Connect(connect_matrix_[to_server], target);
        TCP_SESSION_LOG_UT_DEBUG(connect_matrix_[to_server], TCP_DIR_OUT,
                         "Try to connect to Server @ " << to_server->GetPort());
        return connect_matrix_[to_server];
    }

    void DisconnectFromServer(TcpServer *to_server) {
        TcpSession *session = connect_matrix_[to_server];
        session->set_observer(NULL);
        session->Close();
        session->server()->DeleteSession(session);
        connect_matrix_.erase(to_server);
    }

    uint32_t num_reject_accept() {
        return num_reject_accept_;
    }

    bool IsConnectionPresent(TcpServer *server) {
        std::map<TcpServer*, TcpSession *>::iterator iter;
        iter = connect_matrix_.find(server);
        return iter != connect_matrix_.end();
    }

private:
    uint32_t num_sessions_;
    uint32_t num_req_;
    uint32_t num_reject_accept_;
    std::map<TcpServer*, TcpSession *> connect_matrix_;
};

EchoSession::EchoSession(EchoServer *server, Socket *socket) 
        : TcpSession(server, socket), total_tx_(0), total_rx_(0) {

    //
    // Track the session count
    //
    session_count_++;
}

class EchoServerTest : public ::testing::TestWithParam<TestParams> {
public:
    typedef std::map<TcpSession*, TcpServer *> SessionMatrix;
protected:
    EchoServerTest() : evm_(new EventManager()),
    timer_(TimerManager::CreateTimer(*evm_->io_service(), "Test")),
    connect_success_(0), connect_fail_(0), connect_abort_(0), 
    session_close_(0) { }

    static bool not_connected(const std::pair<TcpSession*, TcpServer *> &v) {
        if (v.first->IsEstablished()) {
            EchoSession *server_session = static_cast<EchoSession *>
                (v.second->GetSession(v.first->local_endpoint()));
            if (server_session) {
                return false;
            }
        }
        return true;
    }

    virtual void SetUp() {
        InitParams();
        thread_.reset(new ServerThread(evm_.get()));
        for (int i = 0; i < max_num_servers_; i++) {
            server_.push_back(new EchoServer(evm_.get()));
            server_[i]->Initialize(0);
            task_util::WaitForIdle();
        }
        thread_->Start();		// Must be called after initialization
        std::cout << "Num Servers " << max_num_servers_ 
            << " Num Connections " << max_num_connections_ 
            << " Maximum packet size " << max_packet_size_
            << " Is blocking " << blocking_ << std::endl;
        for (int i = 0; i < max_num_connections_; i++) {
            uint32_t server = 0;
            uint32_t client = 0;

            do {
                do { 
                    server = rand() % max_num_servers_;
                    client = rand() % max_num_servers_;
                } while (server == client);

                if (server_[client]->IsConnectionPresent(server_[server])) {
                    continue;
                }

                TcpSession *session =
                    server_[client]->CreateClientSession(blocking_);
                session->set_observer(boost::bind(&EchoServerTest::OnEvent,
                                                  this, _1, _2));
                server_[client]->ConnectToServer(server_[server], session);

                mutex::scoped_lock lock(mutex_);
                session_matrix_.insert(make_pair(session, server_[server]));
                break;
            } while (1);
        }
        StartConnectTimer(1);
        bool res = false;
        for (int i = 0; i < 100000; i++) {
            usleep(1000);
            {
                mutex::scoped_lock lock(mutex_);
                res = (find_if(session_matrix_.begin(), session_matrix_.end(), 
                               not_connected) == session_matrix_.end());
                if (res) break;
            }
        }
        EXPECT_TRUE(res);
        timer_->Cancel();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(max_num_connections_,
                            connect_success_-session_close_);
        int num_failures = 0;
        BOOST_FOREACH(EchoServer *server, server_) {
            num_failures += server->num_reject_accept();
        }
        TASK_UTIL_EXPECT_EQ(connect_fail_+session_close_, num_failures);
        TCP_UT_LOG_DEBUG("Num REJECT :" << num_failures);
        TCP_UT_LOG_DEBUG("Num ABORT :" << connect_abort_);
        TCP_UT_LOG_DEBUG("Num CLOSE :" << session_close_);
        TCP_UT_LOG_DEBUG("Num FAIL  :" << connect_fail_);
        TCP_UT_LOG_DEBUG("Num COMPLETE  :" << connect_success_);
    }

    void DeleteAllSessions() {
        mutex::scoped_lock lock(mutex_);
        for (SessionMatrix::iterator it = session_matrix_.begin(), next = it;
             it != session_matrix_.end(); it = next) {
            ++next;
            EchoServer *client = 
                static_cast<EchoServer *>(it->first->server());
            EchoServer *server = static_cast<EchoServer *>(it->second);
            client->DisconnectFromServer(server);
            session_matrix_.erase(it);
        }
    }
 
    virtual void TearDown() {
        task_util::WaitForIdle();
        timer_->Cancel();
        DeleteAllSessions();
        task_util::WaitForIdle();

        for (int i = 0; i < max_num_servers_; i++) {
            if (!server_[i]) continue;
            server_[i]->Shutdown();
            server_[i]->ClearSessions();
            TcpServerManager::DeleteServer(server_[i]);
            server_[i] = NULL;
        }
        task_util::WaitForIdle();

        //
        // Wait until all the sessions are cleaned up, before deleting the
        // servers
        //
        // TASK_UTIL_EXPECT_EQ(0, session_count_);

        evm_->Shutdown();
        task_util::WaitForIdle();

        if (thread_.get() != NULL) {
            thread_->Join();
        }

        TCP_UT_LOG_DEBUG("Delete all(" << server_.size() << ") servers ");
        STLDeleteValues(&server_);
        task_util::WaitForIdle();
        TimerManager::DeleteTimer(timer_);
    }

    bool DummyTimerHandler() {
        bool restart_timer = false;
        mutex::scoped_lock lock(mutex_);
        SessionMatrix tmp_map;
        for (SessionMatrix::iterator it = session_matrix_.begin(), next = it;
             it != session_matrix_.end(); it = next) {
            ++next;
            if (!it->first->IsEstablished()) {
                if (!it->first->IsClosed()) {
                    connect_abort_++;
                }
                EchoServer *client = 
                    static_cast<EchoServer *>(it->first->server());
                EchoServer *server = static_cast<EchoServer *>(it->second);
                client->DisconnectFromServer(server);
                session_matrix_.erase(it);

                if (client->IsConnectionPresent(server)) continue;

                TcpSession *session = client->CreateClientSession(blocking_);
                session->set_observer(boost::bind(&EchoServerTest::OnEvent,
                                                  this, _1, _2));

                client->ConnectToServer(server, session);
                restart_timer = true;
                tmp_map.insert(make_pair(session, server));
            }
        }
        if (!tmp_map.empty()) {
            session_matrix_.insert(tmp_map.begin(), tmp_map.end());
        }
        return restart_timer;
    }

    void StartConnectTimer(int sec) {
        timer_->Start(sec * 1000,
                      boost::bind(&EchoServerTest::DummyTimerHandler, this));
    }

    void OnEvent(TcpSession *session, TcpSession::Event event) {
        assert (event != TcpSession::EVENT_NONE);
        if (event == TcpSession::CONNECT_FAILED) {
            connect_fail_++;
            session->Close();
            return;
        }

        if (event == TcpSession::CONNECT_COMPLETE) {
            connect_success_++;
            assert(session->IsEstablished());
            bool res = session->Send((const u_int8_t *) msg, 
                                     max_packet_size_, NULL);
            EchoSession *esession = static_cast<EchoSession *>(session);
            esession->increment_sent(max_packet_size_);
            EXPECT_TRUE(res);
            return;
        }

        if (event == TcpSession::CLOSE) {
            session_close_++;
            return;
        }

        if (event == TcpSession::ACCEPT) {
            session_accept_++;
            return;
        }

        assert(false);
    }

    void InitParams() {
        max_num_servers_ = std::tr1::get<0>(GetParam());
        max_num_connections_ = std::tr1::get<1>(GetParam());
        max_packet_size_ = std::tr1::get<2>(GetParam());
        blocking_ = std::tr1::get<3>(GetParam());
    }

    bool verify_rx() {
        task_util::WaitForIdle();
        mutex::scoped_lock lock(mutex_);
        uint32_t total_sent = 0;
        uint32_t total_rxed = 0;
        BOOST_FOREACH(SessionMatrix::value_type mapref, session_matrix_) {
            total_sent += 
                static_cast<EchoSession *>(mapref.first)->total_tx();
            EchoSession *server_session = static_cast<EchoSession *>
            (mapref.second->GetSession(mapref.first->local_endpoint()));
            if (server_session) total_rxed += server_session->total_rx();
        }
        TCP_UT_LOG_DEBUG("Sent " << total_sent << ", Rxed " << total_rxed);
        return (total_sent == total_rxed);
    }

    auto_ptr<ServerThread> thread_;
    auto_ptr<EventManager> evm_;
    std::vector<EchoServer *> server_;
    tbb::mutex mutex_;
    SessionMatrix session_matrix_;
    Timer *timer_;
    int connect_success_;
    int connect_fail_;
    int connect_abort_;
    int session_accept_;
    int session_close_;
    int max_num_servers_;
    int max_num_connections_;
    int max_packet_size_;
    int blocking_;
};

TEST_P(EchoServerTest, Basic) {
    BOOST_FOREACH(SessionMatrix::value_type mapref, session_matrix_) {
        if (mapref.first->IsEstablished()) {
            TCP_UT_LOG_DEBUG("Send to " << mapref.first->ToString());
            bool res = mapref.first->Send((const u_int8_t *) msg, 
                                          max_packet_size_, NULL);
            EchoSession *session = static_cast<EchoSession *>(mapref.first);
            session->increment_sent(max_packet_size_);
            EXPECT_TRUE(res);
        }
    }
    TASK_UTIL_ASSERT_TRUE(verify_rx());
}


TEST_P(EchoServerTest, WriteWithBlockedReader) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    BOOST_FOREACH(SessionMatrix::value_type mapref, session_matrix_) {
        if (mapref.first->IsEstablished()) {
            TCP_UT_LOG_DEBUG("Send to " << mapref.first->ToString());
            bool res = mapref.first->Send((const u_int8_t *) msg, 
                                          max_packet_size_, NULL);
            EchoSession *session = static_cast<EchoSession *>(mapref.first);
            session->increment_sent(max_packet_size_);
            EXPECT_TRUE(res);
        }
    }
    scheduler->Start();
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_TRUE(verify_rx());
}

TEST_P(EchoServerTest, WriteWithBlockedReaderParallelDisconnect) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    BOOST_FOREACH(SessionMatrix::value_type mapref, session_matrix_) {
        if (mapref.first->IsEstablished()) {
            TCP_UT_LOG_DEBUG("Send to " << mapref.first->ToString());
            bool res = mapref.first->Send((const u_int8_t *) msg, 
                                          max_packet_size_, NULL);
            EchoSession *session = static_cast<EchoSession *>(mapref.first);
            session->increment_sent(max_packet_size_);
            EXPECT_TRUE(res);
        }
    }

    DeleteAllSessions();

    scheduler->Start();

    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_TRUE(verify_rx());
}

TEST_P(EchoServerTest, ServerShutdown) {
    TASK_UTIL_EXPECT_EQ(max_num_connections_, connect_success_-session_close_);
    for (int i = 0; i < max_num_servers_; i++) {
        if (!server_[i]) continue;
        if (i%4 == 0) {
            TCP_UT_LOG_DEBUG("Shutdown Server @ " << server_[i]->GetPort());
            server_[i]->Shutdown();
            server_[i]->ClearSessions();
            TcpServerManager::DeleteServer(server_[i]);
            server_[i] = NULL;
        }
    }
    task_util::WaitForIdle();
    // Shutdown frees all sessions.
    {
        mutex::scoped_lock lock(mutex_);
        session_matrix_.clear();
    }
}

TEST_P(EchoServerTest, ClientDisconnect) {
    for (int i = 0; i < 10; i++) {
        uint32_t server = rand() % max_num_servers_;
        {
            mutex::scoped_lock lock(mutex_);
            for (SessionMatrix::iterator it = session_matrix_.begin();
                 it != session_matrix_.end(); it++) {
                if (it->second == server_[server]) {
                    EchoServer *client = 
                        static_cast<EchoServer *>(it->first->server());
                    EchoServer *server = static_cast<EchoServer *>(it->second);
                    client->DisconnectFromServer(server);
                    session_matrix_.erase(it->first);
                    break;
                }
            }
        }
    }
    task_util::WaitForIdle();
}
}  // namespace

static vector<int> n_servers = boost::assign::list_of(64);
static vector<int> n_connections = boost::assign::list_of(32);
static vector<int> n_sizes = boost::assign::list_of(4094);
static vector<bool> n_blk_nonblk = boost::assign::list_of(false);

static void process_command_line_args(int argc, char **argv) {
    int servers = 1, connections = 1, size = 128, blocking = false;
    options_description desc("Allowed options");
    bool cmd_line_arg_set = false;
    desc.add_options()
        ("help", "produce help message")
        ("servers", value<int>(), "set number of servers")
        ("connections", value<int>(), "set number of connectios")
        ("blocking", value<bool>(), "Are the connections blocking")
        ("size", value<int>(), "Size of message")
        ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    if (vm.count("servers")) {
        servers = vm["servers"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("connections")) {
        connections = vm["connections"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("size")) {
        size = vm["size"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("blocking")) {
        blocking = vm["blocking"].as<bool>();
        cmd_line_arg_set = true;
    }


    if (cmd_line_arg_set) {
        n_servers.clear();
        n_servers.push_back(servers);
        n_connections.clear();
        n_connections.push_back(connections);
        n_sizes.clear();
        n_sizes.push_back(size);
        n_blk_nonblk.clear();
        n_blk_nonblk.push_back(blocking);
    }
}

static vector<int> GetTestParam() {
    static bool cmd_line_processed;

    if (!cmd_line_processed) {
        cmd_line_processed = true;
        process_command_line_args(gargc, gargv);
    }

    return n_servers;
}

#define COMBINE_PARAMS \
    Combine(ValuesIn(GetTestParam()), \
            ValuesIn(n_connections),  \
            ValuesIn(n_sizes),        \
            ValuesIn(n_blk_nonblk))

INSTANTIATE_TEST_CASE_P(TcpStressTestWithParams, EchoServerTest, 
                        COMBINE_PARAMS);

int main(int argc, char **argv) {
    gargc = argc;
    gargv = argv;
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&gargc, gargv);
    return RUN_ALL_TESTS();
}
