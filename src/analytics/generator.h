/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _GENERATOR_H_
#define _GENERATOR_H_

#include <boost/shared_ptr.hpp>
#include <string>
#include <boost/tuple/tuple.hpp>
#include "viz_message.h"
#include "base/queue_task.h"

#include <sandesh/sandesh.h>
#include <sandesh/sandesh_uve_types.h>
#include <sandesh/sandesh_state_machine.h>
#include "collector_uve_types.h"
#include "syslog_collector.h"

class DbHandler;
class Sandesh;
class VizSession;
class Collector;
class SandeshStateMachineStats;

int GetDeferTimeMSec(uint64_t event_time_usec,
    uint64_t last_expiry_time_usec, uint64_t last_defer_time_usec);

class Generator {
public:
    virtual ~Generator() {}

    virtual const std::string ToString() const = 0;
    virtual bool ProcessRules(const VizMsg *vmsg, bool rsc) = 0;
    virtual DbHandler * GetDbHandler() const = 0; // db_handler_;
    
    bool ReceiveSandeshMsg(const VizMsg *vmsg, bool rsc);
    void SendSandeshMessageStatistics();
    void GetStatistics(std::vector<SandeshStats> *ssv) const;
    void GetStatistics(std::vector<SandeshLogLevelStats> *lsv) const;

private:
    void UpdateStatistics(const VizMsg *vmsg);

    VizMsgStatistics statistics_;
    mutable tbb::mutex smutex_;
};

class SandeshGenerator : public Generator {
public:
    const static int kInitialSmDeferTimeMSec = 300;
    const static int kMaxSmDeferTimeMSec = 30000;

    typedef boost::tuple<std::string /* Source */, std::string /* Module */,
        std::string /* Instance id */, std::string /* Node type */> GeneratorId;

    SandeshGenerator(Collector * const collector, VizSession *session,
            SandeshStateMachine *state_machine,
            const std::string &source, const std::string &module,
            const std::string &instance_id, const std::string &node_type,
            DbHandlerPtr global_db_handler);
    ~SandeshGenerator();

    void ReceiveSandeshCtrlMsg(uint32_t connects);
    void DisconnectSession(VizSession *vsession);
    void ConnectSession(VizSession *vsession, SandeshStateMachine *state_machine);

    bool GetSandeshStateMachineQueueCount(uint64_t &queue_count) const;
    bool GetSandeshStateMachineDropLevel(std::string &drop_level) const;
    bool GetSandeshStateMachineStats(SandeshStateMachineStats &sm_stats,
                                SandeshGeneratorBasicStats &sm_msg_stats) const;
    bool IsStateMachineDeferTimerRunning() const;
    int GetStateMachineDeferTimeMSec() const;

    const std::string &instance_id() const { return instance_id_; }
    const std::string &node_type() const { return node_type_; }
    VizSession * session() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return viz_session_;
    }
    const std::string &module() const { return module_; }
    const std::string &source() const { return source_; }
    virtual const std::string ToString() const { return name_; }
    SandeshStateMachine * get_state_machine(void) {
        return state_machine_;
    }
    const std::string State() const;

    void GetGeneratorInfo(ModuleServerState &genlist) const;
    void SetDbQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm);
    void ResetDbQueueWaterMarkInfo();
    void SetSmQueueWaterMarkInfo(Sandesh::QueueWaterMarkInfo &wm);
    void ResetSmQueueWaterMarkInfo();
    virtual DbHandler * GetDbHandler() const { return db_handler_.get(); }

private:
    virtual bool ProcessRules(const VizMsg *vmsg, bool rsc);
    void set_session(VizSession *session);

    void set_state_machine(SandeshStateMachine *state_machine) {
        state_machine_ = state_machine;
        // Update state machine
        state_machine_->SetGeneratorKey(name_);
    }
    void HandleSeqRedisReply(const std::map<std::string,int32_t> &typeMap);
    void HandleDelRedisReply(bool res);
    void TimerErrorHandler(std::string name, std::string error);

    void ProcessRulesCb(GenDb::DbOpResult::type dresult);
    bool StateMachineDeferTimerExpired();
    void CreateStateMachineDeferTimer();
    void DeleteStateMachineDeferTimer();
    void StartStateMachineDeferTimer(int time_msec);
    void StopStateMachineDeferTimer();
    bool IsStateMachineDeferTimerRunningUnlocked() const;
    int GetStateMachineDeferMSec() const;

    Collector * const collector_;
    SandeshStateMachine *state_machine_;
    VizSession *viz_session_;
    GeneratorInfoAttr gen_attr_;

    const std::string instance_id_;
    const std::string node_type_;
    const std::string source_;
    const std::string module_;
    const std::string name_;
    int instance_;

    tbb::atomic<bool> disconnected_;
    DbHandlerPtr db_handler_;
    GenDb::GenDbIf::DbAddColumnCb process_rules_cb_;
    Timer *sm_defer_timer_;
    uint64_t sm_defer_timer_expiry_time_usec_;
    int sm_defer_time_msec_;
    mutable tbb::mutex mutex_;
};

class SyslogGenerator : public Generator {
  public:
    SyslogGenerator(SyslogListeners *const listeners,
        const std::string &source, const std::string &module);
    const std::string &module() const { return module_; }
    const std::string &source() const { return source_; }
    virtual const std::string ToString() const { return name_; }
    virtual DbHandler * GetDbHandler() const { return db_handler_.get(); }

  private:
    virtual bool ProcessRules(const VizMsg *vmsg, bool rsc);

    SyslogListeners * const syslog_;
    const std::string source_;
    const std::string module_;
    const std::string name_;
    DbHandlerPtr db_handler_;
};

#endif
