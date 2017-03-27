/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */


#include <sstream>
#include <boost/make_shared.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include "analytics_types.h"
#include "configdb_connection.h"
#include "http/client/vncapi.h"
#include "options.h"

ConfigDBConnection::ConfigDBConnection(EventManager *evm,
        const ApiServerList &api_servers,
        const VncApiConfig &api_config)
    : evm_(evm), api_server_list_(api_servers), vnccfg_(api_config),
      api_server_index_(-1) {
    if (!api_server_list_.empty()) {
        api_server_index_ = 0;
        vnccfg_.api_srv_ip = api_server_list_[0].first;
        vnccfg_.api_srv_port = api_server_list_[0].second;
        InitVnc();
    }
}

void
ConfigDBConnection::InitVnc() {
    if (!vnc_) {
        vnc_.reset(new VncApi(evm_, &vnccfg_));
    } else {
        vnc_->SetApiServerAddress();
    }
}

ConfigDBConnection::~ConfigDBConnection() {
}

boost::shared_ptr<VncApi> ConfigDBConnection::GetVnc() {
    return vnc_;
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
        lock.release();
        InitVnc();
    }
}
