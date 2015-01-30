//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <io/io_types.h>

#include "analytics/collector_uve_types.h"
#include "analytics/db_handler.h"
#include "analytics/protobuf_collector.h"

const std::string ProtobufCollector::kDbName("Google Protocol Buffer");
const int ProtobufCollector::kDbTaskInstance(-1);
const std::string ProtobufCollector::kDbTaskName("protobuf_collector::Db");

ProtobufCollector::ProtobufCollector(EventManager *evm,
    uint16_t protobuf_udp_port,
    const std::vector<std::string> &cassandra_ips,
    const std::vector<int> &cassandra_ports, const DbHandler::TtlMap& ttl_map) :
    db_initializer_(new DbHandlerInitializer(evm, kDbName, kDbTaskInstance,
        kDbTaskName, boost::bind(&ProtobufCollector::DbInitializeCb, this),
        cassandra_ips, cassandra_ports, ttl_map)),
    server_(new protobuf::ProtobufServer(evm, protobuf_udp_port,
        boost::bind(&DbHandler::StatTableInsert,
            db_initializer_->GetDbHandler(), _1, _2, _3, _4, _5))) {
}

ProtobufCollector::~ProtobufCollector() {
}

bool ProtobufCollector::Initialize() {
    return db_initializer_->Initialize();
}

void ProtobufCollector::Shutdown() {
    server_->Shutdown();
    db_initializer_->Shutdown();
}

void ProtobufCollector::DbInitializeCb() {
    server_->Initialize();
}

void ProtobufCollector::SendStatistics(const std::string &name) {
    ProtobufCollectorStats stats;
    stats.set_name(name);
    std::vector<SocketIOStats> v_tx_stats;
    std::vector<SocketIOStats> v_rx_stats;
    std::vector<SocketEndpointMessageStats> v_rx_msg_stats;
    server_->GetStatistics(&v_tx_stats, &v_rx_stats, &v_rx_msg_stats);
    stats.set_tx_socket_stats(v_tx_stats);
    stats.set_rx_socket_stats(v_rx_stats);
    stats.set_rx_message_stats(v_rx_msg_stats);
    ProtobufCollectorStatsUve::Send(stats);
}
