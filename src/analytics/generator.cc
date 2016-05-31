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
        sm_back_pressure_timer_(NULL) {

        //Use collector db_handler
        db_handler_ = global_db_handler;
        disconnected_ = false;
        gen_attr_.set_connects(1);
        gen_attr_.set_connect_time(UTCTimestampUsec());
    	// Update state machine
        state_machine_->SetGeneratorKey(name_);
        CreateStateMachineBackPressureTimer();
}

SandeshGenerator::~SandeshGenerator() {
    DeleteStateMachineBackPressureTimer();
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
        StopStateMachineBackPressureTimer();
        DeleteStateMachineBackPressureTimer();
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

bool SandeshGenerator::StateMachineBackPressureTimerExpired() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (state_machine_) {
        state_machine_->SetDeferDequeue(false);
    }
    return false;
}

void SandeshGenerator::CreateStateMachineBackPressureTimer() {
    // Run in the context of sandesh state machine task
    assert(sm_back_pressure_timer_ == NULL);
    sm_back_pressure_timer_ = TimerManager::CreateTimer(
        *collector_->event_manager()->io_service(),
        "SandeshGenerator SM Backpressure Timer: " + name_,
        state_machine_->connection()->GetTaskId(), instance_);
}

void SandeshGenerator::StartStateMachineBackPressureTimer() {
    sm_back_pressure_timer_->Start(Collector::kSmBackPressureTimeMSec,
            boost::bind(
                &SandeshGenerator::StateMachineBackPressureTimerExpired, this),
            boost::bind(&SandeshGenerator::TimerErrorHandler, this, _1, _2));
}

void SandeshGenerator::StopStateMachineBackPressureTimer() {
    assert(sm_back_pressure_timer_->Cancel());
}

void SandeshGenerator::DeleteStateMachineBackPressureTimer() {
    TimerManager::DeleteTimer(sm_back_pressure_timer_);
    sm_back_pressure_timer_ = NULL;
}

bool SandeshGenerator::IsStateMachineBackPressureTimerRunning() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (sm_back_pressure_timer_) {
        return sm_back_pressure_timer_->running();
    }
    return false;
}

void SandeshGenerator::ProcessRulesCb(GenDb::DbOpResult::type dresult) {
    if (dresult == GenDb::DbOpResult::BACK_PRESSURE) {
        tbb::mutex::scoped_lock lock(mutex_);
        if (state_machine_) {
            state_machine_->SetDeferDequeue(true);
            StartStateMachineBackPressureTimer();
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

bool SandeshGenerator::GetDbStats(uint64_t *queue_count, uint64_t *enqueues,
    std::string *drop_level, std::vector<SandeshStats> *vdropmstats) const {
    db_handler_->GetSandeshStats(drop_level, vdropmstats);
    return db_handler_->GetStats(queue_count, enqueues);
}

void SandeshGenerator::SendDbStatistics() {
    // DB stats
    std::vector<GenDb::DbTableInfo> vdbti, vstats_dbti;
    GenDb::DbErrors dbe;
    db_handler_->GetStats(&vdbti, &dbe, &vstats_dbti);
    GeneratorDbStats * snh = GENERATOR_DB_STATS_CREATE();
    snh->set_name(name_);
    snh->set_table_info(vdbti);
    snh->set_errors(dbe);
    snh->set_statistics_table_info(vstats_dbti);
    GENERATOR_DB_STATS_SEND_SANDESH(snh);
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
    CreateStateMachineBackPressureTimer();
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
