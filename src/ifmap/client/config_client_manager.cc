/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"

#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_server.h"
#include "io/event_manager.h"


ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, std::string hostname,
        const IFMapConfigOptions& config_options) 
        : evm_(evm), ifmap_server_(ifmap_server) {
    config_json_parser_.reset(new ConfigJsonParser(ifmap_server_->database()));
    thread_count_ = TaskScheduler::GetInstance()->HardwareThreadCount();
    config_db_client_.reset(
            IFMapFactory::Create<ConfigCassandraClient>(this, evm,
                config_options, config_json_parser_.get(), thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname,
                                                   config_options));
}

void ConfigClientManager::Initialize() {
    config_db_client_->InitDatabase();
}

ConfigJsonParser *ConfigClientManager::config_json_parser() const {
    return config_json_parser_.get();
}

ConfigDbClient *ConfigClientManager::config_db_client() const {
    return config_db_client_.get();
}

ConfigAmqpClient *ConfigClientManager::config_amqp_client() const {
    return config_amqp_client_.get();
}

bool ConfigClientManager::GetEndOfRibComputed() const {
    return config_db_client_->end_of_rib_computed();
}

void ConfigClientManager::EnqueueUUIDRequest(string uuid_str, string obj_type,
                                             string oper) {
    config_db_client_->EnqueueUUIDRequest(uuid_str, obj_type, oper);
}
