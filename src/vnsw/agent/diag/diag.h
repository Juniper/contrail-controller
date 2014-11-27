/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_diag_hpp
#define vnsw_agent_diag_diag_hpp

#include <base/logging.h>
#include <net/address.h>
#include <base/timer.h>
#include "boost/date_time/posix_time/posix_time.hpp"

#include"diag/diag_pkt_handler.h"
struct AgentDiagPktData;
class DiagProto;
class DiagTable;
class DiagPktHandler;

class DiagEntry {
public:
    typedef uint16_t DiagKey;
    typedef Timer DiagTimer;

    DiagEntry(const std::string &sip, const std::string &dip, uint8_t proto,
              uint16_t sport, uint16_t dport, const std::string &vrf_name,
              int timeout, int count, DiagTable *diag_table);
    virtual ~DiagEntry();
    void Init();
    virtual void SendRequest() = 0;
    virtual void HandleReply(DiagPktHandler *handler) = 0;
    virtual void RequestTimedOut(uint32_t seq_no) = 0;
    virtual void SendSummary() { };
    bool TimerExpiry(uint32_t seqno);
    void RestartTimer();
    virtual bool IsDone();
    DiagKey GetKey() { return key_;};
    uint32_t GetSeqNo() {return seq_no_;};
    uint32_t GetMaxAttempts() {return max_attempts_;};
    void SetKey(DiagKey key) {key_ = key;};
    void Retry();
    bool TimerCancel() {
        return timer_->Cancel();
    }
    DiagTable* diag_table() const {
        return diag_table_;
    }

protected:
    Ip4Address sip_;
    Ip4Address dip_;
    uint8_t    proto_;
    uint16_t   sport_;
    uint16_t   dport_;
    std::string vrf_name_;
    boost::system::error_code ec_;

    DiagTable *diag_table_;
    DiagKey key_;

    int timeout_;
    DiagTimer *timer_;
    uint32_t max_attempts_;
    uint32_t seq_no_;
};

struct AgentDiagPktData {
    enum Op {
        DIAG_REQUEST = 1,
        DIAG_REPLY
    };

    uint32_t op_;
    DiagEntry::DiagKey key_;
    uint32_t seq_no_;
    boost::posix_time::ptime rtt_;
};

struct DiagEntryOp {
    enum Op {
        ADD = 1,
        DELETE,
        RETRY,
    };

    DiagEntryOp(DiagEntryOp::Op op, DiagEntry *de): op_(op), de_(de) {
    }

    Op op_;
    DiagEntry *de_;
};

class DiagTable {
public:
    typedef std::map<DiagEntry::DiagKey, DiagEntry *> DiagEntryTree;
    DiagTable(Agent *agent);
    ~DiagTable();
    void Add(DiagEntry *);
    void Delete(DiagEntry *);
    DiagEntry* Find(DiagEntry::DiagKey &key);

    void Shutdown();
    void Enqueue(DiagEntryOp *op);
    bool Process(DiagEntryOp *op);
    Agent* agent() const { return agent_; }

private:
    uint16_t index_;
    DiagEntryTree tree_;
    boost::scoped_ptr<DiagProto> diag_proto_;
    WorkQueue<DiagEntryOp *> *entry_op_queue_;
    Agent *agent_;
};
#endif
