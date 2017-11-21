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

struct UserDefTagMetric {
    UserDefTagMetric() {}
    UserDefTagMetric(std::vector<std::string> &tags,
                     std::vector<std::string> &metrics) {
        for (uint32_t i = 0; i < tags.size(); i++) {
            tags_.insert(tags[i]);
        }
        for (uint32_t i = 0; i < metrics.size(); i++) {
            metrics_.insert(metrics[i]);
        }
    }
    std::set<std::string> tags_;
    std::set<std::string> metrics_;
};

typedef std::map<std::string, UserDefTagMetric> TagMetricMap;
typedef std::map<std::string, std::set<std::string> > DomainList;

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
    TagMetricMap tag_metric_list_;
    DomainList domain_list_;
    SyslogListeners *syslog_listener_;
    StatWalker::StatTableInsertFn db_insert_cb_;
    std::set<string> base_pattern_;
    void install_base_pattern();
    void add_RFC5424_pattern();
    void add_base_pattern(std::string pattern);
    time_t parse_timestamp(std::string time_s);
    void parse_to_stats(string source, uint64_t ts,
                        string domain_id, string s);
};

#endif // SRC_USER_DEFINE_SYSLOG_PARSER_H_ 
