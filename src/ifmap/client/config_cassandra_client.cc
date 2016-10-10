/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include "io/event_manager.h"
#include "database/cassandra/cql/cql_if.h"

static const std::string kUuidTableName = "obj_uuid_table";
static const std::string kFqnTableName = "obj_fq_name_table";

ConfigCassandraClient::ConfigCassandraClient(EventManager *evm,
                                             const IFMapConfigOptions &options)
        : ConfigDbClient(options), evm_(evm) {
    dbif_.reset(new cass::cql::CqlIf(evm, config_db_ips(),
                GetFirstConfigDbPort(), config_db_user(), config_db_password()));
}

ConfigCassandraClient::~ConfigCassandraClient() {
    if (dbif_) {
        //dbif_->Db_Uninit(....);
    }
}

void ConfigCassandraClient::InitDatabase() {
    // dbif_->Db_Init
    // dbif_->Db_SetTablespace
}

