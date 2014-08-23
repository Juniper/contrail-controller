/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/lexical_cast.hpp>
#include <pthread.h>

#include "testing/gunit.h"
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "io/event_manager.h"
#include "io/udp_server.h"
#include "io/test/event_manager_test.h"
#include "io/io_log.h"

namespace {

using boost::asio::buffer_cast;
using boost::asio::mutable_buffer;
using boost::asio::ip::udp;

class EchoServer: public UdpServer {
 public:
    explicit EchoServer(EventManager *evm) : UdpServer(evm),
        tx_count_(0), rx_count_(0) {
    }

    ~EchoServer() { }

    void HandleReceive(boost::asio::const_buffer &recv_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) {
        UDP_UT_LOG_DEBUG("EchoServer rx " << bytes_transferred << "(" <<
            error << ") from " << remote_endpoint);
        if (!error || error == boost::asio::error::message_size) {
            if (error == boost::asio::error::message_size) {
                UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA, "message_size "<< error);
            }

            rx_count_ += bytes_transferred;

            std::ostringstream s;
            boost::system::error_code e;
            s << "Got [" << bytes_transferred << "]<" << GetLocalEndPoint()
              << "<-" << remote_endpoint << ">\"";
            {
                const char *p = buffer_cast<const char *>(recv_buffer);
                for (size_t i = 0; i < bytes_transferred; i++, p++)
                    s << *p;
                s << "\"\n";
            }
            DeallocateBuffer(recv_buffer);
            std::string snd = s.str();
            mutable_buffer send = AllocateBuffer(snd.length());
            {
                char *p = buffer_cast<char *>(send);
                std::copy(snd.begin(), snd.end(), p);
            }

            StartSend(remote_endpoint, snd.length(), send);
            StartReceive();
        } else {
            DeallocateBuffer(recv_buffer);
        }
    }

    void HandleSend(boost::asio::const_buffer send_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) {
        tx_count_ += bytes_transferred;
        UDP_UT_LOG_DEBUG("EchoServer sent " << bytes_transferred << "(" <<
            error << ")\n");
        DeallocateBuffer(send_buffer);
    }

    int GetTxBytes() { return tx_count_; }
    int GetRxBytes() { return rx_count_; }

 private:
    int tx_count_;
    int rx_count_;
};

class EchoClient : public UdpServer {
 public:
    explicit EchoClient(boost::asio::io_service *io_service,
            int buffer_size = kDefaultBufferSize) :
        UdpServer(io_service, buffer_size),
        tx_count_(0), rx_count_(0), client_rx_done_(false) {
    }

    void Send(const std::string &snd, udp::endpoint to) {
        mutable_buffer send = AllocateBuffer(snd.length());
        char *p = buffer_cast<char *>(send);
        std::copy(snd.begin(), snd.end(), p);
        UDP_UT_LOG_DEBUG("EchoClient sending '" << snd << "' to " << to);
        StartSend(to, snd.length(), send);
        StartReceive();
        snd_buf_ = snd;
    }

    void Send(const std::string &snd, std::string ipaddress,
        unsigned short port) {
        boost::system::error_code ec;
        Send(snd, udp::endpoint(boost::asio::ip::address::from_string(
                        ipaddress, ec), port));
    }

    void HandleSend(boost::asio::const_buffer send_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) {
        tx_count_ += bytes_transferred;
        UDP_UT_LOG_DEBUG("EchoClient sent " << bytes_transferred << "(" <<
            error << ")\n");
    }

    void HandleReceive(boost::asio::const_buffer &recv_buffer,
            udp::endpoint remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) {
        rx_count_ += bytes_transferred;
        std::string b;
        const uint8_t *p = buffer_cast<const uint8_t *>(recv_buffer);
        std::copy(p, p+bytes_transferred, std::back_inserter(b));
        UDP_UT_LOG_DEBUG("rx (" << remote_endpoint << ")[" << error << "](" <<
            bytes_transferred << ")\"" << b << "\"\n");
        client_rx_done_ = true;
    }

    int GetTxBytes() { return tx_count_; }
    int GetRxBytes() { return rx_count_; }
    bool client_rx_done() { return client_rx_done_; }

 private:
    int tx_count_;
    int rx_count_;
    std::string snd_buf_;
    bool client_rx_done_;
};

class EchoServerTest : public ::testing::Test {
 protected:
    EchoServerTest() { }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_ = new EchoServer(evm_.get());
        client_ = new EchoClient(evm_.get()->io_service());
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        task_util::WaitForIdle();
        evm_->Shutdown();
        task_util::WaitForIdle();
        client_->Shutdown();
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();
        UdpServerManager::DeleteServer(server_);
        UdpServerManager::DeleteServer(client_);
        task_util::WaitForIdle();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
        task_util::WaitForIdle();
    }

    std::auto_ptr<ServerThread> thread_;
    EchoServer *server_;
    EchoClient *client_;
    std::auto_ptr<EventManager> evm_;
};


class EchoServerBranchTest : public ::testing::Test {
 protected:
    EchoServerBranchTest() : _test_run(false) {}

    virtual void SetUp() {
        UDP_UT_LOG_DEBUG("UDP branch test setup: " << _test_run);
    }
    virtual void TearDown() {
        UDP_UT_LOG_DEBUG("UDP branch test teardown: " << _test_run);
    }
    void TestCreation() {
        boost::asio::io_service io_service;
        UdpServer *s = new UdpServer(&io_service);
        mutable_buffer b = s->AllocateBuffer();
        boost::system::error_code ec;
        udp::endpoint ep(boost::asio::ip::address::from_string(
                        "127.0.0.1", ec), 5555);
        s->StartSend(ep, (size_t)10, b);  // fail
        s->StartReceive();
        s->Initialize("127.0.0.1", 0);
        s->Initialize(0);  // hit error path.. shd ret
        s->Shutdown();
        UdpServerManager::DeleteServer(s);
        UDP_UT_LOG_DEBUG("UDP branch test Shutdown: " << _test_run);
    }

 private:
    bool _test_run;
};

TEST_F(EchoServerTest, Basic) {
    server_->Initialize(0);
    task_util::WaitForIdle();
    thread_->Start();
    server_->StartReceive();
    udp::endpoint server_endpoint = server_->GetLocalEndPoint();
    UDP_UT_LOG_DEBUG("UDP Server: " << server_endpoint);
    int port = server_endpoint.port();
    ASSERT_LT(0, port);
    UDP_UT_LOG_DEBUG("UDP Server port: " << port);

    client_->Initialize(0);

    boost::system::error_code ec;
    client_->Send("Test udp", udp::endpoint(
                boost::asio::ip::address::from_string("127.0.0.1", ec), port));
    // Wait till client get resp
    TASK_UTIL_EXPECT_TRUE(client_->client_rx_done());

    TASK_UTIL_ASSERT_EQ(client_->GetTxBytes(), server_->GetRxBytes());
    TASK_UTIL_ASSERT_EQ(client_->GetRxBytes(), server_->GetTxBytes());
}

TEST_F(EchoServerBranchTest, Basic) {
    TestCreation();
}

}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
