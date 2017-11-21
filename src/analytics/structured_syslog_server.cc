//
// Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
//

#include <utility>
#include <string>
#include <vector>
#include <boost/asio/buffer.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/algorithm/string.hpp>

#include <sandesh/sandesh_message_builder.h>

#include <base/logging.h>
#include <io/io_types.h>
#include <io/tcp_server.h>
#include <io/tcp_session.h>
#include <io/udp_server.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "analytics/structured_syslog_server.h"
#include "analytics/structured_syslog_server_impl.h"
#include "generator.h"
#include "analytics/syslog_collector.h"
#include "analytics/structured_syslog_config.h"

using std::make_pair;

namespace structured_syslog {

namespace impl {

void StructuredSyslogDecorate(SyslogParser::syslog_m_t &v, StructuredSyslogConfig *config_obj,
                              boost::shared_ptr<std::string> msg);
void StructuredSyslogPush(SyslogParser::syslog_m_t v, StatWalker::StatTableInsertFn stat_db_callback,
    std::vector<std::string> tagged_fields);
void StructuredSyslogUVESummarize(SyslogParser::syslog_m_t v, bool summarize_user);
boost::shared_ptr<std::string> StructuredSyslogJsonMessage(SyslogParser::syslog_m_t v);

size_t DecorateMsg(boost::shared_ptr<std::string> msg, const std::string &key, const std::string &val, size_t prev_pos) {
    if (msg == NULL) {
        return 0;
    }
    size_t pos = msg->find(']', prev_pos);
    if (pos == std::string::npos) {
        return 0;
    }
    std::string insert_str = " " + key + "=\"" + val + "\"";
    msg->insert(pos, insert_str);
    return pos;
}

bool ParseStructuredPart(SyslogParser::syslog_m_t *v, const std::string &structured_part,
                         const std::vector<std::string> &int_fields, boost::shared_ptr<std::string> fwd_msg){
    std::size_t start = 0, end = 0;
    size_t prev_pos = 0;
    while ((end = structured_part.find('=', start)) != std::string::npos) {
        const std::string key = structured_part.substr(start, end - start);
        start = end + 2;
        end = structured_part.find('"', start);
        if (end == std::string::npos) {
            LOG(ERROR, "BAD structured_syslog: " << structured_part);
            return false;
          }
        const std::string val = structured_part.substr(start, end - start);
        LOG(DEBUG, "structured_syslog - " << key << " : " << val);
        start = end + 2;
        if (std::find(int_fields.begin(), int_fields.end(),
                      key) != int_fields.end()) {
            LOG(DEBUG, "int field - " << key);
            int64_t ival = atol(val.c_str());
            v->insert(std::pair<std::string, SyslogParser::Holder>(key,
                  SyslogParser::Holder(key, ival)));
        } else {
            v->insert(std::pair<std::string, SyslogParser::Holder>(key,
                  SyslogParser::Holder(key, val)));
        }
        prev_pos = DecorateMsg(fwd_msg, key, val, prev_pos);
  }
  return true;
}

bool StructuredSyslogPostParsing (SyslogParser::syslog_m_t &v, StructuredSyslogConfig *config_obj,
                                  StatWalker::StatTableInsertFn stat_db_callback, const uint8_t *message,
                                  int message_len, boost::shared_ptr<StructuredSyslogForwarder> forwarder){
  /*
  syslog format: <14>1 2016-12-06T11:38:19.818+02:00 csp-ucpe-bglr51 RT_FLOW: APPTRACK_SESSION_CLOSE [junos@2636.1.1.1.2.26
  reason="TCP RST" source-address="4.0.0.3" source-port="13175" destination-address="5.0.0.7"
  destination-port="48334" service-name="None" application="HTTP" nested-application="Facebook"
  nat-source-address="10.110.110.10" nat-source-port="13175" destination-address="96.9.139.213"
  nat-destination-port="48334" src-nat-rule-name="None" dst-nat-rule-name="None" protocol-id="6"
  policy-name="dmz-out" source-zone-name="DMZ" destination-zone-name="Internet" session-id-32="44292"
  packets-from-client="7" bytes-from-client="1421" packets-from-server="6" bytes-from-server="1133"
  elapsed-time="4" username="Frank" roles="Engineering" encrypted="No" profile-name="pf1" rule-name="1"
  routing-instance="inst1" destination-interface-name="xe-1/2/0.0"]
  */

  /*
  Remove unnecessary fields so that we avoid writing them into DB
  */
  v.erase("msglen");
  v.erase("severity");
  v.erase("facility");
  v.erase("year");
  v.erase("month");
  v.erase("day");
  v.erase("hour");
  v.erase("min");
  v.erase("sec");
  v.erase("msec");
  if (SyslogParser::GetMapVal(v, "pid", -1) == -1)
    v.erase("pid");

  const std::string body(SyslogParser::GetMapVals(v, "body", ""));
  std::size_t start = 0, end = 0;
  v.erase("body");

  end = body.find('[', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog: " << body);
    return false;
  }
  end = body.find(']', end+1);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog: " << body);
    return false;
  }
  end = body.find(' ', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog: " << body);
    return false;
  }
  const std::string tag = body.substr(start, end-start);
  LOG(DEBUG, "structured_syslog - tag: " << tag );
  boost::shared_ptr<MessageConfig> mc = config_obj->GetMessageConfig(tag);
  if (mc == NULL || ((mc->process_and_store() == false) && (mc->forward() == false)
     && (mc->process_and_summarize() == false))) {
    LOG(DEBUG, "structured_syslog - not processing message: " << tag );
    return false;
  }
  LOG(DEBUG, "structured_syslog - message_config: " << mc->name());
  boost::shared_ptr<std::string> msg;
  boost::shared_ptr<std::string> hostname;
  start = end + 1;
  v.insert(std::pair<std::string, SyslogParser::Holder>("tag",
        SyslogParser::Holder("tag", tag)));

  end = body.find(' ', start);
  if (end == std::string::npos) {
    LOG(ERROR, "BAD structured_syslog: " << body);
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

  bool ret = ParseStructuredPart(&v, structured_part, mc->ints(), msg);
  if (ret == false){
    return ret;
  }
  if (mc->process_and_store() == true || mc->process_before_forward() == true
      || mc->process_and_summarize() == true) {
      if (forwarder != NULL && mc->forward() == true) {
        msg.reset(new std::string (message, message + message_len));
        hostname.reset(new std::string (SyslogParser::GetMapVals(v, "hostname", "")));
      }
      StructuredSyslogDecorate(v, config_obj, msg);
      if (mc->process_and_summarize() == true) {
        bool syslog_summarize_user = mc->process_and_summarize_user();
        StructuredSyslogUVESummarize(v, syslog_summarize_user);
      }
      if (forwarder != NULL && mc->forward() == true &&
          mc->process_before_forward() == true) {
        std::stringstream msglength;
        msglength << msg->length();
        msg->insert(0, msglength.str() + " ");
        LOG(DEBUG, "forwarding after decoration - " << *msg);
        boost::shared_ptr<std::string> json_msg;
        if (forwarder->kafkaForwarder()) {
            json_msg = StructuredSyslogJsonMessage(v);
        }
        boost::shared_ptr<StructuredSyslogQueueEntry> ssqe(new StructuredSyslogQueueEntry(msg, msg->length(),
                                                                                          json_msg, hostname));
        forwarder->Forward(ssqe);
      }
      if (mc->process_and_store() == true) {
        StructuredSyslogPush(v, stat_db_callback, mc->tags());
      }
  }
  if (forwarder != NULL && mc->forward() == true &&
      mc->process_before_forward() == false) {
    msg.reset(new std::string (message, message + message_len));
    hostname.reset(new std::string (SyslogParser::GetMapVals(v, "hostname", "")));
    std::stringstream msglength;
    msglength << msg->length();
    msg->insert(0, msglength.str() + " ");
    LOG(DEBUG, "forwarding without decoration - " << *msg);
    boost::shared_ptr<std::string> json_msg;
    if (forwarder->kafkaForwarder()) {
        json_msg = StructuredSyslogJsonMessage(v);
    }
    boost::shared_ptr<StructuredSyslogQueueEntry> ssqe(new StructuredSyslogQueueEntry(msg, msg->length(),
                                                                                      json_msg, hostname));
    forwarder->Forward(ssqe);
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
            LOG(DEBUG, "tagged field - " << key);
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

boost::shared_ptr<std::string>
StructuredSyslogJsonMessage(SyslogParser::syslog_m_t v) {
    contrail_rapidjson::Document doc;
    doc.SetObject();
    for (std::map<std::string, SyslogParser::Holder>::iterator i=v.begin(); i!=v.end(); ++i) {
        SyslogParser::Holder val = i->second;
        const std::string &key(val.key);
        if (val.type == SyslogParser::str_type) {
            contrail_rapidjson::Value vk;
            contrail_rapidjson::Value value(contrail_rapidjson::kStringType);
            value.SetString(val.s_val.c_str(), doc.GetAllocator());
            doc.AddMember(vk.SetString(key.c_str(),
                                 doc.GetAllocator()), value, doc.GetAllocator());
        }
        else if (val.type == SyslogParser::int_type) {
            contrail_rapidjson::Value vk;
            contrail_rapidjson::Value value(contrail_rapidjson::kNumberType);
            value.SetUint64(val.i_val);
            doc.AddMember(vk.SetString(key.c_str(),
                                 doc.GetAllocator()), value, doc.GetAllocator());
        }
    }
    contrail_rapidjson::StringBuffer buffer;
    contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    boost::shared_ptr<std::string> json_msg(new std::string(buffer.GetString()));
    return json_msg;
}

void StructuredSyslogUVESummarize(SyslogParser::syslog_m_t v, bool summarize_user) {
    LOG(DEBUG,"UVE: Processing and sending Strucutured syslogs as UVE with flag summarize_user:" << summarize_user);
    AppTrackRecord apprecord;
    const std::string location(SyslogParser::GetMapVals(v, "location", "UNKNOWN"));
    const std::string tenant(SyslogParser::GetMapVals(v, "tenant", "UNKNOWN"));
    std::string link1;
    std::string link2;
    int64_t link1_bytes = SyslogParser::GetMapVal(v, "uplink-tx-bytes", -1);
    int64_t link2_bytes = SyslogParser::GetMapVal(v, "uplink-rx-bytes", -1);
    if (link1_bytes == -1)
        link1_bytes = SyslogParser::GetMapVal(v, "bytes-from-client", 0);
    if (link2_bytes == -1)
        link2_bytes = SyslogParser::GetMapVal(v, "bytes-from-server", 0);

    if (boost::equals(SyslogParser::GetMapVals(v, "tag", "UNKNOWN"), "RT_FLOW_NEXTHOP_CHANGE")) {
        link1 = (SyslogParser::GetMapVals(v, "last-destination-interface-name", "UNKNOWN"));
        link2 = (SyslogParser::GetMapVals(v, "last-incoming-interface-name", "UNKNOWN"));
    } else {
        link1 = (SyslogParser::GetMapVals(v, "destination-interface-name", "UNKNOWN"));
        link2 = (SyslogParser::GetMapVals(v, "uplink-incoming-interface-name", link1));
    }
    const std::string sla_profile(SyslogParser::GetMapVals(v, "sla-profile", "UNKNOWN"));
    const std::string app_category(SyslogParser::GetMapVals(v, "app-category", "UNKNOWN"));

    //username => syslog.username or syslog.source-address
    std::string username(SyslogParser::GetMapVals(v, "username", "UNKNOWN"));
    if (boost::iequals(username, "unknown")) {
        username = SyslogParser::GetMapVals(v, "source-address", "UNKNOWN");
    }
    const std::string department(SyslogParser::GetMapVals(v, "source-zone-name", "UNKNOWN"));
    const std::string device_id(SyslogParser::GetMapVals(v, "device", "UNKNOWN"));

    const std::string uvename= tenant + "::" + location + "::" + device_id;
    apprecord.set_name(uvename);

    std::string appgroup = SyslogParser::GetMapVals(v, "app-groups", "UNKNOWN");
    std::string nested_appname = SyslogParser::GetMapVals(v, "nested-application", "UNKNOWN");
    std::string appname = SyslogParser::GetMapVals(v, "application", "UNKNOWN");
    std::string app_dept_info;
    if (boost::iequals(appgroup, "unknown")) {
        app_dept_info = nested_appname + "(" + appname + "/" + app_category + ")::" + department + "::";
    } else {
        app_dept_info = appgroup + "(" + nested_appname + ":" + appname + "/" + app_category +  ")" + "::" + department + "::";
    }
    if (boost::equals(SyslogParser::GetMapVals(v, "tag", "UNKNOWN"), "APPTRACK_SESSION_CLOSE")) {
        AppMetrics appmetric;
        appmetric.set_total_bytes(SyslogParser::GetMapVal(v, "total-bytes", 0));
        appmetric.set_session_duration(SyslogParser::GetMapVal(v, "elapsed-time", 0));
        appmetric.set_session_count(1);
        // Map 1:  app_metric_sla
        std::map<std::string, AppMetrics> appmetric_sla;
        std::string slamap_key(app_dept_info + sla_profile);
        LOG(DEBUG,"UVE: slamap key :" << slamap_key);
        appmetric_sla.insert(std::make_pair(slamap_key, appmetric));
        apprecord.set_app_metrics_sla(appmetric_sla);
        // Map 3:  app_metric_user: forward only if flag is true
        if (summarize_user == true) {
            std::map<std::string, AppMetrics> appmetric_user;
            std::string usermap_key(app_dept_info + username);
            LOG(DEBUG,"UVE: usermap key :" << usermap_key);
            appmetric_user.insert(std::make_pair(usermap_key, appmetric));
            apprecord.set_app_metrics_user(appmetric_user);
        }
    }
    if (boost::equals(link1, link2)) {
        AppMetrics appmetric;
        appmetric.set_total_bytes(link1_bytes + link2_bytes);
        // Map 2:  app_metric_link
        std::map<std::string, AppMetrics> appmetric_link;
        std::string linkmap_key(app_dept_info + link1);
        LOG(DEBUG,"UVE: linkmap key :" << linkmap_key);
        appmetric_link.insert(std::make_pair(linkmap_key, appmetric));
        apprecord.set_app_metrics_link(appmetric_link);
    } else {
        AppMetrics appmetric1;
        AppMetrics appmetric2;
        appmetric1.set_total_bytes(link1_bytes);
        appmetric2.set_total_bytes(link2_bytes);
        // Map 2:  app_metric_link
        std::map<std::string, AppMetrics> appmetric_link;
        std::string linkmap_key1(app_dept_info + link1);
        std::string linkmap_key2(app_dept_info + link2);
        LOG(DEBUG,"UVE: linkmap key1 :" << linkmap_key1);
        LOG(DEBUG,"UVE: linkmap key2 :" << linkmap_key2);
        appmetric_link.insert(std::make_pair(linkmap_key1, appmetric1));
        appmetric_link.insert(std::make_pair(linkmap_key2, appmetric2));
        apprecord.set_app_metrics_link(appmetric_link);
    }
    AppTrack::Send(apprecord, "ObjectCPETable");
    return;
}

void StructuredSyslogDecorate (SyslogParser::syslog_m_t &v, StructuredSyslogConfig *config_obj,
                               boost::shared_ptr<std::string> msg) {

    int64_t from_client = SyslogParser::GetMapVal(v, "bytes-from-client", 0);
    int64_t from_server = SyslogParser::GetMapVal(v, "bytes-from-server", 0);
    size_t prev_pos = 0;
    v.insert(std::pair<std::string, SyslogParser::Holder>("total-bytes",
    SyslogParser::Holder("total-bytes", (from_client + from_server))));
    std::stringstream total_bytes;
    total_bytes << (from_client + from_server);
    prev_pos = DecorateMsg(msg, "total-bytes", total_bytes.str(), prev_pos);
    v.insert(std::pair<std::string, SyslogParser::Holder>("tenant",
    SyslogParser::Holder("tenant", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("location",
    SyslogParser::Holder("location", "UNKNOWN")));
    v.insert(std::pair<std::string, SyslogParser::Holder>("device",
    SyslogParser::Holder("device", "UNKNOWN")));

    if (config_obj != NULL) {
        std::string hn = SyslogParser::GetMapVals(v, "hostname", "");
        boost::shared_ptr<HostnameRecord> hr =
                config_obj->GetHostnameRecord(hn);
        if (hr != NULL) {
            LOG(DEBUG, "StructuredSyslogDecorate hostname record: " << hn);
            const std::string tenant = hr->tenant();
            if (!tenant.empty()) {
                v.erase("tenant");
                v.insert(std::pair<std::string, SyslogParser::Holder>("tenant",
                SyslogParser::Holder("tenant", tenant)));
                prev_pos = DecorateMsg(msg, "tenant", tenant, prev_pos);
            }
            const std::string location = hr->location();
            if (!location.empty()) {
                v.erase("location");
                v.insert(std::pair<std::string, SyslogParser::Holder>("location",
                SyslogParser::Holder("location", location)));
                prev_pos = DecorateMsg(msg, "location", location, prev_pos);
            }
            const std::string device = hr->device();
            if (!device.empty()) {
                v.erase("device");
                v.insert(std::pair<std::string, SyslogParser::Holder>("device",
                SyslogParser::Holder("device", device)));
                prev_pos = DecorateMsg(msg, "device", device, prev_pos);
            }
            std::string an = SyslogParser::GetMapVals(v, "nested-application", "unknown");
            if (boost::iequals(an, "unknown")) {
                an = SyslogParser::GetMapVals(v, "application", "unknown");
            }
            size_t found = an.find_first_of(':');
            while (found != string::npos) {
                an[found] = '/';
                found = an.find_first_of(':', found+1);
            }
            v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
            SyslogParser::Holder("app-category", "UNKNOWN")));
            v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
            SyslogParser::Holder("app-subcategory", "UNKNOWN")));
            v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
            SyslogParser::Holder("app-groups", "UNKNOWN")));
            v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
            SyslogParser::Holder("app-risk", "UNKNOWN")));

            std::string rule = SyslogParser::GetMapVals(v, "rule-name", "None");
            std::string profile = SyslogParser::GetMapVals(v, "profile-name", "None");
            std::string rule_ar_name = tenant + '/' + profile + '/' + rule + '/' + device + '/' + an;
            boost::shared_ptr<ApplicationRecord> ar;
            boost::shared_ptr<TenantApplicationRecord> rar =
                    config_obj->GetTenantApplicationRecord(rule_ar_name);
            if (rar == NULL) {
                rule_ar_name = tenant + '/' + profile + '/' + rule + '/' + device + "/*";
                rar = config_obj->GetTenantApplicationRecord(rule_ar_name);
            }
            if (rar == NULL) {
                ar = config_obj->GetApplicationRecord(an);
                if (ar == NULL) {
                    ar = config_obj->GetApplicationRecord("*");
                }
            }
            if (rar != NULL) {
                LOG(DEBUG, "StructuredSyslogDecorate device application record: " << rule_ar_name);
                const std::string tenant_app_category = rar->tenant_app_category();
                if (!tenant_app_category.empty()) {
                    v.erase("app-category");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
                    SyslogParser::Holder("app-category", tenant_app_category)));
                    prev_pos = DecorateMsg(msg, "app-category", tenant_app_category, prev_pos);
                }
                const std::string tenant_app_subcategory = rar->tenant_app_subcategory();
                if (!tenant_app_subcategory.empty()) {
                    v.erase("app-subcategory");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
                    SyslogParser::Holder("app-subcategory", tenant_app_subcategory)));
                    prev_pos = DecorateMsg(msg, "app-subcategory", tenant_app_subcategory, prev_pos);
                }
                const std::string tenant_app_groups = rar->tenant_app_groups();
                if (!tenant_app_groups.empty()) {
                    v.erase("app-groups");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
                    SyslogParser::Holder("app-groups", tenant_app_groups)));
                    prev_pos = DecorateMsg(msg, "app-groups", tenant_app_groups, prev_pos);
                }
                const std::string tenant_app_risk = rar->tenant_app_risk();
                if (!tenant_app_risk.empty()) {
                    v.erase("app-risk");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
                    SyslogParser::Holder("app-risk", tenant_app_risk)));
                    prev_pos = DecorateMsg(msg, "app-risk", tenant_app_risk, prev_pos);
                }
                const std::string tenant_app_service_tags = rar->tenant_app_service_tags();
                if (!tenant_app_service_tags.empty()) {
                    std::vector< std::string >  ints;
                    ParseStructuredPart(&v, tenant_app_service_tags, ints, msg);
                }
            }
            else if (ar != NULL) {
                LOG(DEBUG, "StructuredSyslogDecorate application record: " << an);
                const std::string app_category = ar->app_category();
                if (!app_category.empty()) {
                    v.erase("app-category");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-category",
                    SyslogParser::Holder("app-category", app_category)));
                    prev_pos = DecorateMsg(msg, "app-category", app_category, prev_pos);
                }
                const std::string app_subcategory = ar->app_subcategory();
                if (!app_subcategory.empty()) {
                    v.erase("app-subcategory");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-subcategory",
                    SyslogParser::Holder("app-subcategory", app_subcategory)));
                    prev_pos = DecorateMsg(msg, "app-subcategory", app_subcategory, prev_pos);
                }
                const std::string app_groups = ar->app_groups();
                if (!app_groups.empty()) {
                    v.erase("app-groups");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-groups",
                    SyslogParser::Holder("app-groups", app_groups)));
                    prev_pos = DecorateMsg(msg, "app-groups", app_groups, prev_pos);
                }
                const std::string app_risk = ar->app_risk();
                if (!app_risk.empty()) {
                    v.erase("app-risk");
                    v.insert(std::pair<std::string, SyslogParser::Holder>("app-risk",
                    SyslogParser::Holder("app-risk", app_risk)));
                    prev_pos = DecorateMsg(msg, "app-risk", app_risk, prev_pos);
                }
                const std::string app_service_tags = ar->app_service_tags();
                if (!app_service_tags.empty()) {
                    std::vector< std::string >  ints;
                    ParseStructuredPart(&v, app_service_tags, ints, msg);
                }
            } else {
                LOG(ERROR, "StructuredSyslogDecorate: Application Record not found for: " << an);
            }
        } else {
            LOG(DEBUG, "StructuredSyslogDecorate: Hostname Record not found for: " << hn);
        }
    }
}

bool ProcessStructuredSyslog(const uint8_t *data, size_t len,
    const boost::asio::ip::address remote_address,
    StatWalker::StatTableInsertFn stat_db_callback, StructuredSyslogConfig *config_obj,
    boost::shared_ptr<StructuredSyslogForwarder> forwarder, boost::shared_ptr<std::string> sess_buf) {
  boost::system::error_code ec;
  const std::string ip(remote_address.to_string(ec));
  const uint8_t *p;
  std::string full_log;

  size_t end, start = 0;
  bool r;

  while (!*(data + len - 1))
      --len;
  LOG(DEBUG, "full structured_syslog: " << std::string(data + start, data + len) << " len: " << len);
  if (sess_buf != NULL) {
    full_log = *sess_buf + std::string(data + start, data + len);
    p = reinterpret_cast<const uint8_t*>(full_log.data());
    len += sess_buf->length();
    LOG(DEBUG, "structured_syslog sess_buf + new buf: " << full_log);
    sess_buf->clear();
  } else {
    p = data;
  }

  do {
      SyslogParser::syslog_m_t v;
      end = start + 1;
      while ((*(p + end - 1) != ']') && (end < len))
        ++end;

      if ((end == len) && (sess_buf != NULL)) {
       sess_buf->append(std::string(p + start, p + end));
       LOG(DEBUG, "structured_syslog next sess_buf: " << *sess_buf);
       return true;
      }

      r = SyslogParser::parse_syslog (p + start, p + end, v);
      LOG(DEBUG, "structured_syslog: " << std::string(p + start, p + end) <<
                 " start: " << start << " end: " << end << " parsed " << r);
      if (r) {
          v.insert(std::pair<std::string, SyslogParser::Holder>("ip",
                SyslogParser::Holder("ip", ip)));
          std::stringstream lenss;
          lenss << SyslogParser::GetMapVal(v, "msglen", 0);
          std::string lenstr = lenss.str();
          int message_len = SyslogParser::GetMapVal(v, "msglen", len);
          if (message_len != (int)len) {
            start += lenstr.length() + 1;
          }
          LOG(DEBUG, "structured_syslog message_len: " << message_len);
          SyslogParser::PostParsing(v);
          if (StructuredSyslogPostParsing(v, config_obj, stat_db_callback, p + start, end-start, forwarder) == false)
          {
            LOG(DEBUG, "structured_syslog not handled");
          }
          start += message_len;
      } else {
          LOG(ERROR, "structured_syslog parse failed for: " << std::string(p + start, p + end));
          start = end;
      }
      while (!v.empty()) {
          v.erase(v.begin());
      }
  } while (start<len);
  return r;
}

}  // namespace impl

class StructuredSyslogServer::StructuredSyslogServerImpl {
public:
    StructuredSyslogServerImpl(EventManager *evm, uint16_t port,
        const vector<string> &structured_syslog_tcp_forward_dst,
        const std::string &structured_syslog_kafka_broker,
        const std::string &structured_syslog_kafka_topic,
        uint16_t structured_syslog_kafka_partitions,
        ConfigClientCollector *config_client,
        StatWalker::StatTableInsertFn stat_db_callback) :
        udp_server_(new StructuredSyslogUdpServer(evm, port,
            stat_db_callback)),
        tcp_server_(new StructuredSyslogTcpServer(evm, port,
            stat_db_callback)),
        structured_syslog_config_(new StructuredSyslogConfig(config_client)) {
        if ((structured_syslog_tcp_forward_dst.size() != 0) || structured_syslog_kafka_broker != "") {
            forwarder_.reset(new StructuredSyslogForwarder (evm, structured_syslog_tcp_forward_dst,
                                                            structured_syslog_kafka_broker,
                                                            structured_syslog_kafka_topic,
                                                            structured_syslog_kafka_partitions));
        } else {
            LOG(DEBUG, "forward destination not configured");
        }

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
        if (message_config_poll_timer_) {
            TimerManager::DeleteTimer(message_config_poll_timer_);
            message_config_poll_timer_ = NULL;
        }
    }

    StructuredSyslogConfig *GetStructuredSyslogConfig() {
        return structured_syslog_config_;
    }

    boost::shared_ptr<StructuredSyslogForwarder> GetStructuredSyslogForwarder() {
        return forwarder_;
    }
    bool Initialize() {
        if (udp_server_->Initialize(GetStructuredSyslogConfig(), GetStructuredSyslogForwarder())) {
            return tcp_server_->Initialize(GetStructuredSyslogConfig(), GetStructuredSyslogForwarder());
        } else {
            return false;
        }
    }

    void Shutdown() {
        udp_server_->Shutdown();
        UdpServerManager::DeleteServer(udp_server_);
        udp_server_ = NULL;
        tcp_server_->Shutdown();
        TcpServerManager::DeleteServer(tcp_server_);
        if (forwarder_ != NULL)
            forwarder_->Shutdown();
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

        bool Initialize(StructuredSyslogConfig *config_obj,
                        boost::shared_ptr<StructuredSyslogForwarder> forwarder) {
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
            forwarder_ = forwarder;
            return true;
        }

        virtual void OnRead(const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::udp::endpoint &remote_endpoint) {
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));
            if (!structured_syslog::impl::ProcessStructuredSyslog(boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, remote_endpoint.address(), stat_db_callback_, config_obj_, forwarder_,
                    boost::shared_ptr<std::string>())) {
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
        StructuredSyslogConfig *config_obj_;
        boost::shared_ptr<StructuredSyslogForwarder> forwarder_;
    };

    class StructuredSyslogTcpServer;

    class StructuredSyslogTcpSession : public TcpSession {
    public:
        typedef boost::intrusive_ptr<StructuredSyslogTcpSession> StructuredSyslogTcpSessionPtr;
        StructuredSyslogTcpSession (StructuredSyslogTcpServer *server, Socket *socket) :
            TcpSession(server, socket) {
            sess_buf.reset(new std::string(""));
            //set_observer(boost::bind(&SyslogTcpSession::OnEvent, this, _1, _2));
        }
        virtual void OnRead (const boost::asio::const_buffer buf) {
            boost::system::error_code ec;
            StructuredSyslogTcpServer *sserver = dynamic_cast<StructuredSyslogTcpServer *>(server());
            //TODO: handle error
            sserver->ReadMsg(StructuredSyslogTcpSessionPtr(this), buf, socket ()->remote_endpoint(ec));
        }
        boost::shared_ptr<std::string> sess_buf;
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

        bool Initialize(StructuredSyslogConfig *config_obj, boost::shared_ptr<StructuredSyslogForwarder> forwarder) {
            TcpServer::Initialize (port_);
            LOG(DEBUG, __func__ << " Initialization of TCP StructuredSyslog listener @" << port_);
            config_obj_ = config_obj;
            forwarder_ = forwarder;
            return true;
        }

        virtual void ReadMsg(StructuredSyslogTcpSessionPtr sess, const boost::asio::const_buffer &recv_buffer,
            const boost::asio::ip::tcp::endpoint &remote_endpoint) {
            size_t recv_buffer_size(boost::asio::buffer_size(recv_buffer));

            if (!structured_syslog::impl::ProcessStructuredSyslog(
                    boost::asio::buffer_cast<const uint8_t *>(recv_buffer),
                    recv_buffer_size, remote_endpoint.address(), stat_db_callback_, config_obj_,
                    forwarder_, sess->sess_buf)) {
                LOG(ERROR, "ProcessStructuredSyslog TCP FAILED for : " << remote_endpoint);
            } else {
                LOG(DEBUG, "ProcessStructuredSyslog TCP SUCCESS for : " << remote_endpoint);
            }

            //sess->server()->DeleteSession (sess.get());
            sess->ReleaseBuffer(recv_buffer);
        }

    private:
        uint16_t port_;
        StructuredSyslogTcpSession *session_;
        StatWalker::StatTableInsertFn stat_db_callback_;
        StructuredSyslogConfig *config_obj_;
        boost::shared_ptr<StructuredSyslogForwarder> forwarder_;
    };
    StructuredSyslogUdpServer *udp_server_;
    StructuredSyslogTcpServer *tcp_server_;
    boost::shared_ptr<StructuredSyslogForwarder> forwarder_;
    StructuredSyslogConfig *structured_syslog_config_;
    Timer *hostname_record_poll_timer_;
    Timer *application_record_poll_timer_;
    Timer *message_config_poll_timer_;
    static const int kHostnameRecordPollInterval = 180 * 1000; // in ms
    static const int kApplicationRecordPollInterval = 120 * 1000; // in ms
    static const int kMessageConfigPollInterval = 1300 * 1000; // in ms
};

StructuredSyslogServer::StructuredSyslogServer(EventManager *evm,
    uint16_t port, const vector<string> &structured_syslog_tcp_forward_dst,
    const std::string &structured_syslog_kafka_broker,
    const std::string &structured_syslog_kafka_topic,
    uint16_t structured_syslog_kafka_partitions,
    ConfigClientCollector *config_client,
    StatWalker::StatTableInsertFn stat_db_fn) {
    impl_ = new StructuredSyslogServerImpl(evm, port, structured_syslog_tcp_forward_dst,
                                           structured_syslog_kafka_broker,
                                           structured_syslog_kafka_topic,
                                           structured_syslog_kafka_partitions,
                                           config_client, stat_db_fn);
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

//
// StructuredSyslogTcpForwarderSession
//
class StructuredSyslogTcpForwarderSession : public TcpSession {
private:
    StructuredSyslogTcpForwarder *server_;

public:
    StructuredSyslogTcpForwarderSession (StructuredSyslogTcpForwarder *server, Socket *socket) :
        TcpSession((TcpServer*)server, socket),
        server_(server){
    }

    virtual void OnRead (const boost::asio::const_buffer buf) {
        LOG(DEBUG, "StructuredSyslogTcpForwarderSession OnRead");
    }

    virtual void WriteReady(const boost::system::error_code &ec) {
        server_->WriteReady(ec);
    }
};

StructuredSyslogTcpForwarder::StructuredSyslogTcpForwarder(EventManager *evm, const std::string &ipaddress, int port) :
    TcpServer(evm),
    ipaddress_(ipaddress),
    port_(port),
    session_(NULL),
    ready_to_send_(true) {
}

TcpSession* StructuredSyslogTcpForwarder::AllocSession(Socket *socket) {
    session_ = new StructuredSyslogTcpForwarderSession(this, socket);
    return session_;
}

void StructuredSyslogTcpForwarder::WriteReady(const boost::system::error_code &ec) {
    LOG(DEBUG, "StructuredSyslogTcpForwarder::WriteReady");
    if (ec) {
        return;
    }
    {
        tbb::mutex::scoped_lock lock(send_mutex_);
        ready_to_send_ = true;
    }
}

void StructuredSyslogTcpForwarder::Connect() {
    boost::system::error_code ec;
    boost::asio::ip::tcp::endpoint endpoint;
    endpoint.address(boost::asio::ip::address::from_string(ipaddress_, ec));
    endpoint.port(port_);
    TcpServer::Connect(session_, endpoint);
}

bool StructuredSyslogTcpForwarder::Send(const u_int8_t *data, size_t size, size_t *actual) {
    *actual = 0;
    tbb::mutex::scoped_lock lock(send_mutex_);
    if (ready_to_send_) {
        ready_to_send_ = session_->Send(data, size, actual);
    }
    return ready_to_send_;
}

bool StructuredSyslogTcpForwarder::Connected() {
    boost::system::error_code ec;
    Endpoint remote = session_->socket()->remote_endpoint(ec);
    return session_->Connected(remote);
}

void StructuredSyslogTcpForwarder::SetSocketOptions() {
    session_->SetSocketOptions();
}

StructuredSyslogForwarder::StructuredSyslogForwarder(EventManager *evm,
                                                     const vector <std::string> &tcp_forward_dst,
                                                     const std::string &structured_syslog_kafka_broker,
                                                     const std::string &structured_syslog_kafka_topic,
                                                     uint16_t structured_syslog_kafka_partitions):
    evm_(evm) {
    if (tcp_forward_dst.size() != 0) {
        tcpForwarder_poll_timer_= TimerManager::CreateTimer(*evm->io_service(),
                                  "tcpForwarder poll timer",
                                  TaskScheduler::GetInstance()->GetTaskId("tcpForwarder poller"));
        tcpForwarder_poll_timer_->Start(tcpForwarderPollInterval,
        boost::bind(&StructuredSyslogForwarder::PollTcpForwarder, this),
        boost::bind(&StructuredSyslogForwarder::PollTcpForwarderErrorHandler, this, _1, _2));
    }
    Init(tcp_forward_dst, structured_syslog_kafka_broker, structured_syslog_kafka_topic,
         structured_syslog_kafka_partitions);
}

void StructuredSyslogForwarder::Init(const std::vector<std::string> &tcp_forward_dst,
                                     const std::string &structured_syslog_kafka_broker,
                                     const std::string &structured_syslog_kafka_topic,
                                     uint16_t structured_syslog_kafka_partitions) {
    for (std::vector<std::string>::const_iterator it = tcp_forward_dst.begin(); it != tcp_forward_dst.end(); ++it) {
        std::vector<std::string> dest;
        boost::split(dest, *it, boost::is_any_of(":"), boost::token_compress_on);
        StructuredSyslogTcpForwarder* fwder = new StructuredSyslogTcpForwarder(evm_,
                                                                               dest[0],
                                                                               atoi(dest[1].c_str()));
        fwder->CreateSession();
        fwder->Connect();
        fwder->SetSocketOptions();
        tcpForwarder_.push_back(fwder);
    }
    if (structured_syslog_kafka_broker != "") {
        kafkaForwarder_ = new KafkaForwarder(evm_, structured_syslog_kafka_broker,
                                             structured_syslog_kafka_topic,
                                             structured_syslog_kafka_partitions);
    } else {
        kafkaForwarder_ = NULL;
    }
}

StructuredSyslogForwarder::~StructuredSyslogForwarder() {
    Shutdown();
    if (tcpForwarder_poll_timer_) {
        TimerManager::DeleteTimer(tcpForwarder_poll_timer_);
        tcpForwarder_poll_timer_ = NULL;
    }
}

void StructuredSyslogForwarder::PollTcpForwarderErrorHandler(string error_name,
    string error_message) {
    LOG(ERROR, "PollTcpForwarder Timer Err: " << error_name << " " << error_message);
}

bool StructuredSyslogForwarder::PollTcpForwarder() {
    LOG(DEBUG, "PollTcpForwarder start");
    for (std::vector<StructuredSyslogTcpForwarder*>::iterator it = tcpForwarder_.begin();
         it != tcpForwarder_.end(); ++it) {
        if ((*it)->Connected() ==  false) {
            std::string dst = (*it)->GetIpAddress();
            int port = (*it)->GetPort();
            LOG(DEBUG,"reconnecting to remote syslog server " << dst << ":" << port);
            StructuredSyslogTcpForwarder* old_fwder = *it;
            StructuredSyslogTcpForwarder* new_fwder = new StructuredSyslogTcpForwarder(evm_, dst, port);
            new_fwder->CreateSession();
            new_fwder->Connect();
            new_fwder->SetSocketOptions();
            std::replace(tcpForwarder_.begin(),tcpForwarder_.end(), old_fwder, new_fwder);
            old_fwder->ClearSessions();
            TcpServerManager::DeleteServer(old_fwder);
        } else {
            LOG(DEBUG,"connection to remote syslog server " << (*it)->GetIpAddress() << ":"<< (*it)->GetPort() << " is fine");
        }
    }
    return true;
}

void StructuredSyslogForwarder::Shutdown() {
    if (kafkaForwarder_ != NULL) {
        kafkaForwarder_->Shutdown();
    }
    LOG(DEBUG, __func__ << " structured_syslog_forwarder shutdown done");
}

bool StructuredSyslogForwarder::kafkaForwarder() {
    return (kafkaForwarder_ != NULL);
}

void StructuredSyslogForwarder::Forward(boost::shared_ptr<StructuredSyslogQueueEntry> sqe) {
    for (std::vector<StructuredSyslogTcpForwarder*>::iterator it = tcpForwarder_.begin();
         it != tcpForwarder_.end(); ++it) {
        size_t bytes_written;
        (*it)->Send((const u_int8_t*)sqe->data->c_str(), sqe->length, &bytes_written);
        if (bytes_written < sqe->length) {
            LOG(DEBUG, "error writing - bytes_written: " << bytes_written);
        }
    }
    if (kafkaForwarder_ != NULL) {
        LOG(DEBUG, "forwarding json  - " << *(sqe->json_data));
        kafkaForwarder_->Send(*(sqe->json_data), *(sqe->skey));
    }
}

StructuredSyslogQueueEntry::StructuredSyslogQueueEntry(boost::shared_ptr<std::string> d, size_t len,
                                                       boost::shared_ptr<std::string> jd,
                                                       boost::shared_ptr<std::string> key):
        length(len), data(d), json_data(jd),skey(key) {
}

StructuredSyslogQueueEntry::~StructuredSyslogQueueEntry() {
}

}  // namespace structured_syslog
