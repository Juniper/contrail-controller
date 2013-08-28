/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <utility>
#include <vector>
#include <map>
#include <boost/bind.hpp>
#include "base/timer.h"
#include <boost/date_time/posix_time/posix_time.hpp>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/sandesh_session.h>
#include <sandesh/sandesh_constants.h>
#include <sandesh/sandesh_ctrl_types.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>

#include "viz_types.h"
#include "generator.h"
#include "collector.h"
#include "viz_sandesh.h"
#include "OpServerProxy.h"
#include "viz_collector.h"

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
        del_wait_timer_(
                TimerManager::CreateTimer(*collector->event_manager()->io_service(),
                    "Delete wait timer" + source + module)),
        source_(source),
        module_(module),
        name_("Generator(" + source_ + ":" + module_ + ")") {
}

Generator::~Generator() {
    TimerManager::DeleteTimer(del_wait_timer_);
}

void Generator::TimerErrorHandler(string name, string error) {
    GENERATOR_LOG(ERROR, name + " error: " + error);
}

void Generator::EnqueueRedisMessage(RedisReplyMsg *msg) {
    if (state_machine_) {
        state_machine_->OnMessage(msg);
    } else {
        GENERATOR_LOG(INFO, "Received Redis Reply Msg: " <<
                msg->msg_type_  << " after disconnect");
    }
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

void Generator::HandleSeqRedisReply(const map<string,int32_t> &typeMap) {
    std::vector<UVETypeInfo> vu;
    for (map<string,int32_t>::const_iterator it = typeMap.begin();
            it != typeMap.end(); it++) {
        UVETypeInfo uti;
        uti.set_type_name(it->first);
        uti.set_seq_num(it->second);
        vu.push_back(uti);
    }
    if (viz_session_) {
        GENERATOR_LOG(DEBUG, "Sending Ctrl Msg: Size " << vu.size());
        SandeshCtrlServerToClient::Request(vu, true, "ctrl",
                viz_session_->connection());
    } else {
        GENERATOR_LOG(ERROR, "CANNOT send Ctrl Msg");
    }
}

void Generator::HandleDelRedisReply(bool res) {
    std::vector<UVETypeInfo> vu;
    if (viz_session_) {
        GENERATOR_LOG(DEBUG, "Sending Ctrl Msg: Size " << vu.size());
        SandeshCtrlServerToClient::Request(vu, true, "ctrl",
                viz_session_->connection());
    } else {
        GENERATOR_LOG(ERROR, "CANNOT send Ctrl Msg");
    }
    gen_attr_.set_in_clear(false);
}

bool Generator::ReceiveMsg(ssm::Message *msg) {
    RedisReplyMsg *redis_reply = dynamic_cast<RedisReplyMsg *>(msg);
    if (redis_reply != NULL) {
        // Handle reply from Redis
        if (redis_reply->msg_type_ == RedisReplyMsg::REDIS_SEQ_REPLY) {
            HandleSeqRedisReply(redis_reply->typemap_);
        } else if (redis_reply->msg_type_ == RedisReplyMsg::REDIS_DEL_REPLY) {
            HandleDelRedisReply(redis_reply->res_);
        } else {
            GENERATOR_LOG(ERROR, "Unknown Redis reply message type " <<
                    redis_reply->msg_type_);
            return false;
        }
    } else {
        GENERATOR_LOG(ERROR, "Unknown ssm::Message");
        return false;
    }
    return true;
}

void Generator::ReceiveSandeshCtrlMsg(uint32_t connects) {

    del_wait_timer_->Cancel();
     
    // This is a control message during Generator-Collector negotiation
    uint32_t tmp = gen_attr_.get_connects();
    gen_attr_.set_connects(tmp+1);
    gen_attr_.set_connect_time(UTCTimestampUsec());

    if (connects > 1) {
        GeneratorId id(std::make_pair(source_, module_));
        if (!collector_->GetOSP()->GetSeq(source_, module_, boost::bind(
                &Collector::EnqueueSeqRedisReply, collector_, id, _1))) {
            GENERATOR_GETSEQ_TRACE(UVETraceBuf, source_, module_, false);
            GENERATOR_LOG(ERROR, "Session:" << viz_session_->ToString() <<
                    " OSP GetSeq FAILED");

        } else {
            GENERATOR_GETSEQ_TRACE(UVETraceBuf, source_, module_, true);
         }
    } else {
        gen_attr_.set_in_clear(true);
        GeneratorId id(std::make_pair(source_, module_));
        if (!collector_->GetOSP()->DeleteUVEs(source_, module_, boost::bind(
                &Collector::EnqueueDelRedisReply, collector_, id, _1))) {
            GENERATOR_DELUVES_TRACE(UVETraceBuf, source_, module_, false);
            GENERATOR_LOG(ERROR, "Session:" << viz_session_->ToString() <<
                    " OSP DeleteUVEs FAILED");
            std::vector<UVETypeInfo> vu;
            SandeshCtrlServerToClient::Request(vu, false, "ctrl", viz_session_->connection());
            return;            
        } else {
            GENERATOR_DELUVES_TRACE(UVETraceBuf, source_, module_, true);
            GENERATOR_LOG(DEBUG, "Session:" << viz_session_->ToString() <<
                    " Deleting UVEs");
        }
    }

    ModuleServerState ginfo;    
    GetGeneratorInfo(ginfo);
    SandeshModuleServerTrace::Send(ginfo);
}

void Generator::DisconnectSession(VizSession *vsession) {
    GENERATOR_LOG(INFO, "Session:" << vsession->ToString());
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
}

bool Generator::ReceiveSandeshMsg(boost::shared_ptr<VizMsg> &vmsg) {
    // This is a message from the application on the generator side.
    // It will be processed by the rule engine callback after we
    // update statistics
    UpdateMessageTypeStats(vmsg.get());
    UpdateLogLevelStats(vmsg.get());
    return (collector_->ProcessSandeshMsgCb())(vmsg);
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

void GeneratorListReq::HandleRequest() const {
    GeneratorListResp *resp(new GeneratorListResp);
    vector<GeneratorSummaryInfo> genlist;
    VizSandeshContext *vsc = dynamic_cast<VizSandeshContext *>
                                 (Sandesh::client_context());
    if (!vsc) {
        LOG(ERROR, __func__ << ": Sandesh client context NOT PRESENT");
        resp->Response();
        return;
    }
    vsc->Analytics()->GetCollector()->GetGeneratorSummaryInfo(genlist);
    resp->set_genlist(genlist);
    resp->set_context(context());
    resp->Response();
}
