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
    bool use_collector_db_handler, DbHandler* collector_dbhandler) {
    if (!use_collector_db_handler) {
        db_initializer_.reset(new DbHandlerInitializer(evm, kDbName, kDbTaskInstance,
            kDbTaskName, boost::bind(&ProtobufCollector::DbInitializeCb, this),
            cassandra_ips, cassandra_ports, ttl_map, cassandra_user,
            cassandra_password));
        server_.reset(new protobuf::ProtobufServer(evm, protobuf_udp_port,
                    boost::bind(&DbHandler::StatTableInsert,
                    db_initializer_->GetDbHandler(), _1, _2, _3, _4, _5,_6),
                    db_initializer_->GetDbHandler()->GetName()));
    }
    else {
        db_handler_.reset(collector_dbhandler);
        server_.reset(new protobuf::ProtobufServer(evm, protobuf_udp_port,
        boost::bind(&DbHandler::StatTableInsert,
            collector_dbhandler, _1, _2, _3, _4, _5,_6),
        collector_dbhandler->GetName()));
    }
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
    // Database statistics
    std::vector<GenDb::DbTableInfo> v_dbti, v_stats_dbti;
    GenDb::DbErrors dbe;
    DbHandler *db_handler(db_initializer_->GetDbHandler());
    db_handler->GetStats(&v_dbti, &dbe, &v_stats_dbti, db_handler->GetName());
    std::vector<GenDb::DbErrors> v_dbe;
    v_dbe.push_back(dbe);
    stats.set_db_table_info(v_dbti);
    stats.set_db_statistics_table_info(v_stats_dbti);
    stats.set_db_errors(v_dbe);
    uint64_t db_queue_count, db_enqueues;
    db_handler->GetStats(&db_queue_count, &db_enqueues);
    stats.set_db_queue_count(db_queue_count);
    stats.set_db_enqueues(db_enqueues);
    ProtobufCollectorStatsUve::Send(stats);
}
