/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include <net/address_util.h>
#include <boost/functional/hash.hpp>
#include <init/agent_param.h>
#include <cmn/agent_stats.h>
#include <oper/agent_profile.h>
#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>
#include "vrouter/flow_stats/flow_stats_collector.h"
#include "flow_proto.h"
#include "flow_mgmt_dbclient.h"
#include "flow_mgmt.h"
#include "flow_event.h"

//////////////////////////////////////////////////////////////////////////////
// FlowEventQueue routines
//////////////////////////////////////////////////////////////////////////////
FlowEventQueueBase::FlowEventQueueBase(FlowProto *proto,
                                       const std::string &name,
                                       uint32_t task_id, int task_instance,
                                       FlowTokenPool *pool,
                                       uint16_t latency_limit,
                                       uint32_t max_iterations) :
    flow_proto_(proto), token_pool_(pool), task_start_(0), count_(0),
    events_processed_(0), latency_limit_(latency_limit) {
    queue_ = new Queue(task_id, task_instance,
                       boost::bind(&FlowEventQueueBase::Handler, this, _1),
                       Queue::kMaxSize, Queue::kMaxIterations);
    char buff[100];
    sprintf(buff, "%s-%d", name.c_str(), task_instance);
    queue_->set_name(buff);
    if (token_pool_)
        queue_->SetStartRunnerFunc(boost::bind(&FlowEventQueueBase::TokenCheck,
                                               this));
    queue_->set_measure_busy_time(proto->agent()->MeasureQueueDelay());
    if (latency_limit_) {
        queue_->SetEntryCallback(boost::bind(&FlowEventQueueBase::TaskEntry,
                                             this));
        queue_->SetExitCallback(boost::bind(&FlowEventQueueBase::TaskExit,
                                            this, _1));
    }
}

FlowEventQueueBase::~FlowEventQueueBase() {
    delete queue_;
}

void FlowEventQueueBase::Shutdown() {
    queue_->Shutdown();
}

void FlowEventQueueBase::Enqueue(FlowEvent *event) {
    if (CanEnqueue(event) == false) {
        delete event;
        return;
    }
    queue_->Enqueue(event);
}

bool FlowEventQueueBase::TokenCheck() {
    return flow_proto_->TokenCheck(token_pool_);
}

bool FlowEventQueueBase::TaskEntry() {
    count_ = 0;
    task_start_ = ClockMonotonicUsec();
    getrusage(RUSAGE_THREAD, &rusage_);
    return true;
}

void FlowEventQueueBase::TaskExit(bool done) {
    if (task_start_ == 0)
        return;

    uint64_t t = ClockMonotonicUsec();
    if (((t - task_start_) / 1000) >= latency_limit_) {
        struct rusage r;
        getrusage(RUSAGE_THREAD, &r);

        uint32_t user = (r.ru_utime.tv_sec - rusage_.ru_utime.tv_sec) * 1000;
        user += ((r.ru_utime.tv_usec - rusage_.ru_utime.tv_usec) / 1000);

        uint32_t sys = (r.ru_stime.tv_sec - rusage_.ru_stime.tv_sec) * 1000;
        sys += ((r.ru_stime.tv_usec - rusage_.ru_stime.tv_usec) / 1000);

        LOG(ERROR, queue_->Description() 
            << " Time exceeded " << ((t - task_start_) / 1000)
            << " Count " << count_
            << " User " << user << " Sys " << sys);
    }
    return;
}

bool FlowEventQueueBase::Handler(FlowEvent *event) {
    std::auto_ptr<FlowEvent> event_ptr(event);
    count_++;
    if (CanProcess(event) == false) {
        ProcessDone(event, false);
        return true;
    }

    HandleEvent(event);

    ProcessDone(event, true);
    return true;
}

bool FlowEventQueueBase::CanEnqueue(FlowEvent *event) {
    FlowEntry *flow = event->flow();
    bool ret = true;
    switch (event->event()) {

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::DELETE_FLOW: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        ret = flow->GetPendingAction()->SetDelete();
        break;
    }

    // lock already token for the flow
    case FlowEvent::FLOW_MESSAGE: {
        ret = flow->GetPendingAction()->SetRecompute();
        break;
    }

    case FlowEvent::RECOMPUTE_FLOW: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        ret = flow->GetPendingAction()->SetRecomputeDBEntry();
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        ret = flow->GetPendingAction()->SetRevaluate();
        break;
    }

    default:
        break;
    }

    return ret;
}

bool FlowEventQueueBase::CanProcess(FlowEvent *event) {
    FlowEntry *flow = event->flow();
    bool ret = true;
    switch (event->event()) {

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::DELETE_FLOW: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        events_processed_++;
        ret = flow->GetPendingAction()->CanDelete();
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        events_processed_++;
        ret = flow->GetPendingAction()->CanRecompute();
        break;
    }

    case FlowEvent::RECOMPUTE_FLOW: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        events_processed_++;
        ret = flow->GetPendingAction()->CanRecomputeDBEntry();
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        events_processed_++;
        tbb::mutex::scoped_lock mutext(flow->mutex());
        ret = flow->GetPendingAction()->CanRevaluate();
        break;
    }

    default:
        break;
    }

    return ret;
}

void FlowEventQueueBase::ProcessDone(FlowEvent *event, bool update_rev_flow) {
    FlowEntry *flow = event->flow();
    FlowEntry *rflow = NULL;
    if (flow && update_rev_flow)
        rflow = flow->reverse_flow_entry();

    switch (event->event()) {

    case FlowEvent::DELETE_DBENTRY:
    case FlowEvent::DELETE_FLOW: {
        FLOW_LOCK(flow, rflow, event->event());
        flow->GetPendingAction()->ResetDelete();
        if (rflow)
            rflow->GetPendingAction()->ResetDelete();
        break;
    }

    case FlowEvent::FLOW_MESSAGE: {
        FLOW_LOCK(flow, rflow, event->event());
        flow->GetPendingAction()->ResetRecompute();
        if (rflow)
            rflow->GetPendingAction()->ResetRecompute();
        break;
    }

    case FlowEvent::RECOMPUTE_FLOW: {
        tbb::mutex::scoped_lock mutext(flow->mutex());
        flow->GetPendingAction()->ResetRecomputeDBEntry();
        break;
    }

    case FlowEvent::REVALUATE_DBENTRY: {
        FLOW_LOCK(flow, rflow, event->event());
        flow->GetPendingAction()->ResetRevaluate();
        if (rflow)
            rflow->GetPendingAction()->ResetRevaluate();
        break;
    }

    default:
        break;
    }

    return;
}

FlowEventQueue::FlowEventQueue(Agent *agent, FlowProto *proto,
                               FlowTable *table, FlowTokenPool *pool,
                               uint16_t latency_limit,
                               uint32_t max_iterations) :
    FlowEventQueueBase(proto, "Flow Event Queue",
                       agent->task_scheduler()->GetTaskId(kTaskFlowEvent),
                       table->table_index(), pool, latency_limit,
                       max_iterations),
    flow_table_(table) {
}

FlowEventQueue::~FlowEventQueue() {
}

bool FlowEventQueue::HandleEvent(FlowEvent *event) {
    return flow_proto_->FlowEventHandler(event, flow_table_);
}

DeleteFlowEventQueue::DeleteFlowEventQueue(Agent *agent, FlowProto *proto,
                                           FlowTable *table,
                                           FlowTokenPool *pool,
                                           uint16_t latency_limit,
                                           uint32_t max_iterations) :
    FlowEventQueueBase(proto, "Flow Delete Queue",
                       agent->task_scheduler()->GetTaskId(kTaskFlowDelete),
                       table->table_index(), pool, latency_limit,
                       max_iterations),
    flow_table_(table) {
}

DeleteFlowEventQueue::~DeleteFlowEventQueue() {
}

bool DeleteFlowEventQueue::HandleEvent(FlowEvent *event) {
    return flow_proto_->FlowDeleteHandler(event, flow_table_);
}

KSyncFlowEventQueue::KSyncFlowEventQueue(Agent *agent, FlowProto *proto,
                                         FlowTable *table,
                                         FlowTokenPool *pool,
                                         uint16_t latency_limit,
                                         uint32_t max_iterations) :
    FlowEventQueueBase(proto, "Flow KSync Queue",
                       agent->task_scheduler()->GetTaskId(kTaskFlowKSync),
                       table->table_index(), pool, latency_limit,
                       max_iterations),
    flow_table_(table) {
}

KSyncFlowEventQueue::~KSyncFlowEventQueue() {
}

bool KSyncFlowEventQueue::HandleEvent(FlowEvent *event) {
    return flow_proto_->FlowKSyncMsgHandler(event, flow_table_);
}

UpdateFlowEventQueue::UpdateFlowEventQueue(Agent *agent, FlowProto *proto,
                                           FlowTokenPool *pool,
                                           uint16_t latency_limit,
                                           uint32_t max_iterations) :
    FlowEventQueueBase(proto, "Flow Update Queue",
                       agent->task_scheduler()->GetTaskId(kTaskFlowUpdate), 0,
                       pool, latency_limit, max_iterations) {
}

UpdateFlowEventQueue::~UpdateFlowEventQueue() {
}

bool UpdateFlowEventQueue::HandleEvent(FlowEvent *event) {
    return flow_proto_->FlowUpdateHandler(event);
}
