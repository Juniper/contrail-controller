//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#ifndef ANALYTICS_PROTOBUF_SERVER_IMPL_H_
#define ANALYTICS_PROTOBUF_SERVER_IMPL_H_

#include <tbb/mutex.h>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/dynamic_message.h>

#include <analytics/stat_walker.h>

namespace protobuf {
namespace impl {

class ProtobufReader {
 public:
    ProtobufReader();
    virtual ~ProtobufReader();
    virtual bool ParseSelfDescribingMessage(const uint8_t *data, size_t size,
        uint64_t *timestamp, ::google::protobuf::Message **msg);

 private:
    tbb::mutex mutex_;
    ::google::protobuf::DescriptorPool dpool_;
    ::google::protobuf::DynamicMessageFactory dmf_;
};

void ProcessProtobufMessage(const ::google::protobuf::Message& message,
    const uint64_t &timestamp, StatWalker::StatTableInsertFn stat_db_callback);

}  // namespace impl
}  // namespace protobuf

#endif  // ANALYTICS_PROTOBUF_SERVER_IMPL_H_
