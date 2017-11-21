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
tbb::mutex msglist_mutex;

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
    install_base_pattern();
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
void UserDefineSyslogParser::add_RFC5424_pattern() {
   string pattern;
   string name = SYSLOG_RFC5424;
   gp_->create_new_grok(name);
   pattern = "SYSLOG5424PRINTASCII [!-~]+";
   gp_->add_pattern_from_string(name, pattern);
   pattern = "SYSLOG5424PRI <%{NONNEGINT:"
             + SYSLOG_RPI
             + "}>";
   gp_->add_pattern_from_string(name, pattern);
   pattern = "SYSLOG5424SD \\[%{DATA}\\]+";
   gp_->add_pattern_from_string(name, pattern);
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
   gp_->add_pattern_from_string(name, pattern);
   pattern = name + " " + "%{SYSLOG5424BASE} +%{GREEDYDATA:"
             + SYSLOG_MSG_BODY
             + "}";
   gp_->add_pattern_from_string(name, pattern);
   gp_->compile_pattern(name);
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
    if (jdoc.IsObject() && jdoc.HasMember("global_system_config")) {
        if (!jdoc["global_system_config"].HasMember(
                  "user_defined_syslog_parsers")) {
            return;
        }

        if (!jdoc["global_system_config"]
                 ["user_defined_syslog_parsers"].IsObject()) {
            return;
        }

        if (!jdoc["global_system_config"]
                 ["user_defined_syslog_parsers"].HasMember("parserlist")) {
            return;
        }

        const contrail_rapidjson::Value& gsc = jdoc
                ["global_system_config"]["user_defined_syslog_parsers"]["parserlist"];
        if (!gsc.IsArray()) {
            return;
        }

        for (contrail_rapidjson::SizeType i = 0; i < gsc.Size(); i++) {
            if (!gsc[i].IsObject() || !gsc[i].HasMember("name")
                || !gsc[i].HasMember("pattern")
                || !gsc[i].HasMember("tag_list")
                || !gsc[i].HasMember("metric_list")) {
                continue;
            }

            std::string name = gsc[i]["name"].GetString();
            std::string pattern = gsc[i]["pattern"].GetString();
            std::vector<std::string> tags, metrics;
            std::string domain_id;

            std::string s = gsc[i]["tag_list"].GetString();
            boost::split(tags, s, boost::is_any_of(","));
            s = gsc[i]["metric_list"].GetString();
            bool as_tag;
            bool as_domain_id;
            boost::split(metrics, s, boost::is_any_of(","));
            if (gsc[i].HasMember("hostname")) {
                string v = gsc[i]["hostname"].GetString();
                if (gsc[i].HasMember("hostname_as_tag")) {
                    as_tag = gsc[i]["hostname_as_tag"].GetBool();
                    if (as_tag) {
                        tags.push_back(v);
                    }
                }
            }
            if (gsc[i].HasMember("appname")) {
                string v = gsc[i]["appname"].GetString();
                if (gsc[i].HasMember("appname_as_tag")) {
                    as_tag = gsc[i]["appname_as_tag"].GetBool();
                    if (as_tag) {
                        tags.push_back(v);
                    }
                }
                if (gsc[i].HasMember("appname_as_classfier")) {
                    as_domain_id = gsc[i]["appname_as_classfier"].GetBool();
                    if (!as_domain_id) {
                        v = "-";
                    }
                }
                domain_id += v;
            } else {
                domain_id += "-";
            }
            if (gsc[i].HasMember("procid")) {
                string v = gsc[i]["procid"].GetString();
                if (gsc[i].HasMember("procid_as_tag")) {
                    as_tag = gsc[i]["procid_as_tag"].GetBool();
                    if (as_tag) {
                        tags.push_back(v);
                    }
                }
            }
            domain_id += ":";
            if (gsc[i].HasMember("msgid")) {
                string v = gsc[i]["msgid"].GetString();
                if (gsc[i].HasMember("msgid_as_tag")) {
                    as_tag = gsc[i]["msgid_as_tag"].GetBool();
                    if (as_tag) {
                        tags.push_back(v);
                    }
                }
                if (gsc[i].HasMember("msgid_as_classfier")) {
                    as_domain_id = gsc[i]["msgid_as_classfier"].GetBool();
                    if (!as_domain_id) {
                        v = "-";
                    }
                }
                domain_id += v;
            } else {
                domain_id += "-";
            }

            if (add_update) {
                if (domain_list_.find(domain_id) == domain_list_.end()) {
                    std::set<std::string> name_set;
                    domain_list_[domain_id] = name_set;
                }
                domain_list_[domain_id].insert(name);
                if (tag_metric_list_.find(name) != tag_metric_list_.end()){
                    tag_metric_list_.erase(name);
                }
                tag_metric_list_.insert(make_pair(name,
                                        UserDefTagMetric(tags, metrics)));
                gp_->create_new_grok(name);
                gp_->add_pattern_from_string(name, name + " " +pattern);
                gp_->compile_pattern(name);
                if (!syslog_listener_->IsRunning()) {
                    syslog_listener_->Start();
                } 
            } else {
               gp_->delete_grok(name);
               tag_metric_list_.erase(name);
               domain_list_[domain_id].erase(name);
               if (domain_list_[domain_id].empty()) {
                   domain_list_.erase(domain_id);
               }
            }
        }
    }
    return;
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

    std::vector<string> time_tz;
    boost::split(time_tz, time_s, boost::is_any_of("+-Z"));
    std::vector<string> date_time;
    boost::split(date_time, time_tz[0], boost::is_any_of("T"));
    bt::ptime local = bt::time_from_string(date_time[0] 
                                       + " "
                                       + date_time[1]);
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
    std::set<std::string> metric_set;
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
            tag_set = tag_metric_list_[name].tags_;
            metric_set = tag_metric_list_[name].metrics_;
        }
    } while(0);
    if (!metric_set.empty()) {
        std::vector<pair<std::string, std::string> > attrib_v;
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
            if (metric_set.find(it->first) != metric_set.end()) {
                attrib_v.push_back(make_pair(it->first, it->second));
            }
        }
        StatWalker sw(db_insert_cb_, (uint64_t)ts, name, m1);
        DbHandler::Var value;
        for (uint32_t i = 0; i < attrib_v.size(); i++) {
             StatWalker::TagMap tagmap;
             DbHandler::AttribMap attribs;
             boost::regex rx("[0-9]+");
             boost::cmatch cm;
             if (boost::regex_match(attrib_v[i].second.c_str(), cm, rx)) {
                 value  = (uint64_t)boost::lexical_cast<long long>(attrib_v[i].second);
             } else {
                 value = attrib_v[i].second;
             }
             attribs.insert(make_pair(attrib_v[i].first, value));
             sw.Push(attrib_v[i].first, tagmap, attribs);
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
    if (gp_->match(SYSLOG_RFC5424, strin, &header_info) == false) {
        LOG(DEBUG, "dropped: no pattern match in base_");
        return;
    }
    std::string domain_id = header_info[SYSLOG_APP]
                            + ":"
                            + header_info[SYSLOG_MSG_ID];
    
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

std::vector<pair<std::string, std::string> > base_pattern;
void UserDefineSyslogParser::install_base_pattern() {
    base_pattern.push_back(std::make_pair
    ("USERNAME",    "[a-zA-Z0-9._-]+"));
    base_pattern.push_back(std::make_pair
    ("USER",        "%{USERNAME"));
    base_pattern.push_back(std::make_pair
    ("INT",         "(?:[+-]?(?:[0-9]+))"));
    base_pattern.push_back(std::make_pair
    ("BASE10NUM",   "(?<![0-9.+-])(?>[+-]?(?:(?:[0-9]+(?:\\.[0-9]+)?)|(?:\\.[0-9]+)))"));
    base_pattern.push_back(std::make_pair
    ("NUMBER",      "(?:%{BASE10NUM})"));
    base_pattern.push_back(std::make_pair
    ("BASE16NUM",   "(?<![0-9A-Fa-f])(?:[+-]?(?:0x)?(?:[0-9A-Fa-f]+))"));
    base_pattern.push_back(std::make_pair
    ("BASE16FLOAT", "\\b(?<![0-9A-Fa-f.])(?:[+-]?(?:0x)?(?:(?:[0-9A-Fa-f]+(?:\\.[0-9A-Fa-f]*)?)|(?:\\.[0-9A-Fa-f]+)))\\b"));
    base_pattern.push_back(std::make_pair
    ("POSINT",      "\\b(?:[1-9][0-9]*)\\b"));
    base_pattern.push_back(std::make_pair
    ("NONNEGINT",   "\\b(?:[0-9]+)\\b"));
    base_pattern.push_back(std::make_pair
    ("WORD",        "\\b\\w+\\b"));
    base_pattern.push_back(std::make_pair
    ("NOTSPACE",    "\\S+"));
    base_pattern.push_back(std::make_pair
    ("SPACE",       "\\s*"));
    base_pattern.push_back(std::make_pair
    ("DATA",        ".*?"));
    base_pattern.push_back(std::make_pair
    ("GREEDYDATA",  ".*"));
    base_pattern.push_back(std::make_pair
    ("UUID",        "[A-Fa-f0-9]{8}-(?:[A-Fa-f0-9]{4}-){3}[A-Fa-f0-9]{12}"));

    // Networking
    base_pattern.push_back(std::make_pair
    ("IPV6",     "((([0-9A-Fa-f]{1,4}:){7}([0-9A-Fa-f]{1,4}|:))|(([0-9A-Fa-f]{1,4}:){6}(:[0-9A-Fa-f]{1,4}|((25[0-5]|2[0-4]"
                 "\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})|:))|(([0-9A-Fa-f]{1,4}:){5}(((:[0-9"
                 "A-Fa-f]{1,4}){1,2})|:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3})"
                 "|:))|(([0-9A-Fa-f]{1,4}:){4}(((:[0-9A-Fa-f]{1,4}){1,3})|((:[0-9A-Fa-f]{1,4})?:((25[0-5]|2[0-4]\\d|1\\d\\d"
                 "|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){3}(((:[0-9A-Fa-f]{1,4"
                 "}){1,4})|((:[0-9A-Fa-f]{1,4}){0,2}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|["
                 "1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){2}(((:[0-9A-Fa-f]{1,4}){1,5})|((:[0-9A-Fa-f]{1,4}){0,3}:((25[0-5"
                 "]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(([0-9A-Fa-f]{1,4}:){1}"
                 "(((:[0-9A-Fa-f]{1,4}){1,6})|((:[0-9A-Fa-f]{1,4}){0,4}:((25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|"
                 "2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:))|(:(((:[0-9A-Fa-f]{1,4}){1,7})|((:[0-9A-Fa-f]{1,4}){0,5}:((25[0-5]|"
                 "2[0-4]\\d|1\\d\\d|[1-9]?\\d)(\\.(25[0-5]|2[0-4]\\d|1\\d\\d|[1-9]?\\d)){3}))|:)))(%.+)?"));
    base_pattern.push_back(std::make_pair
    ("IPV4",     "(?<![0-9])(?:(?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25["
                 "0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2})[.](?:25[0-5]|2[0-4][0-9]|[0-1]?[0-9]{1,2}))(?![0-9])"));
    base_pattern.push_back(std::make_pair
    ("IP",       "(?:%{IPV6}|%{IPV4})"));
    base_pattern.push_back(std::make_pair
    ("HOSTNAME", "\\b(?:[0-9A-Za-z][0-9A-Za-z-]{0,62})(?:\\.(?:[0-9A-Za-z][0-9A-Za-z-]{0,62}))*(\\.?|\\b)"));
    base_pattern.push_back(std::make_pair
    ("HOST",     "%{HOSTNAME"));
    base_pattern.push_back(std::make_pair
    ("IPORHOST", "(?:%{HOSTNAME}|%{IP})"));
    base_pattern.push_back(std::make_pair
    ("HOSTPORT", "(?:%{IPORHOST=~/\\./}:%{POSINT})"));

    // Months: January, Feb, 3, 03, 12, December
    base_pattern.push_back(std::make_pair
    ("MONTH",    "\\b(?:Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?"
                 "|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\\b"));
    base_pattern.push_back(std::make_pair
    ("MONTHNUM", "(?:0?[1-9]|1[0-2])"));
    base_pattern.push_back(std::make_pair
    ("MONTHDAY", "(?:(?:0[1-9])|(?:[12][0-9])|(?:3[01])|[1-9])"));

    //Days: Monday, Tue, Thu, etc..
    base_pattern.push_back(std::make_pair
    ("DAY", "(?:Mon(?:day)?|Tue(?:sday)?|Wed(?:nesday)?|Thu(?:rsday)?|Fri(?:day)?|Sat(?:urday)?|Sun(?:day)?)"));

    //Years?
    base_pattern.push_back(std::make_pair
    ("YEAR",   "(?>\\d\\d){1,2}"));
    base_pattern.push_back(std::make_pair
    ("HOUR",   "(?:2[0123]|[01]?[0-9])"));
    base_pattern.push_back(std::make_pair
    ("MINUTE", "(?:[0-5][0-9])"));
    // '60' is a leap second in most time standards and thus is valid.
    base_pattern.push_back(std::make_pair
    ("SECOND", "(?:(?:[0-5][0-9]|60)(?:[:.,][0-9]+)?)"));
    base_pattern.push_back(std::make_pair
    ("TIME",   "(?!<[0-9])%{HOUR}:%{MINUTE}(?::%{SECOND})(?![0-9])"));
    // datestamp is YYYY/MM/DD-HH:MM:SS.UUUU (or something like it)
    base_pattern.push_back(std::make_pair
    ("DATE_US", "%{MONTHNUM}[/-]%{MONTHDAY}[/-]%{YEAR}"));
    base_pattern.push_back(std::make_pair
    ("DATE_EU", "%{MONTHDAY}[./-]%{MONTHNUM}[./-]%{YEAR}"));
    base_pattern.push_back(std::make_pair
    ("ISO8601_TIMEZONE", "(?:Z|[+-]%{HOUR}(?::?%{MINUTE}))"));
    base_pattern.push_back(std::make_pair
    ("ISO8601_SECOND", "(?:%{SECOND}|60)"));
    base_pattern.push_back(std::make_pair
    ("TIMESTAMP_ISO8601", "%{YEAR}-%{MONTHNUM}-%{MONTHDAY}[T ]%{HOUR}:?%{MINUTE}(?::?%{SECOND})?%{ISO8601_TIMEZONE}?"));
    base_pattern.push_back(std::make_pair
    ("DATE", "%{DATE_US}|%{DATE_EU}"));
    base_pattern.push_back(std::make_pair
    ("DATESTAMP", "%{DATE}[- ]%{TIME}"));
    base_pattern.push_back(std::make_pair
    ("TZ", "(?:[PMCE][SD]T|UTC)"));
    base_pattern.push_back(std::make_pair
    ("DATESTAMP_RFC822", "%{DAY} %{MONTH} %{MONTHDAY} %{YEAR} %{TIME} %{TZ}"));
    base_pattern.push_back(std::make_pair
    ("DATESTAMP_OTHER", "%{DAY} %{MONTH} %{MONTHDAY} %{TIME} %{TZ} %{YEAR}"));

    for (uint32_t i = 0; i < base_pattern.size(); i++) {
        gp_->add_pattern_from_string("", base_pattern[i].first + " " + base_pattern[i].second);    
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
        LogParserConfigInfo parserInfo;
        parserInfo.set_domain_id(it->first);
        for (std::set<string>::iterator iter = it->second.begin();
               iter != it->second.end(); iter++) {
            LogPatternConfigInfo patternInfo;
            string name = *iter;
            string pattern = gp_->get_pattern(name);
            patternInfo.set_name(name);
            patternInfo.set_pattern(pattern);
            std::set<string>::iterator it1;
            std::vector<std::string> tags;
            for (it1 = tag_metric_list_[*iter].tags_.begin();
                 it1 != tag_metric_list_[*iter].tags_.end(); it1++) {
                tags.push_back(*it1);
            }
            patternInfo.set_tags(tags);
            std::vector<std::string> metrics;
            for (it1 = tag_metric_list_[*iter].metrics_.begin();
                 it1 != tag_metric_list_[*iter].metrics_.end(); it1++) {
                metrics.push_back(*it1);
            }
            patternInfo.set_metrics(metrics);
            parserInfo.pattern_infos.push_back(patternInfo);
        }
        config_info->push_back(parserInfo);
    }

    for (uint32_t i = 0; i < base_pattern.size(); i++) {
        LogParserPreInstallPatternInfo preinstall_pattern;
        string name = base_pattern[i].first;
        string pattern = base_pattern[i].second;
        preinstall_pattern.set_name(name);
        preinstall_pattern.set_pattern(pattern);
        preinstall_info->push_back(preinstall_pattern);
    }
}
