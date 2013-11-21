#include <boost/bind.hpp>
#include <boost/asio.hpp>
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
bool client_rx_done = false;

class EchoServer: public UDPServer
{
  public:
    explicit EchoServer (EventManager *evm): UDPServer(evm),
        tx_count_(0), rx_count_(0) 
    {
    }
    ~EchoServer () { }
    void HandleReceive (mutable_buffer *recv_buffer, 
            udp::endpoint *remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) {
        if (!error || error == boost::asio::error::message_size) {
            if (error == boost::asio::error::message_size) {
                UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA, "message_size "<<error);
            }
            
            rx_count_ += bytes_transferred;

            std::ostringstream s;
            boost::system::error_code e;
            s << "Got [" << bytes_transferred << "]<" << GetLocalEndPoint()
              << "<-" << *remote_endpoint << ">\"";
            char *p = buffer_cast<char *> (*recv_buffer);
            for (size_t i=0; i < bytes_transferred; i++, p++)
                s << *p;
            s << "\"\n";
            DeallocateBuffer (recv_buffer);
            std::string snd = s.str();
            mutable_buffer *send = AllocateBuffer (snd.length());
            p = buffer_cast<char *> (*send);
            std::copy (snd.begin (), snd.end(), p);

            StartSend (remote_endpoint, snd.length(), send);
            StartReceive();
        } else {
            DeallocateBuffer (recv_buffer);
            DeallocateEndPoint (remote_endpoint);
        }
    }

    void HandleSend (mutable_buffer *send_buffer,
            udp::endpoint *remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) 
    {
        tx_count_ += bytes_transferred;
        UDP_UT_LOG_DEBUG( "sent " << bytes_transferred << "(" <<
            error << ")\n");
        DeallocateBuffer (send_buffer);
        DeallocateEndPoint (remote_endpoint);
    }

    int GetTxBytes () { return tx_count_; }
    int GetRxBytes () { return rx_count_; }

    private:
    int tx_count_;
    int rx_count_;

};

class EchoClient : public UDPServer
{
    public:
    explicit EchoClient (boost::asio::io_service& io_service,
            int buffer_size=kDefaultBufferSize): 
        UDPServer (io_service, buffer_size),
        tx_count_(0), rx_count_(0)
    {
    }
    ~EchoClient () { }
    
    void Send (std::string snd, udp::endpoint to)
    {
        mutable_buffer *send = AllocateBuffer (snd.length());
        char *p = buffer_cast<char *> (*send);
        std::copy (snd.begin (), snd.end(), p);
        udp::endpoint *ep = new udp::endpoint(to);
        StartSend (ep, snd.length(), send);
        StartReceive ();
        snd_buf_ = snd;
    }
    void Send (std::string snd, std::string ipaddress, short port)
    {
        udp::endpoint *ep = AllocateEndPoint (ipaddress, port);
        mutable_buffer *send = AllocateBuffer (snd.length());
        char *p = buffer_cast<char *> (*send);
        std::copy (snd.begin (), snd.end(), p);
        StartSend (ep, snd.length(), send);
        StartReceive ();
        snd_buf_ = snd;
    }
    void HandleSend (mutable_buffer *send_buffer,
            udp::endpoint *remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error) 
    {
        tx_count_ += bytes_transferred;
        UDP_UT_LOG_DEBUG( "sent " << bytes_transferred << "(" <<
            error << ")\n");
        DeallocateBuffer (send_buffer);
        DeallocateEndPoint (remote_endpoint);
    }
    void HandleReceive (mutable_buffer *recv_buffer, 
            udp::endpoint *remote_endpoint, std::size_t bytes_transferred,
            const boost::system::error_code& error)
    {
        rx_count_ += bytes_transferred;
        std::string b;
        uint8_t *p = buffer_cast<uint8_t *>(*recv_buffer);
        std::copy(p, p+bytes_transferred, std::back_inserter(b));
        UDP_UT_LOG_DEBUG( "rx (" << *remote_endpoint << ")[" << error << "](" <<
            bytes_transferred << ")\"" << b << "\"\n");
        client_rx_done = true;
    }
    

    int GetTxBytes () { return tx_count_; }
    int GetRxBytes () { return rx_count_; }

    private:
        int tx_count_;
        int rx_count_;
        std::string snd_buf_;
};

class EchoServerTest : public ::testing::Test {
    protected:
    EchoServerTest () { }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        server_ = new EchoServer(evm_.get());
        client_ = new EchoClient (*evm_->io_service ());
        thread_.reset(new ServerThread(evm_.get()));
    }

    virtual void TearDown() {
        server_->Reset ();
        if (client_)
            client_->Reset ();
        evm_->Shutdown();
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
    client_->Send ("Test udp", server_endpoint);
    TASK_UTIL_EXPECT_TRUE(client_rx_done); // wait till client get resp

    TASK_UTIL_ASSERT_EQ (client_->GetTxBytes(), server_->GetRxBytes());
    TASK_UTIL_ASSERT_EQ (client_->GetRxBytes(), server_->GetTxBytes());
}

}  // namespace

int main(int argc, char **argv) {
    LoggingInit();
    Sandesh::SetLoggingParams(true, "", SandeshLevel::UT_DEBUG);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}



