/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"
#include "config_json_parser.h"

#include "base/task.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_table.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, string hostname,
        const IFMapConfigOptions& config_options) 
        : evm_(evm), ifmap_server_(ifmap_server) {
    config_json_parser_.reset(new ConfigJsonParser(this));
    thread_count_ = TaskScheduler::GetInstance()->HardwareThreadCount();
    config_db_client_.reset(
            IFMapFactory::Create<ConfigCassandraClient>(this, evm,
                config_options, config_json_parser_.get(), thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname,
                                                   config_options));
    vnc_cfg_FilterInfo vnc_filter_info;
    bgp_schema_FilterInfo bgp_schema_filter_info;

    bgp_schema_Server_GenerateGraphFilter(&bgp_schema_filter_info);
    vnc_cfg_Server_GenerateGraphFilter(&vnc_filter_info);

    for (vnc_cfg_FilterInfo::iterator it = vnc_filter_info.begin();
         it != vnc_filter_info.end(); it++) {
        link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                        make_pair(it->metadata_, it->linkattr_)));
    }

    for (bgp_schema_FilterInfo::iterator it = bgp_schema_filter_info.begin();
         it != bgp_schema_filter_info.end(); it++) {
        link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                        make_pair(it->metadata_, it->linkattr_)));
    }

    bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
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

void ConfigClientManager::InsertRequestIntoQ(IFMapOrigin::Origin origin,
        const string &neigh_type, const string &neigh_name,
        const string &metaname, auto_ptr<AutogenProperty > pvalue,
        const IFMapTable::RequestKey &key, bool add_change,
        RequestList *req_list) const {

    IFMapServerTable::RequestData *data =
        new IFMapServerTable::RequestData(origin, neigh_type, neigh_name);
    data->metadata = metaname;
    data->content.reset(pvalue.release());

    DBRequest *db_request = new DBRequest();
    db_request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
                        DBRequest::DB_ENTRY_DELETE);
    db_request->key.reset(CloneKey(key));
    db_request->data.reset(data);

    req_list->push_back(db_request);
}

void ConfigClientManager::EnqueueListToTables(RequestList *req_list) const {
    while (!req_list->empty()) {
        auto_ptr<DBRequest> req(req_list->front());
        req_list->pop_front();

        IFMapTable::RequestKey *key =
            static_cast<IFMapTable::RequestKey *>(req->key.get());

        IFMapTable *table = IFMapTable::FindTable(ifmap_server_->database(), key->id_type);
        if (table != NULL) {
            table->Enqueue(req.get());
        } else {
            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table", key->id_type);
        }
    }
}

IFMapTable::RequestKey *ConfigClientManager::CloneKey(
        const IFMapTable::RequestKey &src) const {
    IFMapTable::RequestKey *retkey = new IFMapTable::RequestKey();
    retkey->id_type = src.id_type;
    retkey->id_name = src.id_name;
    // TODO
    //retkey->id_seq_num = what?
    return retkey;
}

string ConfigClientManager::GetLinkName(const string &left,
                                     const string &right) const {
    LinkNameMap::const_iterator it =
        link_name_map_.find(make_pair(left, right));
    assert(it != link_name_map_.end());
    return it->second.first;
}

bool ConfigClientManager::IsLinkWithAttr(const string &left,
                                      const string &right) const {
    LinkNameMap::const_iterator it =
        link_name_map_.find(make_pair(left, right));
    assert(it != link_name_map_.end());
    return it->second.second;
}

string ConfigClientManager::GetWrapperFieldName(const string &type_name,
                                          const string &property_name) const {
    WrapperFieldMap::const_iterator it =
        wrapper_field_map_.find(type_name+':'+property_name);
    if (it == wrapper_field_map_.end()) {
        return "";
    } else {
        string temp_str = it->second;
        std::replace(temp_str.begin(), temp_str.end(), '-', '_');
        return temp_str;
    }
}
