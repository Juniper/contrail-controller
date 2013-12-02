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

#include "viz_types.h"
#include "generator.h"
#include "collector.h"
#include "viz_sandesh.h"
#include "OpServerProxy.h"
#include "viz_collector.h"
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

Generator::Generator(Collector * const collector, VizSession *session,
        SandeshStateMachine *state_machine, const string &source,
        const string &module) :
        collector_(collector),
        state_machine_(state_machine),
        viz_session_(session),
        source_(source),
        module_(module),
        name_(source_ + ":" + module_),
        db_handler_(new DbHandler(collector->event_manager(), boost::bind(&Generator::StartDbifReinit, this),
                collector->cassandra_ip(), collector->cassandra_port(), collector->analytics_ttl(), name_)),
        db_connect_timer_(
                TimerManager::CreateTimer(*collector->event_manager()->io_service(),
                    "Generator db connect timer" + source + module,
                    TaskScheduler::GetInstance()->GetTaskId(Collector::kDbTask),
                    session->GetSessionInstance())),
        del_wait_timer_(
                TimerManager::CreateTimer(*collector->event_manager()->io_service(),
                    "Delete wait timer" + source + module)) {
    disconnected_ = false;
    // Update state machine
    state_machine_->SetGeneratorKey(name_);
}

Generator::~Generator() {
    TimerManager::DeleteTimer(db_connect_timer_);
    db_connect_timer_ = NULL;
    TimerManager::DeleteTimer(del_wait_timer_);
    db_handler_->UnInit(true);
}

void Generator::StartDbifReinit() {
    db_handler_->UnInit(false);
    Start_Db_Connect_Timer();
}

bool Generator::DbConnectTimerExpired() {
    if (disconnected_) {
        return false;
    }
    if (!(Db_Connection_Init())) {
        return true;
    }
    return false;
}

void Generator::Start_Db_Connect_Timer() {
    db_connect_timer_->Start(kDbConnectTimerSec * 1000,
            boost::bind(&Generator::DbConnectTimerExpired, this),
            boost::bind(&Generator::TimerErrorHandler, this, _1, _2));
}

void Generator::Stop_Db_Connect_Timer() {
    db_connect_timer_->Cancel();
}

void Generator::Db_Connection_Uninit() {
    db_handler_->UnInit(false);
    Stop_Db_Connect_Timer();
}

bool Generator::Db_Connection_Init() {
    GenDb::GenDbIf *dbif;

    dbif = db_handler_->get_dbif();

    if (!dbif->Db_Init(Collector::kDbTask, 
                       viz_session_->GetSessionInstance())) {
        GENERATOR_LOG(ERROR, "Database initialization failed");
        return false;
    }

    if (!dbif->Db_SetTablespace(g_viz_constants.COLLECTOR_KEYSPACE)) {
        GENERATOR_LOG(ERROR,  ": Create/Set KEYSPACE: " <<
                g_viz_constants.COLLECTOR_KEYSPACE << " FAILED");
        return false;
    }   
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_tables.begin();
            it != vizd_tables.end(); it++) {
        if (!dbif->Db_UseColumnfamily(*it)) {
            GENERATOR_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            return false;
        }
    }
    /* setup ObjectTables */
    for (std::map<std::string, objtable_info>::const_iterator it =
            g_viz_constants._OBJECT_TABLES.begin();
            it != g_viz_constants._OBJECT_TABLES.end(); it++) {
        if (!dbif->Db_UseColumnfamily(
                    (GenDb::NewCf(it->first,
                                  boost::assign::list_of
                                  (GenDb::DbDataType::Unsigned32Type)
                                  (GenDb::DbDataType::AsciiType),
                                  boost::assign::list_of
                                  (GenDb::DbDataType::Unsigned32Type),
                                  boost::assign::list_of
                                  (GenDb::DbDataType::LexicalUUIDType))))) {
            GENERATOR_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            return false;
        }
    }
    for (std::vector<GenDb::NewCf>::const_iterator it = vizd_flow_tables.begin();
            it != vizd_flow_tables.end(); it++) {
        if (!dbif->Db_UseColumnfamily(*it)) {
            GENERATOR_LOG(ERROR, "Database initialization:Db_UseColumnfamily failed");
            return false;
        }
    }
    dbif->Db_SetInitDone(true);
    return true;
}

void Generator::TimerErrorHandler(string name, string error) {
    GENERATOR_LOG(ERROR, name + " error: " + error);
}

bool Generator::DelWaitTimerExpired() {

    // We are connected to this generator
    // Do not withdraw ownership
    if (gen_attr_.get_connects() > gen_attr_.get_resets()) 
        return false;

    collector_->GetOSP()->WithdrawGenerator(source_, module_);
    GENERATOR_LOG(INFO, "DelWaitTimer is Withdrawing Generator " <<
            source_ << ":" << module_);

    return false;
}

void Generator::ReceiveSandeshCtrlMsg(uint32_t connects) {

    del_wait_timer_->Cancel();
     
    // This is a control message during Generator-Collector negotiation
    uint32_t tmp = gen_attr_.get_connects();
    gen_attr_.set_connects(tmp+1);
    gen_attr_.set_connect_time(UTCTimestampUsec());

    ModuleServerState ginfo;    
    GetGeneratorInfo(ginfo);
    SandeshModuleServerTrace::Send(ginfo);

    if (!Db_Connection_Init()) {
        Start_Db_Connect_Timer();
    }
}

void Generator::DisconnectSession(VizSession *vsession) {
    GENERATOR_LOG(INFO, "Session:" << vsession->ToString());
    disconnected_ = true;
    if (vsession == viz_session_) {
        // This Generator's session is now gone.
        // Start a timer to delete all its UVEs
        uint32_t tmp = gen_attr_.get_resets();
        gen_attr_.set_resets(tmp+1);
        gen_attr_.set_reset_time(UTCTimestampUsec());
        viz_session_ = NULL;
        state_machine_ = NULL;
        del_wait_timer_->Start(kWaitTimerSec * 1000,
            boost::bind(&Generator::DelWaitTimerExpired, this),
            boost::bind(&Generator::TimerErrorHandler, this, _1, _2));

        ModuleServerState ginfo;
        GetGeneratorInfo(ginfo);
        SandeshModuleServerTrace::Send(ginfo);
    } else {
        GENERATOR_LOG(ERROR, "Disconnect for session:" << vsession->ToString() <<
                ", generator session:" << viz_session_->ToString());
    }
    Db_Connection_Uninit();
}

bool Generator::ReceiveSandeshMsg(boost::shared_ptr<VizMsg> &vmsg, bool rsc) {
    // This is a message from the application on the generator side.
    // It will be processed by the rule engine callback after we
    // update statistics
    db_handler_->MessageTableInsert(vmsg);

    UpdateMessageTypeStats(vmsg.get());
    UpdateLogLevelStats(vmsg.get());
    return (collector_->ProcessSandeshMsgCb())(vmsg, rsc, db_handler_.get());
}

void Generator::UpdateMessageTypeStats(VizMsg *vmsg) {
    MessageTypeStatsMap::iterator stats_it =
            stats_map_.find(vmsg->messagetype);
    if (stats_it == stats_map_.end()) {
        stats_it = (stats_map_.insert(vmsg->messagetype,
                new Stats)).first;
    }
    Stats *sandesh_stats = stats_it->second;
    sandesh_stats->messages_++;
    sandesh_stats->bytes_ += vmsg->xmlmessage.size();
    sandesh_stats->last_msg_timestamp_ = vmsg->hdr.get_Timestamp();
}

void Generator::UpdateLogLevelStats(VizMsg *vmsg) {
    // For system log, update the log level stats
    if (vmsg->hdr.get_Type() == SandeshType::SYSTEM) {
        SandeshLevel::type level =
                static_cast<SandeshLevel::type>(vmsg->hdr.get_Level());
        LogLevelStatsMap::iterator level_stats_it;
        if (level < SandeshLevel::INVALID) {
            std::string level_str(
                    Sandesh::LevelToString(level));
            level_stats_it = log_level_stats_map_.find(level_str);
            if (level_stats_it == log_level_stats_map_.end()) {
                level_stats_it = (log_level_stats_map_.insert(
                        level_str, new LogLevelStats)).first;
            }
            LogLevelStats *level_stats = level_stats_it->second;
            level_stats->messages_++;
            level_stats->bytes_ += vmsg->xmlmessage.size();
            level_stats->last_msg_timestamp_ =
                    vmsg->hdr.get_Timestamp();
        }
    }
}

bool Generator::GetSandeshStateMachineQueueCount(uint64_t &queue_count) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetQueueCount(queue_count);
}

bool Generator::GetSandeshStateMachineStats(
                    SandeshStateMachineStats &sm_stats,
                    SandeshGeneratorStats &sm_msg_stats) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetStatistics(sm_stats, sm_msg_stats);
}

bool Generator::GetDbStats(uint64_t &queue_count, uint64_t &enqueues) const {
    return db_handler_->GetStats(queue_count, enqueues);
}
    
void Generator::GetMessageTypeStats(vector<SandeshStats> &ssv) const {
    for (MessageTypeStatsMap::const_iterator mt_it = stats_map_.begin();
         mt_it != stats_map_.end();
         mt_it++) {
        SandeshStats sstats;
        sstats.message_type = mt_it->first;
        sstats.messages = (mt_it->second)->messages_;
        sstats.bytes = (mt_it->second)->bytes_;
        sstats.last_msg_timestamp = (mt_it->second)->last_msg_timestamp_;
        ssv.push_back(sstats);
    }
}

void Generator::GetLogLevelStats(vector<SandeshLogLevelStats> &lsv) const {
    for (LogLevelStatsMap::const_iterator ls_it = log_level_stats_map_.begin();
         ls_it != log_level_stats_map_.end(); ls_it++) {
        SandeshLogLevelStats level_stats;
        level_stats.level = ls_it->first;
        level_stats.messages = (ls_it->second)->messages_;
        level_stats.bytes = (ls_it->second)->bytes_;
        level_stats.last_msg_timestamp =
                (ls_it->second)->last_msg_timestamp_;
        lsv.push_back(level_stats);
    }
}

void Generator::GetGeneratorInfo(ModuleServerState &genlist) const {
    vector<GeneratorInfo> giv;
    GeneratorInfo gi;
    gi.set_hostname(Sandesh::source());
    gi.set_gen_attr(gen_attr_);
    giv.push_back(gi);
    genlist.set_generator_info(giv);
    genlist.set_name(source_ + ":" + module_);
}

const std::string Generator::State() const {
    if (state_machine_) {
        return state_machine_->StateName();
    }
    return "Disconnected";
}

void Generator::ConnectSession(VizSession *session, SandeshStateMachine *state_machine) {
    set_session(session);
    set_state_machine(state_machine);
    disconnected_ = false;
}
