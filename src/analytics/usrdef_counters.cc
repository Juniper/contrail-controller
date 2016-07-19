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

UserDefinedCounters::UserDefinedCounters(EventManager *evm,
        VncApiConfig *vnccfg) : evm_(evm)
{
    InitVnc(evm_, vnccfg);
}

void
UserDefinedCounters::InitVnc(EventManager *evm, VncApiConfig *vnccfg)
{
    VncApi *v = vnccfg?new VncApi(evm, vnccfg):0;
    vnc_.reset(v);
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
    if (vnc_) {
        std::vector<std::string> ids;
        std::vector<std::string> filters;
        std::vector<std::string> parents;
        std::vector<std::string> refs;
        std::vector<std::string> fields;

        fields.push_back("user_defined_log_statistics");

        vnc_->GetConfig("global-system-config", ids, filters, parents, refs,
                fields, boost::bind(&UserDefinedCounters::UDCHandler, this,
                    _1, _2, _3, _4, _5, _6));
    }
}

void
UserDefinedCounters::UDCHandler(rapidjson::Document &jdoc,
            boost::system::error_code &ec,
            std::string version, int status, std::string reason,
            std::map<std::string, std::string> *headers)
{
    if (jdoc.IsObject() && jdoc.HasMember("global-system-configs")) {
        for (rapidjson::SizeType j=0;
                    j < jdoc["global-system-configs"].Size(); j++) {
            if (jdoc["global-system-configs"][j]["user_defined_log_statistics"].
                    IsObject()) {
                const rapidjson::Value& gsc = jdoc["global-system-configs"][j]
                        ["user_defined_log_statistics"]["statlist"];
                if (gsc.IsArray()) {
                    for (rapidjson::SizeType i = 0; i < gsc.Size(); i++) {
                        AddConfig(gsc[i]["name"].GetString(),
                                gsc[i]["pattern"].GetString());
                        std::cout << "\nname: " << gsc[i]["name"].GetString()
                        << "\npattern: " << gsc[i]["pattern"].GetString()
                        << "\n";
                    }
                }
            }
        }
        return;
    } else {
                //Print Errors
    }
    RetryNextApi();
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
UserDefinedCounters::Update(Options *o, DiscoveryServiceClient *c) {
    c->Subscribe(g_vns_constants.API_SERVER_DISCOVERY_SERVICE_NAME,
            0, boost::bind(&UserDefinedCounters::APIfromDisc, this, o, _1));
}

void
UserDefinedCounters::APIfromDisc(Options *o, std::vector<DSResponse> response)
{
    if (api_svr_list_.empty()) {
        api_svr_list_ = response;
        if (!api_svr_list_.empty()) {
            vnccfg_.ks_srv_ip          = o->auth_host();
            vnccfg_.ks_srv_port        = o->auth_port();
            vnccfg_.protocol           = o->auth_protocol();
            vnccfg_.user               = o->auth_user();
            vnccfg_.password           = o->auth_passwd();
            vnccfg_.tenant             = o->auth_tenant();

            RetryNextApi();
        }
    }
}

void
UserDefinedCounters::RetryNextApi()
{
    if (!api_svr_list_.empty()) {
        DSResponse api = api_svr_list_.back();
        api_svr_list_.pop_back();
        vnccfg_.cfg_srv_ip         = api.ep.address().to_string();
        vnccfg_.cfg_srv_port       = api.ep.port();
        InitVnc(evm_, &vnccfg_);
    }
}
