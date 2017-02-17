//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <utility>
#include <string>
#include <vector>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive_ptr.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_message_builder.h>

#include <base/logging.h>
#include <io/io_types.h>
#include <io/tcp_server.h>
#include <io/tcp_session.h>
#include <io/udp_server.h>

#include "analytics/structured_syslog_server.h"
#include "analytics/structured_syslog_server_impl.h"
#include "generator.h"
#include "analytics/syslog_collector.h"
#include "analytics/structured_syslog_config.h"

//#define STRUCTURED_SYSLOG_DEBUG 1

using std::make_pair;

namespace structured_syslog {

namespace impl {

bool StructuredSyslogPostParsing (SyslogParser::syslog_m_t &v, StructuredSyslogServerConfig *config_obj) {
  /*
  syslog format: <14>1 2016-12-06T11:38:19.818+02:00 csp-ucpe-bglr51 RT_FLOW: APPTRACK_SESSION_CLOSE [junos@2636.1.1.1.2.26
  reason="TCP RST" source-address="4.0.0.3" source-port="13175" destination-address="5.0.0.7"
  destination-port="48334" service-name="None" application="HTTP" nested-application="Facebook"
  nat-source-address="10.110.110.10" nat-source-port="13175" destination-address="96.9.139.213"
  nat-destination-port="48334" src-nat-rule-name="None" dst-nat-rule-name="None" protocol-id="6"
  policy-name="dmz-out" source-zone-name="DMZ" destination-zone-name="Internet" session-id-32="44292"
  packets-from-client="7" bytes-from-client="1421" packets-from-server="6" bytes-from-server="1133"
  elapsed-time="4" username="Frank" roles="Engineering" rule="rule1"]
  */

  /*
  Remove unnecessary fields so that we avoid writing them into DB
  */
  v.erase("msglen");
  v.erase("facsev");
  v.erase("severity");
  v.erase("facility");
  v.erase("pid");
  v.erase("version");
  v.erase("year");
  v.erase("month");
  v.erase("day");
  v.erase("hour");
  v.erase("min");
  v.erase("sec");
  v.erase("msec");

  const std::string body(SyslogParser::GetMapVals(v, "body", ""));
  std::size_t start = 0, end = 0;
  v.erase("body");

  end = body.find('[', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog");
    return false;
  }
  end = body.find(']', end+1);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog");
    return false;
  }
  end = body.find(' ', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog");
    return false;
  }
  const std::string tag = body.substr(start, end-start);
  if (std::find(config_obj->messages_handled.begin(),
                config_obj->messages_handled.end(), tag) ==
                config_obj->messages_handled.end()) {
    LOG(DEBUG, "structured_syslog - not processing: " << tag );
    return false;
  }
  start = end + 1;
  LOG(DEBUG, "structured_syslog - tag: " << tag );
  v.insert(std::pair<std::string, SyslogParser::Holder>("tag",
        SyslogParser::Holder("tag", tag)));

  end = body.find(' ', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog");
    return false;
  }
  const std::string hardware = body.substr(start+1, end-start-1);
  start = end + 1;
  LOG(DEBUG, "structured_syslog - hardware: " << hardware);
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
    if (end == std::string::npos) {
        LOG(ERROR, "BAD structured_syslog");
        return false;
      }
    const std::string val = structured_part.substr(start, end - start);
    LOG(DEBUG, "structured_syslog - " << key << " : " << val);
    start = end + 2;
    if (std::find(config_obj->int_fields.begin(), config_obj->int_fields.end(),
                  key) != config_obj->int_fields.end()) {
        int ival = atoi(val.c_str());
        v.insert(std::pair<std::string, SyslogParser::Holder>(key,
              SyslogParser::Holder(key, ival)));
    } else {
        v.insert(std::pair<std::string, SyslogParser::Holder>(key,
              SyslogParser::Holder(key, val)));
    }

  }

  return true;
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
                               StatWalker *stat_walker, std::vector<std::string> tagged_fields) {
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
        PushStructuredSyslogStats(v, "data", stat_walker, tagged_fields);
    }

    // Pop the stats at this level
    if (!top_level) {
        stat_walker->Pop();
    }
}

void PushStructuredSyslogTopLevelTags(SyslogParser::syslog_m_t v, StatWalker::TagMap *top_tags) {
    StatWalker::TagVal tvalue;
    const std::string ip(SyslogParser::GetMapVals(v, "ip", ""));
    const std::string saddr(SyslogParser::GetMapVals(v, "hostname", ip));
    tvalue.val = saddr;
    top_tags->insert(make_pair("Source", tvalue));
}

void StructuredSyslogPush(SyslogParser::syslog_m_t v, StatWalker::StatTableInsertFn stat_db_callback,
    std::vector<std::string> tagged_fields) {
    StatWalker::TagMap top_tags;
    PushStructuredSyslogTopLevelTags(v, &top_tags);
    StatWalker stat_walker(stat_db_callback, (uint64_t)SyslogParser::GetMapVal (v, "timestamp", 0),
                           "JunosSyslog", top_tags);
    PushStructuredSyslogStats(v, std::string(), &stat_walker, tagged_fields);
}

void StructuredSyslogDecorate (SyslogParser::syslog_m_t &v, StructuredSyslogServerConfig *config_obj) {

    int from_client = SyslogParser::GetMapVal(v, "bytes-from-client", 0);
    int from_server = SyslogParser::GetMapVal(v, "bytes-from-server", 0);

    v.insert(std::pair<std::string, SyslogParser::Holder>("total-bytes",
    SyslogParser::Holder("total-bytes", (from_client + from_server))));
    v.insert(std::pair<std::string, SyslogParser::Holder>("tenant",
    SyslogParser::Holder("tenant", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("location",
    SyslogParser::Holder("location", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("device",
    SyslogParser::Holder("device", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
    SyslogParser::Holder("app-category", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
    SyslogParser::Holder("app-subcategory", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
    SyslogParser::Holder("app-groups", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
    SyslogParser::Holder("app-risk", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("app-service-tags",
    SyslogParser::Holder("app-service-tags", "UNKNOWN")));

    if (config_obj->structured_syslog_config_ != NULL) {
        std::string hn = SyslogParser::GetMapVals(v, "hostname", "");
        boost::shared_ptr<HostnameRecord> hr =
                config_obj->structured_syslog_config_->GetHostnameRecord(hn);
        if (hr != NULL) {
            LOG(DEBUG, "StructuredSyslogDecorate hostname record: " << hn);
            const std::string tenant = hr->tenant();
            if (!tenant.empty()) {
                v.erase("tenant");
                v.insert(std::pair<std::string, SyslogParser::Holder>("tenant",
                SyslogParser::Holder("tenant", tenant)));
            }
            const std::string location = hr->location();
            if (!location.empty()) {
                v.erase("location");
                v.insert(std::pair<std::string, SyslogParser::Holder>("location",
                SyslogParser::Holder("location", location)));
            }
            const std::string device = hr->device();
            if (!device.empty()) {
                v.erase("device");
                v.insert(std::pair<std::string, SyslogParser::Holder>("device",
                SyslogParser::Holder("device", device)));
            }
            std::string an = SyslogParser::GetMapVals(v, "nested-application", "");
            std::string rule = SyslogParser::GetMapVals(v, "rule", "None");
            std::string rule_ar_name = tenant + '/' + rule + '/' + device + '/' + an;
            boost::shared_ptr<TenantApplicationRecord> rar =
                    config_obj->structured_syslog_config_->GetTenantApplicationRecord(rule_ar_name);
            boost::shared_ptr<ApplicationRecord> ar =
                    config_obj->structured_syslog_config_->GetApplicationRecord(an);
            if (rar != NULL) {
                LOG(DEBUG, "StructuredSyslogDecorate device application record: " << rule_ar_name);
                const std::string tenant_app_category = rar->tenant_app_category();
                if (!tenant_app_category.empty()) {
                    v.erase("app-category");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
                    SyslogParser::Holder("app-category", tenant_app_category)));
                }
                const std::string tenant_app_subcategory = rar->tenant_app_subcategory();
                if (!tenant_app_subcategory.empty()) {
                    v.erase("app-subcategory");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
                    SyslogParser::Holder("app-subcategory", tenant_app_subcategory)));
                }
                const std::string tenant_app_groups = rar->tenant_app_groups();
                if (!tenant_app_groups.empty()) {
                    v.erase("app-groups");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
                    SyslogParser::Holder("app-groups", tenant_app_groups)));
                }
                const std::string tenant_app_risk = rar->tenant_app_risk();
                if (!tenant_app_risk.empty()) {
                    v.erase("app-risk");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
                    SyslogParser::Holder("app-risk", tenant_app_risk)));
                }
                const std::string tenant_app_service_tags = rar->tenant_app_service_tags();
                if (!tenant_app_service_tags.empty()) {
                    v.erase("app-service-tags");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-service-tags",
                    SyslogParser::Holder("app-service-tags", tenant_app_service_tags)));
                }
            }
            else if (ar != NULL) {
                LOG(DEBUG, "StructuredSyslogDecorate application record: " << an);
                const std::string app_category = ar->app_category();
                if (!app_category.empty()) {
                    v.erase("app-category");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
                    SyslogParser::Holder("app-category", app_category)));
                }
                const std::string app_subcategory = ar->app_subcategory();
                if (!app_subcategory.empty()) {
                    v.erase("app-subcategory");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
                    SyslogParser::Holder("app-subcategory", app_subcategory)));
                }
                const std::string app_groups = ar->app_groups();
                if (!app_groups.empty()) {
                    v.erase("app-groups");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
                    SyslogParser::Holder("app-groups", app_groups)));
                }
                const std::string app_risk = ar->app_risk();
                if (!app_risk.empty()) {
                    v.erase("app-risk");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
                    SyslogParser::Holder("app-risk", app_risk)));
                }
                const std::string app_service_tags = ar->app_service_tags();
                if (!app_service_tags.empty()) {
                    v.erase("app-service-tags");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-service-tags",
                    SyslogParser::Holder("app-service-tags", app_service_tags)));
                }
            }
        }
        else {
            LOG(DEBUG, "StructuredSyslogDecorate: NULL");
        }
    }
}

bool ProcessStructuredSyslog(const uint8_t *data, size_t len,
    const boost::asio::ip::address remote_address,
    StatWalker::StatTableInsertFn stat_db_callback, StructuredSyslogServerConfig *config_obj) {
  boost::system::error_code ec;
  const std::string ip(remote_address.to_string(ec));
  const uint8_t *p = data;
  size_t start = 0;
  bool r;

  while (!*(p + len - 1))
      --len;
  do {
      SyslogParser::syslog_m_t v;
      r = SyslogParser::parse_syslog (p + start, p + len, v);
#ifdef STRUCTURED_SYSLOG_DEBUG
      std::string app_str (p + start, p + len);
      LOG(DEBUG, "structured_syslog: " << app_str << " parsed " << r << ".");
#endif
      if (r) {
          v.insert(std::pair<std::string, SyslogParser::Holder>("ip",
                SyslogParser::Holder("ip", ip)));
          std::stringstream lenss;
          lenss << SyslogParser::GetMapVal(v, "msglen", 0);
          std::string lenstr = lenss.str();
          start += SyslogParser::GetMapVal(v, "msglen", len) + lenstr.length() + 1;
          SyslogParser::PostParsing(v);
          if (StructuredSyslogPostParsing(v, config_obj))
          {
              StructuredSyslogDecorate(v, config_obj);
              StructuredSyslogPush(v, stat_db_callback, config_obj->tagged_fields);
          }
      }
      while (!v.empty()) {
          v.erase(v.begin());
      }
  } while (r && start<len);
  return r;
}

}  // namespace impl

class StructuredSyslogServer::StructuredSyslogServerImpl {
public:
    StructuredSyslogServerImpl(EventManager *evm, uint16_t port,
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection,
        StatWalker::StatTableInsertFn stat_db_callback) :
        udp_server_(new StructuredSyslogUdpServer(evm, port,
            stat_db_callback)),
        tcp_server_(new StructuredSyslogTcpServer(evm, port,
            stat_db_callback)),
        structured_syslog_config_(new StructuredSyslogConfig(cfgdb_connection)),
        hostname_record_poll_timer_(TimerManager::CreateTimer(*evm->io_service(),
            "structured_syslog_config hostname record poll timer",
        TaskScheduler::GetInstance()->GetTaskId("vnc-api http client"))),
        application_record_poll_timer_(TimerManager::CreateTimer(*evm->io_service(),
            "structured_syslog_config application record poll timer",
        TaskScheduler::GetInstance()->GetTaskId("vnc-api http client"))) {
        hostname_record_poll_timer_->Start(30000,
        boost::bind(&StructuredSyslogServerImpl::PollHostnameRecords, this),
        boost::bind(&StructuredSyslogServerImpl::PollStructuredSyslogConfigErrorHandler, this, _1, _2));

        application_record_poll_timer_->Start(60000,
        boost::bind(&StructuredSyslogServerImpl::PollApplicationRecords, this),
        boost::bind(&StructuredSyslogServerImpl::PollStructuredSyslogConfigErrorHandler, this, _1, _2));
    }

    ~StructuredSyslogServerImpl() {
        if (hostname_record_poll_timer_) {
            TimerManager::DeleteTimer(hostname_record_poll_timer_);
            hostname_record_poll_timer_ = NULL;
        }
        if (application_record_poll_timer_) {
            TimerManager::DeleteTimer(application_record_poll_timer_);
            application_record_poll_timer_ = NULL;
        }
    }

    void PollStructuredSyslogConfigErrorHandler(string error_name,
        string error_message) {
        LOG(ERROR, "StructuredSyslogConfig poll Timer Err: " << error_name << " " << error_message);
    }

    bool PollHostnameRecords() {
        LOG(DEBUG, "PollHostnameRecords start");
        hostname_record_poll_timer_->Reschedule(kHostnameRecordPollInterval);
        if(structured_syslog_config_) structured_syslog_config_->PollHostnameRecords();
        return true;
    }

    bool PollApplicationRecords() {
        LOG(DEBUG, "PollApplicationRecords start");
        application_record_poll_timer_->Reschedule(kApplicationRecordPollInterval);
        if(structured_syslog_config_) structured_syslog_config_->PollApplicationRecords();
        return true;
    }

    StructuredSyslogConfig *GetStructuredSyslogConfig() {
        return structured_syslog_config_;
    }

    bool Initialize() {
        StructuredSyslogServerConfig *config_obj = new StructuredSyslogServerConfig(GetStructuredSyslogConfig());
        if (udp_server_->Initialize(config_obj)) {
            return tcp_server_->Initialize(config_obj);
        } else {
            delete config_obj;
            return false;
        }
    }

    void Shutdown() {
        udp_server_->Shutdown();
        UdpServerManager::DeleteServer(udp_server_);
        udp_server_ = NULL;
        tcp_server_->Shutdown();
        TcpServerManager::DeleteServer(tcp_server_);
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

        bool Initialize(StructuredSyslogServerConfig *config_obj) {
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
            StartReceive();
            config_obj_ = config_obj;
            return true;
        }

        virtual void OnRead(const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::udp::endpoint &remote_endpoint) {
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));
            if (!structured_syslog::impl::ProcessStructuredSyslog(boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, remote_endpoint.address(), stat_db_callback_, config_obj_)) {
                LOG(ERROR, "ProcessStructuredSyslog UDP FAILED for : " << remote_endpoint);
            } else {
                LOG(DEBUG, "ProcessStructuredSyslog UDP SUCCESS for : " << remote_endpoint);
            }

            DeallocateBuffer(recv_buffer);
        }

    private:

        static const int kMaxInitRetries = 5;
        static const int kBufferSize = 32 * 1024;

        uint16_t port_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        StructuredSyslogServerConfig *config_obj_;
    };

    class StructuredSyslogTcpServer;

    class StructuredSyslogTcpSession : public TcpSession {
    public:
        typedef boost::intrusive_ptr<StructuredSyslogTcpSession> StructuredSyslogTcpSessionPtr;
        StructuredSyslogTcpSession (StructuredSyslogTcpServer *server, Socket *socket) :
            TcpSession(server, socket) {
            //set_observer(boost::bind(&SyslogTcpSession::OnEvent, this, _1, _2));
        }
        virtual void OnRead (const boost::asio::const_buffer buf) {
            boost::system::error_code ec;
            StructuredSyslogTcpServer *sserver = dynamic_cast<StructuredSyslogTcpServer *>(server());
            //TODO: handle error
            sserver->ReadMsg(StructuredSyslogTcpSessionPtr(this), buf, socket ()->remote_endpoint(ec));
        }
    };

    //
    // StructuredSyslogTcpServer
    //
    class StructuredSyslogTcpServer : public TcpServer {
    public:
        typedef boost::intrusive_ptr<StructuredSyslogTcpSession> StructuredSyslogTcpSessionPtr;
        StructuredSyslogTcpServer(EventManager *evm, uint16_t port,
            StatWalker::StatTableInsertFn stat_db_callback) :
            TcpServer(evm),
            port_(port),
            session_(NULL),
            stat_db_callback_(stat_db_callback) {
        }

        virtual TcpSession *AllocSession(Socket *socket)
        {
            session_ = new StructuredSyslogTcpSession (this, socket);
            return session_;
        }

        bool Initialize(StructuredSyslogServerConfig *config_obj) {
            TcpServer::Initialize (port_);
            LOG(DEBUG, __func__ << " Initialization of TCP StructuredSyslog listener @" << port_);
            config_obj_ = config_obj;
            return true;
        }

        virtual void ReadMsg(StructuredSyslogTcpSessionPtr sess, const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::tcp::endpoint &remote_endpoint) {
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));

            if (!structured_syslog::impl::ProcessStructuredSyslog(
                    boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, remote_endpoint.address(), stat_db_callback_, config_obj_)) {
                LOG(ERROR, "ProcessStructuredSyslog TCP FAILED for : " << remote_endpoint);
            } else {
                LOG(DEBUG, "ProcessStructuredSyslog TCP SUCCESS for : " << remote_endpoint);
            }

            sess->server()->DeleteSession (sess.get());
            sess->ReleaseBuffer(recv_buffer);
        }

    private:

        static const int kMaxInitRetries = 5;
        static const int kBufferSize = 32 * 1024;

        uint16_t port_;
        StructuredSyslogTcpSession *session_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        StructuredSyslogServerConfig *config_obj_;
    };
    StructuredSyslogUdpServer *udp_server_;
    StructuredSyslogTcpServer *tcp_server_;
    StructuredSyslogConfig * structured_syslog_config_;
    Timer *hostname_record_poll_timer_;
    Timer *application_record_poll_timer_;
    static const int kHostnameRecordPollInterval = 120 * 1000; // in ms
    static const int kApplicationRecordPollInterval = 1300 * 1000; // in ms
};


StructuredSyslogServer::StructuredSyslogServer(EventManager *evm,
    uint16_t port, boost::shared_ptr<ConfigDBConnection> cfgdb_connection,
    StatWalker::StatTableInsertFn stat_db_fn) {
    impl_ = new StructuredSyslogServerImpl(evm, port, cfgdb_connection, stat_db_fn);
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

StructuredSyslogServerConfig::StructuredSyslogServerConfig(StructuredSyslogConfig *structured_syslog_config):
    structured_syslog_config_(structured_syslog_config) {
    Init();
}

StructuredSyslogServerConfig::~StructuredSyslogServerConfig() {
    messages_handled.clear();
    tagged_fields.clear();
    int_fields.clear();
}

void StructuredSyslogServerConfig::Init() {
    messages_handled = boost::assign::list_of ("APPTRACK_SESSION_CLOSE");
    tagged_fields = boost::assign::list_of ("tenant") ("source-address") ("tag")
                 ("location") ("app-category") ("app-groups") ("service-tags")
                 ("application") ("nested-application") ("destination-zone-name")
                 ("source-zone-name") ("username") ("roles") ("hostname") ("rule")
                 ("session-id-32");
    int_fields = boost::assign::list_of ("packets-from-client") ("bytes-from-client")
                 ("packets-from-server") ("bytes-from-server") ("session-id-32") ("source-port")
                 ("destination-port") ("nat-source-port") ("nat-destination-port")
                 ("protocol-id") ("elapsed-time") ("total-bytes");
}

}  // namespace structured_syslog
