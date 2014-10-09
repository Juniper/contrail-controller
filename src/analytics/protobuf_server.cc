//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <string>
#include <vector>
#include <boost/asio/buffer.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/dynamic_message.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include <base/logging.h>
#include <io/io_types.h>
#include <io/udp_server.h>

#include "analytics/self_describing_message.pb.h"
#include "analytics/protobuf_server.h"
#include "analytics/protobuf_server_impl.h"

using ::google::protobuf::FileDescriptorSet;
using ::google::protobuf::FileDescriptor;
using ::google::protobuf::FileDescriptorProto;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::DynamicMessageFactory;
using ::google::protobuf::Message;
using ::google::protobuf::Reflection;

namespace protobuf {

namespace impl {

ProtobufReader::ProtobufReader() {
}

ProtobufReader::~ProtobufReader() {
}

bool ProtobufReader::ParseSelfDescribingMessage(const uint8_t *data,
    size_t size, uint64_t *timestamp, Message **msg) {
    // Parse the SelfDescribingMessage from data
    SelfDescribingMessage sdm_message;
    bool success = sdm_message.ParseFromArray(data, size);
    if (!success) {
        LOG(ERROR, "SelfDescribingMessage: Parsing FAILED");
        return false;
    }
    *timestamp = sdm_message.timestamp();
    const std::string &msg_type(sdm_message.type_name());
    // Extract the FileDescriptorProto and populate the Descriptor pool
    const FileDescriptorSet &fds(sdm_message.proto_files());
    for (int i = 0; i < fds.file_size(); i++) {
        const FileDescriptorProto &fdp(fds.file(i));
        tbb::mutex::scoped_lock lock(mutex_);
        const FileDescriptor *fd(dpool_.BuildFile(fdp));
        if (fd == NULL) {
            LOG(ERROR, "SelfDescribingMessage: " << msg_type <<
                ": DescriptorPool BuildFile(" << i << ") FAILED");
            return false;
        }
        lock.release();
    }
    // Extract the Descriptor
    const Descriptor *mdesc = dpool_.FindMessageTypeByName(msg_type);
    if (mdesc == NULL) {
        LOG(ERROR, "SelfDescribingMessage: " << msg_type << ": Descriptor " <<
            " not FOUND");
        return false;
    }
    // Parse the message.
    const Message* msg_proto = dmf_.GetPrototype(mdesc);
    if (msg_proto == NULL) {
        LOG(ERROR, msg_type << ": Prototype FAILED");
        return false;
    }
    *msg = msg_proto->New();
    success = (*msg)->ParseFromString(sdm_message.message_data());
    if (!success) {
        LOG(ERROR, msg_type << ": Parsing FAILED");
        return false;
    }
    return true;
}

void PopulateProtobufStats(const Message& message,
    const std::string &stat_attr_name, StatWalker *stat_walker) {
    const Reflection *reflection(message.GetReflection());
    DbHandler::AttribMap attribs;
    StatWalker::TagMap tags;
    std::vector<const FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (size_t i = 0; i < fields.size(); i++) {
        // Gather attributes and tags at this level
        const FieldDescriptor *field(fields[i]);
        const FieldDescriptor::CppType ftype(field->cpp_type());
        const std::string &fname(field->name());
        switch (ftype) {
          case FieldDescriptor::CPPTYPE_INT32: {
            // Insert into the attribute map
            DbHandler::Var avalue(static_cast<uint64_t>(
                reflection->GetInt32(message, field)));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_INT64: {
            // Insert into the attribute map
            DbHandler::Var avalue(static_cast<uint64_t>(
                reflection->GetInt64(message, field)));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_UINT32: {
            // Insert into the attribute map
            DbHandler::Var avalue(static_cast<uint64_t>(
                reflection->GetUInt32(message, field)));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_UINT64: {
            // Insert into the attribute map
            DbHandler::Var avalue(reflection->GetUInt64(message, field));
            attribs.insert(make_pair(field->name(), avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_DOUBLE: {
            // Insert into the attribute map
            DbHandler::Var avalue(reflection->GetDouble(message, field));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_FLOAT: {
            // Insert into the attribute map
            DbHandler::Var avalue(static_cast<double>(
                reflection->GetFloat(message, field)));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_BOOL: {
            // Insert into the attribute map
            DbHandler::Var avalue(static_cast<uint64_t>(
                reflection->GetBool(message, field)));
            attribs.insert(make_pair(fname, avalue));
            break;
          }
          case FieldDescriptor::CPPTYPE_ENUM: {
            // XXX - Implement
            assert(0);
            break;
          }
          case FieldDescriptor::CPPTYPE_STRING: {
            // Insert into the attribute map
            const std::string &svalue(reflection->GetString(message, field));
            DbHandler::Var avalue(svalue);
            attribs.insert(make_pair(fname, avalue));
            // Insert into the tag map
            StatWalker::TagVal tvalue;
            tvalue.val = svalue;
            tags.insert(make_pair(fname, tvalue));
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
    stat_walker->Pop();
}

void ProcessProtobufMessage(const Message& message,
    const uint64_t &timestamp, StatWalker::StatTableInsertFn stat_db_callback) {
    const std::string &message_name(message.GetTypeName());
    StatWalker::TagMap tags;
    StatWalker stat_walker(stat_db_callback, timestamp, message_name, tags);
    PopulateProtobufStats(message, message_name, &stat_walker);
}

}  // namespace impl

class ProtobufServer::ProtobufServerImpl {
 public:
    ProtobufServerImpl(EventManager *evm, uint16_t udp_server_port,
        StatWalker::StatTableInsertFn stat_db_callback) :
        udp_server_(new ProtobufUdpServer(evm, udp_server_port,
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

 private:
    //
    // ProtobufUdpServer
    //
    class ProtobufUdpServer : public UdpServer {
     public:
        ProtobufUdpServer(EventManager *evm, uint16_t port,
            StatWalker::StatTableInsertFn stat_db_callback) :
            UdpServer(evm),
            port_(port),
            stat_db_callback_(stat_db_callback) {
        }

        bool Initialize() {
            int count = 0;
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

        virtual void OnRead(boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::udp::endpoint &remote_endpoint) {
            uint64_t timestamp;
            Message *message = NULL;
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));
            if (!reader_.ParseSelfDescribingMessage(
                    boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, &timestamp,
                    &message)) {
                LOG(ERROR, "Reading protobuf message FAILED: " <<
                    remote_endpoint);
                msg_stats_.UpdateRxFail(remote_endpoint);
                DeallocateBuffer(recv_buffer);
                return;
            }
            protobuf::impl::ProcessProtobufMessage(*message, timestamp,
                stat_db_callback_);
            msg_stats_.UpdateRx(remote_endpoint, recv_buffer_size);
            delete message;
            DeallocateBuffer(recv_buffer);
        }

        void GetStatistics(std::vector<SocketIOStats> *v_tx_stats,
            std::vector<SocketIOStats> *v_rx_stats,
            std::vector<SocketEndpointMessageStats> *v_rx_msg_stats) {
            SocketIOStats tx_stats;
            GetTxSocketStats(tx_stats);
            v_tx_stats->push_back(tx_stats);
            SocketIOStats rx_stats;
            GetRxSocketStats(rx_stats);
            v_rx_stats->push_back(rx_stats);
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
                uint64_t bytes) {
                Update(remote_endpoint, bytes, false);
            }
            void UpdateRxFail(
                const boost::asio::ip::udp::endpoint &remote_endpoint) {
                Update(remote_endpoint, 0, true);
            }
            void GetRx(
                std::vector<SocketEndpointMessageStats> *semsv) {
                tbb::mutex::scoped_lock lock(mutex_);
                // Send diffs
                GetDiffStats<EndpointStatsMap,
                    const boost::asio::ip::udp::endpoint, MessageInfo,
                    SocketEndpointMessageStats>(rx_stats_map_, o_rx_stats_map_,
                    *semsv);
            }
         private:
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
                void Get(const boost::asio::ip::udp::endpoint &remote_endpoint,
                    SocketEndpointMessageStats &sems) const {
                    std::stringstream ss;
                    ss << remote_endpoint;
                    sems.set_endpoint_name(ss.str());
                    sems.set_messages(messages_);
                    sems.set_bytes(bytes_);
                    sems.set_errors(errors_);
                    sems.set_last_timestamp(last_timestamp_);
                }

             private:
                friend MessageInfo operator+(const MessageInfo &a,
                    const MessageInfo &b) {
                    MessageInfo sum;
                    sum.messages_ = a.messages_ + b.messages_;
                    sum.bytes_ = a.bytes_ + b.bytes_;
                    sum.errors_ = a.errors_ + b.errors_;
                    return sum;
                }
                friend MessageInfo operator-(const MessageInfo &a,
                    const MessageInfo &b) {
                    MessageInfo diff;
                    diff.messages_ = a.messages_ - b.messages_;
                    diff.bytes_ = a.bytes_ - b.bytes_;
                    diff.errors_ = a.errors_ - b.errors_;
                    diff.last_timestamp_ = a.last_timestamp_ - b.last_timestamp_;
                    return diff;
                }
                uint64_t messages_;
                uint64_t bytes_;
                uint64_t errors_;
                uint64_t last_timestamp_;
            };

            void Update(const boost::asio::ip::udp::endpoint &remote_endpoint,
                uint64_t bytes, bool is_dropped) {
                tbb::mutex::scoped_lock lock(mutex_);
                EndpointStatsMap::iterator it =
                    rx_stats_map_.find(remote_endpoint);
                if (it == rx_stats_map_.end()) {
                    it = rx_stats_map_.insert(remote_endpoint,
                        new MessageInfo).first;
                }
                MessageInfo *msg_info = it->second;
                msg_info->Update(bytes, is_dropped);
            }

            typedef boost::ptr_map<const boost::asio::ip::udp::endpoint,
                MessageInfo> EndpointStatsMap;
            EndpointStatsMap rx_stats_map_, o_rx_stats_map_;
            tbb::mutex mutex_;
        };

        static const int kMaxInitRetries = 5;

        protobuf::impl::ProtobufReader reader_;
        uint16_t port_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        MessageStatistics msg_stats_;
    };

    ProtobufUdpServer *udp_server_;
};

ProtobufServer::ProtobufServer(EventManager *evm,
    uint16_t udp_server_port, StatWalker::StatTableInsertFn stat_db_fn) {
    impl_ = new ProtobufServerImpl(evm, udp_server_port, stat_db_fn);
}

ProtobufServer::~ProtobufServer() {
    if (impl_) {
        delete impl_;
        impl_ = NULL;
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

}  // namespace protobuf
