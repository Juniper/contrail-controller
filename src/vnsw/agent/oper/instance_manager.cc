/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/instance_manager.h"

#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <boost/filesystem.hpp>
#include <boost/tokenizer.hpp>
#include <sys/wait.h>
#include "cmn/agent.h"
#include "cmn/agent_signal.h"
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/instance_task.h"
#include "oper/loadbalancer.h"
#include "oper/loadbalancer_pool.h"
#include "oper/loadbalancer_config.h"
#include "oper/loadbalancer_pool_info.h"
#include "oper/operdb_init.h"
#include "oper/service_instance.h"
#include "oper/vm.h"
#include "oper/docker_instance_adapter.h"
#include "oper/netns_instance_adapter.h"
#ifdef WITH_LIBVIRT
    #include "oper/libvirt_instance_adapter.h"
#endif
#include "base/util.h"

using boost::uuids::uuid;

static const char loadbalancer_config_path_default[] =
        "/var/lib/contrail/loadbalancer/";
static const char namespace_store_path_default[] =
        "/var/run/netns";
static const char namespace_prefix[] = "vrouter-";

class InstanceManager::NamespaceStaleCleaner {
public:
    NamespaceStaleCleaner(Agent *agent, InstanceManager *manager)
            : agent_(agent), manager_(manager) {
    }

    void CleanStaleEntries() {
        namespace fs = boost::filesystem;

        //Read all the Namepaces in the system
        fs::path ns(manager_->namespace_store_path_);
        if ( !fs::exists(ns) || !fs::is_directory(ns)) {
            return;
        }

        typedef boost::tokenizer<boost::char_separator<char> > tokenizer;
        boost::char_separator<char> slash_sep("/");
        boost::char_separator<char> colon_sep(":");
        fs::directory_iterator end_iter;
        for(fs::directory_iterator iter(ns); iter != end_iter; iter++) {

            // Get to the name of namespace by removing complete path
            tokenizer tokens(iter->path().string(), slash_sep);
            std::string ns_name;
            for(tokenizer::iterator it=tokens.begin(); it!=tokens.end(); it++){
                ns_name = *it;
            }

            //We are interested only in namespaces starting with a given
            //prefix
            std::size_t vrouter_found;
            vrouter_found = ns_name.find(namespace_prefix);
            if (vrouter_found == std::string::npos) {
                continue;
            }

            //Remove the standard prefix
            ns_name.replace(vrouter_found, strlen(namespace_prefix), "");

            //Namespace might have a ":". Extract both left and right of
            //":" Left of ":" is the VM uuid. If not found in Agent's VM
            //DB, it can be deleted
            tokenizer tok(ns_name, colon_sep);
            boost::uuids::uuid vm_uuid = StringToUuid(*tok.begin());
            VmKey key(vm_uuid);
            if (agent_->vm_table()->Find(&key, true)) {
                continue;
            }

            ServiceInstance::Properties prop;
            prop.instance_id = vm_uuid;
            prop.service_type = ServiceInstance::SourceNAT;
            tokenizer::iterator next_tok = ++(tok.begin());
            //Loadbalancer namespace
            if (next_tok != tok.end()) {
                prop.pool_id = StringToUuid(*next_tok);
                prop.service_type = ServiceInstance::LoadBalancer;
            }

            //Delete Namespace
            manager_->StopStaleNetNS(prop);

            //If Loadbalncer, delete the config files as well
            if (prop.service_type == ServiceInstance::LoadBalancer) {

                std::stringstream cfg_dir_path;
                cfg_dir_path <<
                    manager_->loadbalancer_config_path_ << prop.pool_id;

                boost::system::error_code error;
                if (fs::exists(cfg_dir_path.str())) {
                    fs::remove_all(cfg_dir_path.str(), error);
                    if (error) {
                        LOG(ERROR, "Stale loadbalancer cfg fle delete error"
                                    << error.message());
                    }
                }
            }
        }
    }

private:
    Agent *agent_;
    InstanceManager *manager_;
};

InstanceManager::~InstanceManager() {
    TimerManager::DeleteTimer(stale_timer_);
    STLDeleteValues(&adapters_);
}

InstanceManager::InstanceManager(Agent *agent)
        : si_listener_(DBTableBase::kInvalidId),
          lb_listener_(DBTableBase::kInvalidId),
          lb_pool_listener_(DBTableBase::kInvalidId),
          netns_timeout_(-1),
          work_queue_(TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0,
                      boost::bind(&InstanceManager::DequeueEvent, this, _1)),
          loadbalancer_config_path_(loadbalancer_config_path_default),
          namespace_store_path_(namespace_store_path_default),
          stale_timer_interval_(5 * 60 * 1000),
          lb_config_(new LoadbalancerConfig(agent)),
          stale_timer_(TimerManager::CreateTimer(*(agent->event_manager()->io_service()),
                      "NameSpaceStaleTimer", TaskScheduler::GetInstance()->
                      GetTaskId("db::DBTable"), 0)), agent_(agent) {

}


void InstanceManager::Initialize(DB *database, AgentSignal *signal,
                                 const std::string &netns_cmd,
                                 const std::string &docker_cmd,
                                 const int netns_workers,
                                 const int netns_timeout) {
    if (signal) {
        InitSigHandler(signal);
    }

    DBTableBase *lb_table = agent_->loadbalancer_table();
    assert(lb_table);
    lb_listener_ = lb_table->Register(
        boost::bind(&InstanceManager::LoadbalancerObserver, this, _1, _2));

    DBTableBase *lb_pool_table = agent_->loadbalancer_pool_table();
    assert(lb_pool_table);
    lb_pool_listener_ = lb_pool_table->Register(
        boost::bind(&InstanceManager::LoadbalancerPoolObserver, this, _1, _2));

    DBTableBase *si_table = agent_->service_instance_table();
    assert(si_table);
    si_listener_ = si_table->Register(
        boost::bind(&InstanceManager::EventObserver, this, _1, _2));

    netns_cmd_ = netns_cmd;
    if (netns_cmd_.length() == 0) {
        LOG(ERROR, "NetNS path for network namespace command not specified "
                   "in the config file, the namespaces won't be started");
    }
    if (docker_cmd.length() == 0) {
        LOG(ERROR, "Path for Docker starter command not specified "
                   "in the config file, the Docker instances won't be started");
    }
    adapters_.push_back(new DockerInstanceAdapter(docker_cmd, agent_));
    adapters_.push_back(new NetNSInstanceAdapter(netns_cmd,
                        loadbalancer_config_path_, agent_));
#ifdef WITH_LIBVIRT
    adapters_.push_back(new LibvirtInstanceAdapter(agent_,
                        "qemu:///system"));
#endif

    netns_timeout_ = kTimeoutDefault;
    if (netns_timeout >= 1) {
        netns_timeout_ = netns_timeout;
    }

    int workers = kWorkersDefault;
    if (netns_workers > 0) {
       workers = netns_workers;
    }


    task_queues_.resize(workers);
    for (std::vector<InstanceTaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        InstanceTaskQueue *task_queue =
                new InstanceTaskQueue(agent_->event_manager());
        assert(task_queue);
        task_queue->set_on_timeout_cb(
                        boost::bind(&InstanceManager::OnTaskTimeout,
                                    this, _1));
        *iter = task_queue;
    }
    stale_timer_->Start(StaleTimerInterval(),
                        boost::bind(&InstanceManager::StaleTimeout, this));

}

void InstanceManager::SetNetNSCmd(const std::string &netns_cmd) {
    ServiceInstance::Properties prop;
    prop.virtualization_type =
        ServiceInstance::ServiceInstance::NetworkNamespace;
    NetNSInstanceAdapter *adapter = static_cast<NetNSInstanceAdapter
        *>(FindApplicableAdapter(prop));
    if (adapter)
        adapter->set_cmd(netns_cmd);
}

void InstanceManager::SetStaleTimerInterval(int minutes) {
    stale_timer_interval_ = minutes * 60 * 1000;
}

void InstanceManager::OnTaskTimeout(InstanceTaskQueue *task_queue) {
    InstanceManagerChildEvent event;
    event.type = OnTaskTimeoutEvent;
    event.task_queue = task_queue;

    work_queue_.Enqueue(event);
}

void InstanceManager::OnTaskTimeoutEventHandler(InstanceManagerChildEvent event) {
    ScheduleNextTask(event.task_queue);
}

void InstanceManager::SigChldEventHandler(InstanceManagerChildEvent event) {
    /*
      * check the head of each taskqueue in order to check whether there is
      * a task with the corresponding pid, if present dequeue it.
      */
     for (std::vector<InstanceTaskQueue *>::iterator iter =
                  task_queues_.begin();
          iter != task_queues_.end(); ++iter) {
         InstanceTaskQueue *task_queue = *iter;
         if (!task_queue->Empty()) {
             InstanceTask *task = task_queue->Front();
             if (task->pid() == event.pid) {

                 //Get the sevice instance first, to delete the state later
                 ServiceInstance* svc_instance = GetSvcInstance(task);
                 UpdateStateStatusType(task, event.status);

                 task_queue->Pop();
                 delete task;

                 task_queue->StopTimer();

                 DeleteState(svc_instance);
                 ScheduleNextTask(task_queue);
                 return;
             }
         }
     }
}

void InstanceManager::OnErrorEventHandler(InstanceManagerChildEvent event) {
    ServiceInstance *svc_instance = GetSvcInstance(event.task);
    if (!svc_instance) {
       return;
    }

    InstanceState *state = GetState(svc_instance);
    if (state != NULL) {
       state->set_errors(event.errors);
    }
}

bool InstanceManager::DequeueEvent(InstanceManagerChildEvent event) {
    if (event.type == SigChldEvent) {
        SigChldEventHandler(event);
    } else if (event.type == OnErrorEvent) {
        OnErrorEventHandler(event);
    } else if (event.type == OnTaskTimeoutEvent) {
        OnTaskTimeoutEventHandler(event);
    }

    return true;
}

void InstanceManager::UpdateStateStatusType(InstanceTask* task, int status) {
    ServiceInstance* svc_instance = UnregisterSvcInstance(task);
    if (svc_instance) {
        InstanceState *state = GetState(svc_instance);
        if (state != NULL) {
            state->set_status(status);
            LOG(DEBUG, "NetNS update status for uuid: "
                << svc_instance->ToString()
                << " " << status);

            if (! WIFEXITED(status) || WIFSIGNALED(status) ||
                WEXITSTATUS(status) != 0) {
                if (state->status_type() != InstanceState::Timeout) {
                    state->set_status_type(InstanceState::Error);
                }
            } else if (state->status_type() == InstanceState::Starting) {
                state->set_status_type(InstanceState::Started);
            } else if (state->status_type() == InstanceState::Stopping) {
                state->set_status_type(InstanceState::Stopped);
            }
        }
    }
}

void InstanceManager::HandleSigChild(const boost::system::error_code &error,
                                     int sig, pid_t pid, int status) {
    switch(sig) {
    case SIGCHLD:
        InstanceManagerChildEvent event;
        event.type = SigChldEvent;
        event.pid = pid;
        event.status = status;

        work_queue_.Enqueue(event);
        break;
    }
}

InstanceState *InstanceManager::GetState(ServiceInstance *svc_instance) const {
    return static_cast<InstanceState *>(
        svc_instance->GetState(agent_->service_instance_table(),
                               si_listener_));
}

InstanceState *InstanceManager::GetState(InstanceTask* task) const {
    ServiceInstance* svc_instance = GetSvcInstance(task);
    if (svc_instance) {
        InstanceState *state = GetState(svc_instance);
        return state;
    }
    return NULL;
}

void InstanceManager::SetState(ServiceInstance *svc_instance,
                                InstanceState *state) {
    svc_instance->SetState(agent_->service_instance_table(),
                           si_listener_,state);
}

void InstanceManager::ClearState(ServiceInstance *svc_instance) {
    svc_instance->ClearState(agent_->service_instance_table(), si_listener_);
}

bool InstanceManager::DeleteState(ServiceInstance *svc_instance) {

    if (!svc_instance || !svc_instance->IsDeleted()) {
        return false;
    }

    InstanceState *state = GetState(svc_instance);
    if (state && !state->tasks_running()) {
        ClearState(svc_instance);
        delete state;
        ClearLastCmdType(svc_instance);
        return true;
    }

    return false;
}

void InstanceManager::InitSigHandler(AgentSignal *signal) {
    signal->RegisterChildHandler(
        boost::bind(&InstanceManager::HandleSigChild, this, _1, _2, _3, _4));
}

void InstanceManager::StateClear() {
    DBTablePartition *partition = static_cast<DBTablePartition *>(
        agent_->service_instance_table()->GetTablePartition(0));

    if (!partition)
        return;

    DBEntryBase *next = NULL;
    for (DBEntryBase *entry = partition->GetFirst(); entry; entry = next) {
        next = partition->GetNext(entry);
        DBState *state =
            entry->GetState(agent_->service_instance_table(), si_listener_);
        if (state != NULL) {
            entry->ClearState(agent_->service_instance_table(), si_listener_);
            delete state;
            ClearLastCmdType(static_cast<ServiceInstance *>(entry));
        }
    }
}

void InstanceManager::Terminate() {
    StateClear();
    agent_->loadbalancer_table()->Unregister(lb_listener_);
    agent_->loadbalancer_table()->Clear();
    agent_->loadbalancer_pool_table()->Unregister(lb_pool_listener_);
    agent_->loadbalancer_pool_table()->Clear();
    agent_->service_instance_table()->Unregister(si_listener_);
    agent_->service_instance_table()->Clear();

    InstanceTaskQueue *task_queue;
    for (std::vector<InstanceTaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        if ((task_queue = *iter) == NULL) {
            continue;
        }
        task_queue->Clear();

        delete task_queue;
    }
    work_queue_.Shutdown();
}

void InstanceManager::Enqueue(InstanceTask *task,
                              const boost::uuids::uuid &uuid) {
    std::stringstream ss;
    ss << uuid;
    InstanceTaskQueue *task_queue = GetTaskQueue(ss.str());
    task_queue->Push(task);
    ScheduleNextTask(task_queue);
}

InstanceTaskQueue *InstanceManager::GetTaskQueue(const std::string &str) {
    boost::hash<std::string> hash;
    int index = hash(str) % task_queues_.size();
    return task_queues_[index];
}

bool InstanceManager::StartTask(InstanceTaskQueue *task_queue,
                                InstanceTask *task) {
    pid_t pid;
    if (task->Run())
        pid = task->pid();
    else
        return false;

    InstanceState *state = GetState(task);
    if (state != NULL) {
        state->set_pid(pid);
        state->set_cmd(task->cmd());
        if (task->cmd_type() == Start) {
            state->set_status_type(InstanceState::Starting);
        } else {
            state->set_status_type(InstanceState::Stopping);
        }
    }

    if (pid > 0) {
        task_queue->StartTimer(netns_timeout_ * 1000);
        return true;
    }

    task_queue->Pop();
    UnregisterSvcInstance(task);
    delete task;

    return false;
}

void InstanceManager::ScheduleNextTask(InstanceTaskQueue *task_queue) {
    while (!task_queue->Empty()) {
        InstanceTask *task = task_queue->Front();
        if (!task->is_running()) {
            bool starting = StartTask(task_queue, task);
            if (starting) {
                return;
            }
        } else {
            int delay = time(NULL) - task->start_time();
            if (delay < netns_timeout_) {
               return;
            }
            InstanceState *state = GetState(task);
            if (state) {
                state->set_status_type(InstanceState::Timeout);
            }

            LOG(ERROR, "NetNS error timeout " << delay << " > " <<
                netns_timeout_ << ", " << task->cmd());

            if (delay > (netns_timeout_ * 2)) {
               task->Terminate();
               task_queue->StopTimer();
               task_queue->Pop();

               ServiceInstance* svc_instance = GetSvcInstance(task);
               if (state && svc_instance)
                   state->decr_tasks_running();

               task_svc_instances_.erase(task);

               DeleteState(svc_instance);

               delete task;
            } else {
               task->Stop();
               return;
            }
        }
    }
}

ServiceInstance *InstanceManager::GetSvcInstance(InstanceTask *task) const {
    TaskSvcMap::const_iterator iter =
                    task_svc_instances_.find(task);
    if (iter != task_svc_instances_.end()) {
        return iter->second;
    }
    return NULL;
}

void InstanceManager::RegisterSvcInstance(InstanceTask *task,
                                          ServiceInstance *svc_instance) {
    pair<TaskSvcMap::iterator, bool> result =
         task_svc_instances_.insert(std::make_pair(task, svc_instance));
    assert(result.second);

    InstanceState *state = GetState(svc_instance);
    assert(state);
    state->incr_tasks_running();
}

ServiceInstance *InstanceManager::UnregisterSvcInstance(InstanceTask *task) {
    for (TaskSvcMap::iterator iter =
                    task_svc_instances_.begin();
         iter != task_svc_instances_.end(); ++iter) {
        if (task == iter->first) {
            ServiceInstance *svc_instance = iter->second;
            InstanceState *state = GetState(svc_instance);
            assert(state);
            state->decr_tasks_running();
            task_svc_instances_.erase(iter);
            return svc_instance;
        }
    }

    return NULL;
}

void InstanceManager::UnregisterSvcInstance(ServiceInstance *svc_instance) {

    InstanceState *state = GetState(svc_instance);
    assert(state);

    TaskSvcMap::iterator iter =
        task_svc_instances_.begin();
    while(iter != task_svc_instances_.end()) {
        if (svc_instance == iter->second) {
            task_svc_instances_.erase(iter++);
            state->decr_tasks_running();
        } else {
            ++iter;
        }
    }
}

InstanceManagerAdapter* InstanceManager::FindApplicableAdapter(const ServiceInstance::Properties &props) {
    for (std::vector<InstanceManagerAdapter *>::iterator iter = adapters_.begin();
         iter != adapters_.end(); ++iter) {
         InstanceManagerAdapter *adapter = *iter;
        if (adapter != NULL && adapter->isApplicable(props)) {
            return adapter;
        }
    }
    return NULL;
}

void InstanceManager::StartServiceInstance(ServiceInstance *svc_instance,
                                 InstanceState *state, bool update) {
    const ServiceInstance::Properties &props = svc_instance->properties();
    InstanceManagerAdapter *adapter = this->FindApplicableAdapter(props);
    if (adapter != NULL) {
        InstanceTask *task = adapter->CreateStartTask(props, update);
        if (task != NULL) {
            task->set_on_error_cb(boost::bind(&InstanceManager::OnError,
                                              this, _1, _2));
            state->set_properties(props);
            RegisterSvcInstance(task, svc_instance);
            std::stringstream info;
            info << "Service run command queued: " << task->cmd();
            Enqueue(task, props.instance_id);
            LOG(DEBUG, info.str().c_str());
        } else {
            LOG(ERROR, "Error creating task!");
        }
    } else {
        LOG(ERROR, "Unknown virtualization type: " << props.virtualization_type);
    }
}


void InstanceManager::StopServiceInstance(ServiceInstance *svc_instance,
                                InstanceState *state) {
    const ServiceInstance::Properties &props = state->properties();
    InstanceManagerAdapter *adapter = this->FindApplicableAdapter(props);
    if (adapter != NULL) {
        InstanceTask *task = adapter->CreateStopTask(props);
        if (task != NULL) {
            RegisterSvcInstance(task, svc_instance);
            std::stringstream info;
            info << "Service stop command queued: " << task->cmd();
            Enqueue(task, props.instance_id);
            LOG(DEBUG, info.str().c_str());
        } else {
            LOG(ERROR, "Error creating task!");
        }
    } else {
        LOG(ERROR, "Unknown virtualization type: " << props.virtualization_type);
    }
}

void InstanceManager::OnError(InstanceTask *task,
                              const std::string errors) {

    InstanceManagerChildEvent event;
    event.type = OnErrorEvent;
    event.task = task;
    event.errors = errors;

    work_queue_.Enqueue(event);
}

void InstanceManager::StopStaleNetNS(ServiceInstance::Properties &props) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " destroy";


    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(boost::uuids::nil_uuid());
    cmd_str << " " << UuidToString(boost::uuids::nil_uuid());
    if (props.service_type == ServiceInstance::LoadBalancer) {
        cmd_str << " --cfg-file " << loadbalancer_config_path_default <<
            props.pool_id << "/conf.json";
        cmd_str << " --pool-id " << props.pool_id;
    }

    InstanceTask *task = new InstanceTaskExecvp(cmd_str.str(), Stop,
                                agent_->event_manager());
    std::stringstream info;
    info << "NetNS stale run command queued: " << task->cmd();
    Enqueue(task, props.instance_id);

    LOG(DEBUG, info.str().c_str());
}

void InstanceManager::SetLastCmdType(ServiceInstance *svc_instance,
                                     int last_cmd_type) {
    std::string uuid = UuidToString(svc_instance->uuid());
    std::map<std::string, int>::iterator iter =
            last_cmd_types_.find(uuid);
    if (iter != last_cmd_types_.end()) {
        iter->second = last_cmd_type;
    } else {
        last_cmd_types_.insert(std::make_pair(uuid, last_cmd_type));
    }
}

int InstanceManager::GetLastCmdType(ServiceInstance *svc_instance) const {
    std::string uuid = UuidToString(svc_instance->uuid());
    std::map<std::string, int>::const_iterator iter =
        last_cmd_types_.find(uuid);
    if (iter != last_cmd_types_.end()) {
        return iter->second;
    }

    return 0;
}

void InstanceManager::ClearLastCmdType(ServiceInstance *svc_instance) {
    std::string uuid = UuidToString(svc_instance->uuid());
        std::map<std::string, int>::iterator iter =
            last_cmd_types_.find(uuid);
    if (iter != last_cmd_types_.end()) {
        last_cmd_types_.erase(iter);
    }
}

void InstanceManager::EventObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    InstanceState *state = GetState(svc_instance);
    if (svc_instance->IsDeleted()) {
        if (state) {
            if (GetLastCmdType(svc_instance) == Start) {
                StopServiceInstance(svc_instance, state);
                SetLastCmdType(svc_instance, Stop);
            }
            if (DeleteState(svc_instance)) {
                return;
            }
        }
        ClearLastCmdType(svc_instance);
    } else {
        if (state == NULL) {
            state = new InstanceState();
            SetState(svc_instance, state);
        }

        bool usable = svc_instance->IsUsable();
        LOG(DEBUG, "NetNS event notification for uuid: " << svc_instance->ToString()
            << (usable ? " usable" : " not usable"));
        if (!usable && GetLastCmdType(svc_instance) == Start) {
            LOG(DEBUG, "Stopping service instance!");
            StopServiceInstance(svc_instance, state);
            SetLastCmdType(svc_instance, Stop);
        } else if (usable) {
            if (GetLastCmdType(svc_instance) == Start && state->properties().CompareTo(
                            svc_instance->properties()) != 0) {
                StartServiceInstance(svc_instance, state, true);
            } else if (GetLastCmdType(svc_instance) != Start) {
                StartServiceInstance(svc_instance, state, false);
                SetLastCmdType(svc_instance, Start);
            }
        }
    }
}

void InstanceManager::LoadbalancerObserver(DBTablePartBase *db_part,
                                           DBEntryBase *entry) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);
    std::stringstream pathgen;
    pathgen << loadbalancer_config_path_ << loadbalancer->uuid();

    boost::system::error_code error;
    if (!loadbalancer->IsDeleted()) {
        boost::filesystem::path dir(pathgen.str());
        if (!boost::filesystem::exists(dir, error)) {
            boost::filesystem::create_directories(dir, error);
            if (error) {
                LOG(ERROR, error.message());
                return;
            }
        }
        pathgen << "/conf.json";
        lb_config_->GenerateV2Config(pathgen.str(), loadbalancer);
    } else {
        boost::filesystem::remove_all(pathgen.str(), error);
        if (error) {
            LOG(ERROR, error.message());
            return;
        }
    }
}

void InstanceManager::LoadbalancerPoolObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    LoadbalancerPool *loadbalancer = static_cast<LoadbalancerPool *>(entry);
    LBPoolState *state = static_cast<LBPoolState *>
            (entry->GetState(db_part->parent(), lb_pool_listener_));
    std::stringstream pathgen;
    boost::system::error_code error;
    pathgen << loadbalancer_config_path_ << loadbalancer->uuid();
    if (state) {
        if (loadbalancer->IsDeleted() || (loadbalancer->properties() == NULL) ||
            ((state->type == LoadbalancerPool::LBAAS_V1) &&
             (loadbalancer->type() != LoadbalancerPool::LBAAS_V1))) {
             boost::filesystem::remove_all(pathgen.str(), error);
             if (error) {
                 LOG(ERROR, error.message());
             }
             entry->ClearState(db_part->parent(), lb_pool_listener_);
             delete state;
             return;
        }
    }
    /* Ignore ADD notifications for LoadbalancerPool if it is not of type
     * LBAAS_V1 as config generation for them is taken care in context of
     * Loadbalancer object
     */
    if (loadbalancer->type() != LoadbalancerPool::LBAAS_V1) {
        return;
    }

    if (!loadbalancer->IsDeleted() && loadbalancer->properties() != NULL) {
        boost::filesystem::path dir(pathgen.str());
        if (!boost::filesystem::exists(dir, error)) {
#if 0
            if (error) {
                LOG(ERROR, error.message());
                return;
            }
#endif
            boost::filesystem::create_directories(dir, error);
            if (error) {
                LOG(ERROR, error.message());
                return;
            }
        }
        pathgen << "/conf.json";
        lb_config_->GenerateConfig(pathgen.str(), loadbalancer->uuid(),
                                   *loadbalancer->properties());
        if (!state) {
            state = new LBPoolState(LoadbalancerPool::LBAAS_V1);
            entry->SetState(db_part->parent(), lb_pool_listener_, state);
        }
    }
}

bool InstanceManager::StaleTimeout() {

    if (stale_cleaner_.get())
        return false;
    stale_cleaner_.reset(new NamespaceStaleCleaner(agent_, this));
    stale_cleaner_->CleanStaleEntries();
    stale_cleaner_.reset(NULL);
    return false;
}

void InstanceManager::SetNamespaceStorePath(std::string path) {
    namespace_store_path_ = path;
}

/*
 * InstanceState class
 */
InstanceState::InstanceState() : DBState(),
        pid_(0), status_(0), status_type_(0), tasks_running_(0) {
}

void InstanceState::Clear() {
    pid_ = 0;
    status_ = 0;
    errors_.empty();
    cmd_.empty();
}
