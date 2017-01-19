/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "config_cass2json_adapter.h"
#include "config_json_parser.h"
#include "io/event_manager.h"
#include "database/cassandra/cql/cql_if.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"

#include "sandesh/common/vns_constants.h"

#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/uuid/uuid.hpp>


using namespace std;

const string ConfigCassandraClient::kUuidTableName = "obj_uuid_table";
const string ConfigCassandraClient::kFqnTableName = "obj_fq_name_table";
const string ConfigCassandraClient::kCassClientTaskId = "cassandra::Reader";
const string ConfigCassandraClient::kObjectProcessTaskId =
                                               "cassandra::ObjectProcessor";

ConfigCassandraClient::ConfigCassandraClient(ConfigClientManager *mgr,
                         EventManager *evm, const IFMapConfigOptions &options,
                         ConfigJsonParser *in_parser, int num_workers)
        : ConfigDbClient(options), mgr_(mgr), evm_(evm), parser_(in_parser),
        num_workers_(num_workers), uuid_read_list_(num_workers),
        uuid_read_set_(num_workers), object_cache_map_(num_workers) {
    dbif_.reset(new cass::cql::CqlIf(evm, config_db_ips(),
                GetFirstConfigDbPort(), "", ""));

    int processor_task_id =
        TaskScheduler::GetInstance()->GetTaskId(kObjectProcessTaskId);

    for (int i = 0; i < num_workers_; i++) {
        config_readers_.push_back(boost::shared_ptr<TaskTrigger>(new
             TaskTrigger(boost::bind(&ConfigCassandraClient::ConfigReader,
                                     this, i),
             TaskScheduler::GetInstance()->GetTaskId("cassandra::Reader"), i)));
        WorkQueue<ObjectProcessReq *> *tmp_work_q =
            new WorkQueue<ObjectProcessReq *>(processor_task_id, i,
                      bind(&ConfigCassandraClient::RequestHandler, this, _1));
        obj_process_queue_.push_back(ObjProcessWorkQType(tmp_work_q));
    }
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

void ConfigCassandraClient::HandleObjectDelete(const string &obj_type,
                                               const string &uuid) {
    auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
    ConfigClientManager::RequestList req_list;
    key->id_type = obj_type;
    key->id_name = UUIDToFQName(uuid);
    FormDeleteRequestList(uuid, &req_list, key.get(), false);
    mgr()->EnqueueListToTables(&req_list);
    DeleteFQNameCache(uuid);
}

void ConfigCassandraClient::DeleteFQNameCache(const string &uuid) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    fq_name_cache_.erase(uuid);
}

void ConfigCassandraClient::AddFQNameCache(const string &uuid,
                                           const string &fq_name) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    fq_name_cache_.insert(make_pair(uuid, fq_name));
}

string ConfigCassandraClient::UUIDToFQName(const string &uuid_str) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    map<string, string>::const_iterator it = fq_name_cache_.find(uuid_str);
    if (it == fq_name_cache_.end()) {
        return "ERROR";
    } else {
        return it->second;
    }
}

bool ConfigCassandraClient::ConfigReader(int worker_id) {
    CHECK_CONCURRENCY("cassandra::Reader");

    int num_req_handled = 0;
    for (UUIDProcessList::iterator it = uuid_read_list_[worker_id].begin(),
         itnext; it != uuid_read_list_[worker_id].end(); it = itnext) {
        itnext = it;
        ++itnext;
        ObjectProcessRequestType *obj_req = *it;

        if (obj_req->oper == "CREATE" || obj_req->oper == "UPDATE") {
            if (!ReadUuidTableRow(obj_req->obj_type, obj_req->uuid)) {
                return false;
            }
        } else {
            HandleObjectDelete(obj_req->obj_type, obj_req->uuid);
        }
        uuid_read_set_[worker_id].erase(obj_req->uuid);
        uuid_read_list_[worker_id].erase(it);
        delete obj_req;
        if (++num_req_handled == kMaxRequestsToYield) {
            return false;
        }
    }

    return true;
}

void ConfigCassandraClient::AddUUIDToRequestList(const string &oper,
                                                 const string &obj_type,
                                                 const string &uuid_str) {
    int worker_id = HashUUID(uuid_str);
    pair<UUIDProcessSet::iterator, bool> ret;
    bool trigger = uuid_read_list_[worker_id].empty();
    ObjectProcessRequestType *req =
        new ObjectProcessRequestType(oper, obj_type, uuid_str);
    ret = uuid_read_set_[worker_id].insert(make_pair(uuid_str, req));
    if (ret.second) {
        uuid_read_list_[worker_id].push_back(req);
        if (trigger) {
            config_readers_[worker_id]->Set();
        }
    } else {
        delete req;
        ret.first->second->oper = oper;
    }
}

bool ConfigCassandraClient::StoreKeyIfUpdated(int idx, const string &uuid,
                                      const string &key, uint64_t timestamp) {
    ObjectCacheMap::iterator uuid_iter = object_cache_map_[idx].find(uuid);
    assert(uuid_iter != object_cache_map_[idx].end());
    size_t from_front_pos = key.find(':');
    string type_field = key.substr(0, from_front_pos+1);
    bool is_ref = (type_field == ConfigCass2JsonAdapter::ref_prefix);
    bool is_parent = (type_field == ConfigCass2JsonAdapter::parent_prefix);
    string field_name = key;
    if (is_ref || is_parent) {
        size_t from_back_pos = key.rfind(':');
        string ref_uuid = key.substr(from_back_pos+1);
        string ref_name = UUIDToFQName(ref_uuid);
        field_name = key.substr(0, from_back_pos+1) + ref_name;
    }

    FieldDetailMap::iterator field_iter = uuid_iter->second.find(field_name);
    if (field_iter == uuid_iter->second.end()) {
        // seeing field for first time
        uuid_iter->second.insert(make_pair(field_name,
                                           make_pair(timestamp, true)));
    } else {
        field_iter->second.second = true;
        if (key == "type" || key == "fq_name") {
            return true;
        }
        if (timestamp && field_iter->second.first == timestamp) {
            // No change
            return false;
        }
        field_iter->second.first = timestamp;
    }
    return true;
}

void ConfigCassandraClient::ParseUuidTableRowJson(const string &uuid,
        const string &key, const string &value, uint64_t timestamp,
        CassColumnKVVec *cass_data_vec) {
    int worker_id = HashUUID(uuid);
    // Check whether there was an update to property of ref
    if (StoreKeyIfUpdated(worker_id, uuid, key, timestamp)) {
        // Field is updated.. enqueue to parsing
        cass_data_vec->push_back(JsonAdapterDataType(key, value));
    }
}

void ConfigCassandraClient::MarkCacheDirty(const string &uuid) {
    int idx = HashUUID(uuid);
    ObjectCacheMap::iterator uuid_iter = object_cache_map_[idx].find(uuid);
    if (uuid_iter == object_cache_map_[idx].end()) {
        pair<ObjectCacheMap::iterator, bool> ret_uuid =
            object_cache_map_[idx].insert(make_pair(uuid, FieldDetailMap()));
        assert(ret_uuid.second);
        uuid_iter = ret_uuid.first;
    }

    for (FieldDetailMap::iterator it = uuid_iter->second.begin();
         it != uuid_iter->second.end(); it++) {
        it->second.second = false;
    }
}

bool ConfigCassandraClient::ParseUuidTableRowResponse(const string &uuid,
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec) {
    BOOST_FOREACH(const GenDb::NewCol &ncol, col_list.columns_) {
        assert(ncol.name->size() == 1);
        assert(ncol.value->size() == 1);
        assert(ncol.timestamp->size() == 1);

        const GenDb::DbDataValue &dname(ncol.name->at(0));
        assert(dname.which() == GenDb::DB_VALUE_BLOB);
        GenDb::Blob dname_blob(boost::get<GenDb::Blob>(dname));
        string key(reinterpret_cast<const char *>(dname_blob.data()),
                   dname_blob.size());

        const GenDb::DbDataValue &dvalue(ncol.value->at(0));
        assert(dvalue.which() == GenDb::DB_VALUE_STRING);
        string value(boost::get<string>(dvalue));

        const GenDb::DbDataValue &dtimestamp(ncol.timestamp->at(0));
        assert(dtimestamp.which() == GenDb::DB_VALUE_UINT64);
        uint64_t timestamp = boost::get<uint64_t>(dtimestamp);
        ParseUuidTableRowJson(uuid, key, value, timestamp, cass_data_vec);
    }

    return true;
}

bool ConfigCassandraClient::ParseFQNameRowGetUUIDList(
                  const GenDb::ColList &col_list, ObjTypeUUIDList &uuid_list) {
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
        AddFQNameCache(uuid_str, key.substr(0, temp));
    }

    return true;
}

bool ConfigCassandraClient::ParseRowAndEnqueueToParser(const string &obj_type,
                       const string &uuid_key, const GenDb::ColList &col_list) {
    auto_ptr<CassColumnKVVec> cass_data_vec(new CassColumnKVVec());

    MarkCacheDirty(uuid_key);

    if (ParseUuidTableRowResponse(uuid_key, col_list,
                                  cass_data_vec.get())) {
        // Convert column data to json string.
        auto_ptr<ConfigCass2JsonAdapter> ccja(new ConfigCass2JsonAdapter(this,
                                             obj_type, *(cass_data_vec.get())));

        // Enqueue Json document to the parser here.
        parser_->Receive(uuid_key, ccja->doc_string(), true,
                         IFMapOrigin::CASSANDRA);
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

    GenDb::Blob col_filter(reinterpret_cast<const uint8_t *>("d"), 1);
    GenDb::ColumnNameRange crange;
    crange.start_ = boost::assign::list_of(GenDb::DbDataValue(col_filter));

    GenDb::FieldNamesToReadVec field_vec;
    field_vec.push_back(boost::make_tuple("key", true, false, false));
    field_vec.push_back(boost::make_tuple("column1", false, true, false));
    field_vec.push_back(boost::make_tuple("value", false, false, true));

    if (dbif_->Db_GetRow(&col_list, kUuidTableName, key,
                         GenDb::DbConsistency::QUORUM, crange, field_vec)) {
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
                   kFqnTableName);
        return false;
    }

    for (ObjTypeUUIDList::iterator it = uuid_list.begin();
         it != uuid_list.end(); it++) {
        EnqueueUUIDRequest("CREATE", it->first, it->second);
    }

    return true;
}

bool ConfigCassandraClient::BulkDataSync() {
    ReadAllUuidTableRows();
    return true;
}

void ConfigCassandraClient::EnqueueUUIDRequest(string oper, string obj_type,
                                             string uuid_str) {
    int idx = HashUUID(uuid_str);
    ObjectProcessReq *req = new ObjectProcessReq(oper, obj_type, uuid_str);
    Enqueue(idx, req);
}

void ConfigCassandraClient::Enqueue(int worker_id, ObjectProcessReq *req) {
    obj_process_queue_[worker_id]->Enqueue(req);
}

bool ConfigCassandraClient::RequestHandler(ObjectProcessReq *req) {
    AddUUIDToRequestList(req->oper_, req->obj_type_, req->uuid_str_);
    delete req;
    return true;
}

void ConfigCassandraClient::FormDeleteRequestList(const string &uuid,
                              ConfigClientManager::RequestList *req_list,
                              IFMapTable::RequestKey *key, bool add_change) {
    int idx = HashUUID(uuid);
    ObjectCacheMap::iterator uuid_iter = object_cache_map_[idx].find(uuid);
    if (uuid_iter == object_cache_map_[idx].end()) {
        assert(!add_change);
        return;
    }

    for (FieldDetailMap::iterator it = uuid_iter->second.begin(), itnext;
         it != uuid_iter->second.end(); it = itnext) {
        itnext = it;
        ++itnext;
        if (!add_change || it->second.second == false) {
            //
            // Form delete request for either property or ref
            //
            size_t from_front_pos = it->first.find(':');
            string type_field = it->first.substr(0, from_front_pos+1);
            if (ConfigCass2JsonAdapter::allowed_properties.find(type_field) ==
                ConfigCass2JsonAdapter::allowed_properties.end()) {
                continue;
            }
            string metaname = "";
            string ref_name = "";
            string ref_type = "";
            if (type_field == ConfigCass2JsonAdapter::prop_prefix) {
                metaname  = it->first.substr(from_front_pos+1);
                std::replace(metaname.begin(), metaname.end(), '_', '-');
            } else {
                bool is_ref =
                    (type_field == ConfigCass2JsonAdapter::ref_prefix);
                bool is_parent =
                    (type_field == ConfigCass2JsonAdapter::parent_prefix);
                if (is_ref || is_parent) {
                    string temp_str = it->first.substr(from_front_pos+1);
                    size_t sec_sep_pos = temp_str.find(':');
                    ref_type = temp_str.substr(0, sec_sep_pos);
                    ref_name = temp_str.substr(sec_sep_pos+1);
                    if (is_ref) {
                        metaname = mgr()->GetLinkName(key->id_type, ref_type);
                    } else {
                        metaname = mgr()->GetLinkName(ref_type, key->id_type);
                    }
                } else {
                    size_t from_back_pos = it->first.rfind(':');
                    metaname  = it->first.substr(from_front_pos+1,
                                         (from_back_pos-from_front_pos-1));
                    std::replace(metaname.begin(), metaname.end(), '_', '-');
                }
            }

            auto_ptr<AutogenProperty > pvalue;
            mgr()->InsertRequestIntoQ(IFMapOrigin::CASSANDRA, ref_type,
                          ref_name, metaname, pvalue, *key, false, req_list);
            if (add_change) {
                uuid_iter->second.erase(it);
            }
        }
    }

    if (add_change != true) {
        object_cache_map_[idx].erase(uuid_iter);
    }
}
