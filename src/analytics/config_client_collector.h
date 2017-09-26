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

typedef boost::function<void (const contrail_rapidjson::Document &jdoc, bool)>
                                                   ConfigRxCallback;

class ConfigClientCollector {
public:
    ConfigClientCollector(EventManager *evm, std::string hostname,
                        std::string module_name, Options &option);
    ConfigClientCollector();
    ~ConfigClientCollector();
    void Init();
    void RegisterConfigReceive(std::string name, ConfigRxCallback cb);
    void UnRegisterConfigReceive(std::string name, ConfigRxCallback cb);
private:
    boost::shared_ptr<ConfigClientManager> config_client_;
};

class ConfigJsonParserCollector : public ConfigJsonParserBase {
public:
    typedef map<string, ConfigRxCallback> RxConfigList;
    ConfigJsonParserCollector();
    ~ConfigJsonParserCollector();
    virtual void SetupGraphFilter();
    virtual bool Receive(const ConfigCass2JsonAdapter &adapter, bool add_change);
    void RegisterConfigReceive(std::string name, ConfigRxCallback cb);
    void UnRegisterConfigReceive(std::string name, ConfigRxCallback cb);
private:
    void SetupObjectFilter();
    void SetupSchemaGraphFilter();
    void SetupSchemaWrapperPropertyInfo();
    RxConfigList callback_list_;
};

#endif // config_client_collector_h
