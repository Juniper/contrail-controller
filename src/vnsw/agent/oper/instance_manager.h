/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_INSTANCE_MANAGER_H__
#define __AGENT_OPER_INSTANCE_MANAGER_H__

#include <boost/uuid/uuid.hpp>
#include "cmn/agent_signal.h"
#include "db/db_table.h"
#include "oper/service_instance.h"
#include "oper/instance_manager_adapter.h"
#include "oper/loadbalancer_pool.h"

class Agent;
class DB;
class LoadbalancerConfig;
class InstanceState;
class InstanceTask;
class InstanceTaskQueue;

/*
 * Starts and stops network namespaces corresponding to service-instances.
 *
 * In order to prevent concurrency issues between the signal hanlder leveraged
 * by this class and the db::task context specific methods are protected by
 * a mutex.
 */
class InstanceManager {
 public:
    enum CmdType {
        Start = 1,
        Stop
    };

    enum ChldEventType {
        OnErrorEvent,
        OnTaskTimeoutEvent,
        OnExitEvent
    };

    struct LBPoolState :public DBState {
        LBPoolState (LoadbalancerPool::Type t) : type(t) { }
        LoadbalancerPool::Type type;
    };

    struct InstanceManagerChildEvent {
        int type;

        /*
         * OnError variables
         */
        InstanceTask *task;
        std::string errors;
        int error_val;

        /*
         * OnTimeout
         */
        InstanceTaskQueue *task_queue;
    };

    static const int kTimeoutDefault = 30;
    static const int kWorkersDefault = 1;

    InstanceManager(Agent *);
    ~InstanceManager();

    void Initialize(DB *database, const std::string &netns_cmd,
            const std::string &docker_cmd, const int netns_workers,
            const int netns_timeout);
    void Terminate();
    bool DequeueEvent(InstanceManagerChildEvent event);

    InstanceState *GetState(ServiceInstance *) const;
    bool StaleTimeout();
    const LoadbalancerConfig &lb_config() const { return *(lb_config_.get()); }
    void SetStaleTimerInterval(int minutes);
    int StaleTimerInterval() { return stale_timer_interval_;}
    void SetNamespaceStorePath(std::string path);
    void SetNetNSCmd(const std::string &netns_cmd);

 private:
    friend class InstanceManagerTest;
    class NamespaceStaleCleaner;

    void RegisterSigHandler();
    void InitSigHandler(AgentSignal *signal);

    InstanceManagerAdapter* FindApplicableAdapter(const ServiceInstance::Properties &props);
    void StartServiceInstance(ServiceInstance *svc_instance,
                              InstanceState *state, bool update);
    void StopServiceInstance(ServiceInstance *svc_instance,
                             InstanceState *state);

    void StopStaleNetNS(ServiceInstance::Properties &props);

    void OnError(InstanceTask *task, const std::string errors);
    void OnExit(InstanceTask *task, const boost::system::error_code &ec);
    void RegisterSvcInstance(InstanceTask *task,
                             ServiceInstance *svc_instance);
    void UnregisterSvcInstance(ServiceInstance *svc_instance);
    ServiceInstance *UnregisterSvcInstance(InstanceTask *task);
    ServiceInstance *GetSvcInstance(InstanceTask *task) const;

    InstanceTaskQueue *GetTaskQueue(const std::string &str);
    void Enqueue(InstanceTask *task, const boost::uuids::uuid &uuid);
    void ScheduleNextTask(InstanceTaskQueue *task_queue);
    bool StartTask(InstanceTaskQueue *task_queue, InstanceTask *task);

    InstanceState *GetState(InstanceTask* task) const;
    void SetState(ServiceInstance *svc_instance, InstanceState *state);
    void ClearState(ServiceInstance *svc_instance);
    bool DeleteState(ServiceInstance *svc_instance);
    void UpdateStateStatusType(InstanceManagerChildEvent event);

    void SetLastCmdType(ServiceInstance *svc_instance, int last_cmd_type);
    int GetLastCmdType(ServiceInstance *svc_instance) const;
    void ClearLastCmdType(ServiceInstance *svc_instance);

    void OnTaskTimeout(InstanceTaskQueue *task_queue);

    void OnErrorEventHandler(InstanceManagerChildEvent event);
    void OnExitEventHandler(InstanceManagerChildEvent event);
    void OnTaskTimeoutEventHandler(InstanceManagerChildEvent event);

    /*
     * Clear all the state entries. Used only at process shutdown.
     */
    void StateClear();

    /*
     * Event observer for changes in the "db.service-instance.0" table.
     */
    void EventObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    /*
     * Event observer for changes in the "db.loadbalancer.0" table.
     */
    void LoadbalancerObserver(DBTablePartBase *db_part, DBEntryBase *entry);
    void LoadbalancerPoolObserver(DBTablePartBase *db_part, DBEntryBase *entry);

    DBTableBase::ListenerId si_listener_;
    DBTableBase::ListenerId lb_listener_;
    DBTableBase::ListenerId lb_pool_listener_;
    std::string netns_cmd_;
    int netns_timeout_;
    WorkQueue<InstanceManagerChildEvent> work_queue_;

    std::vector<InstanceTaskQueue *> task_queues_;
    typedef std::map<InstanceTask *, ServiceInstance *> TaskSvcMap;
    TaskSvcMap task_svc_instances_;
    std::map<std::string, int> last_cmd_types_;
    std::string loadbalancer_config_path_;
    std::string namespace_store_path_;
    int stale_timer_interval_;
    std::auto_ptr<LoadbalancerConfig> lb_config_;
    Timer *stale_timer_;
    std::auto_ptr<NamespaceStaleCleaner> stale_cleaner_;
    Agent *agent_;

    std::vector<InstanceManagerAdapter *> adapters_;

    DISALLOW_COPY_AND_ASSIGN(InstanceManager);
};

class InstanceState : public DBState {

 public:
    enum StatusType {
        Starting = 1,
        Started,
        Stopping,
        Stopped,
        Error,
        Timeout
    };

    InstanceState();

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
        errors_.append(errors);
    }

    void reset_errors() {
        errors_.clear();
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

    int tasks_running() const {
        return tasks_running_;
    }

    int incr_tasks_running() {
        return ++tasks_running_;
    }

    int decr_tasks_running() {
        tasks_running_--;
        assert(!(tasks_running_ < 0));
        return tasks_running_;
    }


    void Clear();

 private:
    pid_t pid_;
    int status_;
    std::string errors_;
    std::string cmd_;
    int status_type_;
    int tasks_running_;

    ServiceInstance::Properties properties_;

    boost::system::error_code ec_;
};

#endif
