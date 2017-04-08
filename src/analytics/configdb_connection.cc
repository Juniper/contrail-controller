/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/foreach.hpp>
#include <boost/make_shared.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "configdb_connection.h"
#include "http/client/vncapi.h"
#include "options.h"

ConfigDBConnection::ConfigDBConnection(EventManager *evm,
        const std::vector<std::string> &api_servers,
        const VncApiConfig &api_config)
    : evm_(evm), vnccfg_(api_config), api_server_index_(-1) {
    UpdateApiServerList(api_servers);
}

ConfigDBConnection::~ConfigDBConnection() {
}

void
ConfigDBConnection::InitVnc() {
    if (!vnc_) {
        vnc_.reset(new VncApi(evm_, &vnccfg_));
    } else {
        vnc_->SetApiServerAddress();
    }
}

void
ConfigDBConnection::UpdateApiServerList(
                            const std::vector<std::string> &api_servers) {
    api_server_list_.clear();
    api_server_index_ = -1;
    BOOST_FOREACH(const std::string &api_server, api_servers) {
        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> sep(":");
        tokenizer tokens(api_server, sep);
        tokenizer::iterator tit = tokens.begin();
        string api_server_ip(*tit);
        int api_server_port;
        stringToInteger(*++tit, api_server_port);
        api_server_list_.push_back(std::make_pair(api_server_ip,
                                                  api_server_port));
    }
    if (!api_server_list_.empty()) {
        api_server_index_ = 0;
        vnccfg_.api_srv_ip = api_server_list_[0].first;
        vnccfg_.api_srv_port = api_server_list_[0].second;
        InitVnc();
    }
}

void
ConfigDBConnection::RetryNextApi() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (!api_server_list_.empty()) {
        if (++api_server_index_ == static_cast<int>(api_server_list_.size())) {
            api_server_index_ = 0;
        }
        vnccfg_.api_srv_ip = api_server_list_[api_server_index_].first;
        vnccfg_.api_srv_port = api_server_list_[api_server_index_].second;
        InitVnc();
    }
}

void
ConfigDBConnection::ReConfigApiServerList(
                            const std::vector<std::string> &api_servers) {
    tbb::mutex::scoped_lock lock(mutex_);
    UpdateApiServerList(api_servers);
}
