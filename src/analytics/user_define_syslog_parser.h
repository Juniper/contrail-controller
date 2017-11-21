/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_USER_DEFINE_SYSLOG_PARSER_H_
#define SRC_USER_DEFINE_SYSLOG_PARSER_H_

#include <map>
#include <string>
#include <deque>
#include <set>
#include <vector>
#include "analytics_types.h"
#include "config_client_collector.h"
#include "grok_parser.h"
#include "generator.h"
#include "stat_walker.h"
#include "syslog_collector.h"
struct HeaderCfgInfo {
    std::string hostname;
    std::string appname;
    std::string procid;
    std::string msgid;
    std::string domain_id;
    bool alarm_send;
};

struct MetricInfo {
    MetricInfo(std::string name_, std::string type_) {
        name = name_;
        type = type_;
    }
    std::string name;
    std::string type;
    bool operator<(const MetricInfo &other) const {
        return name < other.name;
    }

    MetricInfo &operator=(const MetricInfo &other) {
        if (this != &other) {
            name = other.name;
            type = other.type;
        }
        return *this;
    }
};

typedef std::map<std::string, std::set<std::string> > PatternList;
typedef std::map<std::string, std::set<std::string> > TagList;
typedef std::map<std::string, std::set<MetricInfo> > MetricList;
typedef std::map<std::string, std::set<std::string> > DomainList;
typedef std::map<std::string, HeaderCfgInfo> NameToHeaderMap;

class UserDefineSyslogParser {
public:
    UserDefineSyslogParser(EventManager *evm,
                           SyslogListeners *syslog_listener,
                           StatWalker::StatTableInsertFn stat_db_cb);
    ~UserDefineSyslogParser();

    /* Receive configuration*/
    void rx_config(const contrail_rapidjson::Document &jdoc, bool);

     /* Get config info*/
    void get_config(std::vector<LogParserConfigInfo> *config_info,
             std::vector<LogParserPreInstallPatternInfo> *preinstall_pattern);

    /* receive syslog and parse*/
    void syslog_parse(std::string ip, std::string strin);

    void send_generic_stat(std::map<std::string, std::string> &m_in);
private:
    boost::scoped_ptr<GrokParser> gp_;
    PatternList pattern_list_;
    TagList     tag_list_;
    MetricList  metric_list_;
    DomainList domain_list_;
    NameToHeaderMap name_to_header_map_;
    SyslogListeners *syslog_listener_;
    StatWalker::StatTableInsertFn db_insert_cb_;
    void install_base_pattern();
    void add_RFC5424_pattern();
    void add_base_pattern(std::string pattern);
    time_t parse_timestamp(std::string time_s);
    void parse_to_stats(string source, uint64_t ts, string domain_id, string s);
    void delete_config(string name, const contrail_rapidjson::Document &jdoc);
    void add_update_config(string name, const contrail_rapidjson::Document &jdoc);
};

#endif // SRC_USER_DEFINE_SYSLOG_PARSER_H_ 
