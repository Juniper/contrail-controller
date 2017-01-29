/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ANALYTICS_CONFIGDB_CONNECTON_H_
#define ANALYTICS_CONFIGDB_CONNECTON_H_


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "discovery/client/discovery_client.h"
#include "http/client/vncapi.h"
#include "parser_util.h"

class Options;
//class DiscoveryServiceClient;

class ConfigDBConnection {
    public:
        ConfigDBConnection(EventManager *evm, VncApiConfig *vnccfg);
        ~ConfigDBConnection();
        void Update(Options *o, DiscoveryServiceClient *c);
        boost::shared_ptr<VncApi> GetVnc();
        void RetryNextApi();

    private:
        void InitVnc(EventManager *evm, VncApiConfig *vnccfg);
        void APIfromDisc(Options *o, std::vector<DSResponse> response);
        boost::shared_ptr<VncApi> vnc_;
        EventManager *evm_;
        VncApiConfig vnccfg_;
        std::vector<DSResponse> api_svr_list_;
        mutable tbb::mutex mutex_;
};


#endif
