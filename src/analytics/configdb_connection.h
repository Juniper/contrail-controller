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
        ConfigDBConnection(EventManager *evm, VncApiConfig *vnccfg);
        ~ConfigDBConnection();
        boost::shared_ptr<VncApi> GetVnc();
        void RetryNextApi();

    private:
        void InitVnc(EventManager *evm, VncApiConfig *vnccfg);
        boost::shared_ptr<VncApi> vnc_;
        EventManager *evm_;
        VncApiConfig vnccfg_;
        mutable tbb::mutex mutex_;
};


#endif
