//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_STRUCTURED_SYSLOG_COLLECTOR_H_
#define ANALYTICS_STRUCTURED_SYSLOG_COLLECTOR_H_

#include <string>
#include <vector>
#include <boost/scoped_ptr.hpp>

#include "analytics/structured_syslog_server.h"

class DbHandlerInitializer;

class StructuredSyslogCollector {
 public:
    StructuredSyslogCollector(EventManager *evm, uint16_t udp_server_port,
        DbHandlerPtr db_handler);
    virtual ~StructuredSyslogCollector();
    bool Initialize();
    void Shutdown();

 private:
    boost::scoped_ptr<structured_syslog::StructuredSyslogServer> server_;
};

#endif  // ANALYTICS_STRUCTURED_SYSLOG_COLLECTOR_H_
