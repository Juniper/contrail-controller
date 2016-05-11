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
    latency_limit_(latency_limit) {
    queue_ = new Queue(task_id, task_instance,
                       boost::bind(&FlowEventQueueBase::Handler, this, _1),
                       Queue::kMaxSize, Queue::kMaxIterations);
    char buff[100];
    sprintf(buff, "%s-%d", name.c_str(), task_instance);
    queue_->set_name(buff);
    queue_->SetStartRunnerFunc(boost::bind(&FlowEventQueueBase::TokenCheck,
                                           this));
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

        uint32_t user = (r.ru_utime.tv_sec - rusage_.ru_utime.tv_sec) * 100;
        user += (r.ru_utime.tv_usec - rusage_.ru_utime.tv_usec);

        uint32_t sys = (r.ru_stime.tv_sec - rusage_.ru_stime.tv_sec) * 100;
        sys += (r.ru_stime.tv_usec - rusage_.ru_stime.tv_usec);

        LOG(ERROR, queue_->Description() 
            << " Time exceeded " << ((t - task_start_) / 1000)
            << " Count " << count_
            << " User " << user << " Sys " << sys);
    }
    return;
}

bool FlowEventQueueBase::Handler(FlowEvent *event) {
    count_++;
    return HandleEvent(event);
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
                       agent->task_scheduler()->GetTaskId(kTaskFlowEvent),
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
                       agent->task_scheduler()->GetTaskId(kTaskFlowEvent),
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
