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

void Generator::UpdateMessageTypeStats(VizMsg *vmsg) {
    tbb::mutex::scoped_lock lock(smutex_);
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
    tbb::mutex::scoped_lock lock(smutex_);
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

void Generator::GetMessageTypeStats(vector<SandeshStats> &ssv) const {
    tbb::mutex::scoped_lock lock(smutex_);
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
    tbb::mutex::scoped_lock lock(smutex_);
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

bool Generator::ReceiveSandeshMsg(boost::shared_ptr<VizMsg> &vmsg, bool rsc) {
    // This is a message from the application on the generator side.
    // It will be processed by the rule engine callback after we
    // update statistics
    GetDbHandler()->MessageTableInsert(vmsg);

    UpdateMessageTypeStats(vmsg.get());
    UpdateLogLevelStats(vmsg.get());
    UpdateMessageStats(vmsg.get());
    return ProcessRules(vmsg, rsc);
}

void Generator::UpdateMessageStats(VizMsg *vmsg) {
    //update the MessageStatsMap
    //Get the loglevel string from the message
    SandeshLevel::type level =
                            static_cast<SandeshLevel::type>(vmsg->hdr.get_Level());
    LogLevelStatsMap::iterator level_stats_it;
    if (level <= SandeshLevel::INVALID) {
        std::string level_str(
        Sandesh::LevelToString(level));
        //Lookup based on the pair(Messagetype,loglevel)
        MessageStatsMap::iterator stats_it = sandesh_stats_map_.find(make_pair(vmsg->messagetype,level_str));
        if (stats_it == sandesh_stats_map_.end()) {
            //New messagetype,loglevel combination
            std::pair<std::string,std::string> key(vmsg->messagetype,level_str);
            tbb::mutex::scoped_lock lock(smutex_);
            stats_it = (sandesh_stats_map_.insert(key,new MessageStats)).first;
         }
    MessageStats *message_stats = stats_it->second;
    message_stats->messages_++;
    message_stats->bytes_ += vmsg->xmlmessage.size();
    } 
} 
    

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
        db_connect_timer_(TimerManager::CreateTimer(
            *collector->event_manager()->io_service(),
            "SandeshGenerator db connect timer" + source + module,
            TaskScheduler::GetInstance()->GetTaskId(Collector::kDbTask),
            session->GetSessionInstance())),
        db_handler_(new DbHandler(
            collector->event_manager(), boost::bind(
                &SandeshGenerator::StartDbifReinit, this),
            collector->cassandra_ip(), collector->cassandra_port(),
            collector->analytics_ttl(), source + ":" + node_type + ":" +
                module + ":" + instance_id)) {
    disconnected_ = false;
    gen_attr_.set_connects(1);
    gen_attr_.set_connect_time(UTCTimestampUsec());
    // Update state machine
    state_machine_->SetGeneratorKey(name_);
}

SandeshGenerator::~SandeshGenerator() {
    TimerManager::DeleteTimer(db_connect_timer_);
    db_connect_timer_ = NULL;
    GetDbHandler()->UnInit(instance_);
}

void SandeshGenerator::set_session(VizSession *session) {
    viz_session_ = session;
    instance_ = session->GetSessionInstance();
}

void SandeshGenerator::StartDbifReinit() {
    GetDbHandler()->UnInit(instance_);
    Start_Db_Connect_Timer();
}

bool SandeshGenerator::DbConnectTimerExpired() {
    if (disconnected_) {
        return false;
    }
    if (!(Db_Connection_Init())) {
        return true;
    }
    return false;
}

void SandeshGenerator::Start_Db_Connect_Timer() {
    db_connect_timer_->Start(kDbConnectTimerSec * 1000,
            boost::bind(&SandeshGenerator::DbConnectTimerExpired, this),
            boost::bind(&SandeshGenerator::TimerErrorHandler, this, _1, _2));
}

void SandeshGenerator::Stop_Db_Connect_Timer() {
    db_connect_timer_->Cancel();
}

void SandeshGenerator::Db_Connection_Uninit() {
    GetDbHandler()->ResetDbQueueWaterMarkInfo();
    GetDbHandler()->UnInit(instance_);
    Stop_Db_Connect_Timer();
}

bool SandeshGenerator::Db_Connection_Init() {
    if (!GetDbHandler()->Init(false, instance_)) {
        GENERATOR_LOG(ERROR, ": Database setup FAILED");
        return false;
    }
    std::vector<DbHandler::DbQueueWaterMarkInfo> wm_info;
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

    if (!Db_Connection_Init()) {
        Start_Db_Connect_Timer();
    }
}

void SandeshGenerator::DisconnectSession(VizSession *vsession) {
    GENERATOR_LOG(INFO, "Session:" << vsession->ToString());
    disconnected_ = true;
    if (vsession == viz_session_) {
        // This SandeshGenerator's session is now gone.
        // Delete all its UVEs
        uint32_t tmp = gen_attr_.get_resets();
        gen_attr_.set_resets(tmp+1);
        gen_attr_.set_reset_time(UTCTimestampUsec());
        viz_session_ = NULL;
        state_machine_ = NULL;
        collector_->GetOSP()->DeleteUVEs(source_, module_, 
                                         node_type_, instance_id_);
        ModuleServerState ginfo;
        GetGeneratorInfo(ginfo);
        SandeshModuleServerTrace::Send(ginfo);
    } else {
        GENERATOR_LOG(ERROR, "Disconnect for session:" << vsession->ToString() <<
                ", generator session:" << viz_session_->ToString());
    }
    Db_Connection_Uninit();
}

bool SandeshGenerator::ProcessRules(boost::shared_ptr<VizMsg> &vmsg,
        bool rsc)
{
    return (collector_->ProcessSandeshMsgCb())(vmsg, rsc, GetDbHandler());
}

bool SandeshGenerator::GetSandeshStateMachineQueueCount(uint64_t &queue_count) const {
    if (!state_machine_) {
        return false;
    }
    return state_machine_->GetQueueCount(queue_count);
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
    std::string &drop_level, uint64_t &msg_dropped) const {
    return db_handler_->GetStats(queue_count, enqueues, drop_level,
               msg_dropped);
}

void Generator::GetSandeshStats(std::vector<SandeshMessageInfo> &smv) {
    //Acquire lock b4 reading map, because an update to map happen in parallel
    tbb::mutex::scoped_lock lock(smutex_);
    for (MessageStatsMap::const_iterator ss_it = sandesh_stats_map_.begin();
         ss_it != sandesh_stats_map_.end(); ss_it++) {
         SandeshMessageInfo msg_stats;
         //Lookup the old stats for the messagetype,loglevel and subtract it
         //from the new message
         msg_stats.type = (ss_it->first).first;
         msg_stats.level = (ss_it->first).second;
         std::pair <std::string,std::string> key = std::make_pair(msg_stats.type,msg_stats.level);
         MessageStatsMap::iterator msg_stats_it;
         msg_stats_it = sandesh_stats_map_old_.find(key);
         //If entry does not exist in old map, insert it
         if (msg_stats_it == sandesh_stats_map_old_.end()) {
             msg_stats_it = (sandesh_stats_map_old_.insert(key,new MessageStats)).first;
         }
         //else subtract the old val from new val 
         uint64_t current_messages = ss_it->second->messages_;
         uint64_t current_bytes = ss_it->second->bytes_;
         msg_stats.messages = current_messages - (msg_stats_it->second)->messages_;
         msg_stats.bytes = current_bytes - (msg_stats_it->second)->bytes_;
         //update the oldmap values
         (msg_stats_it->second)->messages_ = current_messages;
         (msg_stats_it->second)->bytes_ = current_bytes;
         smv.push_back(msg_stats);
    }

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

void SandeshGenerator::ConnectSession(VizSession *session, SandeshStateMachine *state_machine) {
    set_session(session);
    set_state_machine(state_machine);
    disconnected_ = false;
    uint32_t tmp = gen_attr_.get_connects();
    gen_attr_.set_connects(tmp+1);
    gen_attr_.set_connect_time(UTCTimestampUsec());
}

void SandeshGenerator::SetDbQueueWaterMarkInfo(DbHandler::DbQueueWaterMarkInfo &wm) {
    GetDbHandler()->SetDbQueueWaterMarkInfo(wm);
}

void SandeshGenerator::ResetDbQueueWaterMarkInfo() {
    GetDbHandler()->ResetDbQueueWaterMarkInfo();
}

SyslogGenerator::SyslogGenerator(SyslogListeners *const listeners,
        const string &source, const string &module) :
          Generator(),
          syslog_(listeners),
          source_(source),
          module_(module),
          name_(source + ":" + module),
          db_handler_(listeners->GetDbHandler())
{
}

bool SyslogGenerator::ProcessRules(boost::shared_ptr<VizMsg> &vmsg,
        bool rsc)
{
    return (syslog_->ProcessSandeshMsgCb())(vmsg, rsc, GetDbHandler());
}

