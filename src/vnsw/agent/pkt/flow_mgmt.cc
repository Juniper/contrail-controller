#include <bitset>
#include <boost/uuid/uuid_io.hpp>
#include "cmn/agent.h"
#include "controller/controller_init.h"
#include "oper/bgp_as_service.h"
#include "oper/health_check.h"
#include "pkt/flow_proto.h"
#include <pkt/flow_mgmt.h>
#include <pkt/flow_mgmt/flow_mgmt_entry.h>
#include <pkt/flow_mgmt/flow_entry_info.h>
#include <pkt/flow_mgmt/flow_mgmt_request.h>
#include <pkt/flow_mgmt/flow_mgmt_dbclient.h>
#include "uve/flow_uve_stats_request.h"
#include "uve/agent_uve_stats.h"
#include "vrouter/flow_stats/flow_stats_collector.h"

FlowMgmtManager::FlowMgmtQueue *FlowMgmtManager::log_queue_;
/////////////////////////////////////////////////////////////////////////////
// FlowMgmtManager methods
/////////////////////////////////////////////////////////////////////////////
FlowMgmtManager::FlowMgmtManager(Agent *agent, uint16_t table_index) :
    agent_(agent),
    table_index_(table_index),
    acl_flow_mgmt_tree_(this),
    interface_flow_mgmt_tree_(this),
    vn_flow_mgmt_tree_(this),
    ip4_route_flow_mgmt_tree_(this),
    ip6_route_flow_mgmt_tree_(this),
    bridge_route_flow_mgmt_tree_(this),
    vrf_flow_mgmt_tree_(this),
    nh_flow_mgmt_tree_(this),
    flow_mgmt_dbclient_(new FlowMgmtDbClient(agent, this)),
    request_queue_(agent_->task_scheduler()->GetTaskId(kTaskFlowMgmt),
                   table_index,
                   boost::bind(&FlowMgmtManager::RequestHandler, this, _1)),
    db_event_queue_(agent_->task_scheduler()->GetTaskId(kTaskFlowMgmt),
                    table_index,
                    boost::bind(&FlowMgmtManager::DBRequestHandler, this, _1),
                    db_event_queue_.kMaxSize, 1) {
    request_queue_.set_name("Flow management");
    request_queue_.set_measure_busy_time(agent->MeasureQueueDelay());
    db_event_queue_.set_name("Flow DB Event Queue");
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        bgp_as_a_service_flow_mgmt_tree_[count].reset(
            new BgpAsAServiceFlowMgmtTree(this, count));
    }
}

void FlowMgmtManager::Init() {
    flow_mgmt_dbclient_->Init();
    agent_->acl_table()->set_ace_flow_sandesh_data_cb
        (boost::bind(&FlowMgmtManager::SetAceSandeshData, this, _1, _2, _3));
    agent_->acl_table()->set_acl_flow_sandesh_data_cb
        (boost::bind(&FlowMgmtManager::SetAclFlowSandeshData, this, _1, _2,
                     _3));
    // If BGP service is deleted then flush off all the flows for the VMI.
    agent_->oper_db()->bgp_as_a_service()->RegisterServiceDeleteCb(boost::bind
                       (&FlowMgmtManager::BgpAsAServiceNotify, this, _1, _2));
    // If BGP service health check configuration is modified,
    // update the corresponding flows
    agent_->oper_db()->bgp_as_a_service()->RegisterHealthCheckCb(boost::bind
                       (&FlowMgmtManager::BgpAsAServiceHealthCheckNotify, this,
                        _1, _2, _3, _4));
    // If control node goes off delete all flows frmo its tree.
    agent_->controller()->RegisterControllerChangeCallback(boost::bind
                          (&FlowMgmtManager::ControllerNotify, this, _1));
}

void FlowMgmtManager::Shutdown() {
    request_queue_.Shutdown();
    db_event_queue_.Shutdown();
    flow_mgmt_dbclient_->Shutdown();
}

void FlowMgmtManager::InitLogQueue(Agent *agent) {
    uint32_t task_id = agent->task_scheduler()->GetTaskId(kTaskFlowLogging);
    log_queue_ = new FlowMgmtQueue(task_id, 0,
                                   boost::bind(&FlowMgmtManager::LogHandler,
                                               _1));
    log_queue_->set_name("Flow Log Queue");
    log_queue_->SetBounded(true);
}

void FlowMgmtManager::ShutdownLogQueue() {
    log_queue_->Shutdown();
    delete log_queue_;
}

/////////////////////////////////////////////////////////////////////////////
// BGP as a service callbacks
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtManager::BgpAsAServiceNotify(const boost::uuids::uuid &vm_uuid,
                                          uint32_t source_port) {
    FlowMgmtRequestPtr req(new BgpAsAServiceFlowMgmtRequest(vm_uuid,
                                                            source_port));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::BgpAsAServiceHealthCheckNotify(
                      const boost::uuids::uuid &vm_uuid, uint32_t source_port,
                      const boost::uuids::uuid &hc_uuid, bool add) {
    BgpAsAServiceFlowMgmtRequest::Type type = add ?
                        BgpAsAServiceFlowMgmtRequest::HEALTH_CHECK_ADD :
                        BgpAsAServiceFlowMgmtRequest::HEALTH_CHECK_DEL;
    FlowMgmtRequestPtr req(new BgpAsAServiceFlowMgmtRequest(vm_uuid,
                                                            source_port,
                                                            hc_uuid, type));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::ControllerNotify(uint8_t index) {
    FlowMgmtRequestPtr req(new BgpAsAServiceFlowMgmtRequest(index));
    request_queue_.Enqueue(req);
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtManager::SetAceSandeshData(const AclDBEntry *acl,
                                        AclFlowCountResp &data,
                                        const std::string &ace_id) {
    AclFlowMgmtKey key(acl, NULL);
    AclFlowMgmtEntry *entry = static_cast<AclFlowMgmtEntry *>
        (acl_flow_mgmt_tree_.Find(&key));
    if (entry == NULL) {
        return;
    }
    entry->FillAceFlowSandeshInfo(acl, data, ace_id);

}

void FlowMgmtManager::SetAclFlowSandeshData(const AclDBEntry *acl,
                                            AclFlowResp &data,
                                            const int last_count) {
    AclFlowMgmtKey key(acl, NULL);
    AclFlowMgmtEntry *entry = static_cast<AclFlowMgmtEntry *>
        (acl_flow_mgmt_tree_.Find(&key));
    if (entry == NULL) {
        return;
    }
    entry->FillAclFlowSandeshInfo(acl, data, last_count, agent_);
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtManager::AddEvent(FlowEntry *flow) {
    // Check if there is a flow-mgmt request already pending
    // Flow mgmt takes care of current state of flow. So, there is no need to
    // enqueue duplicate requests
    FlowMgmtRequest *req = flow->flow_mgmt_request();
    if (req == NULL) {
        req = new FlowMgmtRequest(FlowMgmtRequest::UPDATE_FLOW, flow);
        flow->set_flow_mgmt_request(req);
        request_queue_.Enqueue(FlowMgmtRequestPtr(req));
    }
}

void FlowMgmtManager::DeleteEvent(FlowEntry *flow,
                                  const RevFlowDepParams &params) {
    // Check if there is a flow-mgmt request already pending
    // Flow mgmt takes care of current state of flow. So, there is no need to
    // enqueue duplicate requests
    FlowMgmtRequest *req = flow->flow_mgmt_request();
    if (req == NULL) {
        req = new FlowMgmtRequest(FlowMgmtRequest::UPDATE_FLOW, flow);
        flow->set_flow_mgmt_request(req);
        request_queue_.Enqueue(FlowMgmtRequestPtr(req));
    }

    req->set_params(params);
}

void FlowMgmtManager::FlowStatsUpdateEvent(FlowEntry *flow, uint32_t bytes,
                                           uint32_t packets,
                                           uint32_t oflow_bytes,
                                           const boost::uuids::uuid &u) {
    if (bytes == 0 && packets == 0 && oflow_bytes == 0) {
        return;
    }

    /* Ignore StatsUpdate request in TSN mode as we don't export flows */
    if (agent_->tsn_enabled()) {
        return;
    }
    FlowMgmtRequestPtr req(new FlowMgmtRequest
                           (FlowMgmtRequest::UPDATE_FLOW_STATS, flow,
                            bytes, packets, oflow_bytes, u));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::RetryVrfDeleteEvent(const VrfEntry *vrf) {
    FlowMgmtRequestPtr req(new FlowMgmtRequest
                           (FlowMgmtRequest::RETRY_DELETE_VRF, vrf, 0));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::DummyEvent() {
    FlowMgmtRequestPtr req(new FlowMgmtRequest(FlowMgmtRequest::DUMMY));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::AddDBEntryEvent(const DBEntry *entry, uint32_t gen_id) {
    FlowMgmtRequestPtr req(new FlowMgmtRequest(FlowMgmtRequest::ADD_DBENTRY,
                                               entry, gen_id));
    db_event_queue_.Enqueue(req);
}

void FlowMgmtManager::ChangeDBEntryEvent(const DBEntry *entry,
                                         uint32_t gen_id) {
    FlowMgmtRequestPtr req(new FlowMgmtRequest(FlowMgmtRequest::CHANGE_DBENTRY,
                                               entry, gen_id));
    db_event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteDBEntryEvent(const DBEntry *entry,
                                         uint32_t gen_id) {
    FlowMgmtRequestPtr req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_DBENTRY,
                                               entry, gen_id));
    db_event_queue_.Enqueue(req);
}

void FlowMgmtManager::RouteNHChangeEvent(const DBEntry *entry,
                                         uint32_t gen_id) {
    FlowMgmtRequestPtr req(new FlowMgmtRequest
                               (FlowMgmtRequest::DELETE_LAYER2_FLOW,
                                entry, gen_id));
    db_event_queue_.Enqueue(req);
}

void FlowMgmtManager::EnqueueFlowEvent(FlowEvent *event) {
    agent_->pkt()->get_flow_proto()->EnqueueFlowEvent(event);
}

void FlowMgmtManager::NonOperEntryEvent(FlowEvent::Event event,
                                        FlowEntry *flow) {
    FlowEvent *flow_resp = new FlowEvent(event, flow->key(), true,
                                         FlowTable::kPortNatFlowTableInstance);
    flow_resp->set_flow(flow);
    EnqueueFlowEvent(flow_resp);
}

void FlowMgmtManager::DBEntryEvent(FlowEvent::Event event, FlowMgmtKey *key,
                                   FlowEntry *flow) {
    FlowEvent *flow_resp = new FlowEvent(event, NULL, key->db_entry());
    key->KeyToFlowRequest(flow_resp);
    flow_resp->set_flow(flow);
    EnqueueFlowEvent(flow_resp);
}

void FlowMgmtManager::FreeDBEntryEvent(FlowEvent::Event event, FlowMgmtKey *key,
                                       uint32_t gen_id) {
    FlowEvent *flow_resp = new FlowEvent(event, table_index_, key->db_entry(),
                                         gen_id);
    EnqueueFlowEvent(flow_resp);
}

void FlowMgmtManager::FlowUpdateQueueDisable(bool disabled) {
    request_queue_.set_disable(disabled);
    db_event_queue_.set_disable(disabled);
}

size_t FlowMgmtManager::FlowUpdateQueueLength() {
    return request_queue_.Length();
}

size_t FlowMgmtManager::FlowDBQueueLength() {
    return db_event_queue_.Length();
}
/////////////////////////////////////////////////////////////////////////////
// Handlers for events from the work-queue
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtManager::ProcessEvent(FlowMgmtRequest *req, FlowMgmtKey *key,
                                   FlowMgmtTree *tree) {
    InetRouteFlowMgmtTree* itree = dynamic_cast<InetRouteFlowMgmtTree*>(tree);
    switch (req->event()) {
    case FlowMgmtRequest::ADD_DBENTRY:
        tree->OperEntryAdd(req, key);
        break;

    case FlowMgmtRequest::CHANGE_DBENTRY:
        tree->OperEntryChange(req, key);
        break;

    case FlowMgmtRequest::DELETE_DBENTRY:
    case FlowMgmtRequest::IMPLICIT_ROUTE_DELETE:
        tree->OperEntryDelete(req, key);
        break;

    case FlowMgmtRequest::DELETE_LAYER2_FLOW:
        assert(itree);
        itree->RouteNHChangeEvent(req, key);
        break;

    default:
        assert(0);
        break;
    }

    return true;
}

bool FlowMgmtManager::DBRequestHandler(FlowMgmtRequest *req,
                                       const DBEntry *entry) {
    const Interface *intf = dynamic_cast<const Interface *>(entry);
    if (intf) {
        InterfaceFlowMgmtKey key(intf);
        return ProcessEvent(req, &key, &interface_flow_mgmt_tree_);
    }

    const VnEntry *vn = dynamic_cast<const VnEntry *>(entry);
    if (vn) {
        VnFlowMgmtKey key(vn);
        return ProcessEvent(req, &key, &vn_flow_mgmt_tree_);
    }

    const AclDBEntry *acl = dynamic_cast<const AclDBEntry *>(entry);
    if (acl) {
        AclFlowMgmtKey key(acl, NULL);
        return ProcessEvent(req, &key, &acl_flow_mgmt_tree_);
    }

    const NextHop *nh = dynamic_cast<const NextHop *>(entry);
    if (nh) {
        NhFlowMgmtKey key(static_cast<const NextHop *>(req->db_entry()));
        return ProcessEvent(req, &key, &nh_flow_mgmt_tree_);
    }

    const InetUnicastRouteEntry *inet_uc_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(entry);
    if (inet_uc_rt) {
        InetRouteFlowMgmtKey key(inet_uc_rt);
        if (inet_uc_rt->addr().is_v4()) {
            return ProcessEvent(req, &key, &ip4_route_flow_mgmt_tree_);
        }
        if (inet_uc_rt->addr().is_v6()) {
            return ProcessEvent(req, &key, &ip6_route_flow_mgmt_tree_);
        }
    }

    const BridgeRouteEntry *bridge =
        dynamic_cast<const BridgeRouteEntry *>(entry);
    if (bridge) {
        BridgeRouteFlowMgmtKey key(bridge);
        return ProcessEvent(req, &key, &bridge_route_flow_mgmt_tree_);
    }

    const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(entry);
    if (vrf) {
        VrfFlowMgmtKey key(vrf);
        return ProcessEvent(req, &key, &vrf_flow_mgmt_tree_);
    }

    assert(0);
    return true;
}

bool
FlowMgmtManager::BgpAsAServiceRequestHandler(FlowMgmtRequest *req) {

    BgpAsAServiceFlowMgmtRequest *bgp_as_a_service_request =
        dynamic_cast<BgpAsAServiceFlowMgmtRequest *>(req);
    if (bgp_as_a_service_request->type() == BgpAsAServiceFlowMgmtRequest::VMI) {
        //Delete it for for all CN trees
        for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
            BgpAsAServiceFlowMgmtKey key(bgp_as_a_service_request->vm_uuid(),
                                         bgp_as_a_service_request->source_port(),
                                         count, NULL, NULL);
            bgp_as_a_service_flow_mgmt_tree_[count].get()->
                BgpAsAServiceDelete(key, req);
        }
    } else if (bgp_as_a_service_request->type() ==
               BgpAsAServiceFlowMgmtRequest::CONTROLLER) {
        bgp_as_a_service_flow_mgmt_tree_[bgp_as_a_service_request->index()].get()->
            DeleteAll();
    } else if (bgp_as_a_service_request->type() ==
               BgpAsAServiceFlowMgmtRequest::HEALTH_CHECK_ADD ||
               bgp_as_a_service_request->type() ==
               BgpAsAServiceFlowMgmtRequest::HEALTH_CHECK_DEL) {
        // Health check added to BGPaaS, check if any flows are impacted
        for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
            BgpAsAServiceFlowMgmtKey key(bgp_as_a_service_request->vm_uuid(),
                                         bgp_as_a_service_request->source_port(),
                                         count, NULL, NULL);
            bgp_as_a_service_flow_mgmt_tree_[count].get()->
                BgpAsAServiceHealthCheckUpdate(agent(), key, bgp_as_a_service_request);
        }
    }

    return true;
}

bool FlowMgmtManager::RequestHandler(FlowMgmtRequestPtr req) {
    switch (req->event()) {
    case FlowMgmtRequest::UPDATE_FLOW: {
        FlowEntry *flow = req->flow().get();
        // Before processing event, set the request pointer in flow to
        // NULL. This ensures flow-entry enqueues new request from now
        // onwards
        tbb::mutex::scoped_lock mutex(flow->mutex());
        flow->set_flow_mgmt_request(NULL);

        // Update flow-mgmt information based on flow-state
        if (flow->deleted() == false) {
            FlowMgmtRequestPtr log_req(new FlowMgmtRequest
                                       (FlowMgmtRequest::ADD_FLOW,
                                        req->flow().get()));
            log_queue_->Enqueue(log_req);

            //Enqueue Add request to flow-stats-collector
            agent_->flow_stats_manager()->AddEvent(req->flow());

            //Enqueue Add request to UVE module for ACE stats
            EnqueueUveAddEvent(flow);

            AddFlow(req->flow());

        } else {
            FlowMgmtRequestPtr log_req(new FlowMgmtRequest
                                       (FlowMgmtRequest::DELETE_FLOW,
                                        req->flow().get(), req->params()));
            log_queue_->Enqueue(log_req);

            //Enqueue Delete request to flow-stats-collector
            agent_->flow_stats_manager()->DeleteEvent(flow, req->params());

            //Enqueue Delete request to UVE module for ACE stats
            EnqueueUveDeleteEvent(flow);

            DeleteFlow(req->flow(), req->params());
        }
        break;
    }

    case FlowMgmtRequest::UPDATE_FLOW_STATS: {
        //Handle Flow stats update for flow-mgmt
        UpdateFlowStats(req->flow(), req->bytes(), req->packets(),
                        req->oflow_bytes(), req->flow_uuid());
        break;
    }

    case FlowMgmtRequest::RETRY_DELETE_VRF: {
        RetryVrfDelete(req->vrf_id());
        break;
    }

    case FlowMgmtRequest::DELETE_BGP_AAS_FLOWS: {
        BgpAsAServiceRequestHandler(req.get());
        break;
    }

    case FlowMgmtRequest::DUMMY:
        break;

    default:
         assert(0);

    }

    return true;
}

bool FlowMgmtManager::DBRequestHandler(FlowMgmtRequestPtr req) {
    switch (req->event()) {
    case FlowMgmtRequest::ADD_DBENTRY:
    case FlowMgmtRequest::CHANGE_DBENTRY:
    case FlowMgmtRequest::DELETE_DBENTRY:
    case FlowMgmtRequest::DELETE_LAYER2_FLOW: {
        DBRequestHandler(req.get(), req->db_entry());
        break;
    }

    default:
         assert(0);

    }

    return true;
}

bool FlowMgmtManager::LogHandler(FlowMgmtRequestPtr req) {
    FlowEntry *flow = req->flow().get();
    FlowEntry *rflow = flow->reverse_flow_entry();

    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);
    switch (req->event()) {
    case FlowMgmtRequest::ADD_FLOW: {
        LogFlowUnlocked(flow, "ADD");
        break;
    }

    case FlowMgmtRequest::DELETE_FLOW: {
        LogFlowUnlocked(flow, "DEL");
        break;
    }

    default:
         assert(0);

    }

    return true;
}

void FlowMgmtManager::RetryVrfDelete(uint32_t vrf_id) {
    vrf_flow_mgmt_tree_.RetryDelete(vrf_id);
}

// Extract all the FlowMgmtKey for a flow
void FlowMgmtManager::LogFlowUnlocked(FlowEntry *flow, const std::string &op) {
    if (flow->trace() == false)
        return;
    FlowInfo trace;
    flow->FillFlowInfo(trace);
    FLOW_TRACE(Trace, op, trace);
}

// Extract all the FlowMgmtKey for a flow
void FlowMgmtManager::MakeFlowMgmtKeyTree(FlowEntry *flow,
                                          FlowMgmtKeyTree *tree) {
    acl_flow_mgmt_tree_.ExtractKeys(flow, tree);
    interface_flow_mgmt_tree_.ExtractKeys(flow, tree);
    vn_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip4_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip6_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    bridge_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    nh_flow_mgmt_tree_.ExtractKeys(flow, tree);
    if (flow->is_flags_set(FlowEntry::BgpRouterService)) {
        int cn_index = BgpAsAServiceFlowMgmtTree::GetCNIndex(flow);
        if (cn_index != BgpAsAServiceFlowMgmtTree::kInvalidCnIndex) {
            bgp_as_a_service_flow_mgmt_tree_[cn_index].get()->
                ExtractKeys(flow, tree);
        }
    }
}

void FlowMgmtManager::EnqueueUveAddEvent(const FlowEntry *flow) const {
    AgentUveStats *uve = dynamic_cast<AgentUveStats *>(agent_->uve());
    if (uve) {
        const Interface *itf = flow->intf_entry();
        const VmInterface *vmi = dynamic_cast<const VmInterface *>(itf);
        const VnEntry *vn = flow->vn_entry();
        string vn_name = vn? vn->GetName() : "";
        string itf_name = vmi? vmi->cfg_name() : "";
        FlowUveVnAcePolicyInfo vn_ace_info;
        FlowUveFwPolicyInfo fw_policy_info;

        flow->FillUveVnAceInfo(&vn_ace_info);
        if (!itf_name.empty()) {
            flow->FillUveFwStatsInfo(&fw_policy_info, true);
        }
        boost::shared_ptr<FlowUveStatsRequest> req(new FlowUveStatsRequest
            (FlowUveStatsRequest::ADD_FLOW, flow->uuid(), itf_name,
             flow->sg_rule_uuid(), vn_ace_info, fw_policy_info));

        if (!req->sg_info_valid() && !req->vn_ace_valid() &&
            !req->fw_policy_valid()) {
            return;
        }

        uve->stats_manager()->EnqueueEvent(req);
    }
}

void FlowMgmtManager::EnqueueUveDeleteEvent(const FlowEntry *flow) const {
    AgentUveStats *uve = dynamic_cast<AgentUveStats *>(agent_->uve());
    if (uve) {
        const Interface *itf = flow->intf_entry();
        const VmInterface *vmi = dynamic_cast<const VmInterface *>(itf);
        string itf_name = vmi? vmi->cfg_name() : "";
        FlowUveFwPolicyInfo fw_policy_info;
        if (!itf_name.empty()) {
            flow->FillUveFwStatsInfo(&fw_policy_info, false);
        }
        boost::shared_ptr<FlowUveStatsRequest> req(new FlowUveStatsRequest
            (FlowUveStatsRequest::DELETE_FLOW, flow->uuid(), itf_name,
             fw_policy_info));
        uve->stats_manager()->EnqueueEvent(req);
    }
}

void FlowMgmtManager::AddFlow(FlowEntryPtr &flow) {
    FlowMgmtKeyTree new_tree;
    MakeFlowMgmtKeyTree(flow.get(), &new_tree);

    // Get old FlowMgmtKeyTree
    FlowEntryInfo *old_info = LocateFlowEntryInfo(flow);
    FlowMgmtKeyTree *old_tree = &old_info->tree_;
    assert(old_tree);
    old_info->count_++;

    // Apply the difference in old and new key tree
    FlowMgmtKeyTree::iterator new_it = new_tree.begin();
    FlowMgmtKeyTree::iterator old_it = old_tree->begin();

    while (new_it != new_tree.end() && old_it != old_tree->end()) {
        FlowMgmtKey *new_key = new_it->first;
        FlowMgmtKey *old_key = old_it->first;
        if (new_key->IsLess(old_key)) {
            AddFlowMgmtKey(flow.get(), old_info, new_key, NULL);
            new_it++;
        } else if (old_key->IsLess(new_key)) {
            FlowMgmtKeyNode *node = old_it->second;
            DeleteFlowMgmtKey(flow.get(), old_info, old_key,
                              node);
            FlowMgmtKeyTree::iterator tmp = old_it++;
            FlowMgmtKey *key = tmp->first;
            old_tree->erase(tmp);
            delete key;
            delete node;
        } else {
            AddFlowMgmtKey(flow.get(), old_info, new_key, old_key);
            old_it++;
            new_it++;
        }
    }

    while (new_it != new_tree.end()) {
        FlowMgmtKey *new_key = new_it->first;
        AddFlowMgmtKey(flow.get(), old_info, new_key, NULL);
        new_it++;
    }

    while (old_it != old_tree->end()) {
        FlowMgmtKey *old_key = old_it->first;
        FlowMgmtKeyNode *node = old_it->second;
        DeleteFlowMgmtKey(flow.get(), old_info, old_key, node);
        FlowMgmtKeyTree::iterator tmp = old_it++;
        FlowMgmtKey *key = tmp->first;
        old_tree->erase(tmp);
        delete key;
        delete node;
    }

    new_it = new_tree.begin();
    while (new_it != new_tree.end()) {
        FlowMgmtKeyTree::iterator tmp = new_it++;
        FlowMgmtKey *key = tmp->first;
        FlowMgmtKeyNode *node = tmp->second;
        new_tree.erase(tmp);
        delete key;
        delete node;
    }
}

void FlowMgmtManager::DeleteFlow(FlowEntryPtr &flow,
                                 const RevFlowDepParams &params) {
    // Delete entries for flow from the tree
    FlowEntryInfo *old_info = FindFlowEntryInfo(flow);
    if (old_info == NULL)
        return;

    FlowMgmtKeyTree *old_tree = &old_info->tree_;
    assert(old_tree);
    old_info->count_++;

    FlowMgmtKeyTree::iterator old_it = old_tree->begin();
    while (old_it != old_tree->end()) {
        FlowMgmtKeyNode *node = old_it->second;
        DeleteFlowMgmtKey(flow.get(), old_info, old_it->first, node);
        FlowMgmtKeyTree::iterator tmp = old_it++;
        FlowMgmtKey *key = tmp->first;
        old_tree->erase(tmp);
        delete key;
        delete node;
    }

    assert(old_tree->size() == 0);
    DeleteFlowEntryInfo(flow);
}

void FlowMgmtManager::UpdateFlowStats(FlowEntryPtr &flow, uint32_t bytes,
                                      uint32_t packets, uint32_t oflow_bytes,
                                      const boost::uuids::uuid &u) {
    //Enqueue Flow Index Update Event request to flow-stats-collector
    agent_->flow_stats_manager()->UpdateStatsEvent(flow, bytes, packets,
                                                   oflow_bytes, u);
}

bool FlowMgmtManager::HasVrfFlows(uint32_t vrf_id) {
    if (ip4_route_flow_mgmt_tree_.HasVrfFlows(vrf_id, Agent::INET4_UNICAST)) {
        return true;
    }

    if (ip6_route_flow_mgmt_tree_.HasVrfFlows(vrf_id, Agent::INET6_UNICAST)) {
        return true;
    }

    if (bridge_route_flow_mgmt_tree_.HasVrfFlows(vrf_id, Agent::BRIDGE)) {
        return true;
    }

    return false;
}

void FlowMgmtManager::VnFlowCounters(const VnEntry *vn, uint32_t *ingress_flow_count,
                                     uint32_t *egress_flow_count) {
    vn_flow_mgmt_tree_.VnFlowCounters(vn, ingress_flow_count,
                                      egress_flow_count);
}

void FlowMgmtManager::InterfaceFlowCount(const Interface *itf,
                                         uint64_t *created, uint64_t *aged,
                                         uint32_t *active_flows) {
    interface_flow_mgmt_tree_.InterfaceFlowCount(itf, created, aged,
                                                 active_flows);
}

FlowEntryInfo *
FlowMgmtManager::FindFlowEntryInfo(const FlowEntryPtr &flow) {
    return flow->flow_mgmt_info();
}

FlowEntryInfo *
FlowMgmtManager::LocateFlowEntryInfo(FlowEntryPtr &flow) {
    FlowEntryInfo *info = FindFlowEntryInfo(flow);
    if (info != NULL)
        return info;
    info = new FlowEntryInfo(flow.get());
    flow->set_flow_mgmt_info(info);
    return info;
}

BgpAsAServiceFlowMgmtKey *
FlowMgmtManager::FindBgpAsAServiceInfo(FlowEntry *flow,
                                       BgpAsAServiceFlowMgmtKey &key) {
    FlowEntryInfo *flow_info = FindFlowEntryInfo(flow);
    if (flow_info == NULL)
        return NULL;

    FlowMgmtKeyTree::iterator key_it = flow_info->tree_.find(&key);
    if (key_it == flow_info->tree().end())
        return NULL;

    BgpAsAServiceFlowMgmtKey *bkey =
        static_cast<BgpAsAServiceFlowMgmtKey *>(key_it->first);
    return bkey;
}

void FlowMgmtManager::DeleteFlowEntryInfo(FlowEntryPtr &flow) {
    FlowEntryInfo *info = flow->flow_mgmt_info();
    if (info == NULL)
        return;

    assert(info->tree_.size() == 0);
    flow->set_flow_mgmt_info(NULL);
    return;
}

/////////////////////////////////////////////////////////////////////////////
// Routines to add/delete Flow and FlowMgmtKey in different trees
/////////////////////////////////////////////////////////////////////////////

// Add a FlowMgmtKey into FlowMgmtKeyTree for an object
// The FlowMgmtKeyTree for object is passed as argument
void FlowMgmtManager::AddFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                                     FlowMgmtKey *key, FlowMgmtKey *old_key) {
    FlowMgmtKey *tmp = key->Clone();
    FlowMgmtKeyNode *node = new FlowMgmtKeyNode(flow);

    std::pair<FlowMgmtKeyTree::iterator, bool> ret = info->tree_.insert(
                                                     make_pair(tmp, node));
    if (ret.second == false) {
        delete tmp;
        delete node;
        if (key->type() == FlowMgmtKey::ACL) {
            /* Copy the ACE Id list to existing key from new Key */
            FlowMgmtKey *existing_key = ret.first->first;
            AclFlowMgmtKey *akey = static_cast<AclFlowMgmtKey *>(existing_key);
            AclFlowMgmtKey *new_key = static_cast<AclFlowMgmtKey *>(key);
            akey->set_ace_id_list(new_key->ace_id_list());
        }
    }

    switch (key->type()) {
    case FlowMgmtKey::INTERFACE:
        interface_flow_mgmt_tree_.Add(key, flow,
                                      (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::ACL:
        acl_flow_mgmt_tree_.Add(key, flow, old_key,
                                (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::VN: {
        bool new_flow = vn_flow_mgmt_tree_.Add(key, flow,
                                               (ret.second)? node : NULL);
        VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>
            (vn_flow_mgmt_tree_.Find(key));
        entry->UpdateCounterOnAdd(flow, new_flow, info->local_flow_,
                                  info->ingress_);
        info->local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
        info->ingress_ = flow->is_flags_set(FlowEntry::IngressDir);
        break;
    }

    case FlowMgmtKey::INET4:
        ip4_route_flow_mgmt_tree_.Add(key, flow,
                                      (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::INET6:
        ip6_route_flow_mgmt_tree_.Add(key, flow,
                                      (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::BRIDGE:
        bridge_route_flow_mgmt_tree_.Add(key, flow,
                                         (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::NH:
        nh_flow_mgmt_tree_.Add(key, flow,
                               (ret.second)? node : NULL);
        break;

    case FlowMgmtKey::BGPASASERVICE: {
        BgpAsAServiceFlowMgmtKey *bgp_service_key =
            static_cast<BgpAsAServiceFlowMgmtKey *>(key);
        int cn_index = bgp_service_key->cn_index();
        if (cn_index != BgpAsAServiceFlowMgmtTree::kInvalidCnIndex) {
            bgp_as_a_service_flow_mgmt_tree_[cn_index].get()->Add(key, flow,
                                                  (ret.second)? node : NULL);
            boost::uuids::uuid hc_uuid;
            if (agent()->oper_db()->bgp_as_a_service()->GetBgpHealthCheck(
                static_cast<const VmInterface *>(flow->intf_entry()), &hc_uuid)) {
                FlowMgmtKey *inserted_key = ret.first->first;
                BgpAsAServiceFlowMgmtKey *bkey =
                    static_cast<BgpAsAServiceFlowMgmtKey *>(inserted_key);
                bkey->StartHealthCheck(agent(), flow, hc_uuid);
            }
        }
        break;
    }

    default:
        assert(0);
    }
}

// Delete a FlowMgmtKey from FlowMgmtKeyTree for an object
// The FlowMgmtKeyTree for object is passed as argument
void FlowMgmtManager::DeleteFlowMgmtKey(
    FlowEntry *flow, FlowEntryInfo *info, FlowMgmtKey *key,
    FlowMgmtKeyNode *node) {

    FlowMgmtKeyTree::iterator it = info->tree_.find(key);
    assert(it != info->tree_.end());

    switch (key->type()) {
    case FlowMgmtKey::INTERFACE:
        interface_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::ACL:
        acl_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::VN: {
        vn_flow_mgmt_tree_.Delete(key, flow, node);
        VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>
            (vn_flow_mgmt_tree_.Find(key));
        if (entry)
            entry->UpdateCounterOnDel(flow, info->local_flow_, info->ingress_);
        info->local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
        info->ingress_ = flow->is_flags_set(FlowEntry::IngressDir);
        break;
    }

    case FlowMgmtKey::INET4:
        ip4_route_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::INET6:
        ip6_route_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::BRIDGE:
        bridge_route_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::NH:
        nh_flow_mgmt_tree_.Delete(key, flow, node);
        break;

    case FlowMgmtKey::BGPASASERVICE: {
        BgpAsAServiceFlowMgmtKey *bkey =
            static_cast<BgpAsAServiceFlowMgmtKey *>(it->first);
        bkey->StopHealthCheck(flow);
        BgpAsAServiceFlowMgmtKey *bgp_service_key =
            static_cast<BgpAsAServiceFlowMgmtKey *>(key);
        uint8_t count = bgp_service_key->cn_index();
        bgp_as_a_service_flow_mgmt_tree_[count].get()->Delete(key, flow, node);
        break;
    }

    default:
        assert(0);
    }
}
