/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */
#ifndef ANALYTICS_CONFIGDB_CONNECTON_H_
#define ANALYTICS_CONFIGDB_CONNECTON_H_


#include <map>
#include <tbb/atomic.h>
#include <boost/shared_ptr.hpp>
#include <boost/regex.hpp>
#include "http/client/vncapi.h"
#include "parser_util.h"

class Options;

class ConfigDBConnection {
    public:
        typedef std::vector<std::pair<std::string, int> > ApiServerList;

        ConfigDBConnection(EventManager *evm,
            const ApiServerList &api_servers,
            const VncApiConfig &api_config);
        ~ConfigDBConnection();
        boost::shared_ptr<VncApi> GetVnc();
        void RetryNextApi();

    private:
        void InitVnc();

        boost::shared_ptr<VncApi> vnc_;
        EventManager *evm_;
        ApiServerList api_server_list_;
        VncApiConfig vnccfg_;
        int api_server_index_;
        mutable tbb::mutex mutex_;
};


#endif
