//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_PROTOBUF_COLLECTOR_H_
#define ANALYTICS_PROTOBUF_COLLECTOR_H_

#include <string>
#include <vector>
#include <boost/scoped_ptr.hpp>

#include "analytics/protobuf_server.h"

class DbHandlerInitializer;

class ProtobufCollector {
 public:
    ProtobufCollector(EventManager *evm, uint16_t udp_server_port,
        const std::vector<std::string> &cassandra_ips,
        const std::vector<int> &cassandra_ports, int analytics_ttl);
    virtual ~ProtobufCollector();
    bool Initialize();
    void Shutdown();
    void SendStatistics(const std::string &name);

 private:
    void DbInitializeCb();

    static const std::string kDbName;
    static const int kDbTaskInstance;
    static const std::string kDbTaskName;

    boost::scoped_ptr<DbHandlerInitializer> db_initializer_;
    boost::scoped_ptr<protobuf::ProtobufServer> server_;
};

#endif  // ANALYTICS_PROTOBUF_COLLECTOR_H_
