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
        VncApiConfig *vnccfg) : evm_(evm) {
    InitVnc(evm_, vnccfg);
}

void
ConfigDBConnection::InitVnc(EventManager *evm, VncApiConfig *vnccfg) {
    if (vnccfg) {
        if (!vnc_) {
            vnc_.reset(new VncApi(evm, vnccfg));
        } else {
            vnc_->SetApiServerAddress();
        }
    }
}

ConfigDBConnection::~ConfigDBConnection() {
}


boost::shared_ptr<VncApi> ConfigDBConnection::GetVnc() {
    return vnc_;
}

#if 0
void
ConfigDBConnection::APIfromDisc(Options *o, std::vector<DSResponse> response) {
    tbb::mutex::scoped_lock lock(mutex_);
    if (api_svr_list_.empty()) {
        api_svr_list_ = response;
        if (!api_svr_list_.empty()) {
            lock.release();
            vnccfg_.ks_srv_ip          = o->auth_host();
            vnccfg_.ks_srv_port        = o->auth_port();
            vnccfg_.protocol           = o->auth_protocol();
            vnccfg_.user               = o->auth_user();
            vnccfg_.password           = o->auth_passwd();
            vnccfg_.tenant             = o->auth_tenant();

            RetryNextApi();
        }
    } else {
        api_svr_list_.erase(api_svr_list_.begin(), api_svr_list_.end());
        api_svr_list_ = response;
    }
}
#endif

void
ConfigDBConnection::RetryNextApi() {
    tbb::mutex::scoped_lock lock(mutex_);
#if 0
    if (!api_svr_list_.empty()) {
        DSResponse api = api_svr_list_.back();
        api_svr_list_.pop_back();
        lock.release();
        vnccfg_.cfg_srv_ip         = api.ep.address().to_string();
        vnccfg_.cfg_srv_port       = api.ep.port();
        InitVnc(evm_, &vnccfg_);
    }
#endif
}
