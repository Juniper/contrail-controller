//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include "analytics/db_handler.h"
#include "analytics/protobuf_collector.h"

const std::string ProtobufCollector::kDbName("Google Protocol Buffer");
const int ProtobufCollector::kDbTaskInstance(-1);
const std::string ProtobufCollector::kDbTaskName("protobuf_collector::Db");

ProtobufCollector::ProtobufCollector(EventManager *evm,
    uint16_t protobuf_udp_port,
    const std::vector<std::string> &cassandra_ips,
    const std::vector<int> &cassandra_ports, int analytics_ttl) :
    db_initializer_(new DbHandlerInitializer(evm, kDbName, kDbTaskInstance,
        kDbTaskName, boost::bind(&ProtobufCollector::DbInitializeCb, this),
        cassandra_ips, cassandra_ports, analytics_ttl)),
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
