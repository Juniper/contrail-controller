/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_NAMESPACE_MANAGER_H__
#define __AGENT_OPER_NAMESPACE_MANAGER_H__

#include <queue>
#include <boost/asio.hpp>
#include <boost/uuid/uuid.hpp>
#include "db/db_table.h"
#include "cmn/agent_signal.h"
#include "db/db_entry.h"
#include "oper/service_instance.h"
#include "base/queue_task.h"

class DB;
class EventManager;
class NamespaceState;
class NamespaceTask;
class NamespaceTaskQueue;

/*
 * Starts and stops network namespaces corresponding to service-instances.
 *
 * In order to prevent concurrency issues between the signal hanlder leveraged
 * by this class and the db::task context specific methods are protected by
 * a mutex.
 */
class NamespaceManager {
 public:
    enum CmdType {
        Start = 1,
        Stop
    };

    enum ChldEventType {
        SigChldEvent = 1,
        OnErrorEvent,
        OnTaskTimeoutEvent
    };

    struct NamespaceManagerChildEvent {
        int type;

        /*
         * SigChld variables
         */
        pid_t pid;
        int status;

        /*
         * OnError variables
         */
        boost::uuids::uuid task_uuid;
        std::string errors;

        /*
         * OnTimeout
         */
        NamespaceTaskQueue *task_queue;
    };

    static const int kTimeoutDefault = 30;
    static const int kWorkersDefault = 1;

    NamespaceManager(EventManager *evm);

    void Initialize(DB *database, AgentSignal *signal,
                    const std::string &netns_cmd, const int netns_workers,
                    const int netns_timeout);
    void Terminate();
    bool DequeueEvent(NamespaceManagerChildEvent event);

    NamespaceState *GetState(ServiceInstance *) const;

 private:
    friend class NamespaceManagerTest;

    void HandleSigChild(const boost::system::error_code& error, int sig,
                        pid_t pid, int status);
    void RegisterSigHandler();
    void InitSigHandler(AgentSignal *signal);
    void StartNetNS(ServiceInstance *svc_instance, NamespaceState *state,
                    bool update);
    void StopNetNS(ServiceInstance *svc_instance, NamespaceState *state);
    void OnError(const boost::uuids::uuid &uuid, const std::string errors);
    void RegisterSvcInstance(NamespaceTask *task,
                             ServiceInstance *svc_instance);
    void UnregisterSvcInstance(ServiceInstance *svc_instance);
    ServiceInstance *UnregisterSvcInstance(NamespaceTask *task);
    ServiceInstance *GetSvcInstance(const boost::uuids::uuid &uuid) const;

    NamespaceTaskQueue *GetTaskQueue(const std::string &str);
    void Enqueue(NamespaceTask *task, const boost::uuids::uuid &uuid);
    void ScheduleNextTask(NamespaceTaskQueue *task_queue);
    bool StartTask(NamespaceTaskQueue *task_queue, NamespaceTask *task);

    NamespaceState *GetState(NamespaceTask* task) const;
    void SetState(ServiceInstance *svc_instance, NamespaceState *state);
    void ClearState(ServiceInstance *svc_instance);
    void UpdateStateStatusType(NamespaceTask* task, int status);

    void SetLastCmdType(ServiceInstance *svc_instance, int last_cmd_type);
    int GetLastCmdType(ServiceInstance *svc_instance) const;
    void ClearLastCmdType(ServiceInstance *svc_instance);

    void OnTaskTimeout(NamespaceTaskQueue *task_queue);

    void SigChlgEventHandler(NamespaceManagerChildEvent event);
    void OnErrorEventHandler(NamespaceManagerChildEvent event);
    void OnTaskTimeoutEventHandler(NamespaceManagerChildEvent event);

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    EventManager *evm_;
    DBTableBase *si_table_;
    DBTableBase::ListenerId listener_id_;
    std::string netns_cmd_;
    int netns_timeout_;
    WorkQueue<NamespaceManagerChildEvent> work_queue_;

    std::vector<NamespaceTaskQueue *> task_queues_;
    std::map<boost::uuids::uuid, ServiceInstance *> task_svc_instances_;
    std::map<std::string, int> last_cmd_types_;
};

class NamespaceState : public DBState {

 public:
    enum StatusType {
        Starting = 1,
        Started,
        Stopping,
        Stopped,
        Error,
        Timeout
    };

    NamespaceState();

    void set_pid(const pid_t &pid) {
        pid_ = pid;
    }
    pid_t pid() const {
        return pid_;
    }

    void set_status(const int status) {
        status_ = status;
    }
    pid_t status() const {
        return status_;
    }

    void set_errors(const std::string &errors) {
        errors_ = errors;
    }
    std::string errors() const {
        return errors_;
    }

    void set_cmd(const std::string &cmd) {
        cmd_ = cmd;
    }
    std::string cmd() const {
        return cmd_;
    }

    void set_properties(const ServiceInstance::Properties &properties) {
        properties_ = properties;
    }
    const ServiceInstance::Properties &properties() const {
        return properties_;
    }

    void set_status_type(const int status) {
        status_type_ = status;
    }
    int status_type() const {
        return status_type_;
    }

    void Clear();

 private:
    pid_t pid_;
    int status_;
    std::string errors_;
    std::string cmd_;
    int status_type_;

    ServiceInstance::Properties properties_;

    boost::system::error_code ec_;
};

class NamespaceTask {
 public:
    static const size_t kBufLen = 4098;
    typedef boost::function<void(boost::uuids::uuid uuid, const std::string errors)> OnErrorCallback;

    NamespaceTask(const std::string &cmd, int cmd_type, EventManager *evm);

    void ReadErrors(const boost::system::error_code &ec, size_t read_bytes);
    pid_t Run();
    void Stop();
    void Terminate();

    bool is_running() const {
        return is_running_;
    }
    pid_t pid() const {
        return pid_;
    }

    time_t start_time() const {
        return start_time_;
    }
    void set_on_error_cb(OnErrorCallback cb) {
        on_error_cb_ = cb;
    }

    const std::string &cmd() const {
        return cmd_;
    }

    int cmd_type() const { return cmd_type_; }

    const boost::uuids::uuid &uuid() const {
        return uuid_;
    }

 private:
    boost::uuids::uuid uuid_;
    const std::string cmd_;

    boost::asio::posix::stream_descriptor errors_;
    std::stringstream errors_data_;
    char rx_buff_[kBufLen];
    AgentSignal::SignalChildHandler sig_handler_;

    bool is_running_;
    pid_t pid_;
    int cmd_type_;

    OnErrorCallback on_error_cb_;

    time_t start_time_;
};

class NamespaceTaskQueue {
public:
    typedef boost::function<void(NamespaceTaskQueue *task_queue)> OnTimeoutCallback;
    NamespaceTaskQueue(EventManager *evm);
    ~NamespaceTaskQueue();

    bool OnTimerTimeout();
    void TimerErrorHandler(const std::string &name, std::string error);

    NamespaceTask *Front() { return task_queue_.front(); }
    void Pop() { task_queue_.pop(); }
    bool Empty() { return task_queue_.empty(); }
    void Push(NamespaceTask *task) { task_queue_.push(task); }
    int Size() { return task_queue_.size(); }
    void StartTimer(int time);
    void StopTimer();
    void Clear();

    void set_on_timeout_cb(OnTimeoutCallback cb) {
        on_timeout_cb_ = cb;
    }

private:
    EventManager *evm_;
    Timer *timeout_timer_;
    std::queue<NamespaceTask *> task_queue_;
    OnTimeoutCallback on_timeout_cb_;
};

#endif
