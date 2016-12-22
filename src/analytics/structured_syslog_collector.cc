//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <io/io_types.h>

#include "analytics/collector_uve_types.h"
#include "analytics/db_handler.h"
#include "analytics/structured_syslog_collector.h"

StructuredSyslogCollector::StructuredSyslogCollector(EventManager *evm,
    uint16_t structured_syslog_udp_port, DbHandlerPtr db_handler) :
    server_(new structured_syslog::StructuredSyslogServer(evm, structured_syslog_udp_port,
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
