/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef config_client_collector_h
#define config_client_collector_h

#include <list>
#include <map>
#include <string>

#include "config/config-client-mgr/config_json_parser_base.h"
#include "rapidjson/document.h"
#include <boost/function.hpp>

#include "analytics/options.h"
#include "usrdef_counters.h"
#include "viz_collector.h"

class ConfigClientCollector {
public:
    ConfigClientCollector(EventManager *evm, std::string hostname,
                        std::string module_name, VizCollector *analytics,
                        Options &option);
    ~ConfigClientCollector();
    void Receive(const ConfigCass2JsonAdapter &adapter, bool add_change);
private:
    boost::scoped_ptr<ConfigClientManager> config_client;
    boost::scoped_ptr<UserDefinedCounters> udc_;
};

class ConfigJsonParserCollector : public ConfigJsonParserBase {
public:
    ConfigJsonParserCollector();
    ~ConfigJsonParserCollector();
    virtual void SetupGraphFilter();
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter, bool add_change);
 
    void SetConfigClient(ConfigClientCollector *client) {
        client_ = client;
    }
private:
    void SetupObjectFilter();
    void SetupSchemaGraphFilter();
    void SetupSchemaWrapperPropertyInfo();
    ConfigClientCollector *client_;
};

#endif // config_client_collector_h
