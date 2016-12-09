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
                                             const IFMapConfigOptions &options,
                                             ConfigJsonParser *in_parser)
        : ConfigDbClient(options), evm_(evm), parser_(in_parser) {
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

bool ConfigCassandraClient::ParseUuidTableRowResponse(const string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec) {

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

bool ConfigCassandraClient::ParseRowAndEnqueueToParser(const string &uuid_key,
        const GenDb::ColList &col_list) {
    auto_ptr<CassColumnKVVec> cass_data_vec(new CassColumnKVVec());
    if (ParseUuidTableRowResponse(uuid_key, col_list, cass_data_vec.get())) {
        // Convert column data to json string.
        ConfigCass2JsonAdapter *ccja =
            new ConfigCass2JsonAdapter(*(cass_data_vec.get()));
        cout << "doc-string is\n" << ccja->doc_string() << endl;

        // Enqueue ccja to the parser here.
        // parser_->Receive(ccja->doc_string().....);
    } else {
        IFMAP_WARN(IFMapGetRowError, "Parsing row response failed for table",
                   kUuidTableName);
        return false;
    }

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
    //ReadUuidTableRow("e6e5609b-64f8-4238-82e6-163e2ec11d21");
    //ReadUuidTableRow("e6e5609b-64f8-4238-82e6-abce2ec1wxyz");
    BulkDataSync();
}

bool ConfigCassandraClient::ReadUuidTableRow(const string &uuid_key) {
    GenDb::ColList col_list;
    GenDb::DbDataValueVec key;

    key.push_back(GenDb::Blob(
        reinterpret_cast<const uint8_t *>(uuid_key.c_str()), uuid_key.size()));

    if (dbif_->Db_GetRow(&col_list, kUuidTableName, key,
                         GenDb::DbConsistency::QUORUM)) {
        if (col_list.columns_.size()) {
            ParseRowAndEnqueueToParser(uuid_key, col_list);
        }
    } else {
        IFMAP_WARN(IFMapGetRowError, "GetRow failed for table", kUuidTableName);
        return false;
    }

    return true;
}

bool ConfigCassandraClient::ReadAllUuidTableRows() {
    GenDb::ColListVec cl_vec;

    if (dbif_->Db_GetAllRows(&cl_vec, kUuidTableName,
                             GenDb::DbConsistency::QUORUM)) {
        cout << "All Rows size " << cl_vec.size() << endl;
        int count = 0;
        BOOST_FOREACH(const GenDb::ColList &cl_list, cl_vec) {
            assert(cl_list.rowkey_.size() == 1);
            assert(cl_list.rowkey_[0].which() == GenDb::DB_VALUE_BLOB);
            GenDb::Blob brk(boost::get<GenDb::Blob>(cl_list.rowkey_[0]));
            string uuid_key(reinterpret_cast<const char *>(brk.data()),
                            brk.size());
            cout << "Row " << ++count << " num-cols " 
                 << cl_list.columns_.size() << " and key " << uuid_key << endl;
            if (cl_list.columns_.size()) {
                ParseRowAndEnqueueToParser(uuid_key, cl_list);
            }
        }
    } else {
        IFMAP_WARN(IFMapGetRowError, "GetAllRows failed for table",
                   kUuidTableName);
        return false;
    }

    return true;
}

bool ConfigCassandraClient::BulkDataSync() {
    ReadAllUuidTableRows();
    return true;
}

