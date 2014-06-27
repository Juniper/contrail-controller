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

void Generator::GetStatistics(vector<SandeshStats> &ssv) const {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Get(ssv);
}

void Generator::GetStatistics(vector<SandeshLogLevelStats> &lsv) const {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Get(lsv);
}

void Generator::GetStatistics(vector<SandeshMessageInfo> &smv) {
    tbb::mutex::scoped_lock lock(smutex_);
    statistics_.Get(smv);
}
    
bool Generator::ReceiveSandeshMsg(const VizMsg *vmsg, bool rsc) {
    GetDbHandler()->MessageTableInsert(vmsg);
    UpdateStatistics(vmsg);
    return ProcessRules(vmsg, rsc);
}

// SandeshGenerator
SandeshGenerator::SandeshGenerator(Collector * const collector, VizSession *session,
        SandeshStateMachine *state_machine, const string &source,
        const string &module, const string &instance_id,
        const string &node_type) :
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
        db_handler_(new DbHandler(
            collector->event_manager(), boost::bind(
                &SandeshGenerator::StartDbifReinit, this),
            collector->cassandra_ips(), collector->cassandra_ports(),
            collector->analytics_ttl(), source + ":" + node_type + ":" +
                module + ":" + instance_id)) {
    disconnected_ = false;
    gen_attr_.set_connects(1);
    gen_attr_.set_connect_time(UTCTimestampUsec());
    // Update state machine
    state_machine_->SetGeneratorKey(name_);
    Create_Db_Connect_Timer();
}

SandeshGenerator::~SandeshGenerator() {
    Delete_Db_Connect_Timer();
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
    GetDbHandler()->UnInit(instance_);
    Start_Db_Connect_Timer();
}

bool SandeshGenerator::DbConnectTimerExpired() {
    tbb::mutex::scoped_lock lock(mutex_);
    if (disconnected_) {
        return false;
    }
    if (!(Db_Connection_Init())) {
        return true;
    }
    return false;
}

void SandeshGenerator::Create_Db_Connect_Timer() {
    assert(db_connect_timer_ == NULL);
    db_connect_timer_ = TimerManager::CreateTimer(
        *collector_->event_manager()->io_service(),
        "SandeshGenerator db connect timer: " + name_,
        TaskScheduler::GetInstance()->GetTaskId(Collector::kDbTask),
        instance_);
}

void SandeshGenerator::Start_Db_Connect_Timer() {
    db_connect_timer_->Start(kDbConnectTimerSec * 1000,
            boost::bind(&SandeshGenerator::DbConnectTimerExpired, this),
            boost::bind(&SandeshGenerator::TimerErrorHandler, this, _1, _2));
}

void SandeshGenerator::Stop_Db_Connect_Timer() {
    db_connect_timer_->Cancel();
}

void SandeshGenerator::Delete_Db_Connect_Timer() {
    TimerManager::DeleteTimer(db_connect_timer_);
    db_connect_timer_ = NULL;
}

void SandeshGenerator::Db_Connection_Uninit() {
    GetDbHandler()->ResetDbQueueWaterMarkInfo();
    GetDbHandler()->UnInit(instance_);
    Delete_Db_Connect_Timer();
}

bool SandeshGenerator::Db_Connection_Init() {
    if (!GetDbHandler()->Init(false, instance_)) {
        GENERATOR_LOG(ERROR, ": Database setup FAILED");
        return false;
    }
    // Setup DB watermarks
    std::vector<Sandesh::QueueWaterMarkInfo> wm_info;
    collector_->GetDbQueueWaterMarkInfo(wm_info);
    for (size_t i = 0; i < wm_info.size(); i++) {
        GetDbHandler()->SetDbQueueWaterMarkInfo(wm_info[i]);
    }
    return true;
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
    // Initialize DB connection
    if (!Db_Connection_Init()) {
        Start_Db_Connect_Timer();
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
        viz_session_ = NULL;
        state_machine_ = NULL;
        vsession->set_generator(NULL);
        collector_->GetOSP()->DeleteUVEs(source_, module_, 
                                         node_type_, instance_id_);
        ModuleServerState ginfo;
        GetGeneratorInfo(ginfo);
        SandeshModuleServerTrace::Send(ginfo);
        Db_Connection_Uninit();
    } else {
        GENERATOR_LOG(ERROR, "Disconnect for session:" << vsession->ToString() <<
                ", generator session:" << viz_session_->ToString());
    }
}

bool SandeshGenerator::ProcessRules(const VizMsg *vmsg, bool rsc) {
    return collector_->ProcessSandeshMsgCb()(vmsg, rsc, GetDbHandler());
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
                    SandeshGeneratorStats &sm_msg_stats) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetStatistics(sm_stats, sm_msg_stats);
}

bool SandeshGenerator::GetDbStats(uint64_t &queue_count, uint64_t &enqueues,
    std::string &drop_level, std::vector<SandeshStats> &vdropmstats) const {
    return db_handler_->GetStats(queue_count, enqueues, drop_level,
               vdropmstats);
}

bool SandeshGenerator::GetDbStats(std::vector<GenDb::DbTableInfo> &vdbti,
    GenDb::DbErrors &dbe) {
    return db_handler_->GetStats(vdbti, dbe);
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
    Create_Db_Connect_Timer();
}

void SandeshGenerator::SetDbQueueWaterMarkInfo(
    Sandesh::QueueWaterMarkInfo &wm) {
    GetDbHandler()->SetDbQueueWaterMarkInfo(wm);
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
    return syslog_->ProcessSandeshMsgCb()(vmsg, rsc, GetDbHandler());
}
