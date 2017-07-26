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

void ConfigClientManager::SetDefaultSchedulingPolicy() {
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
    config_json_parser_.reset(ConfigFactory::Create<ConfigJsonParserBase>());
    config_json_parser_->Init(this);
    thread_count_ = GetNumConfigReader();
    end_of_rib_computed_at_ = UTCTimestampUsec();
    config_db_client_.reset(
            ConfigFactory::Create<ConfigCassandraClient>(this, evm_,
                config_options_, thread_count_));
    config_amqp_client_.reset(new ConfigAmqpClient(this, hostname_,
                                               module_name_, config_options_));
    SetDefaultSchedulingPolicy();
    init_trigger_.reset(new
         TaskTrigger(boost::bind(&ConfigClientManager::InitConfigClient, this),
         TaskScheduler::GetInstance()->GetTaskId("cassandra::Init"), 0));

    reinit_triggered_ = false;
}

ConfigClientManager::ConfigClientManager(EventManager *evm,
        std::string hostname,
        std::string module_name,
        const ConfigClientOptions& config_options)
        : evm_(evm),
        hostname_(hostname), module_name_(module_name),
        config_options_(config_options) {
    end_of_rib_computed_ = false;
    SetUp();
}

ConfigClientManager::~ConfigClientManager() {
}

void ConfigClientManager::Initialize() {
    init_trigger_->Set();
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

uint64_t ConfigClientManager::GetEndOfRibComputedAt() const {
    tbb::mutex::scoped_lock lock(end_of_rib_sync_mutex_);
    return end_of_rib_computed_at_;
}

void ConfigClientManager::EnqueueUUIDRequest(string oper, string obj_type,
                                             string uuid_str) {
    config_db_client_->EnqueueUUIDRequest(oper, obj_type, uuid_str);
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
        config_json_parser()->EndOfConfig();

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

void ConfigClientManager::ReinitConfigClient(
                        const ConfigClientOptions &config) {
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
