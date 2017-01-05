//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_
#define ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_

#include <vector>

#include <boost/system/error_code.hpp>
#include <boost/asio/ip/udp.hpp>

#include <analytics/stat_walker.h>

namespace structured_syslog {

//
// StructuredSyslog Server
//
class StructuredSyslogServer {
 public:
    StructuredSyslogServer(EventManager *evm, uint16_t port,
        StatWalker::StatTableInsertFn stat_db_cb);
    virtual ~StructuredSyslogServer();
    bool Initialize();
    void Shutdown();
    boost::asio::ip::udp::endpoint GetLocalEndpoint(
        boost::system::error_code *ec);

 private:
    class StructuredSyslogServerImpl;
    StructuredSyslogServerImpl *impl_;
};

//
// StructuredSyslog Config
//
class StructuredSyslogConfig {
 public:
    StructuredSyslogConfig();
    virtual ~StructuredSyslogConfig();
    void Init();
    std::vector<std::string> messages_handled;
    std::vector<std::string> tagged_fields;
    std::vector<std::string> int_fields;
};

}  // namespace structured_syslog

#endif  // ANALYTICS_STRUCTURED_SYSLOG_SERVER_H_

