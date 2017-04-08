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
            const std::vector<std::string> &api_servers,
            const VncApiConfig &api_config);
        ~ConfigDBConnection();
        boost::shared_ptr<VncApi> GetVnc() {
            return vnc_;
        }
        void RetryNextApi();
        void ReConfigApiServerList(const std::vector<std::string>&);

    private:
        void InitVnc();
        void UpdateApiServerList(const std::vector<std::string>&);

        boost::shared_ptr<VncApi> vnc_;
        EventManager *evm_;
        ApiServerList api_server_list_;
        VncApiConfig vnccfg_;
        int api_server_index_;
        mutable tbb::mutex mutex_;
};


#endif
