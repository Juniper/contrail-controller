/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/connection_info.h"
#include "base/task.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"
#include "config_json_parser.h"
#include "ifmap/ifmap_config_options.h"
#include "ifmap/ifmap_factory.h"
#include "ifmap/ifmap_log.h"
#include "ifmap/ifmap_log_types.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server_show_types.h"
#include "ifmap/ifmap_server_table.h"
#include "io/event_manager.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace boost::assign;
using namespace std;

const set<string> ConfigClientManager::skip_properties = list_of("perms2");

int ConfigClientManager::GetNumConfigReader() {
    static bool init_ = false;
    static int num_config_readers = 0;

    if (!init_) {
        // XXX To be used for testing purposes only.
        char *count_str = getenv("CONFIG_NUM_WORKERS");
        if (count_str) {
            num_config_readers = strtol(count_str, NULL, 0);
        } else {
            num_config_readers = kNumConfigReaderTasks;
        }
        init_ = true;
    }
    return num_config_readers;
}

void ConfigClientManager::SetUp(string hostname, string module_name,
        const IFMapConfigOptions& config_options) {
    config_json_parser_.reset(new ConfigJsonParser(this));
    thread_count_ = GetNumConfigReader();
    end_of_rib_computed_at_ = UTCTimestampUsec();
    config_db_client_.reset(
            IFMapFactory::Create<ConfigCassandraClient>(this, evm_,
                config_options, config_json_parser_.get(), thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname, module_name,
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

    bgp_schema_Server_GenerateObjectTypeList(&obj_type_to_read_);
    vnc_cfg_Server_GenerateObjectTypeList(&obj_type_to_read_);
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options, bool end_of_rib_computed)
                : end_of_rib_computed_(end_of_rib_computed), evm_(evm),
                  ifmap_server_(ifmap_server) {
    SetUp(hostname, module_name, config_options);
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options)
        : end_of_rib_computed_(false), evm_(evm), ifmap_server_(ifmap_server) {
    SetUp(hostname, module_name, config_options);
}

ConfigClientManager::~ConfigClientManager() {
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
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    return end_of_rib_computed_;
}

void ConfigClientManager::EnqueueUUIDRequest(string oper, string obj_type,
                                             string uuid_str) {
    config_db_client_->EnqueueUUIDRequest(oper, obj_type, uuid_str);
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

        IFMapTable *table = IFMapTable::FindTable(ifmap_server_->database(),
                                                  key->id_type);
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
    if (it == link_name_map_.end())
        return "";
    return it->second.first;
}

bool ConfigClientManager::IsLinkWithAttr(const string &left,
                                         const string &right) const {
    LinkNameMap::const_iterator it =
        link_name_map_.find(make_pair(left, right));
    if (it == link_name_map_.end())
        return false;
    return it->second.second;
}

string ConfigClientManager::GetWrapperFieldName(const string &type_name,
                                          const string &property_name) const {
    WrapperFieldMap::const_iterator it =
        wrapper_field_map_.find(type_name+':'+property_name);
    if (it == wrapper_field_map_.end()) {
        return "";
    } else {
        // TODO: Fix the autogen to have _ instead of -
        string temp_str = it->second;
        std::replace(temp_str.begin(), temp_str.end(), '-', '_');
        return temp_str;
    }
}

void ConfigClientManager::EndOfConfig() {
    {
        // Notify waiting caller with the resultcjjjkkkk
        tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
        assert(!end_of_rib_computed_);
        end_of_rib_computed_ = true;
        cond_var_.notify_all();
        end_of_rib_computed_at_ = UTCTimestampUsec();
    }

    process::ConnectionState::GetInstance()->Update();
}

void ConfigClientManager::WaitForEndOfConfig() {
    tbb::interface5::unique_lock<tbb::mutex> lock(end_of_rib_sync_mutex_);
    // Wait for End of config
    if (!end_of_rib_computed_)
        cond_var_.wait(lock);
}

void ConfigClientManager::GetPeerServerInfo(
                                        IFMapPeerServerInfoUI *server_info) {
    ConfigDBConnInfo status;

    config_db_client()->GetConnectionInfo(status);
    server_info->set_url(status.cluster);
    server_info->set_connection_status(
                       (status.connection_status ? "Up" : "Down"));
    server_info->set_connection_status_change_at(
                       status.connection_status_change_at);
}

void ConfigClientManager::GetClientManagerInfo(
                                   ConfigClientManagerInfo &info) const {
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    info.end_of_rib_computed = end_of_rib_computed_;
    info.end_of_rib_computed_at = end_of_rib_computed_at_;
}

static bool ConfigClientInfoHandleRequest(const Sandesh *sr,
                                         const RequestPipeline::PipeSpec ps,
                                         int stage, int instNum,
                                         RequestPipeline::InstData *data) {
    const ConfigClientInfoReq *request =
        static_cast<const ConfigClientInfoReq *>(ps.snhRequest_.get());
    ConfigClientInfoResp *response = new ConfigClientInfoResp();
    IFMapSandeshContext *sctx = 
        static_cast<IFMapSandeshContext *>(request->module_context("IFMap"));

    ConfigClientManager *config_mgr =
        sctx->ifmap_server()->get_config_manager();

    ConfigAmqpConnInfo amqp_conn_info;
    config_mgr->config_amqp_client()->GetConnectionInfo(amqp_conn_info);

    ConfigDBConnInfo db_conn_info;
    config_mgr->config_db_client()->GetConnectionInfo(db_conn_info);

    ConfigClientManagerInfo client_mgr_info;
    config_mgr->GetClientManagerInfo(client_mgr_info);

    response->set_client_manager_info(client_mgr_info);
    response->set_db_conn_info(db_conn_info);
    response->set_amqp_conn_info(amqp_conn_info);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();
    return true;
}

void ConfigClientInfoReq::HandleRequest() const {
    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("config::SandeshCmd");
    s0.cbFn_ = ConfigClientInfoHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0);
    RequestPipeline rp(ps);
}
