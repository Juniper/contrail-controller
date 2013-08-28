/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
#include "io/tcp_server.h"
#include "io/tcp_session.h"
#include "io/test/event_manager_test.h"
#include "io/io_log.h"

using namespace std;

namespace {

class EchoServer;

class EchoSession : public TcpSession {
  public:
    EchoSession(EchoServer *server, Socket *socket);

  protected:
    virtual void OnRead(Buffer buffer) {
        const u_int8_t *data = BufferData(buffer);
        const size_t len = BufferSize(buffer);
        TCP_UT_LOG_DEBUG("Received " << len << " bytes");
        Send(data, len, NULL);
    }
  private:
    void OnEvent(TcpSession *session, Event event) {
        if (event == CLOSE) {
            TCP_UT_LOG_DEBUG("Close");
        }
    }
};

class EchoServer : public TcpServer {
public:
    explicit EchoServer(EventManager *evm) : TcpServer(evm), session_(NULL) {
    }
    ~EchoServer() {
    }
    virtual TcpSession *AllocSession(Socket *socket) {
        session_ =  new EchoSession(this, socket);
        return session_;
    }

    TcpSession *CreateSession() {
        TcpSession *session = TcpServer::CreateSession();
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

EchoSession::EchoSession(EchoServer *server, Socket *socket)
    : TcpSession(server, socket) {
    set_observer(boost::bind(&EchoSession::OnEvent, this, _1, _2));
}

class TcpLocalClient {
  public:
    explicit TcpLocalClient(int port) : dst_port_(port), socket_(-1) {
    }
    ~TcpLocalClient() {
        if (socket_ != -1) {
            close(socket_);
        }
    }
    bool Connect() {
        socket_ = socket(AF_INET, SOCK_STREAM, 0);
        assert(socket_ != -1);
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
#ifdef __APPLE__
        sin.sin_len = sizeof(struct sockaddr_in);
#endif
        sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        sin.sin_port = htons(dst_port_);
        int res = connect(socket_, (sockaddr *) &sin,
                          sizeof(struct sockaddr_in));
        return (res != -1);
    }
    int Send(const u_int8_t *data, size_t len) {
        return send(socket_, data, len, 0);
    }
    int Recv(u_int8_t *buffer, size_t len) {
        return recv(socket_, buffer, len, 0);
    }
    void Close() {
        int res = shutdown(socket_, SHUT_RDWR);
        assert(res == 0);
    }
  private:
    int dst_port_;
    int socket_;
};

class EchoServerTest : public ::testing::Test {
public:
    void OnEvent(TcpSession *session, TcpSession::Event event) {
        if (event == TcpSession::CONNECT_FAILED) {
            connect_fail_++;
            session->Close();
        }
        if (event == TcpSession::CONNECT_COMPLETE) {
            connect_success_++;
            boost::system::error_code ec;
            timer_.cancel(ec);
        }
    }

protected:
    EchoServerTest()
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
            boost::bind(&EchoServerTest::DummyTimerHandler, this, session,
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

TEST_F(EchoServerTest, Basic) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization
    int port = server_->GetPort();
    ASSERT_LT(0, port);
    TCP_UT_LOG_DEBUG("Server port: " << port);
    TcpLocalClient client(port);
    TASK_UTIL_EXPECT_TRUE(client.Connect());
    const char msg[] = "Test Message";
    int len = client.Send((const u_int8_t *) msg, sizeof(msg));
    TASK_UTIL_EXPECT_EQ((int) sizeof(msg), len);

    u_int8_t data[1024];
    int rlen = client.Recv(data, sizeof(data));
    TASK_UTIL_EXPECT_EQ(len, rlen);
    TASK_UTIL_EXPECT_EQ(0, memcmp(data, msg, rlen));

    client.Close();
}

TEST_F(EchoServerTest, Connect) {
    EchoServer *client = new EchoServer(evm_.get());

    task_util::WaitForIdle();
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization

    TcpSession *session = client->CreateSession();
    session->set_observer(boost::bind(&EchoServerTest::OnEvent, this, _1, _2));
    boost::asio::ip::tcp::endpoint endpoint;
    boost::system::error_code ec;
    endpoint.address(boost::asio::ip::address::from_string("240.0.0.1", ec));
    endpoint.port(179);
    client->Connect(session, endpoint);
    StartConnectTimer(session, 1);
    TASK_UTIL_EXPECT_TRUE(session->IsClosed());
    TASK_UTIL_EXPECT_FALSE(session->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, connect_success_);
    // socket close: will trigger an error.
    // TASK_UTIL_EXPECT_EQ(0, connect_fail_);
    TASK_UTIL_EXPECT_EQ(1, connect_abort_);
    session->Close();
    client->DeleteSession(session);

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    session = client->CreateSession();
    session->set_observer(boost::bind(&EchoServerTest::OnEvent, this, _1, _2));
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(179);
    client->Connect(session, endpoint);
    StartConnectTimer(session, 1);
    TASK_UTIL_EXPECT_TRUE(session->IsClosed());
    TASK_UTIL_EXPECT_FALSE(session->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, connect_success_);
    TASK_UTIL_EXPECT_EQ(1, connect_fail_);
    TASK_UTIL_EXPECT_EQ(0, connect_abort_);
    session->Close();
    client->DeleteSession(session);

    connect_success_ = connect_fail_ = connect_abort_ = 0;
    session = client->CreateSession();
    session->set_observer(boost::bind(&EchoServerTest::OnEvent, this, _1, _2));
    endpoint.address(boost::asio::ip::address::from_string("127.0.0.1", ec));
    endpoint.port(server_->GetPort());
    client->Connect(session, endpoint);
    StartConnectTimer(session, 10);
    TASK_UTIL_EXPECT_TRUE(session->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(session->IsClosed());
    TASK_UTIL_EXPECT_EQ(1, connect_success_);
    TASK_UTIL_EXPECT_EQ(connect_fail_, 0);
    TASK_UTIL_EXPECT_EQ(connect_abort_, 0);
    session->Close();
    client->DeleteSession(session);

    client->Shutdown();
    task_util::WaitForIdle();
    TcpServerManager::DeleteServer(client);
    client = NULL;
}
using boost::asio::mutable_buffer;

class ReaderTest : public TcpMessageReader {
public:
    ReaderTest(TcpSession *session, ReceiveCallback callback)
        : TcpMessageReader(session, callback) {
    }

    virtual const int GetHeaderLenSize() {
        return kHeaderLenSize;
    }

    virtual const int GetMaxMessageSize() {
        return kMaxMessageSize;
    }

    // Extract the total BGP message length. This is a 2 byte field after the
    // 16 byte marker. If the buffer doesn't have 18 bytes available return -1.
    int MsgLength(Buffer buffer, int offset) {
        size_t size = TcpSession::BufferSize(buffer);
        int remain = size - offset;
        if (remain < kHeaderLenSize) {
            return -1;
        }
        const uint8_t *data = TcpSession::BufferData(buffer) + offset;
        data += 16;
        int length = get_value(data, 2);
        return length;
    }

private:
    static const int kHeaderLenSize = 18;
    static const int kMaxMessageSize = 4096;
};

class ReaderTestSession : public TcpSession {
  public:
    ReaderTestSession(TcpServer *server, Socket *socket);

    void Read(Buffer buffer) {
        OnRead(buffer);
    }

    virtual void ReleaseBuffer(Buffer buffer) {
        release_count_++;
    }

    vector<int>::const_iterator begin() const {
        return sizes.begin();
    }
    vector<int>::const_iterator end() const {
        return sizes.end();
    }

    int release_count() const { return release_count_; }

  protected:
    virtual void OnRead(Buffer buffer) {
        reader_->OnRead(buffer);
    }

  private:
    void ReceiveMsg(const u_int8_t *msg, size_t size) {
        TCP_UT_LOG_DEBUG("ReceiveMsg: " << size << " bytes");
        sizes.push_back(size);
    }

    std::auto_ptr<ReaderTest> reader_;
    vector<int> sizes;
    int release_count_;
};

ReaderTestSession::ReaderTestSession(TcpServer *server, Socket *socket)
    : TcpSession(server, socket),
      reader_(new ReaderTest(this,
              boost::bind(&ReaderTestSession::ReceiveMsg, this, _1, _2))),
      release_count_(0) {
}

class ReaderUnitTest : public ::testing::Test {
protected:
    ReaderUnitTest() :
        session_(NULL, NULL) {}

    ReaderTestSession session_;
};

static void CreateFakeMessage(uint8_t *data, size_t length) {
    assert(length > 18);
    memset(data, 0xff, 16);
    put_value(data + 16, 2, length);
    memset(data + 18, 0, length - 18);
}

#define ARRAYLEN(_Array)    sizeof(_Array) / sizeof(_Array[0])

TEST_F(ReaderUnitTest, StreamRead) {
    uint8_t stream[4096];
    int sizes[] = { 100, 400, 80, 110, 40, 60 };
    uint8_t *data = stream;
    for (size_t i = 0; i < ARRAYLEN(sizes); i++) {
        CreateFakeMessage(data, sizes[i]);
        data += sizes[i];
    }
    vector<mutable_buffer> buf_list;
    int segments[] = {
        100 + 20,       // complete msg + start (with header)
        200,            // mid part
        180 + 80 + 10,  // end + start full msg + start (no header)
        7,              // still no header
        10,             // header but no end
        83,             // end of message.
        40,
        60
    };
    data = stream;
    for (size_t i = 0; i < ARRAYLEN(segments); i++) {
        buf_list.push_back(mutable_buffer(data, segments[i]));
        data += segments[i];
    }
    for (size_t i = 0; i < buf_list.size(); i++) {
        session_.Read(buf_list[i]);
    }

    int i = 0;
    for (vector<int>::const_iterator iter = session_.begin();
         iter != session_.end(); ++iter) {
        TASK_UTIL_EXPECT_EQ(sizes[i], *iter);
        i++;
    }
    TASK_UTIL_EXPECT_EQ(ARRAYLEN(sizes), i);
    TASK_UTIL_EXPECT_EQ(buf_list.size(), (size_t) session_.release_count());
}

}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
