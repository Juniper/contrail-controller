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
    const std::vector<int> &cassandra_ports, const TtlMap& ttl_map,
    const std::string& cassandra_user, const std::string& cassandra_password,
    DbHandlerPtr global_dbhandler) {
    db_handler_ = global_dbhandler;
    server_.reset(new protobuf::ProtobufServer(evm, protobuf_udp_port,
        boost::bind(&DbHandler::StatTableInsert, db_handler_,
            _1, _2, _3, _4, _5, GenDb::GenDbIf::DbAddColumnCb())));
}

ProtobufCollector::~ProtobufCollector() {
}

bool ProtobufCollector::Initialize() {
    if (db_initializer_) {
        return db_initializer_->Initialize();
    }
    DbInitializeCb();
    return true;
}

void ProtobufCollector::Shutdown() {
    server_->Shutdown();
    if (db_initializer_) {
        db_initializer_->Shutdown();
    }
}

void ProtobufCollector::DbInitializeCb() {
    server_->Initialize();
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
    if (db_initializer_) {
        // Database statistics
        std::vector<GenDb::DbTableInfo> v_dbti, v_stats_dbti;
        GenDb::DbErrors dbe;
        db_handler_->GetStats(&v_dbti, &dbe, &v_stats_dbti);
        std::vector<GenDb::DbErrors> v_dbe;
        v_dbe.push_back(dbe);
        uint64_t db_queue_count, db_enqueues;
        db_handler_->GetStats(&db_queue_count, &db_enqueues);
        snh->set_db_table_info(v_dbti);
        snh->set_db_statistics_table_info(v_stats_dbti);
        snh->set_db_errors(v_dbe);
        snh->set_db_queue_count(db_queue_count);
        snh->set_db_enqueues(db_enqueues);
    }
    PROTOBUF_COLLECTOR_STATS_SEND_SANDESH(snh);
}
