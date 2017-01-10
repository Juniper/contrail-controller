/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "config_cass2json_adapter.h"
#include "io/event_manager.h"
#include "database/cassandra/cql/cql_if.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

#include "sandesh/common/vns_constants.h"

#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/uuid/uuid.hpp>


using namespace std;

const string ConfigCassandraClient::kUuidTableName = "obj_uuid_table";
const string ConfigCassandraClient::kFqnTableName = "obj_fq_name_table";
const string ConfigCassandraClient::kCassClientTaskId = "cassandra::Reader";
const string ConfigCassandraClient::kObjectProcessTaskId = "cassandra::ObjectProcessor";

ConfigCassandraClient::ConfigCassandraClient(ConfigClientManager *mgr, EventManager *evm,
                                             const IFMapConfigOptions &options,
                                             ConfigJsonParser *in_parser, int num_workers)
        : ConfigDbClient(options), mgr_(mgr), evm_(evm), parser_(in_parser),
        num_workers_(num_workers), uuid_read_list_(num_workers), uuid_read_set_(num_workers) {
    dbif_.reset(new cass::cql::CqlIf(evm, config_db_ips(),
                GetFirstConfigDbPort(), "", ""));

    int thread_count = TaskScheduler::GetInstance()->HardwareThreadCount();
    for (int i = 0; i < thread_count; i++) {
        config_readers_.push_back(boost::shared_ptr<TaskTrigger>(new
               TaskTrigger(boost::bind(&ConfigCassandraClient::ConfigReader,
                                       this, i),
               TaskScheduler::GetInstance()->GetTaskId("cassandra::Reader"), i)));
    }


    bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    int processor_task_id =
        TaskScheduler::GetInstance()->GetTaskId(kObjectProcessTaskId);

    obj_process_queue_.reset(new WorkQueue<ObjectProcessReq *>(processor_task_id, 0,
          bind(&ConfigCassandraClient::RequestHandler, this, _1)));
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

int ConfigCassandraClient::HashUUID(const string &uuid_str) const {
    boost::hash<string> string_hash;
    return string_hash(uuid_str) % num_workers_;
}

bool ConfigCassandraClient::ConfigReader(int worker_id) {
    CHECK_CONCURRENCY("cassandra::Reader");

    BOOST_FOREACH(const ObjTypeUUIDType &obj_type_uuid, uuid_read_list_[worker_id]) {
        cout << "Read uuid " << obj_type_uuid.second << " From : " << worker_id << endl;
        ReadUuidTableRow(obj_type_uuid.first, obj_type_uuid.second);
    }

    uuid_read_list_[worker_id].clear();
    uuid_read_set_[worker_id].clear();
    return true;
}

void ConfigCassandraClient::AddUUIDToRequestList(const string &obj_type,
                                                 const string &uuid_str) {
    int worker_id = HashUUID(uuid_str);
    pair<UUIDReadSet::iterator, bool> ret;
    bool trigger = uuid_read_list_[worker_id].empty();
    ret = uuid_read_set_[worker_id].insert(uuid_str);
    if (ret.second) {
        uuid_read_list_[worker_id].push_back(make_pair(obj_type, uuid_str));
        if (trigger) {
            config_readers_[worker_id]->Set();
        }
    }
}

void ConfigCassandraClient::ParseUuidTableRowJson(const string &uuid,
        const string &key, const string &value,
        CassColumnKVVec *cass_data_vec) {
    if (cass_data_vec->empty()) {
        string uuid_as_str(string("\"" + uuid + "\""));
        cass_data_vec->push_back(JsonAdapterDataType("uuid", uuid_as_str));
    }
    cout << "key is " << key;
    cout << " and value is " << value << endl;
    cass_data_vec->push_back(JsonAdapterDataType(key, value));
}

bool ConfigCassandraClient::ParseUuidTableRowResponse(const string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec) {

    BOOST_FOREACH(const GenDb::NewCol &ncol, col_list.columns_) {
        assert(ncol.name->size() == 1);
        assert(ncol.value->size() == 1);

        const GenDb::DbDataValue &dname(ncol.name->at(0));
        assert(dname.which() == GenDb::DB_VALUE_BLOB);
        GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
        string key(reinterpret_cast<const char *>(dname_blob.data()),
                   dname_blob.size());

        const GenDb::DbDataValue &dvalue(ncol.value->at(0));
        assert(dvalue.which() == GenDb::DB_VALUE_STRING);
        string value(boost::get<string>(dvalue));
        ParseUuidTableRowJson(uuid, key, value, cass_data_vec);
    }

    cout << "Filled in " << cass_data_vec->size() << " entries\n";
    return true;
}

bool ConfigCassandraClient::ParseFQNameRowGetUUIDList(const GenDb::ColList &col_list,
                                  ObjTypeUUIDList &uuid_list) {
    GenDb::Blob dname_blob(boost::get<GenDb::Blob>(col_list.rowkey_[0]));
    string obj_type(reinterpret_cast<const char *>(dname_blob.data()),
               dname_blob.size());
    BOOST_FOREACH(const GenDb::NewCol &ncol, col_list.columns_) {
        assert(ncol.name->size() == 1);
        assert(ncol.value->size() == 1);

        const GenDb::DbDataValue &dname(ncol.name->at(0));
        assert(dname.which() == GenDb::DB_VALUE_BLOB);
        GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
        string key(reinterpret_cast<const char *>(dname_blob.data()),
                   dname_blob.size());
        size_t temp = key.rfind(':');
        if (temp == string::npos) {
            continue;
        }
        string uuid_str = key.substr(temp+1);
        uuid_list.push_back(make_pair(obj_type, uuid_str));
        fq_name_cache_.insert(make_pair(uuid_str, key.substr(0, temp)));
    }

    return true;
}

bool ConfigCassandraClient::ParseRowAndEnqueueToParser(const string &obj_type,
                       const string &uuid_key, const GenDb::ColList &col_list) {
    auto_ptr<CassColumnKVVec> cass_data_vec(new CassColumnKVVec());
    if (ParseUuidTableRowResponse(uuid_key, col_list, cass_data_vec.get())) {
        // Convert column data to json string.
        ConfigCass2JsonAdapter *ccja =
            new ConfigCass2JsonAdapter(this, obj_type, *(cass_data_vec.get()));
        cout << "doc-string is\n" << ccja->doc_string() << endl;

        // Enqueue ccja to the parser here.
        // TODO: Operation is hard coded to add-change
        parser_->Receive(ccja->doc_string(), true, IFMapOrigin::CASSANDRA);
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
    BulkDataSync();
}

bool ConfigCassandraClient::ReadUuidTableRow(const string &obj_type,
                                             const string &uuid_key) {
    GenDb::ColList col_list;
    GenDb::DbDataValueVec key;

    key.push_back(GenDb::Blob(
        reinterpret_cast<const uint8_t *>(uuid_key.c_str()), uuid_key.size()));

    if (dbif_->Db_GetRow(&col_list, kUuidTableName, key,
                         GenDb::DbConsistency::QUORUM)) {
        if (col_list.columns_.size()) {
            ParseRowAndEnqueueToParser(obj_type, uuid_key, col_list);
        }
    } else {
        IFMAP_WARN(IFMapGetRowError, "GetRow failed for table", kUuidTableName);
        return false;
    }

    return true;
}

bool ConfigCassandraClient::ReadAllUuidTableRows() {
    GenDb::ColListVec cl_vec_fq_name;

    ObjTypeUUIDList uuid_list;
    if (dbif_->Db_GetAllRows(&cl_vec_fq_name, kFqnTableName,
                             GenDb::DbConsistency::QUORUM)) {
        BOOST_FOREACH(const GenDb::ColList &cl_list, cl_vec_fq_name) {
            assert(cl_list.rowkey_.size() == 1);
            assert(cl_list.rowkey_[0].which() == GenDb::DB_VALUE_BLOB);

            if (cl_list.columns_.size()) {
                ParseFQNameRowGetUUIDList(cl_list, uuid_list);
            }
        }
    } else {
        IFMAP_WARN(IFMapGetRowError, "GetAllRows failed for table",
                   kUuidTableName);
        return false;
    }

    for (ObjTypeUUIDList::iterator it = uuid_list.begin();
         it != uuid_list.end(); it++) {
        AddUUIDToRequestList(it->first, it->second);
    }

    return true;
}

bool ConfigCassandraClient::BulkDataSync() {
    ReadAllUuidTableRows();
    return true;
}

string ConfigCassandraClient::UUIDToFQName(const string &uuid_str) const {
    map<string, string>::const_iterator it = fq_name_cache_.find(uuid_str);
    if (it == fq_name_cache_.end()) {
        return "ERROR";
    } else {
        return it->second;
    }
}

string ConfigCassandraClient::GetWrapperFieldName(const string &type_name,
                                          const string &property_name) const {
    WrapperFieldMap::const_iterator it =
        wrapper_field_map_.find(type_name+':'+property_name);
    if (it == wrapper_field_map_.end()) {
        return "";
    } else {
        return it->second;
    }
}

void ConfigCassandraClient::EnqueueUUIDRequest(string uuid_str, string obj_type,
                                             string oper) {
    ObjectProcessReq *req = new ObjectProcessReq(uuid_str, obj_type, oper);
    Enqueue(req);
}

void ConfigCassandraClient::Enqueue(ObjectProcessReq *req) {
    obj_process_queue_->Enqueue(req);
}

bool ConfigCassandraClient::RequestHandler(ObjectProcessReq *req) {
    // TODO Now handles only create
    AddUUIDToRequestList(req->obj_type_, req->uuid_str_);
    delete req;
    return true;
}
