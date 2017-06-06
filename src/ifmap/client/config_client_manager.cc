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
#include "base/task_trigger.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"
#include "config_json_parser.h"
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
bool ConfigClientManager::end_of_rib_computed_;

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

void ConfigClientManager::SetUp() {
    config_json_parser_.reset(new ConfigJsonParser(this));
    thread_count_ = GetNumConfigReader();
    end_of_rib_computed_at_ = UTCTimestampUsec();
    config_db_client_.reset(
            IFMapFactory::Create<ConfigCassandraClient>(this, evm_,
                config_options_, config_json_parser_.get(), thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
    vnc_cfg_FilterInfo vnc_filter_info;
    bgp_schema_FilterInfo bgp_schema_filter_info;

    bgp_schema_Server_GenerateGraphFilter(&bgp_schema_filter_info);
    vnc_cfg_Server_GenerateGraphFilter(&vnc_filter_info);

    for (vnc_cfg_FilterInfo::iterator it = vnc_filter_info.begin();
         it != vnc_filter_info.end(); it++) {
        if (it->is_ref_) {
            link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                    make_pair(it->metadata_, it->linkattr_)));
        } else {
            parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                              it->metadata_));
        }
    }

    for (bgp_schema_FilterInfo::iterator it = bgp_schema_filter_info.begin();
         it != bgp_schema_filter_info.end(); it++) {
        if (it->is_ref_) {
            link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                    make_pair(it->metadata_, it->linkattr_)));
        } else {
            parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
                                              it->metadata_));
        }
    }

    bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);

    bgp_schema_Server_GenerateObjectTypeList(&obj_type_to_read_);
    vnc_cfg_Server_GenerateObjectTypeList(&obj_type_to_read_);

    // Init/Reinit task trigger runs in the context of "cassandra::Init" task
    // This task is mutually exclusive to amqp reader task, config reader tasks
    // (both FQName reader or Object UUID table reader) and object processing
    // work queue task
    // During the reinit, reinit_triggered_ flag is turned ON and this task
    // trigger is triggered. All the mutually exclusive tasks will terminate
    // their task execution depending on "reinit_triggered_" flag.
    // Since this task is mutually exclusive to reader tasks, the execution of
    // this task guarantee that reader tasks have terminated and "reinit" can
    // proceed with PostShutdown activities.
    // In PostShutdown, cassandra client disconnects from cassandra cluster and
    // clears the FQName cache and deletes the partitions
    // After this init task continues to connect to new AMQP server and
    // cassandra cluster.
    init_trigger_.reset(new
         TaskTrigger(boost::bind(&ConfigClientManager::InitConfigClient, this),
         TaskScheduler::GetInstance()->GetTaskId("cassandra::Init"), 0));

    reinit_triggered_ = false;
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options, bool end_of_rib_computed)
    : evm_(evm), ifmap_server_(ifmap_server), hostname_(hostname),
      module_name_(module_name), config_options_(config_options) {
    end_of_rib_computed_ = end_of_rib_computed;
    SetUp();
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        IFMapServer *ifmap_server, string hostname, string module_name,
        const IFMapConfigOptions& config_options)
    : evm_(evm), ifmap_server_(ifmap_server),
    hostname_(hostname), module_name_(module_name),
    config_options_(config_options) {
    end_of_rib_computed_ = false;
    SetUp();
}

ConfigClientManager::~ConfigClientManager() {
}

void ConfigClientManager::Initialize() {
    // This function is called from daemon init(Control-node and contrail-dns)
    // The init is performed with init task trigger
    init_trigger_->Set();
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
    // Tag each DB Request with current generation number
    retkey->id_seq_num = GetGenerationNumber();
    return retkey;
}

string ConfigClientManager::GetParentName(const string &left,
                                          const string &right) const {
    ParentNameMap::const_iterator it =
        parent_name_map_.find(make_pair(left, right));
    if (it == parent_name_map_.end())
        return "";
    return it->second;
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
        // Notify waiting caller with the result
        tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
        assert(!end_of_rib_computed_);
        end_of_rib_computed_ = true;
        cond_var_.notify_all();
        end_of_rib_computed_at_ = UTCTimestampUsec();
    }

    // Once we have finished reading the complete cassandra DB, we should verify
    // whether all DBEntries(node/link) are as per the new generation number.
    // The stale entry cleanup task ensure this.
    // There is no need to run stale clean up during first time startup
    if (GetGenerationNumber())
        ifmap_server_->CleanupStaleEntries();

    process::ConnectionState::GetInstance()->Update();
}

// This function waits forever for bulk sync of cassandra config to finish
// The condition variable is triggered even in case of "reinit". In such a case
// wait is terminated and function returns.
// AMQP reader task starts consuming messages only after bulk sync.
// During reinit, the tight loop is broken by triggering the condition variable
void ConfigClientManager::WaitForEndOfConfig() {
    tbb::interface5::unique_lock<tbb::mutex> lock(end_of_rib_sync_mutex_);
    // Wait for End of config
    while (!end_of_rib_computed_) {
        cond_var_.wait(lock);
        if (is_reinit_triggered()) return;
    }
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

void ConfigClientManager::PostShutdown() {
    config_db_client_->PostShutdown();
    reinit_triggered_ = false;
    end_of_rib_computed_ = false;

    // All set to read next version of the config. Increment the generation
    IncrementGenerationNumber();

    // scoped ptr reset deletes the previous config db object
    // Create new config db client and amqp client
    // Delete of config db client object guarantees the flusing of
    // object uuid cache and uuid read request list.
    config_db_client_.reset(IFMapFactory::Create<ConfigCassandraClient>
                            (this, evm_, config_options_,
                             config_json_parser_.get(), thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
}

bool ConfigClientManager::InitConfigClient() {
    if (is_reinit_triggered()) {
        // "cassandra::Init" task is mutually exclusive to
        // 1. FQName reader task
        // 2. Object UUID Table reader task
        // 3. AMQP reader task
        // 4. Object processing Work queue task
        // Due to this task policy, if the reinit task is running, it ensured
        // that above mutually exclusive tasks have finished/aborted
        // Perform PostShutdown to prepare for new connection
        PostShutdown();
    }

    // Common code path for both init/reinit
    config_amqp_client_->StartRabbitMQReader();
    config_db_client_->InitDatabase();
    if (is_reinit_triggered()) return false;
    return true;
}

void ConfigClientManager::ReinitConfigClient(const IFMapConfigOptions &config) {
    config_options_ = config;
    ReinitConfigClient();
}

void ConfigClientManager::ReinitConfigClient() {
    {
        // Wake up the amqp task waiting for EOR for config reading
        tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
        cond_var_.notify_all();
    }
    reinit_triggered_ = true;
    init_trigger_->Set();
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

static bool ConfigClientReinitHandleRequest(const Sandesh *sr,
                                         const RequestPipeline::PipeSpec ps,
                                         int stage, int instNum,
                                         RequestPipeline::InstData *data) {
    const ConfigClientReinitReq *request =
        static_cast<const ConfigClientReinitReq *>(ps.snhRequest_.get());
    ConfigClientReinitResp *response = new ConfigClientReinitResp();
    IFMapSandeshContext *sctx =
        static_cast<IFMapSandeshContext *>(request->module_context("IFMap"));

    ConfigClientManager *config_mgr =
        sctx->ifmap_server()->get_config_manager();

    config_mgr->ReinitConfigClient();

    response->set_success(true);
    response->set_context(request->context());
    response->set_more(false);
    response->Response();
    return true;
}

void ConfigClientReinitReq::HandleRequest() const {
    RequestPipeline::StageSpec s0;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s0.taskId_ = scheduler->GetTaskId("config::SandeshCmd");
    s0.cbFn_ = ConfigClientReinitHandleRequest;
    s0.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0);
    RequestPipeline rp(ps);
}
