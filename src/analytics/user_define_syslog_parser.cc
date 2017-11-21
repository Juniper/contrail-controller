/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <iostream>
#include <cstring>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <vector>
#include <base/logging.h>
#include <tbb/mutex.h>
#include "user_define_syslog_parser.h"


namespace bt = boost::posix_time;

tbb::mutex mutex;
#define SYSLOG_DEFAULT_DOMAIN "default"

std::string SYSLOG_RFC5424    =  "SYSLOG5424LINE";
std::string SYSLOG_RPI        =  "syslog5424_pri";
std::string SYSLOG_VERSION    =  "syslog5424_ver";
std::string SYSLOG_TS         =  "syslog5424_ts";
std::string SYSLOG_HOST       =  "syslog5424_host";
std::string SYSLOG_APP        =  "syslog5424_app";
std::string SYSLOG_PROC       =  "syslog5424_proc";
std::string SYSLOG_MSG_ID     =  "syslog5424_msgid";
std::string SYSLOG_SD_BODY    =  "syslog5424_sd";
std::string SYSLOG_MSG_BODY   =  "syslog5424_msg";

UserDefineSyslogParser::UserDefineSyslogParser(EventManager *evm,
                                     SyslogListeners *syslog_listener,
                                     StatWalker::StatTableInsertFn stat_db_cb) {
    gp_.reset(new GrokParser());
    gp_->init();
    std::set<std::string> name_set;
    domain_list_[SYSLOG_DEFAULT_DOMAIN] = name_set;
    add_RFC5424_pattern();
    syslog_listener_ = syslog_listener;
    db_insert_cb_ = stat_db_cb;
    syslog_listener_->RegistUserParserCb("UserDefSyslog",
              boost::bind(&UserDefineSyslogParser::syslog_parse, this, _1, _2));
}

UserDefineSyslogParser::~UserDefineSyslogParser() {
}

/* add RFC5424 pattern
*  create a new grok for RFC5424 pattern
*  This grok will be used to get RFC5424
*  header info, sd body string and message
*  body string.
*/
/*
erDefineSyslogParser::parse_to_statsYSLOG5424PRINTASCII [!-~]+
SYSLOG5424PRI  <%{NONNEGINT:syslog5424_pri}>
SYSLOG5424SD   \[%{DATA}\]+
SYSLOG5424BASE %{SYSLOG5424PRI}%{NONNEGINT:syslog5424_ver} 
               +(?:%{TIMESTAMP_ISO8601:syslog5424_ts}|-) 
               +(?:%{HOSTNAME:syslog5424_host}|-) 
               +(-|%{SYSLOG5424PRINTASCII:syslog5424_app}) 
               +(-|%{SYSLOG5424PRINTASCII:syslog5424_proc}) 
               +(-|%{SYSLOG5424PRINTASCII:syslog5424_msgid}) 
               +(?:%{SYSLOG5424SD:syslog5424_sd}|-|)
SYSLOG5424LINE %{SYSLOG5424BASE} +%{GREEDYDATA:syslog5424_msg}
*/
void UserDefineSyslogParser::add_RFC5424_pattern() {
   string pattern;
   string name = SYSLOG_RFC5424;
   gp_->create_grok_instance(name);
   pattern = "SYSLOG5424PRINTASCII [!-~]+";
   gp_->add_pattern(name, pattern);
   pattern = "SYSLOG5424PRI <%{NONNEGINT:"
             + SYSLOG_RPI
             + "}>";
   gp_->add_pattern(name, pattern);
   pattern_list_[name].insert(pattern);
   pattern = "SYSLOG5424SD \\[%{DATA}\\]+";
   gp_->add_pattern(name, pattern);
   pattern = "SYSLOG5424BASE %{SYSLOG5424PRI}%{NONNEGINT:"
             + SYSLOG_VERSION +
             + "} +(?:%{TIMESTAMP_ISO8601:"
             + SYSLOG_TS
             + "}|-) +(?:%{HOSTNAME:"
             + SYSLOG_HOST
             + "}|-) +(-|%{SYSLOG5424PRINTASCII:"
             + SYSLOG_APP
             + "}) +(-|%{SYSLOG5424PRINTASCII:"
             + SYSLOG_PROC
             + "}) +(-|%{SYSLOG5424PRINTASCII:"
             + SYSLOG_MSG_ID
             + "}) +(?:%{SYSLOG5424SD:"
             + SYSLOG_SD_BODY
             + "}|-|)";
   gp_->add_pattern(name, pattern);
   pattern_list_[name].insert(pattern);
   pattern = name + " " + "%{SYSLOG5424BASE} +%{GREEDYDATA:"
             + SYSLOG_MSG_BODY
             + "}";
   gp_->add_pattern(name, pattern);
   pattern_list_[name].insert(pattern);
   gp_->compile_pattern(name);
   domain_list_[SYSLOG_DEFAULT_DOMAIN].insert(name);
}

/*
 * The configuration from user is like:
 * string          name
 * string          appname
 * string          msdid
 * string          pattern
 * string          key1,key2,key3,
 * string          metric1,metric2,metric3
 * To speed up syslog parser, we store them like
 * domain_list_: key is combine of msgid and appname, value is set<string> of name
 * use this way, when a syslog in, we can use RFC5424 to parse syslog header and
 * get appname/msgid info, if appname/msgid is not in doamin_list_, we will drop it
 * directly, else we will use domain_list_ to find pattern name, this avoid to walk 
 *  all grok instance to match msg body, only the name is assigned to this domain's 
 * grok will be checked.
*/
void 
UserDefineSyslogParser::rx_config(const contrail_rapidjson::Document &jdoc, bool add_update) {
    if (jdoc.IsObject() && jdoc.HasMember("syslog_parser")) {
        std::string name;
        const contrail_rapidjson::Value& parser = jdoc["syslog_parser"];
        if (parser.HasMember("fq_name")) {
            const contrail_rapidjson::Value& fq_name = parser["fq_name"];
            contrail_rapidjson::SizeType sz = fq_name.Size();
            name = fq_name[sz-1].GetString();
        } else {
            LOG(ERROR, "Object miss FQ name!");
            return;
        }
        if (add_update) {
            add_update_config(name, jdoc);
        } else {
            delete_config(name, jdoc);
        }
    }
}

void
UserDefineSyslogParser::delete_config(std::string name,
                                     const contrail_rapidjson::Document &jdoc) {
    tbb::mutex::scoped_lock lock(mutex);
    /*deal with header info configuration*/
    const contrail_rapidjson::Value& parser = jdoc["syslog_parser"];
    if (parser.HasMember("header_configuration")) {
        /*because headerinfo is "required" in configuration, so
          any one appaers in deletion msg means object is deleting*/
        HeaderCfgInfo header_cfg_info = name_to_header_map_[name];
        std::string domain_id = header_cfg_info.domain_id;
        domain_list_[domain_id].erase(name);
        pattern_list_.erase(name);
        tag_list_.erase(name);
        metric_list_.erase(name);
        name_to_header_map_.erase(name);
        gp_->del_grok_instance(name);
        if (domain_list_[domain_id].empty()) {
            domain_list_.erase(domain_id);
        }
        return;
    }

    if (parser.HasMember("query_tags")) {
        const contrail_rapidjson::Value& querry_tags 
                               = parser["query_tags"]["query_tag"];
        for (contrail_rapidjson::SizeType i = 0; i < querry_tags.Size(); i++) {
            if (querry_tags[i].IsString()) {
                std::string tag = querry_tags[i].GetString();
                tag_list_[name].erase(tag);
            }
        }
    }

    if (parser.HasMember("metrics")) {
        const contrail_rapidjson::Value& metrics
                               = parser["metrics"]["metric"];
        for (contrail_rapidjson::SizeType i = 0; i < metrics.Size(); i++) {
            if (metrics[i].HasMember("name")) {
                std::string metric = metrics[i]["name"].GetString();
                metric_list_[name].erase(MetricInfo(metric, ""));
            }
        }
    }

    if (parser.HasMember("patterns")) {
        std::set<std::string> new_patterns;
        new_patterns = pattern_list_[name];
        const contrail_rapidjson::Value& patterns
                               = parser["patterns"]["pattern"];
        for (contrail_rapidjson::SizeType i = 0; i < patterns.Size(); i++) {
            if (patterns[i].IsString()) {
                std::string pattern = patterns[i].GetString();
                new_patterns.erase(pattern);
            }
        }
        /*first, we need delete old pattern and old grok*/
        gp_->del_grok_instance(name);
        pattern_list_.erase(name);
        /*add new grok instance again with new pattern*/
        gp_->create_grok_instance(name);
        for (std::set<string>::iterator it = new_patterns.begin();
                          it != new_patterns.end(); it++) {
            gp_->add_pattern(name, *it); 
        }
        if (!new_patterns.empty()) {
            if (!gp_->compile_pattern(name)) {
                LOG(ERROR, "compile pattern " << name << " failed");
                GenericStatsAlarm  parser_alarm;
                parser_alarm.set_name(name);
                GenericStatsAlarmUVE::Send(parser_alarm);
                name_to_header_map_[name].alarm_send = true;
                pattern_list_[name] = new_patterns;
                return;
            }
        }
        if (name_to_header_map_[name].alarm_send) {
            GenericStatsAlarm parser_alarm;
            parser_alarm.set_name(name);
            parser_alarm.set_deleted(true);
            GenericStatsAlarmUVE::Send(parser_alarm);
        }

        pattern_list_[name] = new_patterns;
    }
    return;
}

void
UserDefineSyslogParser::add_update_config(std::string name,
                                     const contrail_rapidjson::Document &jdoc) {
    tbb::mutex::scoped_lock lock(mutex);
    const contrail_rapidjson::Value& parser = jdoc["syslog_parser"];
    if (parser.HasMember("header_configuration")) {
        if (name_to_header_map_.find(name) != name_to_header_map_.end()) {
            HeaderCfgInfo header_cfg_info = name_to_header_map_[name];
            std::string domain_id = header_cfg_info.domain_id;
            if (tag_list_[name].find(header_cfg_info.hostname) 
                                       != tag_list_[name].end()) {
                tag_list_[name].erase(header_cfg_info.hostname);
            }
            if (tag_list_[name].find(header_cfg_info.appname)
                                       != tag_list_[name].end()) {
                tag_list_[name].erase(header_cfg_info.appname);
            }
            if (tag_list_[name].find(header_cfg_info.procid)
                                       != tag_list_[name].end()) {
                tag_list_[name].erase(header_cfg_info.procid);
            }
            if (tag_list_[name].find(header_cfg_info.msgid)
                                       != tag_list_[name].end()) {
                tag_list_[name].erase(domain_id);
            }
            domain_list_[domain_id].erase(name);
            name_to_header_map_.erase(name);
        }

        /*second step add new*/
        HeaderCfgInfo header_cfg_info;
        std::string domain_id;
        const contrail_rapidjson::Value& header = parser["header_configuration"];
        header_cfg_info.hostname = header["hostname"].GetString();
        header_cfg_info.appname  = header["appname"].GetString();
        header_cfg_info.procid   = header["procid"].GetString();
        header_cfg_info.msgid    = header["msgid"].GetString();
        if (header["appname_as_classfier"].GetBool()) {
            domain_id += header_cfg_info.appname;
        }
        domain_id += ":";
        if (header["msgid_as_classfier"].GetBool()) {
            domain_id += header_cfg_info.msgid;
        }
        header_cfg_info.domain_id = domain_id;
        name_to_header_map_[name] = header_cfg_info;
        if (domain_list_.find(domain_id) == domain_list_.end()) {
            std::set<std::string> name_set;
            domain_list_[domain_id] = name_set;
        }
        domain_list_[domain_id].insert(name);
        name_to_header_map_[name] = header_cfg_info;
    }

    if (parser.HasMember("query_tags")) {
        const contrail_rapidjson::Value& querry_tags
                               = parser["query_tags"]["query_tag"];
        for (contrail_rapidjson::SizeType i = 0; i < querry_tags.Size(); i++) {
            if (querry_tags[i].IsString()) {
                std::string tag = querry_tags[i].GetString();
                tag_list_[name].insert(tag);
            }
        }
    }

    if (parser.HasMember("metrics")) {
        const contrail_rapidjson::Value& metrics
                               = parser["metrics"]["metric"];
        for (contrail_rapidjson::SizeType i = 0; i < metrics.Size(); i++) {
            if (metrics[i].HasMember("name")) {
                std::string metric = metrics[i]["name"].GetString();
                std::string type = metrics[i]["type_"].GetString();
                metric_list_[name].insert(MetricInfo(metric, type));
            }
        }
    }

    if (parser.HasMember("patterns")) {
        std::set<std::string> new_patterns;
        new_patterns = pattern_list_[name];
        const contrail_rapidjson::Value& patterns
                               = parser["patterns"]["pattern"];
        for (contrail_rapidjson::SizeType i = 0; i < patterns.Size(); i++) {
            if (patterns[i].IsString()) {
                std::string pattern = patterns[i].GetString();
                new_patterns.insert(pattern);
            }
        }
        /*first, we need delete old pattern and old grok*/
        gp_->del_grok_instance(name);
        pattern_list_.erase(name);
        /*add new grok instance again with new pattern*/
        gp_->create_grok_instance(name);
        for (std::set<string>::iterator it = new_patterns.begin();
                              it != new_patterns.end(); it++) {
            gp_->add_pattern(name, *it);
        }
        if (!new_patterns.empty()) {
            if (!gp_->compile_pattern(name)) {
                LOG(ERROR, "compile pattern " << name << " failed");
                GenericStatsAlarm parser_alarm;
                parser_alarm.set_name(name);
                GenericStatsAlarmUVE::Send(parser_alarm);
                name_to_header_map_[name].alarm_send = true;
                pattern_list_[name] = new_patterns;
                return;
            }
        }
        if (name_to_header_map_[name].alarm_send) {
            GenericStatsAlarm parser_alarm;
            parser_alarm.set_name(name);
            parser_alarm.set_deleted(true);
            GenericStatsAlarmUVE::Send(parser_alarm);
        }
        pattern_list_[name] = new_patterns;
    }
}

/*
 * parser  timestamp.
 * input : iso timestamp string.
 * output: timestamp with ms which is relative to 1970/01/01
*/
time_t
UserDefineSyslogParser::parse_timestamp(std::string time_s) {
    bool is_plus_tz = false;
    if (time_s.find("+") != std::string::npos) {
        is_plus_tz = true;
    }

    std::vector<string> date_time;
    boost::split(date_time, time_s, boost::is_any_of("T"));
    std::vector<string> time_tz;
    boost::split(time_tz, date_time[1], boost::is_any_of("+-Z"));
    bt::ptime local = bt::time_from_string(date_time[0] 
                                       + " "
                                       + time_tz[0]);
    bt::ptime epoch(boost::gregorian::date(1970,1,1));
    if (time_tz.size() == 2) {
        bt::time_duration diff = bt::duration_from_string(time_tz[1]);
        if (is_plus_tz) {
            return ((local - epoch).total_microseconds() 
                    - diff.total_microseconds());
        } else {
            return ((local - epoch).total_microseconds()
                    + diff.total_microseconds());
        }
    } else {
        return (local - epoch).total_microseconds();
    }
}


/*
 * parse string and call stats walk.
 * input : 
 *    source    syslog source ip address with string
 *    ts        timestamp (ms realtive to 1970/01/01)
 *    domain_id appname:msgid from syslog header
 *    s         syslog body string
 * output: timestamp with ms which is relative to 1970/01/01
*/
void UserDefineSyslogParser::parse_to_stats(string source,
                                       uint64_t ts,
                                       string domain_id,
                                       string s) {
    std::map<std::string, std::string> body_info;
    std::set<std::string> tag_set;
    std::set<MetricInfo> metric_set;
    std::string name = "";
    do {
        tbb::mutex::scoped_lock lock(mutex);
        DomainList::iterator iter = domain_list_.find(domain_id);
        for (std::set<std::string>::iterator it = iter->second.begin();
                                   it != iter->second.end(); it++) {
            if (gp_->match(*it, s, &body_info)) {
                name = *it;
                break;
            }
        }
        if (!name.empty()) {
            tag_set = tag_list_[name];
            metric_set = metric_list_[name];
        }
    } while(0);
    if (!metric_set.empty()) {
        std::vector<pair<MetricInfo, std::string> > attrib_v;
        StatWalker::TagMap m1;
        StatWalker::TagVal h1;
        h1.val = source;
        m1.insert(make_pair("Source", h1));
        std::map<std::string, std::string>::iterator it;
        for (it = body_info.begin(); it != body_info.end(); it++) {
            if (tag_set.find(it->first) != tag_set.end()) {
                h1.val = it->second;
                m1.insert(make_pair(it->first, h1));
            }
            std::set<MetricInfo>::iterator it1 = 
                               metric_set.find(MetricInfo(it->first, ""));
            if (it1 != metric_set.end()) {
                attrib_v.push_back(make_pair(*it1, it->second));
            }
        }
        StatWalker sw(db_insert_cb_, (uint64_t)ts, name, m1);
        DbHandler::Var value;
        for (uint32_t i = 0; i < attrib_v.size(); i++) {
             StatWalker::TagMap tagmap;
             DbHandler::AttribMap attribs;
             if (attrib_v[i].first.type == "int") {
                 value  = (uint64_t)boost::lexical_cast<long long>(attrib_v[i].second);
             } else {
                 value = attrib_v[i].second;
             }
             attribs.insert(make_pair("value", value));
             sw.Push(attrib_v[i].first.name, tagmap, attribs);
             sw.Pop();
        }
    }
}

/* syslog parser
*  Match system log against all stored grok
*  @Input - strin: complete system log.
*/
void UserDefineSyslogParser::syslog_parse(std::string source, std::string strin) {
    /* 1. we use base_ to parser syslog header*/
    std::map<std::string, std::string> header_info;
    std::string name = "";
    do {
        tbb::mutex::scoped_lock lock(mutex);
        DomainList::iterator iter = domain_list_.find(SYSLOG_DEFAULT_DOMAIN);
        for (std::set<std::string>::iterator it = iter->second.begin();
                                   it != iter->second.end(); it++) {
            if (gp_->match(*it, strin, &header_info)) {
                name = *it;
                break;
            }
        }
    } while(0);
    if (name.empty()) {
        LOG(DEBUG, "dropped: no pattern match in base_");
        return;
    }

    string appname;
    string msgid;
    if (header_info.find(SYSLOG_APP) != header_info.end()) {
        if (!header_info[SYSLOG_APP].empty()) {
            appname = header_info[SYSLOG_APP];
        }
    }
    if (header_info.find(SYSLOG_MSG_ID) != header_info.end()) {
        if (!header_info[SYSLOG_MSG_ID].empty()) {
            msgid = header_info[SYSLOG_MSG_ID];
        }
    }
    std::string domain_id = appname + ":" + msgid;
    
    DomainList::iterator iter = domain_list_.find(domain_id);
    if (iter == domain_list_.end()) {
        LOG(DEBUG, "dropped:no parser config to domain_id: " << domain_id);
        return;
    }
    time_t ts = parse_timestamp(header_info[SYSLOG_TS]);
    /* 2. parse msgbody*/
    if (!header_info[SYSLOG_MSG_BODY].empty()) {
        parse_to_stats(source, ts, domain_id, header_info[SYSLOG_MSG_BODY]);
    }

    /*3. parse SD*/
    if (!header_info[SYSLOG_SD_BODY].empty()) {
        std::vector<std::string> sds;
        size_t len = header_info[SYSLOG_SD_BODY].length() - 2;
        std::string sub = header_info[SYSLOG_SD_BODY].substr(1, len);
        boost::split(sds, sub, boost::is_any_of("]["), boost::token_compress_on);
        for (std::vector<std::string>::iterator s = sds.begin(); 
                                                s != sds.end(); s++) {
            parse_to_stats(source, ts, domain_id, *s);
        }
    }
}

/*
 * get configuration for introspect to verify configuration
 * output parameter: config_info, define by introspect sandesh
*/
void
UserDefineSyslogParser::get_config(std::vector<LogParserConfigInfo> *config_info,
                  std::vector<LogParserPreInstallPatternInfo> *preinstall_info) {
    tbb::mutex::scoped_lock lock(mutex);
    for (DomainList::iterator it = domain_list_.begin();
        it != domain_list_.end(); it++) {
        if (it->first == SYSLOG_DEFAULT_DOMAIN) {
            continue;
        }
        LogParserConfigInfo parserInfo;
        parserInfo.set_domain_id(it->first);
        for (std::set<string>::iterator iter = it->second.begin();
               iter != it->second.end(); iter++) {
            LogPatternConfigInfo patternInfo;
            string name = *iter;
            patternInfo.set_name(name);
            std::set<string>::iterator it1;
            std::vector<std::string> patterns;
            for (it1 = pattern_list_[name].begin();
                it1 != pattern_list_[name].end(); it1++) {
                patterns.push_back(*it1);
            }
            patternInfo.set_patterns(patterns);
            std::vector<std::string> tags;
            for (it1 = tag_list_[name].begin();
                 it1 != tag_list_[name].end(); it1++) {
                tags.push_back(*it1);
            }
            patternInfo.set_tags(tags);
            std::set<MetricInfo>::iterator it2;
            std::vector<std::string> metrics;
            for (it2 = metric_list_[name].begin();
                 it2 != metric_list_[name].end(); it2++) {
                metrics.push_back(it2->name);
            }
            patternInfo.set_metrics(metrics);
            parserInfo.pattern_infos.push_back(patternInfo);
        }
        config_info->push_back(parserInfo);
    }
    /*show default domain*/
    LogParserConfigInfo parserInfo;
    parserInfo.set_domain_id(SYSLOG_DEFAULT_DOMAIN);
    std::set<std::string> name_set = domain_list_[SYSLOG_DEFAULT_DOMAIN];
    for (std::set<string>::iterator iter = name_set.begin();
               iter != name_set.end(); iter++) {
        LogPatternConfigInfo patternInfo;
        string name = *iter;
        patternInfo.set_name(name);
        std::set<string>::iterator it1;
        std::vector<std::string> patterns;
        for (it1 = pattern_list_[name].begin();
            it1 != pattern_list_[name].end(); it1++) {
            patterns.push_back(*it1);
        }
        patternInfo.set_patterns(patterns);
        std::vector<std::string> tags;
        for (it1 = tag_list_[name].begin();
            it1 != tag_list_[name].end(); it1++) {
            tags.push_back(*it1);
        }
        patternInfo.set_tags(tags);
        std::set<MetricInfo>::iterator it2;
        std::vector<std::string> metrics;
        for (it2 = metric_list_[name].begin();
             it2 != metric_list_[name].end(); it2++) {
            metrics.push_back(it2->name);
        }
        patternInfo.set_metrics(metrics);
        parserInfo.pattern_infos.push_back(patternInfo);
    }
    config_info->push_back(parserInfo);
    std::vector<std::string> base_pattern = gp_->get_base_pattern();
    for (uint32_t i = 0; i < base_pattern.size(); i++) {
        LogParserPreInstallPatternInfo preinstall_pattern;
        size_t pos = base_pattern[i].find(" ");
        string name = base_pattern[i].substr(0, pos);
        string pattern = base_pattern[i].substr(pos + 1);
        preinstall_pattern.set_name(name);
        preinstall_pattern.set_pattern(pattern);
        preinstall_info->push_back(preinstall_pattern);
    }
}
