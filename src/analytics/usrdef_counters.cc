/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "usrdef_counters.h"
#include "http/client/vncapi.h"
#include "options.h"

UserDefinedCounters::UserDefinedCounters(boost::shared_ptr<ConfigDBConnection> cfgdb_connection)
                    : cfgdb_connection_(cfgdb_connection)
{

}

UserDefinedCounters::~UserDefinedCounters()
{
    config_.erase(config_.begin(), config_.end());
}

void
UserDefinedCounters::PollCfg()
{
    ReadConfig();
}

void
UserDefinedCounters::ReadConfig()
{
    if (cfgdb_connection_->GetVnc()) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("user_defined_log_statistics");

        cfgdb_connection_->GetVnc()->GetConfig("global-system-config", ids, filters, parents, refs,
                fields, boost::bind(&UserDefinedCounters::UDCHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
UserDefinedCounters::UDCHandler(contrail_rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("global-system-configs")) {
        for (contrail_rapidjson::SizeType j=0;
                    j < jdoc["global-system-configs"].Size(); j++) {

            if (!jdoc["global-system-configs"][j].HasMember(
                      "user_defined_log_statistics")) {
                    continue;
            }

            if (!jdoc["global-system-configs"][j]
                     ["user_defined_log_statistics"].IsObject()) {
                continue;
            }

            if (!jdoc["global-system-configs"][j]
                     ["user_defined_log_statistics"].HasMember("statlist")) {
                continue;
            }

            const contrail_rapidjson::Value& gsc = jdoc["global-system-configs"][j]
                    ["user_defined_log_statistics"]["statlist"];
            if (!gsc.IsArray()) {
                continue;
            }

            for (contrail_rapidjson::SizeType i = 0; i < gsc.Size(); i++) {
                if (!gsc[i].IsObject() || !gsc[i].HasMember("name") ||
                        !gsc[i].HasMember("pattern")) {
                    continue;
                }
                std::string name = gsc[i]["name"].GetString(),
                            patrn = gsc[i]["pattern"].GetString();
                AddConfig(name, patrn);
                std::cout << "\nname: " << name << "\npattern: "
                    << patrn << "\n";
            }

            Cfg_t::iterator cit=config_.begin();
            while (cit != config_.end()) {
                Cfg_t::iterator dit = cit++;
                if (!dit->second->IsRefreshed()) {
                    UserDefinedLogStatistic udc;
                    udc.set_name(dit->first);
                    udc.set_deleted(true);
                    UserDefinedLogStatisticUVE::Send(udc);
                    config_.erase(dit);
                }
            }
        }
        return;
    } else {
                //Print Errors
    }
    cfgdb_connection_->RetryNextApi();
}

void
UserDefinedCounters::MatchFilter(std::string text, LineParser::WordListType *w)
{
    for(Cfg_t::iterator it=config_.begin(); it != config_.end(); ++it) {
        if (LineParser::SearchPattern(it->second->regexp(), text)) {
            UserDefinedLogStatistic udc;
            udc.set_name(it->first);
            udc.set_rx_event(1);
            UserDefinedLogStatisticUVE::Send(udc);
            w->insert(it->first);
        }
    }
}

void
UserDefinedCounters::AddConfig(std::string name, std::string pattern)
{
    Cfg_t::iterator it=config_.find(name);
    if (it  != config_.end()) {
        if (pattern == it->second->pattern()) {
            // ignore
        } else {
            it->second->SetPattern(pattern);
        }
        it->second->Refresh();
    } else {
        boost::shared_ptr<UserDefinedCounterData> c(new UserDefinedCounterData(
                    name, pattern));
        config_.insert(std::make_pair<std::string,
                boost::shared_ptr<UserDefinedCounterData> >(name, c));
    }
}

bool
UserDefinedCounters::FindByName(std::string name) {
    Cfg_t::iterator it=config_.find(name);
    return it  != config_.end();
}

