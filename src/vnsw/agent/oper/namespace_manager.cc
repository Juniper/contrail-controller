/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "oper/namespace_manager.h"

#include <boost/bind.hpp>
#include <boost/functional/hash.hpp>
#include <sys/wait.h>
#include "db/db.h"
#include "io/event_manager.h"
#include "oper/service_instance.h"
#include "cmn/agent_signal.h"

using boost::uuids::uuid;

NamespaceManager::NamespaceManager(EventManager *evm)
        : evm_(evm), si_table_(NULL),
          listener_id_(DBTableBase::kInvalidId), netns_timeout_() {
}

void NamespaceManager::Initialize(DB *database, AgentSignal *signal,
                                  const std::string &netns_cmd,
                                  const int netns_workers,
                                  const int netns_timeout) {
    si_table_ = database->FindTable("db.service-instance.0");
    assert(si_table_);

    if (signal) {
        InitSigHandler(signal);
    }

    listener_id_ = si_table_->Register(
        boost::bind(&NamespaceManager::EventObserver, this, _1, _2));

    netns_cmd_ = netns_cmd;
    if (netns_cmd_.length() == 0) {
        LOG(ERROR, "NetNS path for network namespace command not specified "
                   "in the config file, the namespaces won't be started");
    }

    netns_timeout_ = kTimeoutDefault;
    if (netns_timeout >= 1) {
        netns_timeout_ = netns_timeout;
    }

    int workers = kWorkersDefault;
    if (netns_workers > 0) {
       workers = netns_workers;
    }

    task_queues_.resize(workers);
    for (std::vector<NamespaceTaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        NamespaceTaskQueue *task_queue = new NamespaceTaskQueue(evm_);
        assert(task_queue);
        task_queue->set_on_timeout_cb(
                        boost::bind(&NamespaceManager::ScheduleNextTask,
                                    this, _1));
        *iter = task_queue;
    }
}

void NamespaceManager::UpdateStateStatusType(NamespaceTask* task, int status) {
    ServiceInstance* svc_instance = GetSvcInstance(task);
    if (svc_instance) {
        NamespaceState *state = GetState(svc_instance);
        if (state != NULL) {
            state->set_status(status);
            LOG(DEBUG, "NetNS update status for uuid: "
                << svc_instance->ToString()
                << " " << status);

            if (! WIFEXITED(status) || WIFSIGNALED(status) ||
                WEXITSTATUS(status) != 0) {
                if (state->status_type() != NamespaceState::Timeout) {
                    state->set_status_type(NamespaceState::Error);
                }
            } else if (state->status_type() == NamespaceState::Starting) {
                state->set_status_type(NamespaceState::Started);
            } else if (state->status_type() == NamespaceState::Stopping) {
                state->set_status_type(NamespaceState::Stopped);
            }
        }
    }
}

void NamespaceManager::HandleSigChild(const boost::system::error_code &error,
                                      int sig, pid_t pid, int status) {
    switch(sig) {
    case SIGCHLD:
        /*
         * check the head of each taskqueue in order to check whether there is
         * a task with the corresponding pid, if present dequeue it.
         */
        for (std::vector<NamespaceTaskQueue *>::iterator iter = task_queues_.begin();
                 iter != task_queues_.end(); ++iter) {
            NamespaceTaskQueue *task_queue = *iter;
            if (!task_queue->Empty()) {
                NamespaceTask *task = task_queue->Front();
                if (task->pid() == pid) {
                    UpdateStateStatusType(task, status);

                    task_queue->Pop();
                    delete task;

                    task_queue->StopTimer();

                    ScheduleNextTask(task_queue);
                    return;
                }
            }
        }
        break;
    }
}

NamespaceState *NamespaceManager::GetState(ServiceInstance *svc_instance) const {
    return static_cast<NamespaceState *>(
        svc_instance->GetState(si_table_, listener_id_));
}

NamespaceState *NamespaceManager::GetState(NamespaceTask* task) const {
    ServiceInstance* svc_instance = GetSvcInstance(task);
    if (svc_instance) {
        NamespaceState *state = GetState(svc_instance);
        return state;
    }
    return NULL;
}

void NamespaceManager::SetState(ServiceInstance *svc_instance,
                                NamespaceState *state) {
    svc_instance->SetState(si_table_, listener_id_, state);
}

void NamespaceManager::ClearState(ServiceInstance *svc_instance) {
    svc_instance->ClearState(si_table_, listener_id_);
}

void NamespaceManager::InitSigHandler(AgentSignal *signal) {
    signal->RegisterChildHandler(
        boost::bind(&NamespaceManager::HandleSigChild, this, _1, _2, _3, _4));
}

void NamespaceManager::Terminate() {
    si_table_->Unregister(listener_id_);

    NamespaceTaskQueue *task_queue;
    for (std::vector<NamespaceTaskQueue *>::iterator iter = task_queues_.begin();
         iter != task_queues_.end(); ++iter) {
        if ((task_queue = *iter) == NULL) {
            continue;
        }
        task_queue->Clear();

        delete task_queue;
    }
}

void  NamespaceManager::Enqueue(NamespaceTask *task,
                                const boost::uuids::uuid &uuid) {
    std::stringstream ss;
    ss << uuid;
    NamespaceTaskQueue *task_queue = GetTaskQueue(ss.str());
    task_queue->Push(task);
    ScheduleNextTask(task_queue);
}

NamespaceTaskQueue *NamespaceManager::GetTaskQueue(
                const std::string &str) {
    boost::hash<std::string> hash;
    int index = hash(str) % task_queues_.capacity();
    return task_queues_[index];
}

bool NamespaceManager::StartTask(NamespaceTaskQueue *task_queue,
                                 NamespaceTask *task) {
    pid_t pid = task->Run();
    NamespaceState *state = GetState(task);
    if (state != NULL) {
        state->set_pid(pid);
        state->set_cmd(task->cmd());
        if (task->cmd_type() == Start) {
            state->set_status_type(NamespaceState::Starting);
        } else {
            state->set_status_type(NamespaceState::Stopping);
        }
    }

    if (pid > 0) {
        task_queue->StartTimer(netns_timeout_ * 1000);
        return true;
    }

    task_queue->Pop();
    delete task;

    return false;
}

void NamespaceManager::ScheduleNextTask(NamespaceTaskQueue *task_queue) {
    while (!task_queue->Empty()) {
        NamespaceTask *task = task_queue->Front();
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
            NamespaceState *state = GetState(task);
            if (state) {
                state->set_status_type(NamespaceState::Timeout);
            }

            LOG(ERROR, "NetNS error timeout " << delay << " > " <<
                netns_timeout_ << ", " << task->cmd());

            if (delay > (netns_timeout_ * 2)) {
               task->Terminate();

               task_queue->StopTimer();
               task_queue->Pop();

               delete task;
            } else {
               task->Stop();
               return;
            }
        }
    }
}

ServiceInstance *NamespaceManager::GetSvcInstance(NamespaceTask *task) const {
    std::map<NamespaceTask *, ServiceInstance*>::const_iterator iter =
                    task_svc_instances_.find(task);
    if (iter != task_svc_instances_.end()) {
        return iter->second;
    }
    return NULL;
}

void NamespaceManager::RegisterSvcInstance(NamespaceTask *task,
                                           ServiceInstance *svc_instance) {
    task_svc_instances_.insert(std::make_pair(task, svc_instance));
}

void NamespaceManager::UnRegisterSvcInstance(ServiceInstance *svc_instance) {
    for (std::map<NamespaceTask *, ServiceInstance*>::iterator iter =
                    task_svc_instances_.begin();
         iter != task_svc_instances_.end(); ++iter) {
        if (svc_instance == iter->second) {
            task_svc_instances_.erase(iter);
        }
    }
}

void NamespaceManager::StartNetNS(ServiceInstance *svc_instance,
                                  NamespaceState *state, bool update) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " create";

    const ServiceInstance::Properties &props = svc_instance->properties();
    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);
    cmd_str << " --vmi-left-ip " << props.ip_addr_inside << "/";
    cmd_str << props.ip_prefix_len_inside;
    cmd_str << " --vmi-right-ip " << props.ip_addr_outside << "/";
    cmd_str << props.ip_prefix_len_outside;
    cmd_str << " --vmi-left-mac " << props.mac_addr_inside;
    cmd_str << " --vmi-right-mac " << props.mac_addr_outside;

    if (update) {
        cmd_str << " --update";
    }
    state->set_properties(props);

    NamespaceTask *task = new NamespaceTask(cmd_str.str(), Start, evm_);
    task->set_on_error_cb(boost::bind(&NamespaceManager::OnError,
                                      this, _1, _2));

    RegisterSvcInstance(task, svc_instance);
    boost::uuids::uuid uuid = svc_instance->properties().instance_id;
    Enqueue(task, uuid);

    LOG(DEBUG, "NetNS run command queued: " << task->cmd());
}

void NamespaceManager::OnError(NamespaceTask *task,
                               const std::string errors) {
    ServiceInstance *svc_instance = GetSvcInstance(task);
    if (!svc_instance) {
        return;
    }

    NamespaceState *state = GetState(svc_instance);
    if (state != NULL) {
        state->set_errors(errors);
    }
}

void NamespaceManager::StopNetNS(ServiceInstance *svc_instance,
                                 NamespaceState *state) {
    std::stringstream cmd_str;

    if (netns_cmd_.length() == 0) {
        return;
    }
    cmd_str << netns_cmd_ << " destroy";

    const ServiceInstance::Properties &props = state->properties();
    if (props.instance_id.is_nil() ||
        props.vmi_inside.is_nil() ||
        props.vmi_outside.is_nil()) {
        return;
    }

    cmd_str << " " << props.ServiceTypeString();
    cmd_str << " " << UuidToString(props.instance_id);
    cmd_str << " " << UuidToString(props.vmi_inside);
    cmd_str << " " << UuidToString(props.vmi_outside);

    NamespaceTask *task = new NamespaceTask(cmd_str.str(), Stop, evm_);
    boost::uuids::uuid uuid = svc_instance->properties().instance_id;
    Enqueue(task, uuid);

    RegisterSvcInstance(task, svc_instance);
    LOG(DEBUG, "NetNS run command queued: " << task->cmd());
}

void NamespaceManager::SetLastCmdType(ServiceInstance *svc_instance,
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

int NamespaceManager::GetLastCmdType(ServiceInstance *svc_instance) const {
    std::string uuid = UuidToString(svc_instance->uuid());
    std::map<std::string, int>::const_iterator iter =
        last_cmd_types_.find(uuid);
    if (iter != last_cmd_types_.end()) {
        return iter->second;
    }

    return 0;
}

void NamespaceManager::ClearLastCmdType(ServiceInstance *svc_instance) {
    std::string uuid = UuidToString(svc_instance->uuid());
        std::map<std::string, int>::iterator iter =
            last_cmd_types_.find(uuid);
    if (iter != last_cmd_types_.end()) {
        last_cmd_types_.erase(iter);
    }
}

void NamespaceManager::EventObserver(
    DBTablePartBase *db_part, DBEntryBase *entry) {
    ServiceInstance *svc_instance = static_cast<ServiceInstance *>(entry);

    NamespaceState *state = GetState(svc_instance);
    if (svc_instance->IsDeleted()) {
        UnRegisterSvcInstance(svc_instance);
        if (state) {
            if (GetLastCmdType(svc_instance) == Start) {
                StopNetNS(svc_instance, state);
            }

            ClearState(svc_instance);
            delete state;
        }
        ClearLastCmdType(svc_instance);
    } else {
        if (state == NULL) {
            state = new NamespaceState();
            SetState(svc_instance, state);
        }

        bool usable = svc_instance->IsUsable();
        LOG(DEBUG, "NetNS event notification for uuid: " << svc_instance->ToString()
            << (usable ? " usable" : " not usable"));
        if (!usable && GetLastCmdType(svc_instance) == Start) {
            StopNetNS(svc_instance, state);
            SetLastCmdType(svc_instance, Stop);
        } else if (usable) {
            if (GetLastCmdType(svc_instance) == Start && state->properties().CompareTo(
                            svc_instance->properties()) != 0) {
                StartNetNS(svc_instance, state, true);
            } else if (GetLastCmdType(svc_instance) != Start) {
                StartNetNS(svc_instance, state, false);
                SetLastCmdType(svc_instance, Start);
            }
        }
    }
}

/*
 * NamespaceState class
 */
NamespaceState::NamespaceState() : DBState(),
        pid_(0), status_(0), status_type_(0) {
}

void NamespaceState::Clear() {
    pid_ = 0;
    status_ = 0;
    errors_.empty();
    cmd_.empty();
}

/*
 * NamespaceTask class
 */
NamespaceTask::NamespaceTask(const std::string &cmd, int cmd_type, EventManager *evm) :
        cmd_(cmd), errors_(*(evm->io_service())), is_running_(false),
        pid_(0), cmd_type_(cmd_type), start_time_(0) {
}

void NamespaceTask::ReadErrors(const boost::system::error_code &ec,
                               size_t read_bytes) {
    if (read_bytes) {
        errors_data_ << rx_buff_;
    }

    if (ec) {
        boost::system::error_code close_ec;
        errors_.close(close_ec);

        std::string errors = errors_data_.str();
        if (errors.length() > 0) {
            LOG(ERROR, "NetNS run errors: " << std::endl << errors);

            if (!on_error_cb_.empty()) {
                on_error_cb_(this, errors);
            }
        }
        errors_data_.clear();

        return;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(
                    errors_,
                    boost::asio::buffer(rx_buff_, kBufLen),
                    boost::bind(&NamespaceTask::ReadErrors,
                                this, boost::asio::placeholders::error,
                                boost::asio::placeholders::bytes_transferred));
}

void NamespaceTask::Stop() {
    assert(pid_);
    kill(pid_, SIGTERM);
}

void NamespaceTask::Terminate() {
    assert(pid_);
    kill(pid_, SIGKILL);
}

pid_t NamespaceTask::Run() {
    std::vector<std::string> argv;
    LOG(DEBUG, "NetNS run command: " << cmd_);

    is_running_ = true;

    boost::split(argv, cmd_, boost::is_any_of(" "), boost::token_compress_on);
    std::vector<const char *> c_argv(argv.size() + 1);
    for (std::size_t i = 0; i != argv.size(); ++i) {
        c_argv[i] = argv[i].c_str();
    }

    int err[2];
    if (pipe(err) < 0) {
        return -1;
    }

    /*
     * temporarily block SIGCHLD signals
     */
    sigset_t mask;
    sigset_t orig_mask;
    sigemptyset (&mask);
    sigaddset (&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, &orig_mask) < 0) {
        LOG(ERROR, "NetNS error: sigprocmask, " << strerror(errno));
    }

    pid_ = vfork();
    if (pid_ == 0) {
        close(err[0]);
        dup2(err[1], STDERR_FILENO);
        close(err[1]);

        close(STDOUT_FILENO);
        close(STDIN_FILENO);

        execvp(c_argv[0], (char **) c_argv.data());
        perror("execvp");

        _exit(127);
    }

    if (sigprocmask(SIG_SETMASK, &orig_mask, NULL) < 0) {
        LOG(ERROR, "NetNS error: sigprocmask, " << strerror(errno));
    }
    close(err[1]);

    start_time_ = time(NULL);

    boost::system::error_code ec;
    errors_.assign(::dup(err[0]), ec);
    close(err[0]);
    if (ec) {
        is_running_ = false;
        return -1;
    }

    bzero(rx_buff_, sizeof(rx_buff_));
    boost::asio::async_read(errors_, boost::asio::buffer(rx_buff_, kBufLen),
            boost::bind(&NamespaceTask::ReadErrors,
                        this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));

    return pid_;
}

/*
 *
 */
NamespaceTaskQueue::NamespaceTaskQueue(EventManager *evm) : evm_(evm),
                timeout_timer_(TimerManager::CreateTimer(
                               *evm_->io_service(),
                               "Namespace Manager Task Timeout",
                               TaskScheduler::GetInstance()->GetTaskId(
                                               "db::DBTable"), 0)) {
}

NamespaceTaskQueue::~NamespaceTaskQueue() {
    TimerManager::DeleteTimer(timeout_timer_);
}

void NamespaceTaskQueue::StartTimer(int time) {
    timeout_timer_->Start(time,
                          boost::bind(&NamespaceTaskQueue::OnTimerTimeout,
                                      this),
                          boost::bind(&NamespaceTaskQueue::TimerErrorHandler,
                                      this, _1, _2));
}

void NamespaceTaskQueue::StopTimer() {
    timeout_timer_->Cancel();
}

bool NamespaceTaskQueue::OnTimerTimeout() {
    if (! on_timeout_cb_.empty()) {
        on_timeout_cb_(this);
    }

    return true;
}

void NamespaceTaskQueue::TimerErrorHandler(const std::string &name, std::string error) {
    LOG(ERROR, "NetNS timeout error: " << error);
}

void NamespaceTaskQueue::Clear() {
    timeout_timer_->Cancel();

    while(!task_queue_.empty()) {
        NamespaceTask *task = task_queue_.front();
        task_queue_.pop();
        delete task;
    }
}
