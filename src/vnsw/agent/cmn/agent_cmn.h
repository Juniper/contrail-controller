/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_cmn_hpp
#define vnsw_agent_cmn_hpp

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <boost/intrusive_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/string_generator.hpp>

#include <tbb/atomic.h>
#include <tbb/mutex.h>

#include <io/event_manager.h>
#include <base/logging.h>
#include <net/address.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <base/task.h>
#include <base/task_trigger.h>
#include <cmn/agent_db.h>
#include <ifmap/ifmap_agent_parser.h>
#include <ifmap/ifmap_agent_table.h>
#include <cfg/cfg_listener.h>
#include <io/event_manager.h>
#include <sandesh/sandesh_trace.h>
#include <sandesh/common/vns_constants.h>

#include <cmn/agent.h>

class AgentStats {
public:
    AgentStats(Agent *agent)
        : agent_(agent), xmpp_reconnect_(), xmpp_in_msgs_(), xmpp_out_msgs_(),
        sandesh_reconnects_(0U), sandesh_in_msgs_(0U), sandesh_out_msgs_(0U),
        sandesh_http_sessions_(0U), nh_count_(0U), pkt_exceptions_(0U),
        pkt_invalid_agent_hdr_(0U), pkt_invalid_interface_(0U), 
        pkt_no_handler_(0U), pkt_dropped_(0U), flow_created_(0U),
        flow_aged_(0U), flow_active_(0U), ipc_in_msgs_(0U), ipc_out_msgs_(0U), 
        in_tpkts_(0U), in_bytes_(0U), out_tpkts_(0U), out_bytes_(0U) {
        singleton_ = this;
    }

    static AgentStats *GetInstance() {return singleton_;}
    void Shutdown() {
    }

    void IncrXmppReconnect(uint8_t idx) {xmpp_reconnect_[idx]++;};
    uint16_t GetXmppReconnect(uint8_t idx) {return xmpp_reconnect_[idx];};

    void IncrXmppInMsgs(uint8_t idx) {xmpp_in_msgs_[idx]++;};
    uint64_t GetXmppInMsgs(uint8_t idx) {return xmpp_in_msgs_[idx];};

    void IncrXmppOutMsgs(uint8_t idx) {xmpp_out_msgs_[idx]++;};
    uint64_t GetXmppOutMsgs(uint8_t idx) {return xmpp_out_msgs_[idx];};

    void IncrSandeshReconnects() {sandesh_reconnects_++;};
    uint16_t GetSandeshReconnects() {
        return sandesh_reconnects_;
    };

    void IncrSandeshInMsgs() {sandesh_in_msgs_++;};
    uint64_t GetSandeshInMsgs() {
        return sandesh_in_msgs_;
    };

    void IncrSandeshOutMsgs() {sandesh_out_msgs_++;};
    uint64_t GetSandeshOutMsgs() {
        return sandesh_out_msgs_;
    };

    void IncrSandeshHttpSessions() {
        sandesh_http_sessions_++;
    };
    uint16_t GetSandeshHttpSessions() {
        return sandesh_http_sessions_;
    };

    void IncrFlowCreated() {flow_created_++;};
    uint64_t GetFlowCreated() {return flow_created_; };

    void IncrFlowAged() {flow_aged_++;};
    uint64_t GetFlowAged() {return flow_aged_;};

    void IncrFlowActive() {flow_active_++;};
    void DecrFlowActive() {flow_active_--;};
    uint64_t GetFlowActive() {return flow_active_;};

    void IncrPktExceptions() {pkt_exceptions_++;};
    uint64_t GetPktExceptions() {return pkt_exceptions_;};

    void IncrPktInvalidAgentHdr() {
        pkt_invalid_agent_hdr_++;
    };
    uint64_t GetPktInvalidAgentHdr() {
        return pkt_invalid_agent_hdr_;
    };

    void IncrPktInvalidInterface() {
        pkt_invalid_interface_++;
    };
    uint64_t GetPktInvalidInterface() {
        return pkt_invalid_interface_;
    };

    void IncrPktNoHandler() {pkt_no_handler_++;};
    uint64_t GetPktNoHandler() {return pkt_no_handler_;};

    void IncrPktDropped() {pkt_dropped_++;};
    uint64_t GetPktDropped() {return pkt_dropped_;};

    void IncrIpcInMsgs() {ipc_in_msgs_++;};
    uint64_t GetIpcInMsgs() {return ipc_out_msgs_;};

    void IncrIpcOutMsgs() {ipc_out_msgs_++;};
    uint64_t GetIpcOutMsgs() {return ipc_out_msgs_;};

    void IncrInPkts(uint64_t count) {
        in_tpkts_ += count;
    };
    uint64_t GetInPkts() {
        return in_tpkts_;
    };
    void IncrInBytes(uint64_t count) {
        in_bytes_ += count;
    };
    uint64_t GetInBytes() {
        return in_bytes_;
    };
    void IncrOutPkts(uint64_t count) {
        out_tpkts_ += count;
    };
    uint64_t GetOutPkts() {
        return out_tpkts_;
    };
    void IncrOutBytes(uint64_t count) {
        out_bytes_ += count;
    };
    uint64_t GetOutBytes() {
        return out_bytes_;
    };
private:
    Agent *agent_;
    uint16_t xmpp_reconnect_[MAX_XMPP_SERVERS];
    uint64_t xmpp_in_msgs_[MAX_XMPP_SERVERS];
    uint64_t xmpp_out_msgs_[MAX_XMPP_SERVERS];

    uint16_t sandesh_reconnects_;
    uint64_t sandesh_in_msgs_;
    uint64_t sandesh_out_msgs_;
    uint16_t sandesh_http_sessions_;

    // Number of NH created
    uint32_t nh_count_;

    // Exception packet stats
    uint64_t pkt_exceptions_;
    uint64_t pkt_invalid_agent_hdr_;
    uint64_t pkt_invalid_interface_;
    uint64_t pkt_no_handler_;
    uint64_t pkt_dropped_;

    // Flow stats
    uint64_t flow_created_;
    uint64_t flow_aged_;
    uint64_t flow_active_;

    // Kernel IPC
    uint64_t ipc_in_msgs_;
    uint64_t ipc_out_msgs_;
    uint64_t in_tpkts_;
    uint64_t in_bytes_;
    uint64_t out_tpkts_;
    uint64_t out_bytes_;

    static AgentStats *singleton_;
};

static inline bool UnregisterDBTable(DBTable *table, 
                                     DBTableBase::ListenerId id) {
    table->Unregister(id);
    return true;
}

static inline TaskTrigger *SafeDBUnregister(DBTable *table,
                                            DBTableBase::ListenerId id) {
    TaskTrigger *trigger = 
           new TaskTrigger(boost::bind(&UnregisterDBTable, table, id),
               TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0);
    trigger->Set();
    return trigger;
}

static inline void CfgUuidSet(uint64_t ms_long, uint64_t ls_long,
                              boost::uuids::uuid &u) {
    for (int i = 0; i < 8; i++) {
        u.data[7 - i] = ms_long & 0xFF;
        ms_long = ms_long >> 8;
    }

    for (int i = 0; i < 8; i++) {
        u.data[15 - i] = ls_long & 0xFF;
        ls_long = ls_long >> 8;
    }
}

extern SandeshTraceBufferPtr OperDBTraceBuf;

#define OPER_TRACE(obj, ...)\
do {\
   Oper##obj::TraceMsg(OperDBTraceBuf, __FILE__, __LINE__, __VA_ARGS__);\
} while (false);\

#define IFMAP_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::IFMAP)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);\

#define AGENT_ERROR(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_ERR, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);

#define AGENT_LOG(obj, ...)\
do {\
    if (LoggingDisabled()) break;\
    obj::Send(g_vns_constants.CategoryNames.find(Category::VROUTER)->second,\
              SandeshLevel::SYS_INFO, __FILE__, __LINE__, ##__VA_ARGS__);\
} while (false);

#endif // vnsw_agent_cmn_hpp

