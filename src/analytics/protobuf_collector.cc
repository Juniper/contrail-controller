//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <io/io_types.h>

#include "analytics/collector_uve_types.h"
#include "analytics/db_handler.h"
#include "analytics/protobuf_collector.h"

ProtobufCollector::ProtobufCollector(EventManager *evm,
    uint16_t protobuf_udp_port, DbHandlerPtr db_handler) :
    server_(new protobuf::ProtobufServer(evm, protobuf_udp_port,
        boost::bind(&DbHandler::StatTableInsert, db_handler,
            _1, _2, _3, _4, _5, GenDb::GenDbIf::DbAddColumnCb()))) {
}

ProtobufCollector::~ProtobufCollector() {
}

bool ProtobufCollector::Initialize() {
    server_->Initialize();
    return true;
}

void ProtobufCollector::Shutdown() {
    server_->Shutdown();
}

void ProtobufCollector::SendStatistics(const std::string &name) {
    std::vector<SocketIOStats> v_tx_stats;
    std::vector<SocketIOStats> v_rx_stats;
    std::vector<SocketEndpointMessageStats> v_rx_msg_stats;
    server_->GetStatistics(&v_tx_stats, &v_rx_stats, &v_rx_msg_stats);
    ProtobufCollectorStats *snh(PROTOBUF_COLLECTOR_STATS_CREATE());
    snh->set_name(name);
    snh->set_tx_socket_stats(v_tx_stats);
    snh->set_rx_socket_stats(v_rx_stats);
    snh->set_rx_message_stats(v_rx_msg_stats);
    PROTOBUF_COLLECTOR_STATS_SEND_SANDESH(snh);
}
