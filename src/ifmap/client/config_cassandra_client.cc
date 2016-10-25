/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include "base/logging.h"
#include "io/event_manager.h"
#include "database/cassandra/cql/cql_if.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

#include "sandesh/common/vns_constants.h"

const std::string ConfigCassandraClient::kUuidTableName = "obj_uuid_table";
const std::string ConfigCassandraClient::kFqnTableName = "obj_fq_name_table";
const std::string ConfigCassandraClient::kCassClientTaskId = "CN:CassClient";

ConfigCassandraClient::ConfigCassandraClient(EventManager *evm,
                                             const IFMapConfigOptions &options)
        : ConfigDbClient(options), evm_(evm) {
    dbif_.reset(new cass::cql::CqlIf(evm, config_db_ips(),
                GetFirstConfigDbPort(), "", ""));
}

ConfigCassandraClient::~ConfigCassandraClient() {
    if (dbif_) {
        //dbif_->Db_Uninit(....);
    }
}

void ConfigCassandraClient::InitRetry() {
    dbif_->Db_Uninit();
    sleep(kInitRetryTimeSec);
}

void ConfigCassandraClient::InitDatabase() {
    while (true) {
        if (!dbif_->Db_Init()) {
            CONFIG_CASS_CLIENT_DEBUG(ConfigCassInitErrorMessage,
                                     "Database initialization failed");
            InitRetry();
            continue;
        }
        if (!dbif_->Db_SetTablespace(g_vns_constants.API_SERVER_KEYSPACE_NAME)){
            CONFIG_CASS_CLIENT_DEBUG(ConfigCassInitErrorMessage,
                                     "Setting database keyspace failed");
            InitRetry();
            continue;
        }
        if (!dbif_->Db_UseColumnfamily(kUuidTableName)) {
            InitRetry();
            continue;
        }
        if (!dbif_->Db_UseColumnfamily(kFqnTableName)) {
            InitRetry();
            continue;
        }
        break;
    }
}

