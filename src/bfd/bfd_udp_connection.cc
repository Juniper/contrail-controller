/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "bfd/bfd_udp_connection.h"
#include "bfd/bfd_connection.h"
#include "bfd/bfd_control_packet.h"
#include "bfd/bfd_common.h"

#include <boost/asio.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/random.hpp>
#include "base/logging.h"

namespace BFD {


UDPConnectionManager::UDPRecvServer::UDPRecvServer(EventManager *evm, int recvPort) : UdpServer(evm) {
            Initialize(recvPort);
        }
        void UDPConnectionManager::UDPRecvServer::RegisterCallback(RecvCallback callback) {
            this->callback_ = callback;
        }

        void UDPConnectionManager::UDPRecvServer::HandleReceive(boost::asio::const_buffer &recv_buffer,
                boost::asio::ip::udp::endpoint remote_endpoint, std::size_t bytes_transferred,
                const boost::system::error_code& error) {
            LOG(DEBUG, __func__);

            if (bytes_transferred != (std::size_t)kMinimalPacketLength) {
                LOG(ERROR, __func__ <<  "Wrong packet size: " << bytes_transferred);
                return;
            }
            boost::scoped_ptr<ControlPacket> controlPacket(
                    ParseControlPacket(boost::asio::buffer_cast<const uint8_t *> (recv_buffer), bytes_transferred));
            if (controlPacket == NULL) {
                LOG(ERROR, __func__ <<  "Unable to parse packet");
            } else {
                controlPacket->sender_host = remote_endpoint.address();
                if (callback_)
                    callback_.get()(controlPacket.get());
            }
        }


        UDPConnectionManager::UDPCommunicator::UDPCommunicator(EventManager *evm, int remotePort)
                : UdpServer(evm), remotePort_(remotePort) {
            boost::random::uniform_int_distribution<> dist(kSendPortMin, kSendPortMax);
            for (int i = 0; i < 100 && GetServerState() != OK; ++i) {
                int localPort = dist(randomGen);
                LOG(DEBUG, "Bind UDPCommunicator to localport: " << localPort);
                Initialize(localPort);
                if (GetServerState() != OK) {
                    Reset();
                }
            }
            if (GetServerState() != OK) {
                LOG(ERROR, "Unable to bind to port in range: " << kSendPortMin << "-" << kSendPortMax);
            }
        }
     void UDPConnectionManager::UDPCommunicator::SendPacket(
                 const boost::asio::ip::address &dstAddr, const ControlPacket *packet) {
            LOG(DEBUG, __func__);

            boost::asio::mutable_buffer send = AllocateBuffer(kMinimalPacketLength);  // Packet without auth data
            int pktSize = EncodeControlPacket(packet, boost::asio::buffer_cast<uint8_t *> (send), kMinimalPacketLength);
            if (pktSize != kMinimalPacketLength) {
                LOG(ERROR, "Unable to encode packet");
            } else {
                boost::asio::ip::udp::endpoint dstEndpoint(dstAddr, remotePort_);
                StartSend(dstEndpoint, pktSize, send);
            }
        }


     UDPConnectionManager::UDPConnectionManager(EventManager *evm,  int recvPort, int remotePort)
               : udpRecv_(evm, recvPort), udpSend_(evm, remotePort) {
        if (udpRecv_.GetServerState() != UDPRecvServer::OK)
            LOG(ERROR, "Unable to listen on port " << recvPort);
        else
            udpRecv_.StartReceive();
    }

     void UDPConnectionManager::SendPacket(const boost::asio::ip::address &dstAddr, const ControlPacket *packet) {
        LOG(DEBUG, __func__);
        udpSend_.SendPacket(dstAddr, packet);
    }

    void UDPConnectionManager::RegisterCallback(RecvCallback callback) {
        udpRecv_.RegisterCallback(callback);
    }

    UDPConnectionManager::~UDPConnectionManager() {
        udpRecv_.Shutdown();
        udpSend_.Shutdown();
    }

}  // namespace BFD
