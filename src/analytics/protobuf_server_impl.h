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
    typedef boost::function<void(
        const std::string &message_name)> ParseFailureCallback;
    ProtobufReader();
    virtual ~ProtobufReader();
    virtual bool ParseSchemaFiles(const std::string schema_file_directory);
    virtual bool ParseTelemetryStreamMessage(TelemetryStream *stream,
                                             const uint8_t *data, size_t size,
                                             uint64_t *timestamp, std::string *message_name,
                                             ::google::protobuf::Message **msg,
                                             ParseFailureCallback cb);
 protected:
    virtual const ::google::protobuf::Message* GetPrototype(
        const ::google::protobuf::Descriptor *mdesc);
    virtual const ::google::protobuf::Message* GetPrototype(std::string msg_type);

 private:
    friend class ProtobufReaderTest;

    tbb::mutex mutex_;
    ::google::protobuf::DescriptorPool dpool_;
    ::google::protobuf::DynamicMessageFactory dmf_;
};

void ProcessProtobufMessage(const TelemetryStream& tstream, const std::string &message_name,
                            const ::google::protobuf::Message& message,
                            const uint64_t &timestamp,
                            const boost::asio::ip::udp::endpoint &remote_endpoint,
                            StatWalker::StatTableInsertFn stat_db_callback);

void ProtobufLibraryLog(::google::protobuf::LogLevel level,
    const char* filename, int line, const std::string& message);

log4cplus::LogLevel Protobuf2log4Level(
    ::google::protobuf::LogLevel glevel);

}  // namespace impl
}  // namespace protobuf

#endif  // ANALYTICS_PROTOBUF_SERVER_IMPL_H_
