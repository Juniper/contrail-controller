/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _GENERATOR_H_
#define _GENERATOR_H_

#include <boost/shared_ptr.hpp>
#include <string>
#include "viz_message.h"
#include "base/queue_task.h"
#include "collector_uve_types.h"
#include <sandesh/sandesh_state_machine.h>

class Sandesh;
class VizSession;
class Collector;

struct RedisReplyMsg : public ssm::Message {
    enum RedisReplyMsgType {
        REDIS_INVALID_REPLY = 0,
        REDIS_SEQ_REPLY = 1,
        REDIS_DEL_REPLY = 2
    };

    RedisReplyMsg(const std::map<std::string, int32_t> &typemap) :
        msg_type_(REDIS_SEQ_REPLY), typemap_(typemap) {}
    RedisReplyMsg(const bool res) :
        msg_type_(REDIS_DEL_REPLY), res_(res) {}

    RedisReplyMsgType msg_type_;
    std::map<std::string, int32_t> typemap_;
    bool res_;
};

class Generator {
public:
    typedef std::pair<std::string /* Source */, std::string /* Module */> GeneratorId;

    Generator(Collector * const collector, VizSession *session,
            SandeshStateMachine *state_machine,
            const std::string &source, const std::string &module);
    ~Generator();

    void ReceiveSandeshCtrlMsg(uint32_t connects);
    bool ReceiveSandeshMsg(boost::shared_ptr<VizMsg> &vmsg);
    bool ReceiveMsg(ssm::Message *msg);
    void DisconnectSession(VizSession *vsession);
    void EnqueueRedisMessage(RedisReplyMsg *msg);

    void GetMessageTypeStats(std::vector<SandeshStats> &ssv) const;
    void GetLogLevelStats(std::vector<SandeshLogLevelStats> &lsv) const;

    const std::string &module() const { return module_; }
    const std::string &source() const { return source_; }
    VizSession * session() const { return viz_session_; }
    const std::string ToString() const { return name_; }
    void set_session(VizSession *session) { viz_session_ = session; }
    void set_state_machine(SandeshStateMachine *state_machine) {
        state_machine_ = state_machine;
    }

    void GetGeneratorInfo(ModuleServerState &genlist) const;
private:
    struct Stats {
        Stats() : messages_(0), bytes_(0), last_msg_timestamp_(0) {}
        uint64_t messages_;
        uint64_t bytes_;
        uint64_t last_msg_timestamp_;
    };
    typedef boost::ptr_map<std::string /* MessageType */, Stats> MessageTypeStatsMap;
    MessageTypeStatsMap stats_map_;

    struct LogLevelStats {
        LogLevelStats() : messages_(0), bytes_(0), last_msg_timestamp_(0) {}
        uint64_t messages_;
        uint64_t bytes_;
        uint64_t last_msg_timestamp_;
    };
    typedef boost::ptr_map<std::string /* Log level */, LogLevelStats> LogLevelStatsMap;
    LogLevelStatsMap log_level_stats_map_;

    void UpdateMessageTypeStats(VizMsg *vmsg);
    void UpdateLogLevelStats(VizMsg *vmsg);
    void HandleSeqRedisReply(const std::map<std::string,int32_t> &typeMap);
    void HandleDelRedisReply(bool res);
    void TimerErrorHandler(std::string name, std::string error);
    bool DelWaitTimerExpired();

    static const uint32_t kWaitTimerSec = 10;

    Collector * const collector_;
    SandeshStateMachine *state_machine_;
    VizSession *viz_session_;
    GeneratorInfoAttr gen_attr_;

    Timer *del_wait_timer_;
    const std::string source_;
    const std::string module_;
    const std::string name_;
};

#endif
