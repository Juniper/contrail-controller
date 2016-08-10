/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#ifndef __USER_DEFINED_COUNTER_H__
#define __USER_DEFINED_COUNTER_H__


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "discovery/client/discovery_client.h"
#include "http/client/vncapi.h"
#include "parser_util.h"

class Options;
//class DiscoveryServiceClient;


class UserDefinedCounterData {
    public:
        explicit UserDefinedCounterData(std::string name, std::string pat):
            name_(name) {
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
        void Update(Options *o, DiscoveryServiceClient *c);
        const std::string pattern() const { return regexp_str_; }
    private:
        std::string            name_;
        std::string            regexp_str_;
        boost::regex           regexp_;
};

typedef std::map<std::string, boost::shared_ptr<UserDefinedCounterData> > Cfg_t;

class UserDefinedCounters {
    public:
        UserDefinedCounters(EventManager *evm, VncApiConfig *vnccfg);
        ~UserDefinedCounters();
        void MatchFilter(std::string text, LineParser::WordListType *words);
        void SendUVEs();
        void AddConfig(std::string name, std::string pattern);
        bool FindByName(std::string name);
        void PollCfg();
        void Update(Options *o, DiscoveryServiceClient *c);

    private:
        void ReadConfig();
        std::string ExecHelper(const std::string cmd);
        void UDCHandler(rapidjson::Document &jdoc,
                    boost::system::error_code &ec,
                    std::string version, int status, std::string reason,
                    std::map<std::string, std::string> *headers);
        void InitVnc(EventManager *evm, VncApiConfig *vnccfg);
        void RetryNextApi();
        void APIfromDisc(Options *o, std::vector<DSResponse> response);

        Cfg_t config_;
        std::string call_str_;
        boost::shared_ptr<VncApi> vnc_;
        EventManager *evm_;
        VncApiConfig vnccfg_;
        std::vector<DSResponse> api_svr_list_;
};


#endif
