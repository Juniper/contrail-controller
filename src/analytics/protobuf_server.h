//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_PROTOBUF_SERVER_H_
#define ANALYTICS_PROTOBUF_SERVER_H_

#include <analytics/stat_walker.h>

namespace protobuf {

//
// Protobuf Server
//
class ProtobufServer {
 public:
    ProtobufServer(EventManager *evm, uint16_t udp_server_port,
        StatWalker::StatTableInsertFn stat_db_cb);
    virtual ~ProtobufServer();
    bool Initialize();
    void Shutdown();

 private:
    class ProtobufServerImpl;
    ProtobufServerImpl *impl_;
};

}  // namespace protobuf

#endif  // ANALYTICS_PROTOBUF_SERVER_H_
