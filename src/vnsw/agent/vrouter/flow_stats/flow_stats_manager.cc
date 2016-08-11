/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>

#include <cmn/agent_cmn.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <uve/agent_uve.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <uve/interface_uve_stats_table.h>
#include <algorithm>
#include <pkt/flow_proto.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_types.h>
#include <oper/global_vrouter.h>
#include <init/agent_param.h>

SandeshTraceBufferPtr FlowExportStatsTraceBuf(SandeshTraceBufferCreate(
    "FlowExportStats", 3000));
const uint8_t FlowStatsManager::kCatchAllProto;

void FlowStatsManager::UpdateThreshold(uint32_t new_value) {
    if (new_value < kMinFlowSamplingThreshold) {
        threshold_ = kMinFlowSamplingThreshold;
    } else {
        threshold_ = new_value;
    }
}

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        FlowStatsManager *fam = Agent::GetInstance()->flow_stats_manager();
        FlowStatsCollector *fsc = fam->default_flow_stats_collector();
        if (fsc) {
            fsc->set_expiry_time(get_interval() * 1000);
        }
        resp = new FlowStatsCfgResp();
    } else {
        resp = new FlowStatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void GetFlowStatsInterval::HandleRequest() const {
    FlowStatsIntervalResp_InSeconds *resp =
        new FlowStatsIntervalResp_InSeconds();
    resp->set_flow_stats_interval((Agent::GetInstance()->flow_stats_manager()->
                default_flow_stats_collector()->expiry_time())/1000);

    resp->set_context(context());
    resp->Response();
    return;
}

FlowStatsManager::FlowStatsManager(Agent *agent) : agent_(agent),
    request_queue_(agent_->task_scheduler()->GetTaskId("Agent::FlowStatsManager"),
                   StatsCollector::FlowStatsCollector,
                   boost::bind(&FlowStatsManager::RequestHandler, this, _1)),
    flow_export_count_(), prev_flow_export_rate_compute_time_(0),
    flow_export_rate_(0), threshold_(kDefaultFlowSamplingThreshold),
    flow_export_disable_drops_(), flow_export_sampling_drops_(),
    flow_export_drops_(), prev_cfg_flow_export_rate_(0),
    timer_(TimerManager::CreateTimer(*(agent_->event_manager())->io_service(),
           "FlowThresholdTimer",
           TaskScheduler::GetInstance()->GetTaskId("Agent::FlowStatsManager"), 0)),
    delete_short_flow_(true) {
    flow_export_count_ = 0;
    flow_export_disable_drops_ = 0;
    flow_export_sampling_drops_ = 0;
    flow_export_drops_ = 0;
    flows_sampled_atleast_once_ = false;
    request_queue_.set_measure_busy_time(agent->MeasureQueueDelay());
}

FlowStatsManager::~FlowStatsManager() {
    assert(flow_aging_table_map_.size() == 0);
}

bool FlowStatsManager::RequestHandler(boost::shared_ptr<FlowStatsCollectorReq>
                                      req) {
    switch (req->event) {
    case FlowStatsCollectorReq::ADD_FLOW_STATS_COLLECTOR: {
        AddReqHandler(req);
        break;
    }

    case FlowStatsCollectorReq::DELETE_FLOW_STATS_COLLECTOR: {
        DeleteReqHandler(req);
        break;
    }

    case FlowStatsCollectorReq::FREE_FLOW_STATS_COLLECTOR: {
        FreeReqHandler(req);
        break;
    }

    default: {
        assert(0);
        break;
    }
    }
    return true;
}

void FlowStatsManager::AddReqHandler(boost::shared_ptr<FlowStatsCollectorReq>
                                    req) {
    FlowAgingTableMap::iterator it = flow_aging_table_map_.find(req->key);
    if (it != flow_aging_table_map_.end()) {
        it->second->set_flow_age_time_intvl(
                1000000L * (uint64_t)req->flow_cache_timeout);
        it->second->set_deleted(false);
        return;
    }

    uint32_t instance_id = instance_table_.Insert(NULL);
    FlowAgingTablePtr aging_table(
        AgentObjectFactory::Create<FlowStatsCollector>(
        *(agent()->event_manager()->io_service()),
        req->flow_stats_interval, req->flow_cache_timeout,
        agent()->uve(), instance_id, &(req->key), this));

    flow_aging_table_map_.insert(FlowAgingTableEntry(req->key, aging_table));
    if (req->key.proto == kCatchAllProto && req->key.port == 0) {
        default_flow_stats_collector_ = aging_table;
    }

    if (req->key.port == 0) {
        protocol_list_[req->key.proto] = aging_table.get();
    }
}

void FlowStatsManager::DeleteReqHandler(boost::shared_ptr<FlowStatsCollectorReq>
                                       req) {
    FlowAgingTableMap::iterator it = flow_aging_table_map_.find(req->key);
    if (it == flow_aging_table_map_.end()) {
        return;
    }

    FlowAgingTablePtr flow_aging_table_ptr = it->second;
    flow_aging_table_ptr->set_deleted(true);

    if (flow_aging_table_ptr->flow_tree_.size() == 0 &&
        flow_aging_table_ptr->request_queue_.IsQueueEmpty() == true) {
        flow_aging_table_map_.erase(it);
        protocol_list_[req->key.proto] = NULL;
    }
}

void FlowStatsManager::FreeReqHandler(boost::shared_ptr<FlowStatsCollectorReq>
                                     req) {
    FlowAgingTableMap::iterator it = flow_aging_table_map_.find(req->key);
    if (it == flow_aging_table_map_.end()) {
        return;
    }

    FlowAgingTablePtr flow_aging_table_ptr = it->second;
    if (flow_aging_table_ptr->deleted() == false) {
        return;
    }
    assert(flow_aging_table_ptr->flow_tree_.size() == 0);
    assert(flow_aging_table_ptr->request_queue_.IsQueueEmpty() == true);
    flow_aging_table_ptr->Shutdown();
    flow_aging_table_map_.erase(it);
    protocol_list_[req->key.proto] = NULL;
}

void FlowStatsManager::Add(const FlowAgingTableKey &key,
                          uint64_t flow_stats_interval,
                          uint64_t flow_cache_timeout) {
    boost::shared_ptr<FlowStatsCollectorReq>
        req(new FlowStatsCollectorReq(
                    FlowStatsCollectorReq::ADD_FLOW_STATS_COLLECTOR,
                    key, flow_stats_interval, flow_cache_timeout));
    request_queue_.Enqueue(req);
}

void FlowStatsManager::Delete(const FlowAgingTableKey &key) {
    if (key.proto == kCatchAllProto) {
        return;
    }
    boost::shared_ptr<FlowStatsCollectorReq>
        req(new FlowStatsCollectorReq(
                    FlowStatsCollectorReq::DELETE_FLOW_STATS_COLLECTOR,
                    key));
    request_queue_.Enqueue(req);
}

void FlowStatsManager::Free(const FlowAgingTableKey &key) {
    boost::shared_ptr<FlowStatsCollectorReq>
        req(new FlowStatsCollectorReq(
                    FlowStatsCollectorReq::FREE_FLOW_STATS_COLLECTOR,
                    key));
    request_queue_.Enqueue(req);
}

const FlowStatsCollector*
FlowStatsManager::Find(uint32_t proto, uint32_t port) const {

     FlowAgingTableKey key1(proto, port);
     FlowAgingTableMap::const_iterator key1_it = flow_aging_table_map_.find(key1);

     if (key1_it == flow_aging_table_map_.end()){
         return NULL;
     }

     return key1_it->second.get();
}

FlowStatsCollector*
FlowStatsManager::GetFlowStatsCollector(const FlowEntry *flow) const {
    /* If the reverse flow already has FlowStatsCollector assigned, return
     * the same to ensure that forward and reverse flows go to same
     * FlowStatsCollector */
    const FlowEntry *rflow = flow->reverse_flow_entry();
    if (rflow && rflow->fsc()) {
        return rflow->fsc();
    }
    FlowStatsCollector* col = NULL;

    const FlowKey &key = flow->key();
    FlowAgingTableKey key1(key.protocol, key.src_port);
    FlowAgingTableMap::const_iterator key1_it =
        flow_aging_table_map_.find(key1);

    if (key1_it != flow_aging_table_map_.end()) {
        col = key1_it->second.get();
        if (!col->deleted())
            return col;
    }

    FlowAgingTableKey key2(key.protocol, key.dst_port);
    FlowAgingTableMap::const_iterator key2_it =
        flow_aging_table_map_.find(key2);
    if (key2_it != flow_aging_table_map_.end()) {
        col = key2_it->second.get();
        if (!col->deleted())
            return col;
    }

    if (protocol_list_[key.protocol] != NULL) {
        col = protocol_list_[key.protocol];
        if (!col->deleted())
            return col;
    }
    return default_flow_stats_collector_.get();
}

void FlowStatsManager::AddEvent(FlowEntryPtr &flow) {
    if (flow == NULL) {
        return;
    }

    FlowStatsCollector *fsc = NULL;
    if (flow->fsc() == NULL) {
        fsc = GetFlowStatsCollector(flow.get());
        flow->set_fsc(fsc);
    } else {
        fsc = flow->fsc();
    }

    fsc->AddEvent(flow);
}

void FlowStatsManager::DeleteEvent(const FlowEntryPtr &flow,
                                   const RevFlowDepParams &params) {
    if (flow == NULL) {
        return;
    }
    FlowStatsCollector *fsc = flow->fsc();
    /* Ignore delete requests if FlowStatsCollector is NULL */
    if (fsc != NULL) {
        fsc->DeleteEvent(flow, params);
        flow->set_fsc(NULL);
    }
}

void FlowStatsManager::UpdateStatsEvent(const FlowEntryPtr &flow,
                                        uint32_t bytes, uint32_t packets,
                                        uint32_t oflow_bytes) {
    if (flow == NULL) {
        return;
    }

    FlowStatsCollector *fsc = flow->fsc();
    if (fsc == NULL) {
        /* Ignore stats update request, if the flow does not have any
         * FlowStatsCollector associated with it */
        return;
    }

    fsc->UpdateStatsEvent(flow, bytes, packets, oflow_bytes);
}

void FlowStatsManager::FreeIndex(uint32_t idx) {
    instance_table_.Remove(idx);
}

void FlowStatsManager::FlowStatsReqHandler(Agent *agent,
        uint32_t protocol, uint32_t port, uint64_t timeout) {
    if (timeout == 0) {
        agent->flow_stats_manager()->Delete(
                FlowAgingTableKey(protocol, port));
    } else {
        agent->flow_stats_manager()->Add(
                FlowAgingTableKey(protocol, port), 
                agent->params()->flow_stats_interval(), timeout);
    }
}

void FlowStatsManager::Init(uint64_t flow_stats_interval,
                           uint64_t flow_cache_timeout) {
    Add(FlowAgingTableKey(kCatchAllProto, 0),
        flow_stats_interval, flow_cache_timeout);
    if (agent_->tsn_enabled()) {
        /* In TSN mode, we don't support add/delete of FlowStatsCollector
         * (so we don't invoke set_flow_stats_req_handler)
         * Also, we don't export flows, so we don't start UpdateFlowThreshold
         * timer */
        return;
    }
    agent_->set_flow_stats_req_handler(&(FlowStatsManager::FlowStatsReqHandler));
    timer_->Start(FlowThresoldUpdateTime,
                  boost::bind(&FlowStatsManager::UpdateFlowThreshold, this));
}

void FlowStatsManager::InitDone() {
    AgentProfile *profile = agent_->oper_db()->agent_profile();
    profile->RegisterFlowStatsCb(boost::bind(&FlowStatsManager::SetProfileData,
                                             this, _1));
}

void FlowStatsManager::Shutdown() {
    default_flow_stats_collector_->Shutdown();
    default_flow_stats_collector_.reset();
    flow_aging_table_map_.clear();
    timer_->Cancel();

}

void ShowAgingConfig::HandleRequest() const {
    SandeshResponse *resp;

    FlowStatsManager *fam = Agent::GetInstance()->flow_stats_manager();
    resp = new AgingConfigResponse();

    FlowStatsManager::FlowAgingTableMap::const_iterator it = fam->begin();
    while (it != fam->end()) {
        AgingConfig cfg;
        cfg.set_protocol(it->first.proto);
        cfg.set_port(it->first.port);
        cfg.set_cache_timeout(it->second->flow_age_time_intvl_in_secs());
        cfg.set_stats_interval(0);
        std::vector<AgingConfig> &list =
            const_cast<std::vector<AgingConfig>&>(
                    ((AgingConfigResponse *)resp)->get_aging_config_list());
        list.push_back(cfg);
        it++;
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void AddAgingConfig::HandleRequest() const {
    FlowStatsManager *fam = Agent::GetInstance()->flow_stats_manager();
    fam->Add(FlowAgingTableKey(get_protocol(), get_port()),
                               get_stats_interval(), get_cache_timeout());
    SandeshResponse *resp = new FlowStatsCfgResp();
    resp->set_context(context());
    resp->Response();
    return;
}

void DeleteAgingConfig::HandleRequest() const {
    FlowStatsManager *fam = Agent::GetInstance()->flow_stats_manager();
    fam->Delete(FlowAgingTableKey(get_protocol(), get_port()));

    SandeshResponse *resp = new FlowStatsCfgResp();
    resp->set_context(context());
    resp->Response();
    return;
}

static void SetQueueStats(Agent *agent, FlowStatsCollector *fsc,
                          ProfileData::WorkQueueStats *stats) {
    stats->name_ = fsc->queue()->Description();
    stats->queue_count_ = fsc->queue()->Length();
    stats->enqueue_count_ = fsc->queue()->NumEnqueues();
    stats->dequeue_count_ = fsc->queue()->NumDequeues();
    stats->max_queue_count_ = fsc->queue()->max_queue_len();
    stats->start_count_ = fsc->queue()->task_starts();
    stats->busy_time_ = fsc->queue()->busy_time();
    fsc->queue()->set_measure_busy_time(agent->MeasureQueueDelay());
    if (agent->MeasureQueueDelay())
        fsc->queue()->ClearStats();
}

void FlowStatsManager::SetProfileData(ProfileData *data) {
    data->flow_.flow_stats_queue_.resize(flow_aging_table_map_.size());
    int i = 0;
    FlowAgingTableMap::iterator it = flow_aging_table_map_.begin();
    while (it != flow_aging_table_map_.end()) {
        SetQueueStats(agent(), it->second.get(),
                      &data->flow_.flow_stats_queue_[i]);
        i++;
        it++;
    }
}

void FlowStatsManager::UpdateFlowExportStats(uint32_t count,
                                             bool sampled_flow) {
    flow_export_count_ += count;
    if (!sampled_flow) {
        flow_export_without_sampling_ += count;
    }
}
