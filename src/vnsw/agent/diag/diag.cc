/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/os.h"
#include <map>
#include "vr_defs.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag.h"
#include "diag/diag_proto.h"
#include "diag/ping.h"
#include "oper/mirror_table.h"


const std::string KDiagName("DiagTimeoutHandler");

////////////////////////////////////////////////////////////////////////////////

DiagEntry::DiagEntry(int timeout, int count,DiagTable *diag_table):
    diag_table_(diag_table) , timeout_(timeout),
    timer_(TimerManager::CreateTimer(*(diag_table->agent()->event_manager())->io_service(), 
    "DiagTimeoutHandler")), count_(count), seq_no_(0) {
}

DiagEntry::~DiagEntry() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
    //Delete entry in DiagTable
    diag_table_->Delete(this);
}

void DiagEntry::Init() {
    DiagEntryOp *entry_op = new DiagEntryOp(DiagEntryOp::ADD, this);
    diag_table_->Enqueue(entry_op);
}

void DiagEntry::RestartTimer() {
    //Cancel timer of running
    timer_->Cancel();
    timer_->Start(timeout_, boost::bind(&DiagEntry::TimerExpiry, this, seq_no_));
}

bool DiagEntry::TimerExpiry( uint32_t seq_no) {
    DiagEntryOp *op;
    RequestTimedOut(seq_no);
    if (seq_no == GetCount()) {
        op = new DiagEntryOp(DiagEntryOp::DELETE, this);
    } else {
        op = new DiagEntryOp(DiagEntryOp::RETRY, this);
    }
    diag_table_->Enqueue(op);
    return false;
}

void DiagEntry::Retry() {
    SendRequest();
    RestartTimer();
}

bool DiagTable::Process(DiagEntryOp *op) {
    switch (op->op_) {
    case DiagEntryOp::ADD:
        Add(op->de_);
        break;

    case DiagEntryOp::DELETE:
        if (op->de_->TimerCancel() == true) {
            op->de_->SendSummary();
            delete op->de_;
        }
        break;

    case DiagEntryOp::RETRY:
        op->de_->Retry();
        break;
    }

    delete op;
    return true;
}

DiagTable::DiagTable(Agent *agent):agent_(agent) {
    diag_proto_.reset(
        new DiagProto(agent, *(agent->event_manager())->io_service()));
    entry_op_queue_ = new WorkQueue<DiagEntryOp *>
                    (TaskScheduler::GetInstance()->GetTaskId("Agent::Diag"), 0,
                     boost::bind(&DiagTable::Process, this, _1));
    index_ = 1;
    Ping::PingInit();
}

void DiagTable::Shutdown() {
    entry_op_queue_->Shutdown();
    delete entry_op_queue_;
    diag_proto_.reset(NULL);
}

DiagTable::~DiagTable() {
    assert(tree_.size() == 0);
}

void DiagTable::Add(DiagEntry *de) {
    de->SetKey(index_++);
    tree_.insert(std::make_pair(de->GetKey(), de));
    de->SendRequest();
    de->RestartTimer();
}

void DiagTable::Delete(DiagEntry *de) {
    tree_.erase(de->GetKey());
}

DiagEntry* DiagTable::Find(DiagEntry::DiagKey &key) {
    DiagEntryTree::const_iterator it;

    it = tree_.find(key);
    if (it == tree_.end()) {
        return NULL;
    }

    return static_cast<DiagEntry *>(it->second);
}

void DiagTable::Enqueue(DiagEntryOp *op) {
    entry_op_queue_->Enqueue(op);
}
