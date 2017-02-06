/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <utility>
#include <vector>
#include <map>
#include <boost/bind.hpp>
#include "base/timer.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/assign/list_of.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <sandesh/sandesh_uve_types.h>
#include <sandesh/sandesh_message_builder.h>

#include "OpServerProxy.h"
#include "db_handler.h"
#include "collector.h"
#include "generator.h"
#include "viz_collector.h"
#include "viz_sandesh.h"
#include "viz_types.h"
#include "vizd_table_desc.h"

extern SandeshTraceBufferPtr UVETraceBuf;

using std::string;
using std::pair;
using std::vector;
using std::map;

#define GENERATOR_LOG(_Level, _Msg)                                            \
    do {                                                                       \
        if (LoggingDisabled()) break;                                          \
        log4cplus::Logger _Xlogger = log4cplus::Logger::getRoot();             \
        if (_Xlogger.isEnabledFor(log4cplus::_Level##_LOG_LEVEL)) {            \
            log4cplus::tostringstream _Xbuf;                                   \
            _Xbuf << ToString() << ": " << __func__ << ": " << _Msg;           \
            _Xlogger.forcedLog(log4cplus::_Level##_LOG_LEVEL,                  \
                               _Xbuf.str());                                   \
        }                                                                      \
    } while (false)

void Generator::UpdateStatistics(const VizMsg *vmsg) {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Update(vmsg);
}

void Generator::GetStatistics(vector<SandeshStats> *ssv) const {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Get(ssv);
}

void Generator::GetStatistics(vector<SandeshLogLevelStats> *lsv) const {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Get(lsv);
}

void Generator::SendSandeshMessageStatistics() {
    vector<SandeshMessageInfo> smv;
    {
        tbb::mutex::scoped_lock lock(smutex_);
        statistics_.Get(&smv);
    }
    SandeshMessageStat * snh = SANDESH_MESSAGE_STAT_CREATE();
    snh->set_name(ToString());
    snh->set_msg_info(smv);
    SANDESH_MESSAGE_STAT_SEND_SANDESH(snh);
}
    
bool Generator::ReceiveSandeshMsg(const VizMsg *vmsg, bool rsc) {
    UpdateStatistics(vmsg);
    return ProcessRules(vmsg, rsc);
}

// SandeshGenerator
SandeshGenerator::SandeshGenerator(Collector * const collector, VizSession *session,
        SandeshStateMachine *state_machine, const string &source,
        const string &module, const string &instance_id,
        const string &node_type,
        DbHandlerPtr global_db_handler) :
        Generator(),
        collector_(collector),
        state_machine_(state_machine),
        viz_session_(session),
        instance_id_(instance_id),
        node_type_(node_type),
        source_(source),
        module_(module),
        name_(source + ":" + node_type_ + ":" + module + ":" + instance_id_),
        instance_(session->GetSessionInstance()),
        db_connect_timer_(NULL),
        process_rules_cb_(
            boost::bind(&SandeshGenerator::ProcessRulesCb, this, _1)),
        sm_defer_timer_(NULL),
        sm_defer_timer_expiry_time_usec_(0),
        sm_defer_time_msec_(0) {
        //Use collector db_handler
        db_handler_ = global_db_handler;
        disconnected_ = false;
        gen_attr_.set_connects(1);
        gen_attr_.set_connect_time(UTCTimestampUsec());
    	// Update state machine
        state_machine_->SetGeneratorKey(name_);
        CreateStateMachineDeferTimer();
}

SandeshGenerator::~SandeshGenerator() {
    DeleteStateMachineDeferTimer();
    GetDbHandler()->UnInit(instance_);
}

void SandeshGenerator::set_session(VizSession *session) {
    viz_session_ = session;
    instance_ = session->GetSessionInstance();
    session->set_generator(this);
}

void SandeshGenerator::StartDbifReinit() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (disconnected_) {
        return;
    }
    GetDbHandler()->UnInitUnlocked(instance_);
}

bool SandeshGenerator::DbConnectTimerExpired() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (disconnected_) {
        return false;
    }
    return false;
}

void SandeshGenerator::TimerErrorHandler(string name, string error) {
    GENERATOR_LOG(ERROR, name + " error: " + error);
}

void SandeshGenerator::ReceiveSandeshCtrlMsg(uint32_t connects) {
    // This is a control message during SandeshGenerator-Collector negotiation
    ModuleServerState ginfo;
    GetGeneratorInfo(ginfo);
    SandeshModuleServerTrace::Send(ginfo);
    // Setup state machine watermarks
    std::vector<Sandesh::QueueWaterMarkInfo> wm_info;
    collector_->GetSmQueueWaterMarkInfo(wm_info);
    for (size_t i = 0; i < wm_info.size(); i++) {
        state_machine_->SetQueueWaterMarkInfo(wm_info[i]);
    }
}

void SandeshGenerator::DisconnectSession(VizSession *vsession) {
    tbb::mutex::scoped_lock lock(mutex_);
    GENERATOR_LOG(INFO, "Session:" << vsession->ToString());
    if (vsession == viz_session_) {
        disconnected_ = true;
        // This SandeshGenerator's session is now gone.
        // Delete all its UVEs
        uint32_t tmp = gen_attr_.get_resets();
        gen_attr_.set_resets(tmp+1);
        gen_attr_.set_reset_time(UTCTimestampUsec());
        state_machine_->ResetQueueWaterMarkInfo();
        StopStateMachineDeferTimer();
        DeleteStateMachineDeferTimer();
        sm_defer_timer_expiry_time_usec_ = 0;
        sm_defer_time_msec_ = 0;
        viz_session_ = NULL;
        state_machine_ = NULL;
        vsession->set_generator(NULL);
        collector_->GetOSP()->DeleteUVEs(source_, module_, 
                                         node_type_, instance_id_);
        ModuleServerState ginfo;
        GetGeneratorInfo(ginfo);
        SandeshModuleServerTrace::Send(ginfo);
    } else {
        GENERATOR_LOG(ERROR, "Disconnect for session:" << vsession->ToString() <<
                ", generator session:" << viz_session_->ToString());
    }
}

bool SandeshGenerator::StateMachineDeferTimerExpired() {
    tbb::mutex::scoped_lock lock(mutex_);
    sm_defer_timer_expiry_time_usec_ = UTCTimestampUsec();
    if (state_machine_) {
        state_machine_->SetDeferDequeue(false);
    }
    return false;
}

void SandeshGenerator::CreateStateMachineDeferTimer() {
    // Run in the context of sandesh state machine task
    assert(sm_defer_timer_ == NULL);
    sm_defer_timer_ = TimerManager::CreateTimer(
        *collector_->event_manager()->io_service(),
        "SandeshGenerator SM Defer Timer: " + name_,
        state_machine_->connection()->GetTaskId(), instance_);
}

void SandeshGenerator::StartStateMachineDeferTimer(int time_msec) {
    sm_defer_timer_->Start(time_msec,
            boost::bind(
                &SandeshGenerator::StateMachineDeferTimerExpired, this),
            boost::bind(&SandeshGenerator::TimerErrorHandler, this, _1, _2));
}

void SandeshGenerator::StopStateMachineDeferTimer() {
    assert(sm_defer_timer_->Cancel());
}

void SandeshGenerator::DeleteStateMachineDeferTimer() {
    TimerManager::DeleteTimer(sm_defer_timer_);
    sm_defer_timer_ = NULL;
}

bool SandeshGenerator::IsStateMachineDeferTimerRunningUnlocked() const {
    if (sm_defer_timer_) {
        return sm_defer_timer_->running();
    }
    return false;
}

bool SandeshGenerator::IsStateMachineDeferTimerRunning() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return IsStateMachineDeferTimerRunningUnlocked();
}

int SandeshGenerator::GetStateMachineDeferTimeMSec() const {
    tbb::mutex::scoped_lock lock(mutex_);
    return sm_defer_time_msec_;
}

int GetDeferTimeMSec(uint64_t event_time_usec,
    uint64_t last_expiry_time_usec, uint64_t last_defer_time_usec) {
    // If this is the first time, then defer the state machine with
    // initial defer time
    if (last_defer_time_usec == 0 || last_expiry_time_usec == 0) {
        return SandeshGenerator::kInitialSmDeferTimeMSec;
    }
    assert(event_time_usec >= last_expiry_time_usec);
    uint64_t time_since_expiry_usec(event_time_usec - last_expiry_time_usec);
    // We will double the defer time if we get a back pressure
    // event within 2 * last defer time. If the back pressure
    // event is between 2 * last defer time and 4 * last defer
    // time, then the defer time will be same as the current
    // defer time. If the back pressure event is after  4 * last
    // defer time, then we will reset the defer time to the
    // initial defer time
    if (time_since_expiry_usec <= 2 * last_defer_time_usec) {
        uint64_t ndefer_time_msec((2 * last_defer_time_usec)/1000);
        return std::min(ndefer_time_msec,
            static_cast<uint64_t>(SandeshGenerator::kMaxSmDeferTimeMSec));
    } else if ((2 * last_defer_time_usec <= time_since_expiry_usec) &&
        (time_since_expiry_usec <= 4 * last_defer_time_usec)) {
        return last_defer_time_usec/1000;
    } else {
        return SandeshGenerator::kInitialSmDeferTimeMSec;
    }
}

void SandeshGenerator::ProcessRulesCb(GenDb::DbOpResult::type dresult) {
    tbb::mutex::scoped_lock lock(mutex_);
    if (dresult == GenDb::DbOpResult::BACK_PRESSURE) {
        if (state_machine_) {
            // If state mchine defer timer is running just return to
            // avoid increasing the defer time more than once every
            // timer expiry
            if (IsStateMachineDeferTimerRunningUnlocked()) {
                return;
            }
            state_machine_->SetDeferDequeue(true);
            uint64_t now_usec(UTCTimestampUsec());
            int defer_time_msec(GetDeferTimeMSec(now_usec,
                sm_defer_timer_expiry_time_usec_, sm_defer_time_msec_ * 1000));
            sm_defer_time_msec_ = defer_time_msec;
            StartStateMachineDeferTimer(sm_defer_time_msec_);
        }
    }
}

bool SandeshGenerator::ProcessRules(const VizMsg *vmsg, bool rsc) {
    return collector_->ProcessSandeshMsgCb()(vmsg, rsc, GetDbHandler(),
        process_rules_cb_);
}

bool SandeshGenerator::GetSandeshStateMachineQueueCount(
    uint64_t &queue_count) const {
    if (!state_machine_) {
        // Return 0 so that last stale value is not displayed
        queue_count = 0;
        return true;
    }
    return state_machine_->GetQueueCount(queue_count);
}

bool SandeshGenerator::GetSandeshStateMachineDropLevel(
    std::string &drop_level) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetMessageDropLevel(drop_level);
}

bool SandeshGenerator::GetSandeshStateMachineStats(
                    SandeshStateMachineStats &sm_stats,
                    SandeshGeneratorBasicStats &sm_msg_stats) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetStatistics(sm_stats, sm_msg_stats);
}

void SandeshGenerator::GetGeneratorInfo(ModuleServerState &genlist) const {
    vector<GeneratorInfo> giv;
    GeneratorInfo gi;
    gi.set_hostname(Sandesh::source());
    gi.set_gen_attr(gen_attr_);
    giv.push_back(gi);
    genlist.set_generator_info(giv);
    genlist.set_name(source() + ":" + node_type_ + ":" + module() + ":" +
        instance_id_);
}

const std::string SandeshGenerator::State() const {
    if (state_machine_) {
        return state_machine_->StateName();
    }
    return "Disconnected";
}

void SandeshGenerator::ConnectSession(VizSession *session,
    SandeshStateMachine *state_machine) {
    tbb::mutex::scoped_lock lock(mutex_);
    set_session(session);
    set_state_machine(state_machine);
    disconnected_ = false;
    uint32_t tmp = gen_attr_.get_connects();
    gen_attr_.set_connects(tmp+1);
    gen_attr_.set_connect_time(UTCTimestampUsec());
    CreateStateMachineDeferTimer();
}

void SandeshGenerator::SetDbQueueWaterMarkInfo(
    Sandesh::QueueWaterMarkInfo &wm) {
    bool high(boost::get<2>(wm));
    bool defer_undefer(boost::get<3>(wm));
    boost::function<void (void)> cb;
    if (high && defer_undefer) {
        cb = boost::bind(&SandeshStateMachine::SetDeferDequeue,
                state_machine_, true);
    } else if (!high && defer_undefer) {
        cb = boost::bind(&SandeshStateMachine::SetDeferDequeue,
                state_machine_, false);
    }
    GetDbHandler()->SetDbQueueWaterMarkInfo(wm, cb);
}

void SandeshGenerator::ResetDbQueueWaterMarkInfo() {
    GetDbHandler()->ResetDbQueueWaterMarkInfo();
}

void SandeshGenerator::SetSmQueueWaterMarkInfo(
    Sandesh::QueueWaterMarkInfo &wm) {
    if (state_machine_) {
        state_machine_->SetQueueWaterMarkInfo(wm);
    }
}

void SandeshGenerator::ResetSmQueueWaterMarkInfo() {
    if (state_machine_) {
        state_machine_->ResetQueueWaterMarkInfo();
    }
}

// SyslogGenerator
SyslogGenerator::SyslogGenerator(SyslogListeners *const listeners,
        const string &source, const string &module) :
          Generator(),
          syslog_(listeners),
          source_(source),
          module_(module),
          name_(source + ":" + module),
          db_handler_(listeners->GetDbHandler()) {
}

bool SyslogGenerator::ProcessRules(const VizMsg *vmsg, bool rsc) {
    return syslog_->ProcessSandeshMsgCb()(vmsg, rsc, GetDbHandler(),
        GenDb::GenDbIf::DbAddColumnCb());
}
