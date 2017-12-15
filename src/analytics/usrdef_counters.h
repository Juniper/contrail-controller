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
#include "analytics_types.h"
#include "config_client_collector.h"

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

class UserDefinedCounters {
    public:
        UserDefinedCounters();
        virtual ~UserDefinedCounters();
        virtual void MatchFilter(std::string text, LineParser::WordListType *words);
        void SendUVEs();
        void AddConfig(std::string name, std::string pattern);
        bool FindByName(std::string name);
        void UDCHandler(const contrail_rapidjson::Document &jdoc, bool add_change);
        void GetUDCConfig(std::vector<LogStatisticConfigInfo> *config_info);
    private:
        Cfg_t config_;
};


#endif
