/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"

#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "io/event_manager.h"

int ConfigClientManager::thread_count_;

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, const IFMapConfigOptions& config_options) 
    : evm_(evm), ifmap_server_(ifmap_server) {
        config_db_client_.reset(new ConfigCassandraClient(evm, config_options));
        thread_count_ = TaskScheduler::GetInstance()->HardwareThreadCount();
}

void ConfigClientManager::Initialize() {
    config_db_client_->InitDatabase();
}

ConfigDbClient *ConfigClientManager::config_db_client() const {
    return config_db_client_.get();
}

bool ConfigClientManager::GetEndOfRibComputed() const {
    return config_db_client_->end_of_rib_computed();
}

