/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <map>
#include "vr_defs.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "diag/diag_table.h"
#include "diag/diag_proto.h"
#include "diag/ping.h"
#include "oper/mirror_table.h"


const std::string KDiagName("DiagTimeoutHandler");

////////////////////////////////////////////////////////////////////////////////

DiagEntry::DiagEntry(int timeout, int count,DiagTable *diag):
    diag_(diag),timeout_(timeout), 
    timer_(TimerManager::CreateTimer(*(diag->GetAgent()->GetEventManager())->io_service(), 
    "DiagTimeoutHandler")), count_(count), seq_no_(0) {
}

DiagEntry::~DiagEntry() {
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
    //Delete entry in DiagTable
    diag_->Delete(this);
}

void DiagEntry::Init() {
    DiagEntryOp *entry_op = new DiagEntryOp(DiagEntryOp::ADD, this);
    entry_op->de_->diag_=diag_;
    diag_->Enqueue(entry_op);
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
    op->de_->diag_=diag_;
    diag_->Enqueue(op);
    return false;
}

void DiagEntry::Retry() {
    SendRequest();
    RestartTimer();
}

void DiagPktHandler::SetReply() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;
    ad->op_ = htonl(AgentDiagPktData::DIAG_REPLY);
}

void DiagPktHandler::SetDiagChkSum() {
    pkt_info_->ip->check = 0xffff;
}

void DiagPktHandler::Reply() {
    SetReply();
    Swap();
    SetDiagChkSum();
    Send(GetLen() - (2 * IPC_HDR_LEN), GetIntf(), GetVrf(), 
         AGENT_CMD_ROUTE, PktHandler::DIAG);
}
 
bool DiagPktHandler::Run() {
    AgentDiagPktData *ad = (AgentDiagPktData *)pkt_info_->data;

    if (!ad) {
        //Ignore if packet doesnt have proper L4 header
        return true;
    }

    if (ntohl(ad->op_) == AgentDiagPktData::DIAG_REQUEST) {
        //Request received swap the packet
        //and dump the packet back
        Reply();
        return false;
    }

    if (ntohl(ad->op_) != AgentDiagPktData::DIAG_REPLY) {
        return true;
    }

    //Reply for a query we sent
    DiagEntry::DiagKey key = ntohl(ad->key_);
    DiagEntry *entry = diag_->Find(key);
    if (!entry) {
        return true;
    }

    entry->HandleReply(this);

    if (entry->GetSeqNo() == entry->GetCount()) {
        DiagEntryOp *op;
        op = new DiagEntryOp(DiagEntryOp::DELETE, entry);
        entry->GetDiag()->Enqueue(op);
    } else {
        entry->Retry();
    }
        
    return true;
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

DiagTable::DiagTable(Agent *agent) {
    agent_=agent;
    diag_proto_.reset(
        new DiagProto(agent, *(agent->GetEventManager())->io_service()));
    entry_op_queue_ = new WorkQueue<DiagEntryOp *>
                    (TaskScheduler::GetInstance()->GetTaskId("Agent::Diag"), 0,
                     boost::bind(&DiagTable::Process, this, _1));
    index_ = 1;
    Ping::PingInit();
}

void DiagTable::Shutdown() {
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
