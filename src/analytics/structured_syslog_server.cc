//
// Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
//

#include <utility>
#include <string>
#include <vector>
#include <boost/asio/buffer.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_message_builder.h>

#include <base/logging.h>
#include <io/io_types.h>
#include <io/udp_server.h>

#include "analytics/structured_syslog_server.h"
#include "analytics/structured_syslog_server_impl.h"
#include "generator.h"
#include "analytics/syslog_collector.h"

#define STRUCTURED_SYSLOG_DEBUG 1

using std::make_pair;

namespace structured_syslog {

namespace impl {

StructuredSyslogReader::StructuredSyslogReader() {
}

StructuredSyslogReader::~StructuredSyslogReader() {
}


}  // namespace impl

class StructuredSyslogServer::StructuredSyslogServerImpl {
public:
    StructuredSyslogServerImpl(EventManager *evm, uint16_t udp_server_port,
        StatWalker::StatTableInsertFn stat_db_callback) :
        udp_server_(new StructuredSyslogUdpServer(evm, udp_server_port,
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


private:
    //
    // StructuredSyslogUdpServer
    //
    class StructuredSyslogUdpServer : public UdpServer {
    public:
        StructuredSyslogUdpServer(EventManager *evm, uint16_t port,
            StatWalker::StatTableInsertFn stat_db_callback) :
            UdpServer(evm, kBufferSize),
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
                LOG(ERROR, "EXITING: StructuredSyslogUdpServer initialization failed "
                    << "for port " << port_);
                exit(1);
            }

            tagged_fields = boost::assign::list_of ("tenant") ("source-address") ("geo")
                 ("service-name") ("application") ("nested-application") ("destination-zone-name")
                 ("source-zone-name") ("username") ("roles") ("hostname") ("location") ("session-id-32");
            int_fields = boost::assign::list_of ("packets-from-client") ("bytes-from-client")
                 ("packets-from-server") ("bytes-from-server") ("session-id-32") ("source-port")
                 ("destination-port") ("nat-source-port") ("nat-destination-port")
                 ("protocol-id") ("elapsed-time");
            StartReceive();
            return true;
        }


        void StructuredSyslogPostParsing (SyslogParser::syslog_m_t &v) {
          /*
          syslog format: <14>Dec 10 00:18:07 csp-ucpe-bglr51 RT_FLOW: APPTRACK_SESSION_CLOSE [junos@2636.1.1.1.2.26
          reason="TCP RST" source-address="4.0.0.3" source-port="13175" destination-address="5.0.0.7"
          destination-port="48334" service-name="None" application="MTV" nested-application="None"
          nat-source-address="10.110.110.10" nat-source-port="13175" destination-address="96.9.139.213"
          nat-destination-port="48334" src-nat-rule-name="None" dst-nat-rule-name="None" protocol-id="6"
          policy-name="dmz-out" source-zone-name="DMZ" destination-zone-name="Internet" session-id-32="44292"
          packets-from-client="7" bytes-from-client="1421" packets-from-server="6" bytes-from-server="1133"
          elapsed-time="4" username="Frank" roles="Engineering"]
          */
          const std::string body(SyslogParser::GetMapVals(v, "body", ""));
          std::size_t start = 0, end = 0;

          end = body.find('[', start);
          if (end == std::string::npos) {
            LOG(ERROR, "BAD structured_syslog");
            return;
          }
          end = body.find(']', end+1);
          if (end == std::string::npos) {
            LOG(ERROR, "BAD structured_syslog");
            return;
          }
          end = body.find(' ', start);
          const std::string tag = body.substr(start, end-start);
          start = end + 1;
          LOG(DEBUG, "structured_syslog - tag: " << tag );
          v.insert(std::pair<std::string, SyslogParser::Holder>("tag",
                SyslogParser::Holder("tag", tag)));

          end = body.find(' ', start);
          const std::string hardware = body.substr(start+1, end-start);
          start = end + 1;
          LOG(DEBUG, "structured_syslog - hardware: " << hardware );
          v.insert(std::pair<std::string, SyslogParser::Holder>("hardware",
                SyslogParser::Holder("hardware", hardware)));

          end = body.find(']', start);
          std::string structured_part = body.substr(start, end-start);
          LOG(DEBUG, "structured_syslog - struct_data: " << structured_part);

          start = 0;
          end = 0;
          while ((end = structured_part.find('=', start)) != std::string::npos) {
            const std::string key = structured_part.substr(start, end - start);
            start = end + 2;
            end = structured_part.find('"', start);
            const std::string val = structured_part.substr(start, end - start);
            LOG(DEBUG, "structured_syslog - " << key << " : " << val);
            start = end + 2;
            if (std::find(int_fields.begin(), int_fields.end(), key) != int_fields.end()) {
                int ival = atoi(val.c_str());
                v.insert(std::pair<std::string, SyslogParser::Holder>(key,
                      SyslogParser::Holder(key, ival)));
            } else {
                v.insert(std::pair<std::string, SyslogParser::Holder>(key,
                      SyslogParser::Holder(key, val)));
            }

          }

          /*
          Remove unnecessary fields so that we avoid writing them into DB
          */
          v.erase("facsev");
          v.erase("month");
          v.erase("day");
          v.erase("hour");
          v.erase("min");
          v.erase("sec");
          /*
          TODO: Add additional fields like tenant, region, app_category and app_group
          Need to have a mapping of hostip->tenant/region and app->app_category/app_group in configDB which needs
          to be referred to while adding new fields
          */
        }

        static inline void PushStructuredSyslogAttribsAndTags(DbHandler::AttribMap *attribs,
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

        void PushStructuredSyslogStats(SyslogParser::syslog_m_t v, const std::string &stat_attr_name,
                                       StatWalker *stat_walker) {
            // At the top level the stat walker already has the tags so
            // we need to skip going through the elemental types and
            // creating the tag and attribute maps. At lower levels,
            // only strings are inserted into the tag map
            bool top_level(stat_attr_name.empty());
            DbHandler::AttribMap attribs;
            StatWalker::TagMap tags;

            if (!top_level) {

              int i = 0;
              while (!v.empty()) {
              /*
              All the key-value pairs in v will be iterated over and pushed into the stattable
              */
                  SyslogParser::Holder d = v.begin()->second;
                  const std::string &key(d.key);
                  bool is_tag = false;
                   if (std::find(tagged_fields.begin(), tagged_fields.end(), key) != tagged_fields.end()) {
                    is_tag = true;
                   }

                  if (d.type == SyslogParser::str_type) {
                    const std::string &sval(d.s_val);
                    LOG(DEBUG, i++ << " - " << key << " : " << sval << " is_tag: " << is_tag);
                    DbHandler::Var svalue(sval);
                    PushStructuredSyslogAttribsAndTags(&attribs, &tags, is_tag, key, svalue);
                  }
                  else if (d.type == SyslogParser::int_type) {
                    LOG(DEBUG, i++ << " - " << key << " : " << d.i_val << " is_tag: " << is_tag);
                    if (d.i_val >= 0) { /* added this condition as having -ve values was resulting in a crash */
                        DbHandler::Var ivalue(static_cast<uint64_t>(d.i_val));
                        PushStructuredSyslogAttribsAndTags(&attribs, &tags, is_tag, key, ivalue);
                    }
                  }
                  else {
                    LOG(ERROR, i++ << "BAD Type: ");
                  }
                  v.erase(v.begin());
              }

              // Push the stats at this level
              stat_walker->Push(stat_attr_name, tags, attribs);

            }
            // Perform traversal of children
            else {
                PushStructuredSyslogStats(v, SyslogParser::GetMapVals(v, "tag", ""), stat_walker);
            }

            // Pop the stats at this level
            if (!top_level) {
                stat_walker->Pop();
            }
        }

        void PushStructuredSyslogTopLevelTags(SyslogParser::syslog_m_t v, StatWalker::TagMap *top_tags) {
            StatWalker::TagVal tvalue;
            const std::string saddr(SyslogParser::GetMapVals(v, "ip", ""));
            tvalue.val = saddr;
            top_tags->insert(make_pair("Source", tvalue));
        }

        void StructuredSyslogPush(SyslogParser::syslog_m_t v, StatWalker::StatTableInsertFn stat_db_callback) {
            const std::string &message_name(SyslogParser::GetMapVals(v, "prog", ""));
            LOG(DEBUG, "writing into: StatTable." << message_name);
            StatWalker::TagMap top_tags;
            PushStructuredSyslogTopLevelTags(v, &top_tags);
            StatWalker stat_walker(stat_db_callback, (uint64_t)SyslogParser::GetMapVal (v, "timestamp", 0),
                                   message_name, top_tags);
            PushStructuredSyslogStats(v, std::string(), &stat_walker);
        }

        bool ProcessStructuredSyslog(const uint8_t *data, size_t len,
            const boost::asio::ip::udp::endpoint &remote_endpoint,
            StatWalker::StatTableInsertFn stat_db_callback) {
          boost::asio::ip::address remote_address(remote_endpoint.address());
          boost::system::error_code ec;
          const std::string ip(remote_address.to_string(ec));
          const uint8_t *p = data;

          SyslogParser::syslog_m_t v;
          while (!*(p + len - 1))
              --len;
          bool r = SyslogParser::parse_syslog (p, p + len, v);
#ifdef STRUCTURED_SYSLOG_DEBUG
          std::string app_str (p, p + len);
          LOG(DEBUG, "structured_syslog: " << app_str << " parsed " << r << ".");
#endif
          if (r) {
              v.insert(std::pair<std::string, SyslogParser::Holder>("ip",
                    SyslogParser::Holder("ip", ip)));
              SyslogParser::PostParsing(v);
              StructuredSyslogPostParsing(v);
              StructuredSyslogPush(v, stat_db_callback);
          }

          return r;
        }

        virtual void OnRead(const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::udp::endpoint &remote_endpoint) {
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));

            if (!ProcessStructuredSyslog(boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, remote_endpoint, stat_db_callback_)) {
                LOG(ERROR, "ProcessStructuredSyslog FAILED for : " << remote_endpoint);
            } else {
                LOG(DEBUG, "ProcessStructuredSyslog SUCCESS for : " << remote_endpoint);
            }

            DeallocateBuffer(recv_buffer);
        }

    private:

        static const int kMaxInitRetries = 5;
        static const int kBufferSize = 32 * 1024;

        structured_syslog::impl::StructuredSyslogReader reader_;
        uint16_t port_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        std::vector<std::string> tagged_fields;
        std::vector<std::string> int_fields;
    };

    StructuredSyslogUdpServer *udp_server_;
};


StructuredSyslogServer::StructuredSyslogServer(EventManager *evm,
    uint16_t udp_server_port, StatWalker::StatTableInsertFn stat_db_fn) {
    impl_ = new StructuredSyslogServerImpl(evm, udp_server_port, stat_db_fn);
}

StructuredSyslogServer::~StructuredSyslogServer() {
    if (impl_) {
        delete impl_;
        impl_ = NULL;
    }
}

bool StructuredSyslogServer::Initialize() {
    return impl_->Initialize();
}

void StructuredSyslogServer::Shutdown() {
    impl_->Shutdown();
}

boost::asio::ip::udp::endpoint StructuredSyslogServer::GetLocalEndpoint(
    boost::system::error_code *ec) {
    return impl_->GetLocalEndpoint(ec);
}

}  // namespace structured_syslog

