/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "config_client_manager.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/request_pipeline.h>

#include "base/connection_info.h"
#include "base/task.h"
#include "base/task_trigger.h"
#include "config_amqp_client.h"
#include "config_db_client.h"
#include "config_cassandra_client.h"
//#include "config_json_parser.h"
#include "config_factory.h"
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

void ConfigClientManager::SetDefaultSchedulingPolicy(){
    static bool config_policy_set;
    if (config_policy_set)
        return;
    config_policy_set = true;
    
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    // Policy for cassandra::Reader Task.
    TaskPolicy cassadra_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::Init")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::FQNameReader")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_reader_policy.push_back(
        TaskExclusion(scheduler->GetTaskId("cassandra::ObjectProcessor"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::Reader"),
        cassadra_reader_policy);

    // Policy for cassandra::ObjectProcessor Task.
    TaskPolicy cassadra_obj_process_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::Init")));
    for (int idx = 0; idx < ConfigClientManager::GetNumConfigReader(); ++idx) {
        cassadra_obj_process_policy.push_back(
                 TaskExclusion(scheduler->GetTaskId("cassandra::Reader"), idx));
    }
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::ObjectProcessor"),
        cassadra_obj_process_policy);

    // Policy for cassandra::FQNameReader Task.
    TaskPolicy fq_name_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::Init")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::FQNameReader"),
        fq_name_reader_policy);

    // Policy for cassandra::Init process
    TaskPolicy cassandra_init_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("amqp::RabbitMQReader")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::ObjectProcessor")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::FQNameReader")))
        (TaskExclusion(scheduler->GetTaskId("cassandra::Reader")));
    scheduler->SetPolicy(scheduler->GetTaskId("cassandra::Init"),
        cassandra_init_policy);

    // Policy for amqp::RabbitMQReader process
    TaskPolicy rabbitmq_reader_policy = boost::assign::list_of
        (TaskExclusion(scheduler->GetTaskId("cassandra::Init")));
    scheduler->SetPolicy(scheduler->GetTaskId("amqp::RabbitMQReader"),
        rabbitmq_reader_policy);
}

void ConfigClientManager::SetUp() {
    //config_json_parser_.reset(new ConfigJsonParser(this));
    thread_count_ = GetNumConfigReader();
    end_of_rib_computed_at_ = UTCTimestampUsec();
    config_db_client_.reset(
            ConfigFactory::Create<ConfigCassandraClient>(this, evm_,
                config_options_, thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
    //vnc_cfg_FilterInfo vnc_filter_info;
    //bgp_schema_FilterInfo bgp_schema_filter_info;

    //bgp_schema_Server_GenerateGraphFilter(&bgp_schema_filter_info);
    //vnc_cfg_Server_GenerateGraphFilter(&vnc_filter_info);

    //for (vnc_cfg_FilterInfo::iterator it = vnc_filter_info.begin();
    //     it != vnc_filter_info.end(); it++) {
    //    if (it->is_ref_) {
    //        link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
    //                                make_pair(it->metadata_, it->linkattr_)));
    //    } else {
    //        parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
    //                                          it->metadata_));
    //    }
    //}

    //for (bgp_schema_FilterInfo::iterator it = bgp_schema_filter_info.begin();
    //     it != bgp_schema_filter_info.end(); it++) {
    //    if (it->is_ref_) {
    //        link_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
    //                                make_pair(it->metadata_, it->linkattr_)));
    //    } else {
    //        parent_name_map_.insert(make_pair(make_pair(it->left_, it->right_),
    //                                          it->metadata_));
    //    }
    //}

    //bgp_schema_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);
    //vnc_cfg_Server_GenerateWrapperPropertyInfo(&wrapper_field_map_);

    //bgp_schema_Server_GenerateObjectTypeList(&obj_type_to_read_);
    //vnc_cfg_Server_GenerateObjectTypeList(&obj_type_to_read_);
    SetDefaultSchedulingPolicy();
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
        std::string hostname,
        std::string module_name,
        const ConfigClientOptions& config_options,
        boost::function<void(const contrail_rapidjson::Document &)> cb)
        : evm_(evm),
        hostname_(hostname), module_name_(module_name),
        config_options_(config_options) {
    cfg_change_callback_ = cb;
    SetUp();
}

ConfigClientManager::~ConfigClientManager() {
}

void ConfigClientManager::Initialize() {
    // This function is called from daemon init(Control-node and contrail-dns)
    // The init is performed with init task trigger
    init_trigger_->Set();
}

//ConfigJsonParser *ConfigClientManager::config_json_parser() const {
//    return config_json_parser_.get();
//}

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

//void ConfigClientManager::InsertRequestIntoQ(IFMapOrigin::Origin origin,
//        const string &neigh_type, const string &neigh_name,
//        const string &metaname, auto_ptr<AutogenProperty > pvalue,
//        const IFMapTable::RequestKey &key, bool add_change,
//        RequestList *req_list) const {

//    IFMapServerTable::RequestData *data =
//        new IFMapServerTable::RequestData(origin, neigh_type, neigh_name);
//    data->metadata = metaname;
//    data->content.reset(pvalue.release());

//    DBRequest *db_request = new DBRequest();
//    db_request->oper = (add_change ? DBRequest::DB_ENTRY_ADD_CHANGE :
//                        DBRequest::DB_ENTRY_DELETE);
//    db_request->key.reset(CloneKey(key));
//    db_request->data.reset(data);
//
//    req_list->push_back(db_request);
//}

void ConfigClientManager::NotifyChange(const contrail_rapidjson::Document &jdoc) {
    if (cfg_change_callback_) {
        cfg_change_callback_(jdoc);
    }
}

//void ConfigClientManager::EnqueueListToTables(RequestList *req_list) const {
//    while (!req_list->empty()) {
//        auto_ptr<DBRequest> req(req_list->front());
//        req_list->pop_front();
//        IFMapTable::RequestKey *key =
//            static_cast<IFMapTable::RequestKey *>(req->key.get());

//        IFMapTable *table = IFMapTable::FindTable(ifmap_server_->database(),
//                                                  key->id_type);
//        if (table != NULL) {
//            table->Enqueue(req.get());
//        } else {
//            IFMAP_TRACE(IFMapTblNotFoundTrace, "Cant find table", key->id_type);
//        }
//    }
//}

//IFMapTable::RequestKey *ConfigClientManager::CloneKey(
//        const IFMapTable::RequestKey &src) const {
//    IFMapTable::RequestKey *retkey = new IFMapTable::RequestKey();
//    retkey->id_type = src.id_type;
//    retkey->id_name = src.id_name;
//    // Tag each DB Request with current generation number
//    retkey->id_seq_num = GetGenerationNumber();
//    return retkey;
//}

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

//void ConfigClientManager::GetPeerServerInfo(
//                                        IFMapPeerServerInfoUI *server_info) {
//    ConfigDBConnInfo status;

//    config_db_client()->GetConnectionInfo(status);
//    server_info->set_url(status.cluster);
//    server_info->set_connection_status(
//                       (status.connection_status ? "Up" : "Down"));
//    server_info->set_connection_status_change_at(
//                       status.connection_status_change_at);
//}

//void ConfigClientManager::GetClientManagerInfo(
//                                   ConfigClientManagerInfo &info) const {
//    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
//    info.end_of_rib_computed = end_of_rib_computed_;
//    info.end_of_rib_computed_at = end_of_rib_computed_at_;
//}

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
    config_db_client_.reset(ConfigFactory::Create<ConfigCassandraClient>
                            (this, evm_, config_options_,
                             thread_count_));
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

void ConfigClientManager::ReinitConfigClient(const ConfigClientOptions &config) {
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
