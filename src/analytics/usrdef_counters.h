/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __USER_DEFINED_COUNTER_H__
#define __USER_DEFINED_COUNTER_H__


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "http/client/vncapi.h"
#include "parser_util.h"
#include "configdb_connection.h"
#include "config/config-client-mgr/config_cass2json_adapter.h"
#include "config/config-client-mgr/config_json_parser_base.h"

class Options;


class UserDefinedCounterData {
    public:
        explicit UserDefinedCounterData(std::string name, std::string pat):
            refreshed_(true), name_(name) {
            SetPattern(pat);
        }
        void SetPattern(std::string pat) {
            regexp_ = boost::regex(pat);
            regexp_str_ = pat;
        }
        bool operator==(const UserDefinedCounterData &rhs) {
            return rhs.IsMe(name_) &&  rhs.pattern() == regexp_str_;
        }
        const boost::regex& regexp() const { return regexp_; }
        const std::string name() { return name_; }
        bool IsMe(std::string name) const { return name == name_; }
        const std::string pattern() const { return regexp_str_; }
        void Refresh() { refreshed_ = true; }
        bool IsRefreshed() { bool r = refreshed_; refreshed_ = false; return r;}
    private:
        bool                   refreshed_;
        std::string            name_;
        std::string            regexp_str_;
        boost::regex           regexp_;
};

typedef std::map<std::string, boost::shared_ptr<UserDefinedCounterData> > Cfg_t;

class UserDefinedCounters : public ConfigJsonParserBase {
    public:
        UserDefinedCounters();
        virtual ~UserDefinedCounters();
        virtual void MatchFilter(std::string text, LineParser::WordListType *words);
        void SendUVEs();
        void AddConfig(std::string name, std::string pattern);
        bool FindByName(std::string name);
        virtual void setup_graph_filter();
        virtual bool Receive(const ConfigCass2JsonAdapter &adapter, bool add_change);

    private:
        void setup_objector_filter();
        void setup_schema_graph_filter();
        void setup_schema_wrapper_property_info();
        Cfg_t config_;
        boost::shared_ptr<ConfigDBConnection> cfgdb_connection_;

    friend class DbHandlerMsgKeywordInsertTest;
};


#endif
