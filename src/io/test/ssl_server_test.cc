/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <memory>

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

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
    EchoSession(EchoServer *server, SslSocket *socket);

  protected:
    virtual void OnRead(Buffer buffer) {
        const u_int8_t *data = BufferData(buffer);
        const size_t len = BufferSize(buffer);
        TCP_UT_LOG_DEBUG("Received " << BufferData(buffer) << " " << len << " bytes");
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
    explicit EchoServer(EventManager *evm) :
        SslServer(evm, boost::asio::ssl::context::tlsv1_server), session_(NULL) {
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->set_verify_mode(boost::asio::ssl::context::verify_none, ec);
        assert(ec.value() == 0);
        ctx->use_certificate_chain_file
            ("controller/src/ifmap/client/test/newcert.pem", ec);
        assert(ec.value() == 0);
        ctx->use_private_key_file("controller/src/ifmap/client/test/server.pem",
                                  boost::asio::ssl::context::pem, ec);
        assert(ec.value() == 0);
        ctx->add_verify_path("controller/src/ifmap/client/test/", ec);
        assert(ec.value() == 0);
        ctx->load_verify_file("controller/src/ifmap/client/test/newcert.pem",
                              ec);
        assert(ec.value() == 0);
    }
    ~EchoServer() {
    }
    virtual SslSession *AllocSession(SslSocket *socket) {
        session_ =  new EchoSession(this, socket);
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

private:
    EchoSession *session_;
};

EchoSession::EchoSession(EchoServer *server, SslSocket *socket)
    : SslSession(server, socket) {
    set_observer(boost::bind(&EchoSession::OnEvent, this, _1, _2));
}

class SslClient;

class ClientSession : public SslSession {
  public:
    ClientSession(SslClient *server, SslSocket *socket);

    void OnEvent(TcpSession *session, Event event) {
        if (event == CONNECT_COMPLETE) {
            const u_int8_t *data = (const u_int8_t *)"Hello there !";
            size_t len = 14;
            Send(data, len, NULL);
        }
    }

    std::size_t &len() { return len_; }

  protected:
    virtual void OnRead(Buffer buffer) {
        const u_int8_t *data = BufferData(buffer);
        const size_t len = BufferSize(buffer);
        TCP_UT_LOG_DEBUG("Received " << BufferData(buffer) << " " << len << " bytes");
        len_ += len;
    }

  private:
    std::size_t len_;
};

class SslClient : public SslServer {
public:
    explicit SslClient(EventManager *evm) :
        SslServer(evm, boost::asio::ssl::context::tlsv1), session_(NULL) {
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;
        ctx->set_verify_mode(boost::asio::ssl::context::verify_none, ec);
        assert(ec.value() == 0);
        ctx->use_certificate_chain_file
            ("controller/src/ifmap/client/test/newcert.pem", ec);
        assert(ec.value() == 0);
        ctx->use_private_key_file("controller/src/ifmap/client/test/server.pem",
                                  boost::asio::ssl::context::pem, ec);
        assert(ec.value() == 0);
        ctx->add_verify_path("controller/src/ifmap/client/test/", ec);
        assert(ec.value() == 0);
        ctx->load_verify_file("controller/src/ifmap/client/test/newcert.pem",
                              ec);
        assert(ec.value() == 0);
    }
    ~SslClient() {
    }
    virtual SslSession *AllocSession(SslSocket *socket) {
        session_ =  new ClientSession(this, socket);
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

    ClientSession *GetSession() const { return session_; }

private:
    ClientSession *session_;
};

ClientSession::ClientSession(SslClient *server, SslSocket *socket)
    : SslSession(server, socket) {
    set_observer(boost::bind(&ClientSession::OnEvent, this, _1, _2));
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
    virtual void SetUp() {
        server_ = new EchoServer(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
        session_ = NULL;
    }

    virtual void TearDown() {
        if (server_->GetSession()) {
            server_->GetSession()->Close();
        }
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
    TASK_UTIL_EXPECT_EQ(session->len(), 14);

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
