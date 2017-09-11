/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <memory>

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <boost/bind.hpp>
#include <tbb/recursive_mutex.h>

#include "testing/gunit.h"

#include "base/logging.h"
#include "base/parse_object.h"

#include "base/task.h"
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
    int GetTotal() const { return total_; }
    void ResetTotal() { total_ = 0; }
    virtual void WriteReady(const boost::system::error_code &error) {
        called = true;
    }
    bool called;

protected:
    virtual ~EchoSession() {
    }

    virtual void OnRead(Buffer buffer) {
        // const u_int8_t *data = BufferData(buffer);
        const size_t len = BufferSize(buffer);
        TCP_UT_LOG_DEBUG("Received " << len << " bytes");
        total_ += len;
    }
private:
    void OnEvent(TcpSession *session, Event event) {
        if (event == CLOSE) {
            TCP_UT_LOG_DEBUG("Event Close");
        }
        if (event == ACCEPT) {
            TCP_UT_LOG_DEBUG("Event Accept");
        }
        if (event == CONNECT_COMPLETE) {
            TCP_UT_LOG_DEBUG("Event ConnectComplete");
        }
        if (event == CONNECT_FAILED) {
            TCP_UT_LOG_DEBUG("Event ConnectFailed");
        }
        if (event == EVENT_NONE) {
            TCP_UT_LOG_DEBUG("Event None");
        }
    }
    int total_;
};

class EchoServer : public TcpServer {
public:
    explicit EchoServer(EventManager *evm) : TcpServer(evm), session_(NULL) {
    }
    ~EchoServer() {
    }

    virtual TcpSession *AllocSession(Socket *socket) {
        session_ = new EchoSession(this, socket);
        return session_;
    }

    void SessionReset() {
        if (session_) {
            DeleteSession(session_);
        }
        session_ = NULL;
    }
    void ConnectTest(int port) {
        boost::system::error_code ec;
        boost::asio::ip::tcp::endpoint endpoint;
        endpoint.address(boost::asio::ip::address::from_string("127.0.0.1",
                                                               ec));
        endpoint.port(port);
        return TcpServer::Connect(session_, endpoint);
    }

    bool Send(const u_int8_t *data, size_t size, size_t *actual) {
        return session_->Send(data, size, actual);
    }

    EchoSession *GetSession() const { return session_; }
    void SetSocketOptions() { session_->SetSocketOptions(); }

private:
    EchoSession *session_;
};

/*
 * To simulate write blocked, we need to send more chunks of
 * data to the kernel.
 */
static const int kSendBufferSize = (1 *1024);
EchoSession::EchoSession(EchoServer *server, Socket *socket)
    : TcpSession(server, socket, true, kSendBufferSize), called(false), total_(0) {
    set_observer(boost::bind(&EchoSession::OnEvent, this, _1, _2));
}

class EchoServerTest : public ::testing::Test {
protected:
    EchoServerTest() {
    }
    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_ = new EchoServer(evm_.get());
        client_ = new EchoServer(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        if (server_->GetSession()) {
            server_->GetSession()->Close();
        }
        if (client_->GetSession()) {
            client_->GetSession()->Close();
        }
        task_util::WaitForIdle();

        server_->Shutdown();
        server_->SessionReset();
        client_->Shutdown();
        client_->SessionReset();
        task_util::WaitForIdle();

        TcpServerManager::DeleteServer(server_);
        server_ = NULL;
        TcpServerManager::DeleteServer(client_);
        client_ = NULL;

        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    auto_ptr<ServerThread> thread_;
    EchoServer *server_;
    EchoServer *client_;
    auto_ptr<EventManager> evm_;
};

TEST_F(EchoServerTest, Basic) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization
    int port = server_->GetPort();
    ASSERT_LT(0, port);
    TCP_UT_LOG_DEBUG("Server port: " << port);

    client_->CreateSession();
    client_->EchoServer::ConnectTest(port);
    client_->SetSocketOptions();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(client_->GetSession()->IsEstablished());
    TASK_UTIL_ASSERT_TRUE((server_->GetSession() != NULL));

    const char msg[] = "Test Message";
    bool res = client_->Send((const u_int8_t *) msg, sizeof(msg), NULL);

    TASK_UTIL_ASSERT_EQ(sizeof(msg), server_->GetSession()->GetTotal());
    server_->GetSession()->ResetTotal();

    char msg2[4096];
    int i = 0;
    size_t sent = 0;
    int total;

    //
    // TODO: Test needs to block the receiver.
    //
    {
    while (res) {
        i++;
        res = client_->Send((const u_int8_t *) msg2, sizeof(msg2), &sent);
    }

    total = sizeof(msg2) * i;
    EXPECT_NE(total, server_->GetSession()->GetTotal());
    TCP_UT_LOG_DEBUG("Total bytes txed: " << total);
    TCP_UT_LOG_DEBUG("Total bytes rxed: " << server_->GetSession()->GetTotal());

    // try sending more to build internal buffer_queue_
    for (int i = 0 ; i < 5; i++) {
        res = client_->Send((const u_int8_t *) msg2, sizeof(msg2), &sent);
        total += sizeof(msg2);
    }

    }
    TASK_UTIL_ASSERT_EQ(total, server_->GetSession()->GetTotal());
    TCP_UT_LOG_DEBUG("Total bytes txed: " << total);
    TCP_UT_LOG_DEBUG("Total bytes rxed: " << server_->GetSession()->GetTotal());
    server_->GetSession()->ResetTotal();

    TCP_UT_LOG_DEBUG("----  Second iteration ---- ");
    res = true;
    i = 0;
    client_->GetSession()->called = false;

    //
    // TODO: block the receiver
    //
    {
    // send more. Test writeready callback.
    while (res) {
        i++;
        res = client_->Send((const u_int8_t *) msg2, sizeof(msg2), NULL);
    }

    total = sizeof(msg2) * i;
    for (i = 0 ; i < 5; i++) {
        res = client_->Send((const u_int8_t *) msg2, sizeof(msg2), &sent);
        total += sizeof(msg2);
    }

    TASK_UTIL_ASSERT_EQ(total, server_->GetSession()->GetTotal());
    TCP_UT_LOG_DEBUG("2nd iteration. Total bytes sent: " << total);
    TCP_UT_LOG_DEBUG("2nd iteration, Total bytes rxed: " << server_->GetSession()->GetTotal());
    }

    // Wait for write ready
    TASK_UTIL_ASSERT_TRUE(client_->GetSession()->called);
    client_->GetSession()->called = false;

    // WriteReady is called. Ready to send again.
    res = client_->Send((const u_int8_t *) msg2, sizeof(msg2), NULL);
    total += sizeof(msg2);
    task_util::WaitForIdle();
    TASK_UTIL_ASSERT_EQ(total, server_->GetSession()->GetTotal());
    server_->GetSession()->ResetTotal();
}

TEST_F(EchoServerTest, ReadInterrupt) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();		// Must be called after initialization
    int port = server_->GetPort();
    ASSERT_LT(0, port);
    TCP_UT_LOG_DEBUG("Server port: " << port);

    client_->CreateSession();
    client_->EchoServer::ConnectTest(port);
    client_->SetSocketOptions();

    // Stop scheduler to prevent running Reader Task
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();

    TASK_UTIL_ASSERT_TRUE((server_->GetSession() != NULL));
    TASK_UTIL_ASSERT_TRUE(client_->GetSession()->IsEstablished());
    TASK_UTIL_ASSERT_TRUE(server_->GetSession()->IsEstablished());
    const char msg[] = "Test Message";

    (void) client_->Send((const u_int8_t *) msg, sizeof(msg), NULL);

    client_->GetSession()->Close();
    client_->SessionReset();

    // The session will be deleted only after the read error
    TASK_UTIL_ASSERT_EQ(0, server_->GetSession()->GetTotal());

    // Start the scheduler to run the Reader Task
    scheduler->Start();
    task_util::WaitForIdle();

    // Reader task will read the buffer and close the session on subsequent read error
    TASK_UTIL_ASSERT_TRUE(!server_->GetSession()->IsEstablished());
    EXPECT_EQ(server_->GetSession()->GetTotal(), 13);
}

TEST_F(EchoServerTest, SendAfterClose) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();
    int port = server_->GetPort();
    ASSERT_LT(0, port);
    TCP_UT_LOG_DEBUG("Server port: " << port);

    client_->CreateSession();
    client_->EchoServer::ConnectTest(port);

    TASK_UTIL_ASSERT_TRUE((server_->GetSession() != NULL));
    TASK_UTIL_ASSERT_TRUE(client_->GetSession()->IsEstablished());

    char msg[4096];
    memset(msg, 0xcd, sizeof(msg));
    for (int i = 0; i < 16; i++) {
        client_->Send((const u_int8_t *) msg, sizeof(msg), NULL);
    }
    client_->GetSession()->Close();

    bool res = client_->Send((const u_int8_t *) msg, sizeof(msg), NULL);
    EXPECT_FALSE(res);
    // Wait for all pending writes to complete and socket close
    // being called.
    TASK_UTIL_ASSERT_TRUE(client_->GetSession()->IsClosed() == true);
    // Ensure all data is received at other end before close was sent
    // and no more data after.
    TASK_UTIL_ASSERT_EQ(sizeof(msg) * 16,
                        server_->GetSession()->GetTotal());
    TASK_UTIL_ASSERT_TRUE((server_->GetSession() != NULL));
    TASK_UTIL_ASSERT_NE(0, server_->GetSession()->GetTotal());
}

TEST_F(EchoServerTest, DeferRead) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();
    int port = server_->GetPort();
    ASSERT_LT(0, port);
    TCP_UT_LOG_DEBUG("Server port: " << port);
    client_->CreateSession();
    client_->EchoServer::ConnectTest(port);
    client_->SetSocketOptions();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(client_->GetSession()->IsEstablished());
    TASK_UTIL_ASSERT_TRUE((server_->GetSession() != NULL));
    // Defer reader
    server_->GetSession()->SetDeferReader(true);
    const char msg[] = "Test Message before Defer";
    size_t sent = 0;
    bool res = client_->Send((const u_int8_t *) msg, sizeof(msg), &sent);
    EXPECT_TRUE(res);
    EXPECT_EQ(sizeof(msg), sent);
    TASK_UTIL_ASSERT_EQ(sizeof(msg), server_->GetSession()->GetTotal());
    server_->GetSession()->ResetTotal();
    // Verify read statistics
    SocketIOStats rx_stats;
    server_->GetSession()->GetRxSocketStats(&rx_stats);
    EXPECT_EQ(1, rx_stats.blocked_count);
    EXPECT_EQ(sizeof(msg), rx_stats.bytes);
    EXPECT_EQ("00:00:00", rx_stats.blocked_duration);
    // Verify that message is not read since reader is blocked
    size_t sent1 = 0;
    const char msg1[] = "Test Message after Defer";
    res = client_->Send((const u_int8_t *)msg1, sizeof(msg1), &sent1);
    EXPECT_TRUE(res);
    EXPECT_EQ(sizeof(msg1), sent1);
    TASK_UTIL_ASSERT_EQ(0, server_->GetSession()->GetTotal());
    // Undefer reader
    server_->GetSession()->SetDeferReader(false);
    TASK_UTIL_ASSERT_EQ(sizeof(msg1), server_->GetSession()->GetTotal());
    // Verify read statistics
    SocketIOStats rx_stats1;
    server_->GetSession()->GetRxSocketStats(&rx_stats1);
    EXPECT_EQ(1, rx_stats1.blocked_count);
    EXPECT_EQ(sizeof(msg) + sizeof(msg1), rx_stats1.bytes);
    EXPECT_NE("00:00:00", rx_stats1.blocked_duration);
}

}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
