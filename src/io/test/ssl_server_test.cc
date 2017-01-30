/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>
#include <pthread.h>
#include <memory>
#include <netinet/in.h>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>

#include "testing/gunit.h"

#include "base/logging.h"
#include "base/parse_object.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "io/ssl_server.h"
#include "io/ssl_session.h"
#include "io/test/event_manager_test.h"
#include "io/io_log.h"

using namespace std;

namespace {

class EchoServer;

class EchoSession : public SslSession {
  public:
    EchoSession(EchoServer *server, SslSocket *socket,
                bool ssl_handshake_delayed);

  protected:
    virtual void OnRead(Buffer buffer) {
        const u_int8_t *data = BufferData(buffer);
        const size_t len = BufferSize(buffer);
        //TCP_UT_LOG_DEBUG("Received " << BufferData(buffer) << " " << len << " bytes");
        Send(data, len, NULL);
    }
  private:
    void OnEvent(TcpSession *session, Event event) {
        if (event == ACCEPT) {
            TCP_UT_LOG_DEBUG("Accept");
        }
        if (event == CLOSE) {
            TCP_UT_LOG_DEBUG("Close");
        }
    }
};

class EchoServer : public SslServer {
public:
    EchoServer(EventManager *evm, bool ssl_handshake_delayed = false) :
        SslServer(evm, boost::asio::ssl::context::tlsv1_server,
                  true, ssl_handshake_delayed),
        session_(NULL),
        ssl_handshake_delayed_(ssl_handshake_delayed),
        ssl_handshake_count_(0) {
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->set_verify_mode((boost::asio::ssl::verify_peer |
                              boost::asio::ssl::verify_fail_if_no_peer_cert), ec);
        assert(ec.value() == 0);
        ctx->use_certificate_chain_file
            ("controller/src/io/test/newcert.pem", ec);
        assert(ec.value() == 0);
        ctx->use_private_key_file("controller/src/io/test/privkey.pem",
                                  boost::asio::ssl::context::pem, ec);
        assert(ec.value() == 0);
        ctx->load_verify_file("controller/src/io/test/ssl_client_cert.pem",
                              ec);
        assert(ec.value() == 0);
    }

    ~EchoServer() {
    }

    void set_verify_fail_certs() {
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->load_verify_file("controller/src/io/test/newcert.pem",
                              ec);
        assert(ec.value() == 0);
    }

    virtual SslSession *AllocSession(SslSocket *socket) {
        session_ =  new EchoSession(this, socket, ssl_handshake_delayed_);
        return session_;
    }

    TcpSession *CreateSession() {
        TcpSession *session = SslServer::CreateSession();
        Socket *socket = session->socket();

        boost::system::error_code err;
        socket->open(boost::asio::ip::tcp::v4(), err);
        if (err) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                                  "open failed: " << err.message());
        }   
        err = session->SetSocketOptions();
        if (err) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                                  "sockopt: " << err.message());
        }
        return session;
    }

    EchoSession *GetSession() const { return session_; }

    int GetSslHandShakeCount() { return ssl_handshake_count_; }
    void ProcessSslHandShakeResponse(SslSessionPtr session, const boost::system::error_code& error) {
        ssl_handshake_count_++;
        if (!error) {
            session->AsyncReadStart();
        }
    }

private:
    EchoSession *session_;
    bool ssl_handshake_delayed_;
    int ssl_handshake_count_;
};

EchoSession::EchoSession(EchoServer *server, SslSocket *socket,
                         bool ssl_handshake_delayed)
    : SslSession(server, socket) {

    if (!ssl_handshake_delayed) {
        set_observer(boost::bind(&EchoSession::OnEvent, this, _1, _2));
    }
}

static size_t sent_data_size;

class SslClient;

class ClientSession : public SslSession {
  public:
    ClientSession(SslClient *server, SslSocket *socket,
                  bool ssl_handshake_delayed = false,
                  bool large_data = false);

    void OnEvent(TcpSession *session, Event event) {
        if (event == CONNECT_COMPLETE) {
            if (!large_data_) {
                const u_int8_t *data = (const u_int8_t *)"Hello there !";
                sent_data_size = strlen((const char *) data) + 1;
                Send(data, sent_data_size, NULL);
                return;
            }

            // Send a large xml file as data.
            ifstream ifs("controller/src/ifmap/testdata/scaled_config.xml");
            stringstream s;
            while (ifs >> s.rdbuf());
            string str = s.str();
            sent_data_size = str.size() + 1;
            Send((const u_int8_t *) str.c_str(), sent_data_size, NULL);
        }
    }

    std::size_t &len() { return len_; }

  protected:
    virtual void OnRead(Buffer buffer) {
        const size_t len = BufferSize(buffer);
        //TCP_UT_LOG_DEBUG("Received " << len << " bytes");
        len_ += len;
    }

  private:
    std::size_t len_;
    bool large_data_;
};

class SslClient : public SslServer {
public:
    SslClient(EventManager *evm, bool ssl_handshake_delayed = false,
            bool large_data = false) :
        SslServer(evm, boost::asio::ssl::context::tlsv1, true, ssl_handshake_delayed),
        session_(NULL),
        large_data_(large_data),
        ssl_handshake_delayed_(ssl_handshake_delayed),
        ssl_handshake_count_(0) {

        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->set_verify_mode(boost::asio::ssl::context::verify_none, ec);
        assert(ec.value() == 0);
        ctx->use_certificate_chain_file
            ("controller/src/io/test/ssl_client_cert.pem", ec);
        assert(ec.value() == 0);
        ctx->use_private_key_file("controller/src/io/test/ssl_client_privkey.pem",
                                  boost::asio::ssl::context::pem, ec);
        assert(ec.value() == 0);
        ctx->load_verify_file("controller/src/io/test/newcert.pem",
                              ec);
        assert(ec.value() == 0);
    }
    ~SslClient() {
    }
    virtual SslSession *AllocSession(SslSocket *socket) {
        session_ =  new ClientSession(this, socket, ssl_handshake_delayed_,
                                      large_data_);
        return session_;
    }

    void set_verify_fail_certs() {
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->set_verify_mode((boost::asio::ssl::verify_peer |
                              boost::asio::ssl::verify_fail_if_no_peer_cert), ec);
        assert(ec.value() == 0);
        ctx->use_certificate_chain_file
            ("controller/src/io/test/newcert.pem", ec);
        assert(ec.value() == 0);
        ctx->load_verify_file("controller/src/io/test/ssl_client_cert.pem",
                              ec);
        assert(ec.value() == 0);
    }

    TcpSession *CreateSession() {
        TcpSession *session = SslServer::CreateSession();
        Socket *socket = session->socket();

        boost::system::error_code err;
        socket->open(boost::asio::ip::tcp::v4(), err);
        if (err) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                                  "open failed: " << err.message());
        }   
        err = session->SetSocketOptions();
        if (err) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                                  "sockopt: " << err.message());
        }
        return session;
    }

    ClientSession *GetSession() const { return session_; }

    int GetSslHandShakeCount() { return ssl_handshake_count_; }
    void ProcessSslHandShakeResponse(SslSessionPtr session, const boost::system::error_code& error) {
        ssl_handshake_count_++;
        if (!error) {
            session->AsyncReadStart();
            const u_int8_t *data = (const u_int8_t *)"Encrypted Hello !";
            size_t len = 18;
            session->Send(data, len, NULL);
        }
    }

private:
    ClientSession *session_;
    bool large_data_;
    bool ssl_handshake_delayed_;
    int ssl_handshake_count_;
};

ClientSession::ClientSession(SslClient *server, SslSocket *socket,
                             bool ssl_handshake_delayed,
                             bool large_data)
    : SslSession(server, socket), len_(0), large_data_(large_data) {
    if (!ssl_handshake_delayed) {
        set_observer(boost::bind(&ClientSession::OnEvent, this, _1, _2));
    }
}

class SslEchoServerTest : public ::testing::Test {
public:
    void OnEvent(TcpSession *session, SslSession::Event event) {
        boost::system::error_code ec;
        timer_.cancel(ec);
        ClientSession *client_session = static_cast<ClientSession *>(session);
        client_session->OnEvent(session, event);
        if (event == SslSession::CONNECT_FAILED) {
            connect_fail_++;
            session->Close();
        }
        if (event == SslSession::CONNECT_COMPLETE) {
            connect_success_++;
        }
    }

protected:
    SslEchoServerTest()
        : evm_(new EventManager()), timer_(*evm_->io_service()),
          connect_success_(0), connect_fail_(0), connect_abort_(0) {
    }

    void SetUpImmedidate() {
        server_ = new EchoServer(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
        session_ = NULL;
    }
    void SetUpDelayedHandShake() {
        server_ = new EchoServer(evm_.get(), true);
        thread_.reset(new ServerThread(evm_.get()));
        session_ = NULL;
    }


    virtual void TearDown() {
        if (session_) session_->Close();
        task_util::WaitForIdle();
        server_->Shutdown();
        server_->ClearSessions();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(server_);
        server_ = NULL;
        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }


    void DummyTimerHandler(TcpSession *session, 
                           const boost::system::error_code &error) {
        if (error) {
            return;
        }
        if (!session->IsClosed()) {
            connect_abort_++;
        }
        session->Close();
    }

    void StartConnectTimer(TcpSession *session, int sec) {
        boost::system::error_code ec;
        timer_.expires_from_now(boost::posix_time::seconds(sec), ec);
        timer_.async_wait(
            boost::bind(&SslEchoServerTest::DummyTimerHandler, this, session,
                        boost::asio::placeholders::error));
    }
    auto_ptr<ServerThread> thread_;
    auto_ptr<EventManager> evm_;
    EchoServer *server_;
    boost::asio::deadline_timer timer_;
    EchoSession *session_;
    int connect_success_;
    int connect_fail_;
    int connect_abort_;
};

TEST_F(SslEchoServerTest, msg_send_recv) {

    SetUpImmedidate();
    SslClient *client = new SslClient(evm_.get());

    task_util::WaitForIdle();
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    ClientSession *session = static_cast<ClientSession *>(client->CreateSession());
    session->set_observer(boost::bind(&SslEchoServerTest::OnEvent, this, _1, _2));
    boost::asio::ip::tcp::endpoint endpoint;
    boost::system::error_code ec;
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(server_->GetPort());
    client->Connect(session, endpoint);
    task_util::WaitForIdle();
    StartConnectTimer(session, 10);
    TASK_UTIL_EXPECT_TRUE(session->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(session->IsClosed());
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 0);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);
    // wait for on connect message to come back from echo server.
    TASK_UTIL_EXPECT_EQ(sent_data_size, session->len());

    session->Close();
    client->DeleteSession(session);

    client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(client);
    client = NULL;
}

TEST_F(SslEchoServerTest, large_msg_send_recv) {

    SetUpImmedidate();
    SslClient *client = new SslClient(evm_.get(), false, true);

    task_util::WaitForIdle();
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    ClientSession *session = static_cast<ClientSession *>(client->CreateSession());
    session->set_observer(boost::bind(&SslEchoServerTest::OnEvent, this, _1, _2));
    boost::asio::ip::tcp::endpoint endpoint;
    boost::system::error_code ec;
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(server_->GetPort());
    client->Connect(session, endpoint);
    task_util::WaitForIdle();
    StartConnectTimer(session, 10);
    TASK_UTIL_EXPECT_TRUE(session->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(session->IsClosed());
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 0);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);
    // wait for on connect message to come back from echo server.
    TASK_UTIL_EXPECT_EQ(sent_data_size, session->len());

    session->Close();
    client->DeleteSession(session);

    client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(client);
    client = NULL;
}

TEST_F(SslEchoServerTest, HandshakeFailure) {

    SetUpImmedidate();
    SslClient *client = new SslClient(evm_.get());
    SslClient *client_fail = new SslClient(evm_.get());

    // set context to verify certs to fail handshake
    client_fail->set_verify_fail_certs();

    task_util::WaitForIdle();
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    ClientSession *session = static_cast<ClientSession *>(client->CreateSession());
    ClientSession *session_fail = static_cast<ClientSession *>(client_fail->CreateSession());
    session->set_observer(boost::bind(&SslEchoServerTest::OnEvent, this, _1, _2));
    session_fail->set_observer(boost::bind(&SslEchoServerTest::OnEvent, this, _1, _2));
    boost::asio::ip::tcp::endpoint endpoint;
    boost::system::error_code ec;
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(server_->GetPort());
    client->Connect(session, endpoint);
    task_util::WaitForIdle();
    StartConnectTimer(session, 10);
    TASK_UTIL_EXPECT_TRUE(session->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(session->IsClosed());
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 0);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);
    // wait for on connect message to come back from echo server.
    TASK_UTIL_EXPECT_EQ(sent_data_size, session->len());

    client_fail->Connect(session_fail, endpoint);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 1);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);

    session->Close();
    session_fail->Close();
    client_fail->DeleteSession(session_fail);
    client->DeleteSession(session);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, server_->GetSessionCount());
    TASK_UTIL_EXPECT_FALSE(server_->HasSessions());

    client_fail->Shutdown();
    client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(client_fail);
    TcpServerManager::DeleteServer(client);
    client_fail = NULL;
    client = NULL;
}

TEST_F(SslEchoServerTest, DISABLED_test_delayed_ssl_handshake) {

    SetUpDelayedHandShake();

    // create a ssl client with delayed handshake = true
    SslClient *client = new SslClient(evm_.get(), true);

    task_util::WaitForIdle();
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    ClientSession *session = static_cast<ClientSession *>(client->CreateSession());
    session->set_observer(boost::bind(&SslEchoServerTest::OnEvent, this, _1, _2));
    boost::asio::ip::tcp::endpoint endpoint;
    boost::system::error_code ec;
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(server_->GetPort());
    client->Connect(session, endpoint);
    task_util::WaitForIdle();
    StartConnectTimer(session, 10);
    TASK_UTIL_EXPECT_TRUE(session->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(session->IsClosed());
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 0);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);
    // wait till plain-text data is transferred
    TASK_UTIL_EXPECT_EQ(sent_data_size, session->len());

    // Trigger delayed ssl handshake on server side
    server_->GetSession()->TriggerSslHandShake(
        boost::bind(&EchoServer::ProcessSslHandShakeResponse, server_, _1, _2));

    // Trigger delayed ssl handshake on client side
    session->TriggerSslHandShake(
        boost::bind(&SslClient::ProcessSslHandShakeResponse, client, _1, _2));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(client->GetSslHandShakeCount(), 1);
    TASK_UTIL_EXPECT_EQ(server_->GetSslHandShakeCount(), 1);
    // wait till encrypted data is transferred
    TASK_UTIL_EXPECT_EQ(session->len(), 32);

    session->Close();
    client->DeleteSession(session);

    client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(client);
    client = NULL;
}



}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
