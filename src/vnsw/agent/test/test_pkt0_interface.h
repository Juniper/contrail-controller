/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_test_pkt0_interface_hpp
#define vnsw_agent_pkt_test_pkt0_interface_hpp

#include <string>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <pkt/vrouter_interface.h>
#include <pkt/packet_buffer.h>

// Tap interface used while not running with vrouter (unit test cases)
// Send to & receive from Agent using this class
class TestPkt0Interface : public VrouterControlInterface {
public:
    typedef boost::function<void(uint8_t*, std::size_t)> Callback;

    TestPkt0Interface(Agent *agent, const std::string &name,
                      boost::asio::io_service &io)
        : agent_(agent), name_(name), count_(0), pkt0_sock_(io),
        pkt0_client_sock_(io),
        client_cb_(boost::bind(&TestPkt0Interface::DummyClientReceive, this,
                               _1, _2)) {
    }

    virtual ~TestPkt0Interface() {
        if (pkt0_read_buff_) {
            delete pkt0_read_buff_;
        }
        if (pkt0_client_read_buff_) {
            delete pkt0_client_read_buff_;
        }
    }

    void ShutdownControlInterface() { }

    const std::string &Name() const { return name_; }

    void IoShutdownControlInterface() {
        pkt0_sock_.close();
        pkt0_client_sock_.close();
    }

    void InitControlInterface() {
        VrouterControlInterface::InitControlInterface();
        // Setup socket for pkt0 interface
        if ((pkt0_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot open test UDP socket");
            assert(0);
        }
        struct sockaddr_in sin;
        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(pkt0_fd_, (sockaddr *) &sin, sizeof(sin)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot bind to test UDP socket");
            assert(0);
        }

        socklen_t len = sizeof(sin);
        memset((char *) &sin, 0, len);
        if (getsockname(pkt0_fd_, (sockaddr *) &sin, &len) == -1) {
            LOG(ERROR, "Packet Test Tap : Unable to get socket info");
            assert(0);
        }
        pkt0_udp_port_ = ntohs(sin.sin_port);
        pkt0_ep_ = boost::asio::ip::udp::endpoint
            (boost::asio::ip::address::from_string("127.0.0.1"),pkt0_udp_port_);
        pkt0_sock_.assign(boost::asio::ip::udp::v4(), pkt0_fd_);

        // Setup socket for test_rcv socket
        if ((pkt0_client_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot open test UDP socket");
            assert(0);
        }

        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(pkt0_client_fd_, (sockaddr *) &sin, sizeof(sin)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot bind to test UDP socket");
            assert(0);
        }

        len = sizeof(sin);
        memset((char *) &sin, 0, len);
        if (getsockname(pkt0_client_fd_, (sockaddr *) &sin, &len) == -1) {
            LOG(ERROR, "Packet Test Tap : Unable to get socket info");
            assert(0);
        }
        pkt0_client_udp_port_ = ntohs(sin.sin_port);
        pkt0_client_ep_ = boost::asio::ip::udp::endpoint
            (boost::asio::ip::address::from_string("127.0.0.1"),
             pkt0_client_udp_port_);
        pkt0_client_sock_.assign(boost::asio::ip::udp::v4(), pkt0_client_fd_);

        Pkt0Read();
        Pkt0ClientRead();
    }

    void Pkt0WriteHandler(const boost::system::error_code &err,
                          std::size_t length, PacketBufferPtr pkt,
                          uint8_t *buff) {
        if (err) {
            LOG(ERROR, "Packet Test Tap Error <" <<
                err.message() << "> sending packet");
        }
        delete [] buff;
    }

    // Send from Agent
    int Send(uint8_t *buff, uint16_t buff_len, const PacketBufferPtr &pkt) {
        std::vector<boost::asio::const_buffer> buff_list;
        buff_list.push_back(boost::asio::buffer(buff, buff_len));
        buff_list.push_back(boost::asio::buffer(pkt->data(), pkt->data_len()));

        pkt0_sock_.async_send_to(buff_list, pkt0_client_ep_,
            boost::bind(&TestPkt0Interface::Pkt0WriteHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, pkt,
                        buff));
        return buff_len + pkt->data_len();
    }

    void DummyClientReceive(uint8_t *buff, std::size_t len) {
    }
    void RegisterCallback(Callback cb) { client_cb_ = cb; }
    uint32_t GetPktCount() const { return count_; }

    void Pkt0ClientWriteHandler(const boost::system::error_code &err,
                              std::size_t length, uint8_t *buff) {
        if (err) {
            LOG(ERROR, "Packet Test Tap Error <" <<
                err.message() << "> sending packet");
            assert(0);
        }
        delete [] buff;
    }

    void TxPacket(uint8_t *buff, std::size_t len) {
        pkt0_client_sock_.async_send_to(
            boost::asio::buffer(buff, len), pkt0_ep_,
            boost::bind(&TestPkt0Interface::Pkt0ClientWriteHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, buff));
    }

    bool ProcessFlowPacket(uint8_t *buff, uint32_t payload_len,
                           uint32_t buff_len) {
        PacketBufferPtr pkt(agent_->pkt()->packet_buffer_manager()->Allocate
            (PktHandler::RX_PACKET, buff, buff_len, buff_len - payload_len,
             payload_len, 0));
        VrouterControlInterface::Process(pkt);
        return true;
    }

private:
    void Pkt0ReadHandler(const boost::system::error_code &error,
                            std::size_t length) {
        if (!error) {
            PacketBufferPtr pkt(agent_->pkt()->packet_buffer_manager()->Allocate
                                (PktHandler::RX_PACKET, pkt0_read_buff_,
                                 ControlInterface::kMaxPacketSize, 0, length, 0));
            pkt0_read_buff_ = NULL;
            VrouterControlInterface::Process(pkt);
            Pkt0Read();
        }
    }

    void Pkt0Read() {
        pkt0_read_buff_ = new uint8_t[ControlInterface::kMaxPacketSize];
        pkt0_sock_.async_receive(
            boost::asio::buffer(pkt0_read_buff_,
                                ControlInterface::kMaxPacketSize),
            boost::bind(&TestPkt0Interface::Pkt0ReadHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }

    void Pkt0ClientReadHandler(const boost::system::error_code &error,
                               std::size_t length) {
        if (!error) {
            count_++;
            client_cb_(pkt0_client_read_buff_, length);
            delete pkt0_client_read_buff_;
            Pkt0ClientRead();
        }
    }

    void Pkt0ClientRead() {
        pkt0_client_read_buff_ = new uint8_t[ControlInterface::kMaxPacketSize];
        pkt0_client_sock_.async_receive(
            boost::asio::buffer(pkt0_client_read_buff_,
                                ControlInterface::kMaxPacketSize),
            boost::bind(&TestPkt0Interface::Pkt0ClientReadHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }

    Agent *agent_;
    std::string name_;
    int count_;

    // fd and endpoint representing pkt0 interface
    int pkt0_fd_;
    int pkt0_udp_port_;
    boost::asio::ip::udp::socket pkt0_sock_;
    boost::asio::ip::udp::endpoint pkt0_ep_;
    uint8_t *pkt0_read_buff_;

    // fd and endpoint representing validation modules
    int pkt0_client_fd_;
    int pkt0_client_udp_port_;
    boost::asio::ip::udp::socket pkt0_client_sock_;
    boost::asio::ip::udp::endpoint pkt0_client_ep_;
    uint8_t *pkt0_client_read_buff_;
    Callback client_cb_;

    DISALLOW_COPY_AND_ASSIGN(TestPkt0Interface);
};

#endif // vnsw_agent_pkt_test_pkt0_interface_hpp
