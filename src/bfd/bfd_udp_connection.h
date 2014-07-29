/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_UDP_CONNECTION_H_
#define BFD_UDP_CONNECTION_H_

#include "bfd/bfd_connection.h"

#include <boost/optional.hpp>
#include "io/udp_server.h"


namespace BFD {

class UDPConnectionManager :  public Connection {
 public:
    typedef boost::function<void(const ControlPacket *)> RecvCallback;

 private:
    static const int kRecvPortDefault = 3784;
    static const int kSendPortMin = 49152;
    static const int kSendPortMax = 65535;


    class UDPRecvServer : public UdpServer {
        boost::optional<RecvCallback> callback_;
     public:
        UDPRecvServer(EventManager *evm, int recvPort);
        void RegisterCallback(RecvCallback callback);
        void HandleReceive(boost::asio::const_buffer &recv_buffer,
                boost::asio::ip::udp::endpoint remote_endpoint, std::size_t bytes_transferred,
                const boost::system::error_code& error);
    } udpRecv_;

    class UDPCommunicator : public UdpServer {
        const int remotePort_;
     public:
        UDPCommunicator(EventManager *evm, int remotePort);
        virtual void SendPacket(const boost::asio::ip::address &dstAddr, const ControlPacket *packet);
    } udpSend_;  // TODO multiple instances to randomize udp source port

 public:
    UDPConnectionManager(EventManager *evm,  int recvPort = kRecvPortDefault, int remotePort = kRecvPortDefault);

    virtual void SendPacket(const boost::asio::ip::address &dstAddr, const ControlPacket *packet);
    void RegisterCallback(RecvCallback callback);
    ~UDPConnectionManager();
};
}  // namespace BFD

#endif /* BFD_UDP_CONNECTION_H_ */
