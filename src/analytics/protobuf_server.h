//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_PROTOBUF_SERVER_H_
#define ANALYTICS_PROTOBUF_SERVER_H_

#include <vector>

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

#include <analytics/stat_walker.h>

class SocketIOStats;
class SocketEndpointMessageStats;

namespace protobuf {

//
// Protobuf Server
//
class ProtobufServer {
 public:
    ProtobufServer(EventManager *evm, uint16_t udp_server_port,
        StatWalker::StatTableInsertFn stat_db_cb);
    virtual ~ProtobufServer();
    bool Initialize();
    void Shutdown();
    boost::asio::ip::udp::endpoint GetLocalEndpoint(
        boost::system::error_code *ec);
    void GetStatistics(std::vector<SocketIOStats> *v_tx_stats,
        std::vector<SocketIOStats> *v_rx_stats,
        std::vector<SocketEndpointMessageStats> *v_rx_msg_stats);
    void GetReceivedMessageStatistics(
        std::vector<SocketEndpointMessageStats> *v_rx_msg_stats);
    void SetShutdownLibProtobufOnDelete(bool shutdown_on_delete);

 private:
    bool shutdown_libprotobuf_on_delete_;
    class ProtobufServerImpl;
    ProtobufServerImpl *impl_;
};

}  // namespace protobuf

#endif  // ANALYTICS_PROTOBUF_SERVER_H_
