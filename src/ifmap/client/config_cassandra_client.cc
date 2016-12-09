/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include "base/logging.h"
#include "config_cass2json_adapter.h"
#include "io/event_manager.h"
#include "database/cassandra/cql/cql_if.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

#include "sandesh/common/vns_constants.h"

#include <boost/foreach.hpp>
#include <boost/uuid/uuid.hpp>

using namespace std;

const string ConfigCassandraClient::kUuidTableName = "obj_uuid_table";
const string ConfigCassandraClient::kFqnTableName = "obj_fq_name_table";
const string ConfigCassandraClient::kCassClientTaskId = "CN:CassClient";

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

bool ConfigCassandraClient::ParseCassandraResponse(const string &uuid,
        const GenDb::ColList &col_list, CassDataVec *cass_data_vec) {

    string uuid_as_str(string("\"" + uuid + "\""));
    cass_data_vec->push_back(JsonAdapterDataType("uuid", uuid_as_str));
    BOOST_FOREACH(const GenDb::NewCol &ncol, col_list.columns_) {
        assert(ncol.name->size() == 1);
        assert(ncol.value->size() == 1);

        const GenDb::DbDataValue &dname(ncol.name->at(0));
        assert(dname.which() == GenDb::DB_VALUE_BLOB);
        GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
        string key(reinterpret_cast<const char *>(dname_blob.data()),
                    dname_blob.size());
        std::cout << "key is " << key;

        const GenDb::DbDataValue &dvalue(ncol.value->at(0));
        assert(dvalue.which() == GenDb::DB_VALUE_STRING);
        string value(boost::get<string>(dvalue));
        std::cout << " and value is " << value << std::endl;

        cass_data_vec->push_back(JsonAdapterDataType(key, value));
    }

    cout << "Filled in " << cass_data_vec->size() << " entries\n";
    return true;
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
    // TODO: remove this after all testing.
    //GetRow("e6e5609b-64f8-4238-82e6-163e2ec11d21");
    //GetRow("e6e5609b-64f8-4238-82e6-abce2ec1wxyz");
}

bool ConfigCassandraClient::GetRow(const string &uuid) {
    GenDb::ColList col_list;
    GenDb::DbDataValueVec key;

    auto_ptr<CassDataVec> cass_data_vec(new CassDataVec());
    key.push_back(GenDb::Blob(reinterpret_cast<const uint8_t *>(uuid.c_str()),
                  uuid.size()));

    if (dbif_->Db_GetRow(&col_list, kUuidTableName, key,
                         GenDb::DbConsistency::QUORUM)) {
        if (col_list.GetSize() &&
                ParseCassandraResponse(uuid, col_list, cass_data_vec.get())) {
            ConfigCass2JsonAdapter *ccja =
                new ConfigCass2JsonAdapter(*(cass_data_vec.get()));
            cout << "doc-string is\n" << ccja->doc_string() << endl;
        }
    } else {
        IFMAP_WARN(IFMapGetRowError, "GetRow failed for table", kUuidTableName);
        return false;
    }

    return true;
}

