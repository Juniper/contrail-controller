//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <utility>
#include <string>
#include <vector>
#include <boost/asio/buffer.hpp>
#include <boost/foreach.hpp>
#include <fstream>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/dynamic_message.h>

#include <base/logging.h>
#include <io/io_types.h>
#include <io/udp_server.h>
#include <dirent.h>
#include <string.h>

#include "analytics/jti-protos/telemetry_top.pb.h"
#include "analytics/protobuf_server.h"
#include "analytics/protobuf_server_impl.h"

using ::google::protobuf::FileDescriptorSet;
using ::google::protobuf::FileDescriptor;
using ::google::protobuf::FileDescriptorProto;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::FieldOptions;
using ::google::protobuf::EnumValueDescriptor;
using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::DynamicMessageFactory;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;

using std::make_pair;

namespace protobuf {

namespace impl {

ProtobufReader::ProtobufReader() {
}

ProtobufReader::~ProtobufReader() {
}

bool ProtobufReader::ParseSchemaFiles(std::string schema_file_directory) {
    // Read all .desc files from schema_file_directory and populate our
    // descriptor pool
    return 1;
    int count = 0;
    if (schema_file_directory.empty())
        return 0;

    DIR *schemadir = opendir(schema_file_directory.c_str());
    struct dirent *entry;
    if (schemadir == NULL)
        return 0;

    while ((entry = readdir(schemadir))) {
        if (entry->d_name[0] == '.')
            continue;

        if (strstr(entry->d_name, ".desc") == NULL)
            continue;

        std::string filename = schema_file_directory + "/" + std::string(entry->d_name);
        std::ifstream desc_file(filename.c_str());
        FileDescriptorSet fds;
        fds.ParseFromIstream(&desc_file);
        for (int i = 0; i < fds.file_size(); i++) {
            const FileDescriptorProto &fdp(fds.file(i));
            const FileDescriptor *fd(dpool_.BuildFile(fdp));
            if (fd == NULL) {
                return 0;
            }
        }
        count++;
    }
    LOG(INFO, "Registered " << count << " .desc files");
    return 1;
}

const Message* ProtobufReader::GetPrototype(std::string msg_type) {
    const Descriptor *mdesc = dpool_.FindMessageTypeByName(msg_type);
    if (mdesc)
        return dmf_.GetPrototype(mdesc);
    else
        return NULL;
}

const Message* ProtobufReader::GetPrototype(const ::google::protobuf::Descriptor *mdesc) {
    if (mdesc)
        return dmf_.GetPrototype(mdesc);
    else
        return NULL;
}

// All JTI messages will be encapsulated inside TelemetryStream message
// This function parses the Telemetary Stream and populates the sensor message
// encapsulated inside Telemetry Stream
bool ProtobufReader::ParseTelemetryStreamMessage(TelemetryStream *tstream,
                                                 const uint8_t *data,
                                                 size_t size,
                                                 uint64_t *tstamp,
                                                 std::string *message_name,
                                                 Message **msg,
                                                 ParseFailureCallback
                                                 parse_failure_cb) {

    LOG(DEBUG, "Parsing Telemetry Stream message JTI");
    // Parse TelemetryStream from data
    bool success = tstream->ParseFromArray(data, size);
    if (!success) {
        if (!parse_failure_cb.empty()) {
            parse_failure_cb("Unknown");
        }
        LOG(ERROR, "TelemetryStream: Parsing FAILED");
        return false;
    }

    if (tstream->has_timestamp()) {
      *tstamp = (tstream->timestamp() * 1000);
    } else {
        *tstamp = UTCTimestampUsec();
    }
    // Sensor message is availabe in enterprise field on TelemetryStream
    // message
    // Get Extension Data
    LOG(DEBUG, "JTI Tstream message" << tstream->DebugString());
    JuniperNetworksSensors *jsensor(tstream->mutable_enterprise()->MutableExtension(juniperNetworks));
    message_name->append("enterprise.juniperNetworks");
    const Reflection *reflection(jsensor->GetReflection());
    std::vector<const FieldDescriptor*> fields;
    reflection->ListFields(*jsensor, &fields);
    if (fields.size() == 0) {
        if (!parse_failure_cb.empty()) {
            parse_failure_cb("JuniperNetworksSensors");
        }
        return false;
    }
    // Message name of the sensor data
    message_name->append("." + fields[0]->lowercase_name());

    const FieldDescriptor *field(fields[0]);
    const std::string &msg_type(fields[0]->message_type()->name());
    const Descriptor *mdesc(field->message_type());

    if (mdesc == NULL) {
        if (!parse_failure_cb.empty()) {
            parse_failure_cb("TelemetryStream");
        }
        LOG(ERROR, "JuniperNetworksSensors: " << msg_type << ": Descriptor " <<
            "not FOUND");
        return false;
    }

    const Message* msg_proto = GetPrototype(mdesc);
    if (msg_proto == NULL) {
        if (!parse_failure_cb.empty()) {
            parse_failure_cb("TelemetryStream");
        }
        LOG(ERROR, "TelemetryStream: Prototype FAILED");
        return false;
    }
    LOG(DEBUG, "Populating extension message from Telemetry Stream");

    *msg = msg_proto->New();
    success = (*msg)->ParseFromString(reflection->GetMessage(*jsensor,
                                                             field).SerializeAsString());
    if (!success) {
        if (!parse_failure_cb.empty()) {
            parse_failure_cb(msg_type);
        }
        return false;
    }
    return true;
}

void PopulateProtobufTopLevelTags(const Message& message,
    const boost::asio::ip::udp::endpoint &remote_endpoint,
    StatWalker::TagMap *top_tags) {
    // Insert the remote endpoint address as a tag
    boost::asio::ip::address remote_address(remote_endpoint.address());
    boost::system::error_code ec;
    const std::string saddr(remote_address.to_string(ec));
    if (ec) {
        LOG(ERROR, "Remote endpoint: " << remote_endpoint <<
            " address to string FAILED: " << ec);
    }
    StatWalker::TagVal tvalue;
    tvalue.val = saddr;
    top_tags->insert(make_pair("Source", tvalue));
    // At the top level all elemental fields are inserted into the tag map
    const Reflection *reflection(message.GetReflection());
    std::vector<const FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (size_t i = 0; i < fields.size(); i++) {
        // Gather tags
        const FieldDescriptor *field(fields[i]);
        const FieldOptions &foptions(field->options());
        bool is_tag(false);
        if (foptions.HasExtension(telemetry_options)) {
            const TelemetryFieldOptions &toptions(
                foptions.GetExtension(telemetry_options));
            is_tag = toptions.has_is_key() && toptions.is_key();
        }
        if (is_tag == false) {
            continue;
        }
        const FieldDescriptor::CppType ftype(field->cpp_type());
        const std::string &fname(field->name());
        switch (ftype) {
          case FieldDescriptor::CPPTYPE_INT32: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<uint64_t>(
                reflection->GetInt32(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_INT64: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<uint64_t>(
                reflection->GetInt64(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_UINT32: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<uint64_t>(
                reflection->GetUInt32(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_UINT64: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<uint64_t>(
                reflection->GetUInt64(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_DOUBLE: {
            StatWalker::TagVal tvalue;
            tvalue.val = reflection->GetDouble(message, field);
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_FLOAT: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<double>(
                reflection->GetFloat(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_BOOL: {
            StatWalker::TagVal tvalue;
            tvalue.val = static_cast<uint64_t>(
                reflection->GetBool(message, field));
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_ENUM: {
            const EnumValueDescriptor *edesc(reflection->GetEnum(message,
                field));
            StatWalker::TagVal tvalue;
            tvalue.val = edesc->name();
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_STRING: {
            StatWalker::TagVal tvalue;
            tvalue.val = reflection->GetString(message, field);
            top_tags->insert(make_pair(fname, tvalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_MESSAGE: {
            break;
          }
          default: {
            LOG(ERROR, "Unknown protobuf field type: " << ftype);
            break;
          }
        }
    }
}

static inline void PopulateAttribsAndTags(DbHandler::AttribMap *attribs,
    StatWalker::TagMap *tags, bool is_tag, const std::string &name,
    DbHandler::Var value) {
    // Insert into the attribute map
    attribs->insert(make_pair(name, value));
    if (is_tag) {
        // Insert into the tag map
        StatWalker::TagVal tvalue;
        tvalue.val = value;
        tags->insert(make_pair(name, tvalue));
    }
}

void PopulateProtobufStats(const Message& message,
    const std::string &stat_attr_name, StatWalker *stat_walker) {
    // At the top level the stat walker already has the tags so
    // we need to skip going through the elemental types and
    // creating the tag and attribtute maps. At lower levels,
    // only strings are inserted into the tag map
    bool top_level(stat_attr_name.empty());
    const Reflection *reflection(message.GetReflection());
    DbHandler::AttribMap attribs;
    StatWalker::TagMap tags;
    std::vector<const FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    if (!top_level) {
        for (size_t i = 0; i < fields.size(); i++) {
            // Gather attributes and tags at this level
            const FieldDescriptor *field(fields[i]);
            const FieldDescriptor::CppType ftype(field->cpp_type());
            const std::string &fname(field->name());
            const FieldOptions &foptions(field->options());
            bool is_tag(false);
            if (foptions.HasExtension(telemetry_options)) {
                const TelemetryFieldOptions &toptions(
                    foptions.GetExtension(telemetry_options));
                is_tag = toptions.has_is_key() && toptions.is_key();
            }
            switch (ftype) {
              case FieldDescriptor::CPPTYPE_INT32: {
                // Insert into the attribute and tag map
                DbHandler::Var value(static_cast<uint64_t>(
                    reflection->GetInt32(message, field)));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_INT64: {
                // Insert into the attribute and tag map
                DbHandler::Var value(static_cast<uint64_t>(
                    reflection->GetInt64(message, field)));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_UINT32: {
                // Insert into the attribute and tag map
                DbHandler::Var value(static_cast<uint64_t>(
                    reflection->GetUInt32(message, field)));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_UINT64: {
                // Insert into the attribute and tag map
                DbHandler::Var value(reflection->GetUInt64(message, field));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_DOUBLE: {
                // Insert into the attribute and tag map
                DbHandler::Var value(reflection->GetDouble(message, field));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_FLOAT: {
                // Insert into the attribute and tag map
                DbHandler::Var value(static_cast<double>(
                    reflection->GetFloat(message, field)));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_BOOL: {
                // Insert into the attribute and tag map
                DbHandler::Var value(static_cast<uint64_t>(
                    reflection->GetBool(message, field)));
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_ENUM: {
                const EnumValueDescriptor *edesc(reflection->GetEnum(message,
                    field));
                const std::string &svalue(edesc->name());
                // Insert into the attribute and tag map
                DbHandler::Var value(svalue);
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_STRING: {
                const std::string &svalue(reflection->GetString(message, field));
                // Insert into the attribute and tag map
                DbHandler::Var value(svalue);
                PopulateAttribsAndTags(&attribs, &tags, is_tag, fname, value);
                break;
              }
              case FieldDescriptor::CPPTYPE_MESSAGE: {
                break;
              }
              default: {
                LOG(ERROR, "Unknown protobuf field type: " << ftype);
                break;
              }
            }
        }
        // Push the stats at this level
        stat_walker->Push(stat_attr_name, tags, attribs);
    }
    // Perform traversal of children
    for (size_t i = 0; i < fields.size(); i++) {
        const FieldDescriptor* field(fields[i]);
        const FieldDescriptor::CppType ftype(field->cpp_type());
        const std::string &fname(field->name());
        switch (ftype) {
          case FieldDescriptor::CPPTYPE_MESSAGE: {
            if (field->is_repeated()) {
                int size = reflection->FieldSize(message, field);
                for (int i = 0; i < size; i++) {
                    const Message& sub_message(
                        reflection->GetRepeatedMessage(message, field, i));
                    PopulateProtobufStats(sub_message, fname, stat_walker);
                }
            } else {
                const Message& sub_message(reflection->GetMessage(message,
                                                 field));
                PopulateProtobufStats(sub_message, fname, stat_walker);
            }
            break;
          }
          default: {
            break;
          }
        }
    }
    // Pop the stats at this level
    if (!top_level) {
        stat_walker->Pop();
    }
}

void ProcessProtobufMessage(const TelemetryStream& tstream,
    const std::string &message_name, const Message& message,
    const uint64_t &timestamp,
    const boost::asio::ip::udp::endpoint &remote_endpoint,
    StatWalker::StatTableInsertFn stat_db_callback) {
    StatWalker::TagMap top_tags;
    // Extract all the fields present in the TelemetryStream message

    LOG(DEBUG, "Processing JTI Protobuf message");

    StatWalker::TagVal sysidvalue;
    sysidvalue.val = tstream.system_id();
    top_tags.insert(make_pair("system_id", sysidvalue));

    StatWalker::TagVal compidvalue;
    compidvalue.val = static_cast<uint64_t>(tstream.component_id());
    top_tags.insert(make_pair("component_id", compidvalue));

    StatWalker::TagVal subcompidvalue;
    subcompidvalue.val = static_cast<uint64_t>(tstream.sub_component_id());
    top_tags.insert(make_pair("sub_component_id", subcompidvalue));

    StatWalker::TagVal sequencenum;
    sequencenum.val = static_cast<uint64_t>(tstream.sequence_number());
    top_tags.insert(make_pair("sequence_number", sequencenum));

    StatWalker::TagVal sensorvalue;
    sensorvalue.val = tstream.sensor_name();
    top_tags.insert(make_pair("sensor_name", sensorvalue));

    StatWalker::TagVal version_major;
    version_major.val = static_cast<uint64_t>(tstream.version_major());
    top_tags.insert(make_pair("version_major", version_major));

    StatWalker::TagVal version_minor;
    version_minor.val = static_cast<uint64_t>(tstream.version_minor());
    top_tags.insert(make_pair("version_minor", version_minor));

    // Populate the Message encapsulated inside TelemetryStream
    PopulateProtobufTopLevelTags(message, remote_endpoint, &top_tags);
    if (stat_db_callback) {
        StatWalker stat_walker(stat_db_callback, timestamp, "TelemetryStream",
                               top_tags);
        PopulateProtobufStats(message, message_name, &stat_walker);
    }
}

}  // namespace impl

class ProtobufServer::ProtobufServerImpl {
public:
    ProtobufServerImpl(EventManager *evm, uint16_t udp_server_port,
		       const std::string schema_file_directory,
		       StatWalker::StatTableInsertFn stat_db_callback) :
	udp_server_(new ProtobufUdpServer(evm, udp_server_port, schema_file_directory,
            stat_db_callback)) {
    }

    bool Initialize() {
        return udp_server_->Initialize();
    }

    void Shutdown() {
        udp_server_->Shutdown();
        UdpServerManager::DeleteServer(udp_server_);
        udp_server_ = NULL;
    }

    boost::asio::ip::udp::endpoint GetLocalEndpoint(
        boost::system::error_code *ec) {
        return udp_server_->GetLocalEndpoint(ec);
    }

    void GetStatistics(std::vector<SocketIOStats> *v_tx_stats,
        std::vector<SocketIOStats> *v_rx_stats,
        std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
        return udp_server_->GetStatistics(v_tx_stats, v_rx_stats,
            v_rx_msg_stats);
    }

    void GetReceivedMessageStatistics(
        std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
        return udp_server_->GetReceivedMessageStatistics(v_rx_msg_stats);
    }

private:
    //
    // ProtobufUdpServer
    //
    class ProtobufUdpServer : public UdpServer {
    public:
        ProtobufUdpServer(EventManager *evm, uint16_t port,
            const std::string schema_file_directory,
            StatWalker::StatTableInsertFn stat_db_callback) :
            UdpServer(evm, kBufferSize),
            port_(port),
            schema_file_directory_(schema_file_directory),
            stat_db_callback_(stat_db_callback) {
        }

        bool Initialize() {
            int count = 0;
            if (!reader_.ParseSchemaFiles(schema_file_directory_)) {
                LOG(ERROR, "EXITING: ProtobufUdpServer failed to parse schema file");
                exit(1);
            }
            while (count++ < kMaxInitRetries) {
                if (UdpServer::Initialize(port_)) {
                    break;
                }
                sleep(1);
            }
            if (!(count < kMaxInitRetries)) {
                LOG(ERROR, "EXITING: ProtobufUdpServer initialization failed "
                    << "for port " << port_);
                exit(1);
            }
            StartReceive();
            return true;
        }

        virtual void OnRead(const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::udp::endpoint &remote_endpoint) {
            uint64_t timestamp;
            std::string message_name;
            Message *message = NULL;
            TelemetryStream tstream;
            // The data is available in recv_buffer encoded serially in
            // protobuf format
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));
            LOG(INFO, "Received JTI data, parsing TelemetryStream");
            // Deserialize the data in the Message object
            if (!reader_.ParseTelemetryStreamMessage(&tstream,
                    boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, &timestamp, &message_name,
                    &message, boost::bind(&MessageStatistics::UpdateRxFail,
                    &msg_stats_, remote_endpoint, _1))) {
                LOG(ERROR, "Reading protobuf message FAILED: " <<
                    remote_endpoint);
                DeallocateBuffer(recv_buffer);
                return;
            }
            protobuf::impl::ProcessProtobufMessage(tstream, message_name,
                                                   *message, timestamp,
                                                   remote_endpoint,
                                                   stat_db_callback_);
            msg_stats_.UpdateRx(remote_endpoint, message_name,
                recv_buffer_size);
            delete message;
            DeallocateBuffer(recv_buffer);
        }

        void GetStatistics(std::vector<SocketIOStats> *v_tx_stats,
            std::vector<SocketIOStats> *v_rx_stats,
            std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
            if (v_tx_stats != NULL) {
                SocketIOStats tx_stats;
                GetTxSocketStats(&tx_stats);
                v_tx_stats->push_back(tx_stats);
            }
            if (v_rx_stats != NULL) {
                SocketIOStats rx_stats;
                GetRxSocketStats(&rx_stats);
                v_rx_stats->push_back(rx_stats);
            }
            if (v_rx_msg_stats != NULL) {
                msg_stats_.GetRxDiff(v_rx_msg_stats);
            }
        }

        void GetReceivedMessageStatistics(
            std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
            msg_stats_.GetRx(v_rx_msg_stats);
        }

    private:
        //
        // MessageStatistics
        //
        class MessageStatistics {
        public:
            void UpdateRx(
                const boost::asio::ip::udp::endpoint &remote_endpoint,
                const std::string &message_name,
                uint64_t bytes) {
                Update(remote_endpoint, message_name, bytes, false);
            }
            void UpdateRxFail(
                const boost::asio::ip::udp::endpoint &remote_endpoint,
                const std::string &message_name) {
                Update(remote_endpoint, message_name, 0, true);
            }
            void GetRxDiff(
                std::vector<SocketEndpointMessageStats> *semsv) {
                GetRxInternal(semsv, true);
            }
            void GetRx(
                std::vector<SocketEndpointMessageStats> *semsv) {
                GetRxInternal(semsv, false);
            }
        private:
            class MessageInfo;

            void GetRxInternal(
                std::vector<SocketEndpointMessageStats> *semsv,
                bool clear_stats) {
                tbb::mutex::scoped_lock lock(mutex_);
                BOOST_FOREACH(
                    const EndpointStatsMessageMap::value_type &esmm_value,
                    rx_stats_map_) {
                    const EndpointMessageKey &key(esmm_value.first);
                    const MessageInfo *msg_info(esmm_value.second);
                    SocketEndpointMessageStats sems;
                    msg_info->Get(key, &sems);
                    semsv->push_back(sems); 
                }
                if (clear_stats) {
                    rx_stats_map_.clear();
                }
            }

            void Update(const boost::asio::ip::udp::endpoint &remote_endpoint,
                const std::string &message_name, uint64_t bytes,
                bool is_dropped) {
                EndpointMessageKey key(make_pair(remote_endpoint,
                    message_name));
                tbb::mutex::scoped_lock lock(mutex_);
                EndpointStatsMessageMap::iterator it =
                    rx_stats_map_.find(key);
                if (it == rx_stats_map_.end()) {
                    it = rx_stats_map_.insert(key,
                        new MessageInfo).first;
                }
                MessageInfo *msg_info = it->second;
                msg_info->Update(bytes, is_dropped);
            }

            typedef std::pair<boost::asio::ip::udp::endpoint,
                std::string> EndpointMessageKey;
            typedef boost::ptr_map<EndpointMessageKey,
                MessageInfo> EndpointStatsMessageMap;
            EndpointStatsMessageMap rx_stats_map_;
            tbb::mutex mutex_;

            //
            // MessageInfo
            //
            class MessageInfo {
            public:
                MessageInfo() :
                    messages_(0),
                    bytes_(0),
                    errors_(0),
                    last_timestamp_(0) {
                }
                void Update(uint64_t bytes, bool is_dropped) {
                    if (is_dropped) {
                        errors_++;
                    } else {
                        messages_++;
                        bytes_ += bytes;
                    }
                    last_timestamp_ = UTCTimestampUsec();
                }
                void Get(const EndpointMessageKey &key,
                    SocketEndpointMessageStats *sems) const {
                    const boost::asio::ip::udp::endpoint remote_endpoint(
                        key.first);
                    const std::string &message_name(key.second);
                    std::stringstream ss;
                    ss << remote_endpoint;
                    sems->set_endpoint_name(ss.str());
                    sems->set_message_name(message_name);
                    sems->set_messages(messages_);
                    sems->set_bytes(bytes_);
                    sems->set_errors(errors_);
                    sems->set_last_timestamp(last_timestamp_);
                }

            private:
                uint64_t messages_;
                uint64_t bytes_;
                uint64_t errors_;
                uint64_t last_timestamp_;
            };
        };

        static const int kMaxInitRetries = 5;
        static const int kBufferSize = 32 * 1024;

        protobuf::impl::ProtobufReader reader_;
        uint16_t port_;
        std::string schema_file_directory_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        MessageStatistics msg_stats_;
    };

    ProtobufUdpServer *udp_server_;
};


log4cplus::LogLevel protobuf::impl::Protobuf2log4Level(
    google::protobuf::LogLevel glevel) {
    switch (glevel) {
      case google::protobuf::LOGLEVEL_INFO:
          return log4cplus::INFO_LOG_LEVEL;
      case google::protobuf::LOGLEVEL_WARNING:
          return log4cplus::WARN_LOG_LEVEL;
      case google::protobuf::LOGLEVEL_ERROR:
          return log4cplus::ERROR_LOG_LEVEL;
      case google::protobuf::LOGLEVEL_FATAL:
          return log4cplus::FATAL_LOG_LEVEL;
      default:
          return log4cplus::ALL_LOG_LEVEL;
    }
}

void protobuf::impl::ProtobufLibraryLog(google::protobuf::LogLevel level,
    const char* filename, int line, const std::string& message) {
    if (LoggingDisabled()) {
        return;
    }
    log4cplus::LogLevel log4level(Protobuf2log4Level(level));
    log4cplus::Logger logger(log4cplus::Logger::getRoot());
    if (logger.isEnabledFor(log4level)) {
        log4cplus::tostringstream buf;
        buf << "ProtobufLibrary: " << filename << ":" << line << "] " <<
            message;
        logger.forcedLog(log4level, buf.str());
    }
}

ProtobufServer::ProtobufServer(EventManager *evm, uint16_t udp_server_port,
                               const std::string schema_file_directory,
                               StatWalker::StatTableInsertFn stat_db_fn) :
    shutdown_libprotobuf_on_delete_(true) {
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    google::protobuf::SetLogHandler(&protobuf::impl::ProtobufLibraryLog);
    impl_ = new ProtobufServerImpl(evm, udp_server_port, schema_file_directory, stat_db_fn);
}

ProtobufServer::~ProtobufServer() {
    if (impl_) {
        delete impl_;
        impl_ = NULL;
    }
    if (shutdown_libprotobuf_on_delete_) {
        google::protobuf::ShutdownProtobufLibrary();
    }
}

bool ProtobufServer::Initialize() {
    return impl_->Initialize();
}

void ProtobufServer::Shutdown() {
    impl_->Shutdown();
}

boost::asio::ip::udp::endpoint ProtobufServer::GetLocalEndpoint(
    boost::system::error_code *ec) {
    return impl_->GetLocalEndpoint(ec);
}

void ProtobufServer::GetStatistics(std::vector<SocketIOStats> *v_tx_stats,
    std::vector<SocketIOStats> *v_rx_stats,
    std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
    return impl_->GetStatistics(v_tx_stats, v_rx_stats, v_rx_msg_stats);
}

void ProtobufServer::GetReceivedMessageStatistics(
    std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
    return impl_->GetReceivedMessageStatistics(v_rx_msg_stats);
}

void ProtobufServer::SetShutdownLibProtobufOnDelete(bool shutdown_on_delete) {
    shutdown_libprotobuf_on_delete_ = shutdown_on_delete;
}

}  // namespace protobuf
