//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <io/io_types.h>

#include "analytics/collector_uve_types.h"
#include "analytics/db_handler.h"
#include "analytics/structured_syslog_collector.h"

StructuredSyslogCollector::StructuredSyslogCollector(EventManager *evm,
    uint16_t structured_syslog_port, const vector<string> &structured_syslog_tcp_forward_dst,
    const std::string &structured_syslog_kafka_broker,
    const std::string &structured_syslog_kafka_topic,
    uint16_t structured_syslog_kafka_partitions,
    DbHandlerPtr db_handler):
    server_(new structured_syslog::StructuredSyslogServer(evm, structured_syslog_port,
        structured_syslog_tcp_forward_dst, structured_syslog_kafka_broker,
        structured_syslog_kafka_topic,
        structured_syslog_kafka_partitions,
        db_handler->GetConfigClient(),
        boost::bind(&DbHandler::StatTableInsert, db_handler,
            _1, _2, _3, _4, _5, GenDb::GenDbIf::DbAddColumnCb()))) {
}

StructuredSyslogCollector::~StructuredSyslogCollector() {
}

bool StructuredSyslogCollector::Initialize() {
    server_->Initialize();
    return true;
}

void StructuredSyslogCollector::Shutdown() {
    server_->Shutdown();
}

