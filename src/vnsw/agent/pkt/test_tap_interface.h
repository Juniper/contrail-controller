/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_test_tap_intf_hpp
#define vnsw_agent_pkt_test_tap_intf_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

// Tap interface used while not running with vrouter (unit test cases)
// Send to & receive from Agent using this class
class TestTapInterface : public TapInterface {
public:
    // Receive packets sent by Agent to the pkt0 interface and
    // calls back registered callback
    class TestPktHandler {
    public:
        typedef boost::function<void(uint8_t*, std::size_t)> Callback;

        TestPktHandler(Agent *agent, uint32_t tap_port) : count_(0), 
          cb_(boost::bind(&TestPktHandler::TestPktReceive, this, _1, _2)), 
          test_ep_(boost::asio::ip::address::from_string("127.0.0.1", ec_), 0), 
          agent_ep_(boost::asio::ip::address::from_string("127.0.0.1", ec_), tap_port),
          test_sock_(*agent->event_manager()->io_service()),
          write_sock_(*agent->event_manager()->io_service()) {
            test_sock_.open(boost::asio::ip::udp::v4(), ec_);
            test_sock_.bind(test_ep_, ec_);
            write_sock_.open(boost::asio::ip::udp::v4(), ec_);
            AsyncRead();
        }

        void RegisterCallback(Callback cb) { cb_ = cb; }
        uint32_t GetPktCount() const { return count_; }

        void TestPktSend(uint8_t *buf, std::size_t len) {
            write_sock_.send_to(boost::asio::buffer(buf, len), agent_ep_, 0, ec_);
        }

        uint16_t GetTestPktHandlerPort() { 
            boost::system::error_code ec;
            return test_sock_.local_endpoint(ec).port();
        }

    private:
        void TestPktReadHandler(const boost::system::error_code &error,
                                std::size_t length) {
            if (!error) {
                count_++;
                cb_(pkt_buf_, length);
                AsyncRead();
            }
        }

        void AsyncRead() {
            test_sock_.async_receive(
                boost::asio::buffer(pkt_buf_, TapInterface::kMaxPacketSize),
                boost::bind(&TestPktHandler::TestPktReadHandler, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        }

        void TestPktReceive(uint8_t *buf, std::size_t len) {
        }

        uint32_t count_;
        Callback cb_;
        uint8_t pkt_buf_[TapInterface::kMaxPacketSize];
        boost::system::error_code ec_;
        boost::asio::ip::udp::endpoint test_ep_;
        boost::asio::ip::udp::endpoint agent_ep_;
        boost::asio::ip::udp::socket test_sock_;
        boost::asio::ip::udp::socket write_sock_;
    };

    TestTapInterface(Agent *agent, const std::string &name,
                     boost::asio::io_service &io, PktReadCallback cb)
                   : TapInterface(agent, name, io, cb), agent_(agent),
                     agent_rcv_port_(-1), test_write_(io),
                     test_pkt_handler_(NULL) {
    }

    virtual ~TestTapInterface() {}

    TestPktHandler *GetTestPktHandler() const { return test_pkt_handler_.get(); }

    void SetupTap() {
        // Use a socket to receive packets in the test mode
        if ((tap_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot open test UDP socket");
            assert(0);
        }
        struct sockaddr_in sin;
        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = 0;
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(tap_fd_, (sockaddr *) &sin, sizeof(sin)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot bind to test UDP socket");
            assert(0);
        }

        test_write_.open(boost::asio::ip::udp::v4(), ec_);
        assert(ec_ == 0);

        socklen_t len = sizeof(sin);
        memset((char *) &sin, 0, len);
        if (getsockname(tap_fd_, (sockaddr *) &sin, &len) == -1) {
            LOG(ERROR, "Packet Test Tap : Unable to get socket info");
            assert(0);
        }
        agent_rcv_port_ = ntohs(sin.sin_port);

        test_pkt_handler_.reset(new TestPktHandler(agent_, agent_rcv_port_));
        test_rcv_ep_.address(
                boost::asio::ip::address::from_string("127.0.0.1", ec_));
        test_rcv_ep_.port(test_pkt_handler_->GetTestPktHandlerPort());
    }

    // Send to Agent
    void AsyncWrite(uint8_t *buf, std::size_t len) {
        test_write_.async_send_to(
            boost::asio::buffer(buf, len), test_rcv_ep_,
            boost::bind(&TestTapInterface::WriteHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, buf));
    }

private:
    void WriteHandler(const boost::system::error_code &err, std::size_t length,
		              uint8_t *buf) {
        if (err) {
            LOG(ERROR, "Packet Test Tap Error <" << 
                err.message() << "> sending packet");
            assert(0);
        }
        delete [] buf;
    }

    Agent *agent_;
    uint32_t agent_rcv_port_;
    boost::system::error_code ec_;
    boost::asio::ip::udp::socket test_write_;
    boost::asio::ip::udp::endpoint test_rcv_ep_;
    boost::scoped_ptr<TestPktHandler> test_pkt_handler_;
    DISALLOW_COPY_AND_ASSIGN(TestTapInterface);
};

#endif // vnsw_agent_pkt_test_tap_intf_hpp
