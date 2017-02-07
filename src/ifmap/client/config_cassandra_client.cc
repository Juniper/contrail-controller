/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include "config_cassandra_client.h"

#include <boost/foreach.hpp>
#include <boost/functional/hash.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/connection_info.h"
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
#include "ifmap/ifmap_server_show_types.h"

#include "sandesh/common/vns_constants.h"

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

    // Initialized the casssadra connection status;
    cassandra_connection_up_ = false;
    connection_status_change_at_ = UTCTimestampUsec();
    bulk_sync_status_ = 0;

    int processor_task_id =
        TaskScheduler::GetInstance()->GetTaskId(kObjectProcessTaskId);

    for (int i = 0; i < num_workers_; i++) {
        config_readers_.push_back(boost::shared_ptr<TaskTrigger>(new
             TaskTrigger(boost::bind(&ConfigCassandraClient::ConfigReader,
                                     this, i),
             TaskScheduler::GetInstance()->GetTaskId("cassandra::Reader"), i)));
        WorkQueue<ObjectProcessReq *> *tmp_work_q =
            new WorkQueue<ObjectProcessReq *>(processor_task_id, i,
                      bind(&ConfigCassandraClient::RequestHandler, this, i, _1),
                      WorkQueue<ObjectProcessReq *>::kMaxSize, 512);
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

void ConfigCassandraClient::HandleObjectDelete(const string &uuid) {
    auto_ptr<IFMapTable::RequestKey> key(new IFMapTable::RequestKey());
    ConfigClientManager::RequestList req_list;
    ObjTypeFQNPair obj_type_fq_name_pair = UUIDToFQName(uuid, true);
    if (obj_type_fq_name_pair.second == "ERROR") return;
    key->id_type = obj_type_fq_name_pair.first;
    key->id_name = obj_type_fq_name_pair.second;
    FormDeleteRequestList(uuid, &req_list, key.get(), false);
    mgr()->EnqueueListToTables(&req_list);
    PurgeFQNameCache(uuid);
}

void ConfigCassandraClient::PurgeFQNameCache(const string &uuid) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    fq_name_cache_.erase(uuid);
}

void ConfigCassandraClient::AddFQNameCache(const string &uuid,
                               const string &obj_type, const string &fq_name) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    FQNameCacheType cache_obj(obj_type, fq_name);
    fq_name_cache_.insert(make_pair(uuid, cache_obj));
    return;
}

void ConfigCassandraClient::InvalidateFQNameCache(const string &uuid) {
    tbb::spin_rw_mutex::scoped_lock write_lock(rw_mutex_, true);
    map<string, FQNameCacheType>::iterator it = fq_name_cache_.find(uuid);
    if (it != fq_name_cache_.end()) {
        it->second.deleted = true;
    }
    return;
}

ConfigCassandraClient::ObjTypeFQNPair ConfigCassandraClient::UUIDToFQName(
                                  const string &uuid, bool deleted_ok) const {
    tbb::spin_rw_mutex::scoped_lock read_lock(rw_mutex_, false);
    map<string, FQNameCacheType>::const_iterator it = fq_name_cache_.find(uuid);
    if (it != fq_name_cache_.end()) {
        if (!it->second.deleted || (it->second.deleted && deleted_ok)) {
            return make_pair(it->second.obj_type, it->second.obj_name);
        }
    }
    return make_pair("ERROR", "ERROR");
}

bool ConfigCassandraClient::BunchReadReq(const UUIDProcessList &req_list) {
    vector<string> uuid_list;
    BOOST_FOREACH(ObjectProcessRequestType *req, req_list) {
        uuid_list.push_back(req->uuid);
    }
    if (!ReadUuidTableRows(uuid_list)) {
        return false;
    }
    return true;
}

void ConfigCassandraClient::RemoveObjReqEntries(int worker_id,
                                                UUIDProcessList &req_list) {
    for (UUIDProcessList::iterator req_it = req_list.begin();
         req_it != req_list.end(); req_it++) {
        RemoveObjReqEntry(worker_id, *req_it);
    }
    req_list.clear();
}

void ConfigCassandraClient::RemoveObjReqEntry(int worker_id,
                                              ObjectProcessRequestType *req) {
    uuid_read_set_[worker_id].erase(GetUUID(req->uuid));
    uuid_read_list_[worker_id].pop_front();
    delete req;
}

bool ConfigCassandraClient::ConfigReader(int worker_id) {
    CHECK_CONCURRENCY("cassandra::Reader");

    int num_req_handled = 0;
    UUIDProcessList bunch_req_list;
    for (UUIDProcessList::iterator it = uuid_read_list_[worker_id].begin(),
         itnext; it != uuid_read_list_[worker_id].end(); it = itnext) {
        itnext = it;
        ++itnext;
        ObjectProcessRequestType *obj_req = *it;

        if (obj_req->oper == "CREATE" || obj_req->oper == "UPDATE") {
            bunch_req_list.push_back(obj_req);
            bool is_last = (itnext == uuid_read_list_[worker_id].end());
            if (is_last ||
                bunch_req_list.size() == GetNumReadRequestToBunch()) {
                if (!BunchReadReq(bunch_req_list)) {
                    return false;
                }
                num_req_handled += bunch_req_list.size();
                RemoveObjReqEntries(worker_id, bunch_req_list);
                if (num_req_handled >= kMaxRequestsToYield) {
                    return false;
                }
            }
            continue;
        } else if (obj_req->oper == "DELETE") {
            HandleObjectDelete(obj_req->uuid);
        } else if (obj_req->oper == "EndOfConfig") {
            BulkSyncDone(worker_id);
        }
        RemoveObjReqEntry(worker_id, obj_req);
        if (++num_req_handled == kMaxRequestsToYield) {
            return false;
        }
    }

    if (!bunch_req_list.empty()) {
        if (!BunchReadReq(bunch_req_list))
            return false;
        RemoveObjReqEntries(worker_id, bunch_req_list);
    }
    assert(uuid_read_list_[worker_id].empty());
    assert(uuid_read_set_[worker_id].empty());
    return true;
}

void ConfigCassandraClient::AddUUIDToRequestList(int worker_id,
                                                 const string &oper,
                                                 const string &obj_type,
                                                 const string &uuid_str) {
    pair<UUIDProcessSet::iterator, bool> ret;
    bool trigger = uuid_read_list_[worker_id].empty();
    ObjectProcessRequestType *req =
        new ObjectProcessRequestType(oper, obj_type, uuid_str);
    ret = uuid_read_set_[worker_id].insert(make_pair(GetUUID(uuid_str), req));
    if (ret.second) {
        uuid_read_list_[worker_id].push_back(req);
        if (trigger) {
            config_readers_[worker_id]->Set();
        }
    } else {
        delete req;
        ret.first->second->oper = oper;
        ret.first->second->uuid = uuid_str;
    }
}

bool ConfigCassandraClient::StoreKeyIfUpdated(int idx, const string &uuid,
                  const string &key, const string &value, uint64_t timestamp,
                  ConfigCassandraParseContext &context) {
    ObjectCacheMap::iterator uuid_iter = object_cache_map_[idx].find(uuid);
    assert(uuid_iter != object_cache_map_[idx].end());
    size_t from_front_pos = key.find(':');
    size_t from_back_pos = key.rfind(':');
    string type_field = key.substr(0, from_front_pos+1);
    bool is_ref = (type_field == ConfigCass2JsonAdapter::ref_prefix);
    bool is_parent = (type_field == ConfigCass2JsonAdapter::parent_prefix);
    bool is_propl = (type_field == ConfigCass2JsonAdapter::list_prop_prefix);
    bool is_propm = (type_field == ConfigCass2JsonAdapter::map_prop_prefix);
    bool is_prop = (type_field == ConfigCass2JsonAdapter::prop_prefix);
    if (is_prop) {
        string prop_name  = key.substr(from_front_pos+1);
        //
        // properties like perms2 has no importance to control-node/dns
        // This property is present on each config object. Hence skipping such
        // properties gives performance improvement
        //
        if (ConfigClientManager::skip_properties.find(prop_name) !=
            ConfigClientManager::skip_properties.end()) {
            return false;
        }
    }
    string field_name = key;
    string prop_name = "";
    if (is_ref || is_parent) {
        string ref_uuid = key.substr(from_back_pos+1);
        string ref_name = UUIDToFQName(ref_uuid).second;
        if (ref_name == "ERROR") {
            return false;
        }
        field_name = key.substr(0, from_back_pos+1) + ref_name;
    } else if (is_propl || is_propm) {
        prop_name = key.substr(0, from_back_pos);
        context.list_map_properties.insert(make_pair(prop_name,
                                            JsonAdapterDataType(key, value)));
    }

    if (key == "type") {
        if (context.obj_type.empty()) {
            context.obj_type = value;
            context.obj_type.erase(remove(context.obj_type.begin(),
                      context.obj_type.end(), '\"' ), context.obj_type.end());
        }
    } else if (key == "fq_name") {
        context.fq_name_present = true;
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
    if (is_propl || is_propm) {
        context.updated_list_map_properties.insert(prop_name);
        return false;
    } else {
        return true;
    }
}

void ConfigCassandraClient::ParseUuidTableRowJson(const string &uuid,
        const string &key, const string &value, uint64_t timestamp,
        CassColumnKVVec *cass_data_vec, ConfigCassandraParseContext &context) {
    int worker_id = HashUUID(uuid);
    // Check whether there was an update to property of ref
    if (StoreKeyIfUpdated(worker_id, uuid, key, value, timestamp, context)) {
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
        const GenDb::ColList &col_list, CassColumnKVVec *cass_data_vec,
        ConfigCassandraParseContext &context) {
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
        ParseUuidTableRowJson(uuid, key, value, timestamp, cass_data_vec,
                              context);
    }

    return true;
}

string ConfigCassandraClient::FetchUUIDFromFQNameEntry(const string &key) const {
    size_t temp = key.rfind(':');
    return (temp == string::npos) ? "" : key.substr(temp+1);
}

string ConfigCassandraClient::GetUUID(const string &key) const {
    return key;
}

void ConfigCassandraClient::UpdateCache(const std::string &key,
        const std::string &obj_type, ObjTypeUUIDList &uuid_list) {
    string uuid_str = FetchUUIDFromFQNameEntry(key);
    if (uuid_str.empty())
        return;
    uuid_list.push_back(make_pair(obj_type, uuid_str));
    AddFQNameCache(uuid_str, obj_type, key.substr(0, key.rfind(':')));
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
        UpdateCache(key, obj_type, uuid_list);
    }

    return true;
}

bool ConfigCassandraClient::ParseRowAndEnqueueToParser(const string &uuid_key,
                                           const GenDb::ColList &col_list) {
    CassColumnKVVec cass_data_vec;

    MarkCacheDirty(uuid_key);

    ConfigCassandraParseContext context;

    if (ParseUuidTableRowResponse(uuid_key, col_list,
                                  &cass_data_vec, context)) {
        //
        // If type or fq-name is not present in the db object, ignore the object
        // and trigger delete of the object
        //
        if (context.obj_type.empty() || !context.fq_name_present) {
            // Handle as delete
            HandleObjectDelete(uuid_key);
            return false;
        }
        // Read the context for map and list properties
        if (context.updated_list_map_properties.size()) {
            for (set<string>::iterator it =
                 context.updated_list_map_properties.begin();
                 it != context.updated_list_map_properties.end(); it++) {
                pair<multimap<string, JsonAdapterDataType>::iterator,
                    multimap<string, JsonAdapterDataType>::iterator> ret =
                    context.list_map_properties.equal_range(*it);
                for (multimap<string, JsonAdapterDataType>::iterator mit =
                     ret.first; mit != ret.second; mit++) {
                    cass_data_vec.push_back(mit->second);
                }
            }
        }

        // Convert column data to json string.
        ConfigCass2JsonAdapter ccja(uuid_key, this, context.obj_type, cass_data_vec);
        // Enqueue Json document to the parser here.
        parser_->Receive(ccja, IFMapOrigin::CASSANDRA);
    } else {
        IFMAP_WARN(IFMapGetRowError, "Parsing row response failed for table",
                   kUuidTableName, uuid_key);
        return false;
    }

    return true;
}

void ConfigCassandraClient::InitDatabase() {
    HandleCassandraConnectionStatus(false);
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
    HandleCassandraConnectionStatus(true);
    BulkDataSync();
}

bool ConfigCassandraClient::ReadUuidTableRows(const vector<string> &uuid_list) {
    GenDb::ColListVec col_list_vec;

    std::vector<GenDb::DbDataValueVec> keys;
    for (vector<string>::const_iterator it = uuid_list.begin();
         it != uuid_list.end(); it++) {
        GenDb::DbDataValueVec key;
        key.push_back(GenDb::Blob(reinterpret_cast<const uint8_t *>
                                  (it->c_str()), it->size()));
        keys.push_back(key);
    }

    GenDb::Blob col_filter(reinterpret_cast<const uint8_t *>("d"), 1);
    GenDb::ColumnNameRange crange;
    crange.start_ = boost::assign::list_of(GenDb::DbDataValue(col_filter));

    GenDb::FieldNamesToReadVec field_vec;
    field_vec.push_back(boost::make_tuple("key", true, false, false));
    field_vec.push_back(boost::make_tuple("column1", false, true, false));
    field_vec.push_back(boost::make_tuple("value", false, false, true));

    if (dbif_->Db_GetMultiRow(&col_list_vec, kUuidTableName, keys,
                         crange, field_vec)) {
        //
        // If the UUID doesn't exist in the table, read will return success with
        // empty columns as output
        // Failure is returned only due to connectivity issue or consistency
        // issues in reading from cassandra
        //
        HandleCassandraConnectionStatus(true);
        assert(uuid_list.size() == col_list_vec.size());

        BOOST_FOREACH(const GenDb::ColList &col_list, col_list_vec) {
            assert(col_list.rowkey_.size() == 1);
            assert(col_list.rowkey_[0].which() == GenDb::DB_VALUE_BLOB);
            if (col_list.columns_.size()) {
                GenDb::Blob uuid(boost::get<GenDb::Blob>(col_list.rowkey_[0]));
                string uuid_str(reinterpret_cast<const char *>(uuid.data()),
                                uuid.size());
                ParseRowAndEnqueueToParser(uuid_str, col_list);
            }
        }
    } else {
        HandleCassandraConnectionStatus(false);
        IFMAP_WARN(IFMapGetRowError, "Db_GetMultiRow failed for table",
                   kUuidTableName, "");
        //
        // Task is rescheduled to read the request queue
        // Due to a bug CQL driver from datastax, connection status is
        // not notified asynchronously. Because of this, polling is the only
        // choice to determine the cql connection status.
        // Since there are dedicated threads to read config,
        // and it is ok to retry by rescheduling the reader task
        // TODO: Sleep or No Sleep?
        //
        return false;
    }

    return true;
}

void ConfigCassandraClient::HandleCassandraConnectionStatus(bool success) {
    bool previous_status = cassandra_connection_up_.fetch_and_store(success);
    if (previous_status == success) {
        return;
    }

    connection_status_change_at_ = UTCTimestampUsec();
    if (success) {
        // Update connection info
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "Cassandra",
            process::ConnectionStatus::UP,
            dbif_->Db_GetEndpoints(), "Established Cassandra connection");
    } else {
        process::ConnectionState::GetInstance()->Update(
            process::ConnectionType::DATABASE, "Cassandra",
            process::ConnectionStatus::DOWN,
            dbif_->Db_GetEndpoints(), "Lost Cassandra connection");
    }
}

bool ConfigCassandraClient::ReadAllFqnTableRows() {
    GenDb::ColListVec cl_vec_fq_name;

    ObjTypeUUIDList uuid_list;
    while (true) {
        if (dbif_->Db_GetAllRows(&cl_vec_fq_name, kFqnTableName,
                                 GenDb::DbConsistency::QUORUM)) {
            HandleCassandraConnectionStatus(true);
            BOOST_FOREACH(const GenDb::ColList &cl_list, cl_vec_fq_name) {
                assert(cl_list.rowkey_.size() == 1);
                assert(cl_list.rowkey_[0].which() == GenDb::DB_VALUE_BLOB);

                if (cl_list.columns_.size()) {
                    ParseFQNameRowGetUUIDList(cl_list, uuid_list);
                }
            }
            break;
        } else {
            HandleCassandraConnectionStatus(false);
            IFMAP_WARN(IFMapGetRowError, "GetAllRows failed for table. Retry !",
                       kFqnTableName, "");
            sleep(kInitRetryTimeSec);
        }
    }
    return EnqueueUUIDRequest(uuid_list);
}

bool ConfigCassandraClient::EnqueueUUIDRequest(
        const ObjTypeUUIDList &uuid_list) {
    for (ObjTypeUUIDList::const_iterator it = uuid_list.begin();
         it != uuid_list.end(); it++) {
        EnqueueUUIDRequest("CREATE", it->first, it->second);
    }

    for (int i = 0; i < num_workers_; i++) {
        ObjectProcessReq *req = new ObjectProcessReq("EndOfConfig","", "");
        Enqueue(i, req);
    }

    return true;
}

bool ConfigCassandraClient::BulkDataSync() {
    bulk_sync_status_ = num_workers_;
    ReadAllFqnTableRows();
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

bool ConfigCassandraClient::RequestHandler(int worker_id,
                                           ObjectProcessReq *req) {
    AddUUIDToRequestList(worker_id, req->oper_, req->obj_type_, req->uuid_str_);
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

    set<string> list_map_property_erased;
    for (FieldDetailMap::iterator it = uuid_iter->second.begin(), itnext;
         it != uuid_iter->second.end(); it = itnext) {
        itnext = it;
        ++itnext;
        if (!add_change || !it->second.second) {
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
                bool is_propm =
                    (type_field == ConfigCass2JsonAdapter::map_prop_prefix);
                bool is_propl =
                    (type_field == ConfigCass2JsonAdapter::list_prop_prefix);
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
                    if (is_propm || is_propl) {
                        pair<set<string>::iterator, bool> ret =
                            list_map_property_erased.insert(
                                        it->first.substr(0, from_back_pos));
                        if (add_change) {
                            uuid_iter->second.erase(it);
                            continue;
                        } else if (!ret.second) {
                            continue;
                        }
                    }
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
    } else {
        if (list_map_property_erased.empty()) {
            return;
        }
        for (set<string>::iterator it = list_map_property_erased.begin();
             it != list_map_property_erased.end(); it++) {
            UpdatePropertyDeleteToReqList(key, uuid_iter, *it, req_list);
        }
    }
}

void ConfigCassandraClient::UpdatePropertyDeleteToReqList(
      IFMapTable::RequestKey *key, ObjectCacheMap::iterator uuid_iter,
      const string &lookup_key, ConfigClientManager::RequestList *req_list) {
    FieldDetailMap::iterator lower_bound_it =
        uuid_iter->second.lower_bound(lookup_key);
    if (lower_bound_it != uuid_iter->second.end() &&
        boost::starts_with(lower_bound_it->first, lookup_key)) {
        return;
    }
    size_t from_front_pos = lookup_key.find(':');
    string metaname = lookup_key.substr(from_front_pos+1);
    std::replace(metaname.begin(), metaname.end(), '_', '-');
    auto_ptr<AutogenProperty > pvalue;
    mgr()->InsertRequestIntoQ(IFMapOrigin::CASSANDRA, "", "", metaname,
                              pvalue, *key, false, req_list);
}

void ConfigCassandraClient::BulkSyncDone(int worker_id) {
    long num_config_readers_still_processing =
        bulk_sync_status_.fetch_and_decrement();
    if (num_config_readers_still_processing == 1) {
        mgr()->EndOfConfig();
    }
}

void ConfigCassandraClient::GetConnectionInfo(ConfigDBConnInfo &status) const {
    status.cluster = boost::algorithm::join(config_db_ips(), ", ");
    status.connection_status = cassandra_connection_up_;
    status.connection_status_change_at = connection_status_change_at_;
    return;
}

uint32_t ConfigCassandraClient::GetNumReadRequestToBunch() {
    static bool init_ = false;
    static uint32_t num_read_req_to_bunch = 0;

    if (!init_) {
        // XXX To be used for testing purposes only.
        char *count_str = getenv("CONFIG_NUM_DB_READ_REQ_TO_BUNCH");
        if (count_str) {
            num_read_req_to_bunch = strtol(count_str, NULL, 0);
        } else {
            num_read_req_to_bunch = kMaxNumUUIDToRead;
        }
        init_ = true;
    }
    return num_read_req_to_bunch;
}
