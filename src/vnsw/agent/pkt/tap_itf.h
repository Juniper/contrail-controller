/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_tap_intf_hpp
#define vnsw_agent_pkt_tap_intf_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>

#define MAC_ALEN 6

class TapDescriptor {
public:
    TapDescriptor(const std::string &name);
    virtual ~TapDescriptor();
    int Id() { return fd_; }
    unsigned char *MacAddr() { return mac_; }
private:
    int fd_;
    unsigned char mac_[MAC_ALEN];
};

class TapInterface {
public:
    enum { max_packet_size = 9060 };
    typedef boost::function<void(uint8_t*, std::size_t)> PktReadCallback;

    TapInterface(const std::string &name, boost::asio::io_service &io, 
                 PktReadCallback cb);
    virtual ~TapInterface() { 
        if (read_buf_) {
            delete [] read_buf_;
        }
    }

    virtual void AsyncWrite(uint8_t *buf, std::size_t len);
    unsigned char *MacAddr() { return tap_.MacAddr(); }

protected:
    void SetupAsio();
    void SetupTap(const std::string& name);
    void AsyncRead();
    void ReadHandler(const boost::system::error_code &err, std::size_t length);
    void WriteHandler(const boost::system::error_code &err, std::size_t length,
		              uint8_t *buf);

    uint8_t *read_buf_;
    PktReadCallback pkt_handler_;
    TapDescriptor tap_;
    boost::asio::posix::stream_descriptor input_;
};

class TestTapInterface : public TapInterface {
public:
    class TestPktHandler {
    public:
        typedef boost::function<void(uint8_t*, std::size_t)> Callback;

        void RegisterCallback(Callback cb) { cb_ = cb; }
        uint32_t GetPktCount() { return count_; }

        void TestPktSend(uint8_t *buf, std::size_t len) {
            write_sock_.send_to(boost::asio::buffer(buf, len), agent_ep_, 0, ec_);
        }

        uint16_t GetTestPktHandlerPort() { 
            boost::system::error_code ec;
            return test_sock_.local_endpoint(ec).port();
        }

        TestPktHandler(uint32_t tap_port) : count_(0), 
          cb_(boost::bind(&TestPktHandler::TestPktReceive, this, _1, _2)), 
          test_ep_(boost::asio::ip::address::from_string("127.0.0.1", ec_), 0), 
          agent_ep_(boost::asio::ip::address::from_string("127.0.0.1", ec_), tap_port),
          test_sock_(*Agent::GetInstance()->GetEventManager()->io_service()),
          write_sock_(*Agent::GetInstance()->GetEventManager()->io_service()) {
            test_sock_.open(boost::asio::ip::udp::v4(), ec_);
            test_sock_.bind(test_ep_, ec_);
            write_sock_.open(boost::asio::ip::udp::v4(), ec_);
            AsyncRead();
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
                boost::asio::buffer(pkt_buf_, TapInterface::max_packet_size),
                boost::bind(&TestPktHandler::TestPktReadHandler, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        }

        void TestPktReceive(uint8_t *buf, std::size_t len) {
        }

        uint32_t count_;
        Callback cb_;
        uint8_t pkt_buf_[TapInterface::max_packet_size];
        boost::system::error_code ec_;
        boost::asio::ip::udp::endpoint test_ep_;
        boost::asio::ip::udp::endpoint agent_ep_;
        boost::asio::ip::udp::socket test_sock_;
        boost::asio::ip::udp::socket write_sock_;
    };

    TestTapInterface(const std::string &name, boost::asio::io_service &io, 
                     PktReadCallback cb) 
                   : TapInterface(name, io, cb), test_write_(io) {
        test_write_.open(boost::asio::ip::udp::v4(), ec_);
        assert(ec_ == 0);

        struct sockaddr_in sin;
        socklen_t len = sizeof(sin);
        memset((char *) &sin, 0, len);
        if (getsockname(tap_.Id(), (sockaddr *) &sin, &len) == -1) {
            LOG(ERROR, "Packet Test Tap : Unable to get socket info");
            assert(0);
        }
        agent_rcv_port_ = ntohs(sin.sin_port);

        test_pkt_handler_ = new TestPktHandler(agent_rcv_port_);
        test_rcv_ep_.address(
                boost::asio::ip::address::from_string("127.0.0.1", ec_));
        test_rcv_ep_.port(test_pkt_handler_->GetTestPktHandlerPort());
    }
    virtual ~TestTapInterface() { 
        delete test_pkt_handler_;
    }
    void AsyncWrite(uint8_t *buf, std::size_t len) {
        test_write_.async_send_to(
            boost::asio::buffer(buf, len), test_rcv_ep_,
            boost::bind(&TestTapInterface::WriteHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, buf));
    }
    TestPktHandler *GetTestPktHandler() { return test_pkt_handler_; }

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
    uint32_t agent_rcv_port_;
    boost::system::error_code ec_;
    boost::asio::ip::udp::socket test_write_;
    boost::asio::ip::udp::endpoint test_rcv_ep_;
    TestPktHandler *test_pkt_handler_;
};

#endif // vnsw_agent_pkt_tap_intf_hpp
