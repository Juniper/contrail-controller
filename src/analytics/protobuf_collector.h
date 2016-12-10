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
        DbHandlerPtr db_handler);
    virtual ~ProtobufCollector();
    bool Initialize();
    void Shutdown();
    void SendStatistics(const std::string &name);

 private:
    boost::scoped_ptr<protobuf::ProtobufServer> server_;
};

#endif  // ANALYTICS_PROTOBUF_COLLECTOR_H_
