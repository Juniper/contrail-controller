/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_client_collector.h"

ConfigClientCollector::ConfigClientCollector(EventManager *evm, 
                                             std::string hostname, 
                                             std::string module_name, 
                                             Options &options) {

    ConfigFactory::Register<ConfigJsonParserBase>(
                          boost::factory<ConfigJsonParserCollector *>());
    config_client_.reset(new ConfigClientManager(evm, hostname,
                            module_name, options.configdb_options()));

    options.set_config_client_manager(config_client_);
}

ConfigClientCollector::~ConfigClientCollector() {
}

void
ConfigClientCollector::Init() {
    config_client_->Initialize();
}

void
ConfigClientCollector::RegisterConfigReceive(std::string name,
                                             ConfigRxCallback cb) {
    ConfigJsonParserCollector *json_parser =
     static_cast<ConfigJsonParserCollector *>(
                              config_client_->config_json_parser());
    json_parser->RegisterConfigReceive(name, cb);
}

void
ConfigClientCollector::UnRegisterConfigReceive(std::string name,
                                               ConfigRxCallback cb) {
    ConfigJsonParserCollector *json_parser =
     static_cast<ConfigJsonParserCollector *>(
                              config_client_->config_json_parser());
    json_parser->UnRegisterConfigReceive(name, cb);
}


ConfigJsonParserCollector::ConfigJsonParserCollector() {
}

ConfigJsonParserCollector::~ConfigJsonParserCollector() {
}

// All of these info is manually copy from schema/xxx_server.cc,
// since these auto generate file is couple with ifmap_server
// we should strength autogen to decouple them
// Object info: xxx_Server_GenerateObjectTypeList fuction
// graph: xxx_Server_GenerateGraphFilter function
// wrapper: xxx_GenerateWrapperPropertyInfo function
void ConfigJsonParserCollector::SetupObjectFilter() {
    // for user defined log stats
    AddObjectType("global_system_config");
    // for structured syslog
    AddObjectType("structured_syslog_application_record");
    AddObjectType("structured_syslog_hostname_record");
    AddObjectType("structured_syslog_config");
    AddObjectType("project");
    AddObjectType("global_analytics_config");
    AddObjectType("global_system_config");
    AddObjectType("structured_syslog_message");
}
void ConfigJsonParserCollector::SetupSchemaGraphFilter() {
    // for structed system log
    AddParentName(make_pair("structured_syslog_config", 
                            "structured_syslog_message"), 
                            "structured-syslog-config-structured-syslog-message");
    AddParentName(make_pair("structured_syslog_config",
                            "structured_syslog_hostname_record"),
                 "structured-syslog-config-structured-syslog-hostname-record");
    AddParentName(make_pair("structured_syslog_config",
                            "structured_syslog_application_record"), 
                 "structured-syslog-config-structured-syslog-application-record");
    AddParentName(make_pair("project", "structured_syslog_config"), 
                            "project-structured-syslog-config");
    AddParentName(make_pair("global_analytics_config", "structured_syslog_config"),
                            "global-analytics-config-structured-syslog-config");
    AddParentName(make_pair("global_system_config", "global_analytics_config"), 
                            "global-system-config-global-analytics-config");
}
void ConfigJsonParserCollector::SetupSchemaWrapperPropertyInfo() {
    // for user defined log stats
    AddWrapperField("global_system_config:user_defined_log_statistics",
                                                            "statlist");
    AddWrapperField("global_system_config:user_defined_syslog_patterns", 
                                                         "patternlist");
}

void ConfigJsonParserCollector::SetupGraphFilter() {
    SetupObjectFilter();
    SetupSchemaGraphFilter();
    SetupSchemaWrapperPropertyInfo();
}

void
ConfigJsonParserCollector::RegisterConfigReceive(std::string name,
                                                 ConfigRxCallback cb) {
    if (callback_list_.find(name) != callback_list_.end()) {
        return;
    }
    callback_list_.insert(make_pair(name, cb));
}

void
ConfigJsonParserCollector::UnRegisterConfigReceive(std::string name,
                                                   ConfigRxCallback cb) {
    if (callback_list_.find(name) == callback_list_.end()) {
        return;
    }
    callback_list_.erase(name);
}

bool ConfigJsonParserCollector::Receive(const ConfigCass2JsonAdapter &adapter,
                                                bool add_change) {
    for (RxConfigList::const_iterator it = callback_list_.begin();
        it != callback_list_.end(); it++) {
        it->second(adapter.document(), add_change);
    }    
    return true;
}
