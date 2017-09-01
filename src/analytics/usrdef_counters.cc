/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include "analytics_types.h"
#include "usrdef_counters.h"
#include "http/client/vncapi.h"
#include "options.h"

UserDefinedCounters::UserDefinedCounters()
{
}

UserDefinedCounters::~UserDefinedCounters()
{
    config_.erase(config_.begin(), config_.end());
}

void UserDefinedCounters::SetupObjectFilter() {
    // we only care global-system-config row change.
    AddObjectType("global_system_config");
}
void UserDefinedCounters::SetupSchemaGraphFilter() {
    // we do not care ref/parent info
}
void UserDefinedCounters::SetupSchemaWrapperPropertyInfo() {
    // we only care user_defined_log_statistics
    AddWrapperField("global_system_config:user_defined_log_statistics", "statlist");
}

void UserDefinedCounters::SetupGraphFilter() {
    SetupObjectFilter();
    SetupSchemaGraphFilter();
    SetupSchemaWrapperPropertyInfo();
}

bool
UserDefinedCounters::Receive(const ConfigCass2JsonAdapter &adapter, bool add_change)
{
    if (!add_change) {
        return true;
    }

    if (adapter.document().IsObject() 
                 && adapter.document().HasMember("global_system_config")) {
        if (!adapter.document()["global_system_config"].HasMember(
                  "user_defined_log_statistics")) {
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
            return true;
        }

        if (!adapter.document()["global_system_config"]
                 ["user_defined_log_statistics"].IsObject()) {
            return false;
        }

        if (!adapter.document()["global_system_config"]
                 ["user_defined_log_statistics"].HasMember("statlist")) {
            return false;
        }

        const contrail_rapidjson::Value& gsc = adapter.document()
                ["global_system_config"]["user_defined_log_statistics"]["statlist"];
        if (!gsc.IsArray()) {
            return false;
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
    } else {
        return false;
    }

    return true;
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

void 
UserDefinedCounters::GetUDCConfig(std::vector<LogStatisticConfigInfo> 
                                              *config_info) {
    for (Cfg_t::iterator it = config_.begin(); 
                 it != config_.end(); it++) {
        LogStatisticConfigInfo config;
        config.set_name(it->second->name());
        config.set_pattern(it->second->pattern());
        config_info->push_back(config);
    }
}
