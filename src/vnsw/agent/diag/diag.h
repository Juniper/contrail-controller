/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_diag_diag_hpp
#define vnsw_agent_diag_diag_hpp

#include <base/logging.h>
#include <net/address.h>
#include <base/timer.h>
#include "boost/date_time/posix_time/posix_time.hpp"

struct AgentDiagPktData;
class DiagPktHandler : public ProtoHandler {
public:
    DiagPktHandler(PktInfo *info, boost::asio::io_service &io):
        ProtoHandler(info, io) { };
    virtual bool Run();
    void SetReply();
    void SetDiagChkSum();
    void Reply();
    AgentDiagPktData* GetData() {
        return (AgentDiagPktData *)(pkt_info_->data);
    }
};

class DiagEntry {
public:
    typedef uint32_t DiagKey;
    typedef Timer DiagTimer;

    DiagEntry(int timeout, int count);
    virtual ~DiagEntry();
    void Init();
    virtual void SendRequest() = 0;
    virtual void HandleReply(DiagPktHandler *handler) = 0;
    virtual void RequestTimedOut(uint32_t seq_no) = 0;
    virtual void SendSummary() { };
    static bool TimerExpiry(DiagEntry *entry, uint32_t seqno);
    void RestartTimer();
    DiagKey GetKey() { return key_;};
    uint32_t GetSeqNo() {return seq_no_;};
    uint32_t GetCount() {return count_;};
    void SetKey(DiagKey key) {key_ = key;};
    void Retry();
    bool TimerCancel() {
        return timer_->Cancel();
    }

protected:
    DiagKey key_;
    int timeout_;
    DiagTimer *timer_;
    uint32_t count_;
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

    DiagEntryOp(DiagEntryOp::Op op, DiagEntry *de):
        op_(op), de_(de) {
    }

    Op op_;
    DiagEntry *de_;
};

class DiagTable {
public:
    typedef std::map<DiagEntry::DiagKey, DiagEntry *> DiagEntryTree;
    DiagTable();
    ~DiagTable();
    void Add(DiagEntry *);
    void Delete(DiagEntry *);
    DiagEntry* Find(DiagEntry::DiagKey &key);
    static DiagTable* GetTable() {
        return singleton_;
    }
    static void Init() {
        if (singleton_ == NULL) {
            singleton_ = new DiagTable();
        }
    }
    void Shutdown();
    void Enqueue(DiagEntryOp *op);
    bool Process(DiagEntryOp *op);
private:
    uint32_t index_; 
    DiagEntryTree tree_;
    static DiagTable *singleton_;
    WorkQueue<DiagEntryOp *> *entry_op_queue_;
};
#endif
