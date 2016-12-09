/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"

#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_server.h"
#include "io/event_manager.h"

int ConfigClientManager::thread_count_;

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, const IFMapConfigOptions& config_options) 
        : evm_(evm), ifmap_server_(ifmap_server) {
    config_json_parser_.reset(new ConfigJsonParser(ifmap_server_->database()));
    config_db_client_.reset(new ConfigCassandraClient(evm, config_options,
                                    config_json_parser_.get()));
    thread_count_ = TaskScheduler::GetInstance()->HardwareThreadCount();
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

bool ConfigClientManager::GetEndOfRibComputed() const {
    return config_db_client_->end_of_rib_computed();
}

