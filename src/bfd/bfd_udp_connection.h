/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_UDP_CONNECTION_H_
#define SRC_BFD_UDP_CONNECTION_H_

#include "bfd/bfd_connection.h"

#include <boost/optional.hpp>

#include "io/udp_server.h"

namespace BFD {
class UDPConnectionManager : public Connection {
 public:
    typedef boost::function<void(boost::asio::ip::udp::endpoint remote_endpoint,
                                 const boost::asio::const_buffer &recv_buffer,
                                 std::size_t bytes_transferred,
                                 const boost::system::error_code& error)>
                RecvCallback;

    UDPConnectionManager(EventManager *evm, int recvPort = kSingleHop,
                         int remotePort = kSingleHop);
    ~UDPConnectionManager();
    void RegisterCallback(RecvCallback callback);

    virtual void SendPacket(
        const boost::asio::ip::udp::endpoint &local_endpoint,
        const boost::asio::ip::udp::endpoint &remote_endpoint,
        const SessionIndex &session_index,
        const boost::asio::mutable_buffer &send, int pktSize);
    void SendPacket(boost::asio::ip::address remoteHost,
                    const ControlPacket *packet);
    virtual Server *GetServer() const;
    virtual void SetServer(Server *server);
    virtual void NotifyStateChange(const SessionKey &key, const bool &up);

 private:

    class UDPRecvServer : public UdpServer {
     public:
        UDPRecvServer(UDPConnectionManager *parent,
                      EventManager *evm, int recvPort);
        void RegisterCallback(RecvCallback callback);
        void HandleReceive(const boost::asio::const_buffer &recv_buffer,
                boost::asio::ip::udp::endpoint remote_endpoint,
                std::size_t bytes_transferred,
                const boost::system::error_code &error);

     private:
        UDPConnectionManager *parent_;
        boost::optional<RecvCallback> callback_;
    } *udpRecv_;

    class UDPCommunicator : public UdpServer {
     public:
        UDPCommunicator(EventManager *evm, int remotePort);
        // TODO(bfd) add multiple instances to randomize source port (RFC5881)
        int remotePort() const { return remotePort_; }
     private:
        const int remotePort_;
    } *udpSend_;

    Server *server_;
};
}  // namespace BFD

#endif  // SRC_BFD_UDP_CONNECTION_H_
