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

void UserDefinedCounters::setup_schema_graph_filter() {
}

void UserDefinedCounters::setup_schema_wrapper_property_info() {
}

void UserDefinedCounters::setup_objector_filter() { 
}

bool
UserDefinedCounters::Receive(const ConfigCass2JsonAdapter &adapter, bool add_change)
{
    if (!add_change) {
        return true;
    }
    if (adapter.document().IsObject() && adapter.document().HasMember("global-system-configs")) {
        for (contrail_rapidjson::SizeType j=0;
                    j < adapter.document()["global-system-configs"].Size(); j++) {

            if (!adapter.document()["global-system-configs"][j].HasMember(
                      "user_defined_log_statistics")) {
                    continue;
            }

            if (!adapter.document()["global-system-configs"][j]
                     ["user_defined_log_statistics"].IsObject()) {
                continue;
            }

            if (!adapter.document()["global-system-configs"][j]
                     ["user_defined_log_statistics"].HasMember("statlist")) {
                continue;
            }

            const contrail_rapidjson::Value& gsc = adapter.document()["global-system-configs"][j]
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
        return true;
    } else {
        return false;
    }
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

