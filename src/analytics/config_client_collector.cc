/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_client_collector.h"

ConfigClientCollector::ConfigClientCollector(EventManager *evm,
                    std::string hostname, std::string module_name,
                    VizCollector *analytics, Options &options) {

    ConfigFactory::Register<ConfigJsonParserBase>(
                          boost::factory<ConfigJsonParserCollector *>());
    config_client.reset(new ConfigClientManager(evm, hostname,
                            module_name, options.configdb_options()));
    udc_.reset(new UserDefinedCounters());

    options.set_config_client_manager(config_client.get());
    analytics->GetDbHandler()->SetUDCHandler(udc_.get());
    ConfigJsonParserCollector *json_parser =
     static_cast<ConfigJsonParserCollector *>(config_client->config_json_parser());
    json_parser->SetConfigClient(this);
    config_client->Initialize();
}

ConfigClientCollector::~ConfigClientCollector() {
}

void ConfigClientCollector::Receive(const ConfigCass2JsonAdapter &adapter,
                                                bool add_change) {
    if (add_change) {
        udc_->UDCHandler(adapter.document());
    }
}

ConfigJsonParserCollector::ConfigJsonParserCollector() {
}

ConfigJsonParserCollector::~ConfigJsonParserCollector() {
}

void ConfigJsonParserCollector::SetupObjectFilter() {
    // we only care global-system-config row change.
    AddObjectType("global_system_config");
}
void ConfigJsonParserCollector::SetupSchemaGraphFilter() {
    // we do not care ref/parent info
}
void ConfigJsonParserCollector::SetupSchemaWrapperPropertyInfo() {
    // we only care user_defined_log_statistics
    AddWrapperField("global_system_config:user_defined_log_statistics",
                                                            "statlist");
}

void ConfigJsonParserCollector::SetupGraphFilter() {
    SetupObjectFilter();
    SetupSchemaGraphFilter();
    SetupSchemaWrapperPropertyInfo();
}

bool ConfigJsonParserCollector::Receive(const ConfigCass2JsonAdapter &adapter,
                                                bool add_change) {
    if (client_) {
        client_->Receive(adapter, add_change);
    }
    return true;
}
