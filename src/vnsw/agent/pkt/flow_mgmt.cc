#include <bitset>
#include <boost/uuid/uuid_io.hpp>
#include "cmn/agent.h"
#include "controller/controller_init.h"
#include "oper/bgp_as_service.h"
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_mgmt_request.h"
#include "pkt/flow_mgmt_dbclient.h"
#include "uve/flow_ace_stats_request.h"
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

void FlowMgmtManager::ControllerNotify(uint8_t index) {
    FlowMgmtRequestPtr req(new BgpAsAServiceFlowMgmtRequest(index));
    request_queue_.Enqueue(req);
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtManager::SetAceSandeshData(const AclDBEntry *acl,
                                        AclFlowCountResp &data,
                                        int ace_id) {
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
static bool ProcessEvent(FlowMgmtRequest *req, FlowMgmtKey *key,
                         FlowMgmtTree *tree) {
    switch (req->event()) {
    case FlowMgmtRequest::ADD_DBENTRY:
        tree->OperEntryAdd(req, key);
        break;

    case FlowMgmtRequest::CHANGE_DBENTRY:
        tree->OperEntryChange(req, key);
        break;

    case FlowMgmtRequest::DELETE_DBENTRY:
        tree->OperEntryDelete(req, key);
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

/////////////////////////////////////////////////////////////////////////////
// Bgp as a service flow management
/////////////////////////////////////////////////////////////////////////////
bool BgpAsAServiceFlowMgmtEntry::NonOperEntryDelete(FlowMgmtManager *mgr,
                                                    const FlowMgmtRequest *req,
                                                    FlowMgmtKey *key) {
    oper_state_ = OPER_DEL_SEEN;
    gen_id_ = req->gen_id();
    FlowEvent::Event event = req->GetResponseEvent();
    if (event == FlowEvent::INVALID)
        return false;

    FlowList::iterator it = flow_list_.begin();
    while (it != flow_list_.end()) {
        FlowMgmtKeyNode *node = &(*it);
        mgr->NonOperEntryEvent(event, node->flow_entry());
        it++;
    }
    return true;
}

void BgpAsAServiceFlowMgmtTree::FreeNotify(FlowMgmtKey *key, uint32_t gen_id) {
    assert(key->db_entry() == NULL);
}

void BgpAsAServiceFlowMgmtTree::ExtractKeys(FlowEntry *flow,
                                            FlowMgmtKeyTree *tree) {
    if (flow->is_flags_set(FlowEntry::BgpRouterService) == false)
        return;
    const VmInterface *vm_intf =
        dynamic_cast<const VmInterface *>(flow->intf_entry());
    if (!vm_intf || (flow->bgp_as_a_service_port() == 0))
        return;

    BgpAsAServiceFlowMgmtKey *key =
        new BgpAsAServiceFlowMgmtKey(vm_intf->GetUuid(),
                                 flow->bgp_as_a_service_port(),
                                 index_);
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *BgpAsAServiceFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new BgpAsAServiceFlowMgmtEntry();
}

bool BgpAsAServiceFlowMgmtTree::BgpAsAServiceDelete
(BgpAsAServiceFlowMgmtKey &key, const FlowMgmtRequest *req) {
    FlowMgmtEntry *entry = Find(&key);
    if (entry == NULL) {
        return true;
    }

    entry->NonOperEntryDelete(mgr_, req, &key);
    return TryDelete(&key, entry);
}

void BgpAsAServiceFlowMgmtTree::DeleteAll() {
    Tree::iterator it = tree_.begin();
    while (it != tree_.end()) {
        BgpAsAServiceFlowMgmtKey *key =
            static_cast<BgpAsAServiceFlowMgmtKey *>(it->first);
        mgr_->BgpAsAServiceNotify(key->uuid(), key->source_port());
        it++;
    }
}

int BgpAsAServiceFlowMgmtTree::GetCNIndex(const FlowEntry *flow) {
    IpAddress dest_ip = IpAddress();
    if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
        dest_ip = flow->key().src_addr;
    } else {
        //No reverse flow means no CN to map to so dont add flow key.
        if (flow->reverse_flow_entry() == NULL)
            return BgpAsAServiceFlowMgmtTree::kInvalidCnIndex;
        dest_ip = flow->reverse_flow_entry()->key().src_addr;
    }
    for (uint8_t count = 0; count < MAX_XMPP_SERVERS; count++) {
        if (flow->flow_table()->agent()->controller_ifmap_xmpp_server(count) ==
            dest_ip.to_string()) {
            return count;
        }
    }
    return BgpAsAServiceFlowMgmtTree::kInvalidCnIndex;
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
                                         count);
            bgp_as_a_service_flow_mgmt_tree_[count].get()->
                BgpAsAServiceDelete(key, req);
        }
    } else if (bgp_as_a_service_request->type() ==
               BgpAsAServiceFlowMgmtRequest::CONTROLLER) {
        bgp_as_a_service_flow_mgmt_tree_[bgp_as_a_service_request->index()].get()->
            DeleteAll();
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
    case FlowMgmtRequest::DELETE_DBENTRY: {
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
    tbb::mutex::scoped_lock mutex(flow->mutex());
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
        if ((!itf_name.empty() && !flow->sg_rule_uuid().empty()) ||
            (!vn_name.empty() && !flow->nw_ace_uuid().empty())) {
            boost::shared_ptr<FlowAceStatsRequest> req(new FlowAceStatsRequest
                (FlowAceStatsRequest::ADD_FLOW, flow->uuid(), itf_name,
                 flow->sg_rule_uuid(), vn_name, flow->nw_ace_uuid()));
            uve->stats_manager()->EnqueueEvent(req);
        }
    }
}

void FlowMgmtManager::EnqueueUveDeleteEvent(const FlowEntry *flow) const {
    AgentUveStats *uve = dynamic_cast<AgentUveStats *>(agent_->uve());
    if (uve) {
        boost::shared_ptr<FlowAceStatsRequest> req(new FlowAceStatsRequest
            (FlowAceStatsRequest::DELETE_FLOW, flow->uuid()));
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

/////////////////////////////////////////////////////////////////////////////
// FlowMgmtKey routines
/////////////////////////////////////////////////////////////////////////////

// Event to be enqueued to free an object
FlowEvent::Event FlowMgmtKey::FreeDBEntryEvent() const {
    FlowEvent::Event event = FlowEvent::INVALID;
    switch (type_) {
    case INTERFACE:
    case ACL:
    case VN:
    case INET4:
    case INET6:
    case BRIDGE:
    case NH:
    case VRF:
        event = FlowEvent::FREE_DBENTRY;
        break;

    case ACE_ID:
    case VM:
        event = FlowEvent::INVALID;
        break;
    case BGPASASERVICE:
        event = FlowEvent::INVALID;

    default:
        assert(0);
    }
    return event;
}

/////////////////////////////////////////////////////////////////////////////
// Routines on FlowMgmtTree structure within the FlowMgmtManager
// Generic code for all FlowMgmtTrees
/////////////////////////////////////////////////////////////////////////////
FlowMgmtEntry *FlowMgmtTree::Find(FlowMgmtKey *key) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end())
        return NULL;

    return it->second;
}

FlowMgmtEntry *FlowMgmtTree::Locate(FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Find(key);
    if (entry == NULL) {
        entry = Allocate(key);
        InsertEntry(key->Clone(), entry);
    }

    return entry;
}

void FlowMgmtTree::InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry) {
    tree_[key] = entry;
}

FlowMgmtKey *FlowMgmtTree::LowerBound(FlowMgmtKey *key) {
    Tree::iterator it = tree_.lower_bound(key);
    if (it == tree_.end())
        return NULL;

    return it->first;
}

bool FlowMgmtTree::TryDelete(FlowMgmtKey *key, FlowMgmtEntry *entry) {
    if (entry->CanDelete() == false)
        return false;

    // Send message only if we have seen DELETE message from FlowTable
    if (entry->oper_state() == FlowMgmtEntry::OPER_DEL_SEEN) {
        FreeNotify(key, entry->gen_id());
    }

    Tree::iterator it = tree_.find(key);
    assert(it != tree_.end());
    FlowMgmtKey *first = it->first;
    RemoveEntry(it);
    delete entry;
    delete first;

    return true;
}

void FlowMgmtTree::RemoveEntry(Tree::iterator it) {
    tree_.erase(it);
}

/////////////////////////////////////////////////////////////////////////////
// Generic Event handler on tree for add/delete of a flow
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtTree::AddFlowMgmtKey(FlowMgmtKeyTree *tree, FlowMgmtKey *key) {
    FlowMgmtKeyNode *node = new FlowMgmtKeyNode();
    std::pair<FlowMgmtKeyTree::iterator, bool> ret;
    ret = tree->insert(make_pair(key, node));
    if (ret.second == false) {
        delete key;
        delete node;
    }
    return ret.second;
}

// Adds Flow to a FlowMgmtEntry defined by key. Does not allocate FlowMgmtEntry
// if its not already present
bool FlowMgmtTree::Add(FlowMgmtKey *key, FlowEntry *flow,
                       FlowMgmtKeyNode *node) {
    FlowMgmtEntry *entry = Locate(key);
    if (entry == NULL) {
        return false;
    }

    return entry->Add(flow, node);
}

bool FlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow,
                          FlowMgmtKeyNode *node) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end()) {
        return false;
    }

    FlowMgmtEntry *entry = it->second;
    bool ret = entry->Delete(flow, node);

    TryDelete(it->first, entry);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Event handler for add/delete/change of an object
/////////////////////////////////////////////////////////////////////////////

// Send DBEntry Free message to DB Client module
void FlowMgmtTree::FreeNotify(FlowMgmtKey *key, uint32_t gen_id) {
    assert(key->db_entry() != NULL);
    FlowEvent::Event event = key->FreeDBEntryEvent();
    if (event == FlowEvent::INVALID)
        return;
    mgr_->FreeDBEntryEvent(event, key, gen_id);
}

// An object is added/updated. Enqueue REVALUATE for flows dependent on it
bool FlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Locate(key);
    entry->OperEntryAdd(mgr_, req, key);
    return true;
}

bool FlowMgmtTree::OperEntryChange(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Find(key);
    if (entry) {
        entry->OperEntryChange(mgr_, req, key);
    }
    return true;
}

// Send DELETE Entry message to FlowTable module
bool FlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Find(key);
    if (entry == NULL) {
        FreeNotify(key, req->gen_id());
        return true;
    }

    entry->OperEntryDelete(mgr_, req, key);
    return TryDelete(key, entry);
}

bool FlowMgmtTree::RetryDelete(FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Find(key);
    if (entry == NULL) {
        return true;
    }

    return TryDelete(key, entry);
}

/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
// Object Entry code
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtEntry::Add(FlowEntry *flow, FlowMgmtKeyNode *node) {
    if (node) {
        flow_list_.push_back(*node);
        return true;
    }
    return false;
}

bool FlowMgmtEntry::Delete(FlowEntry *flow, FlowMgmtKeyNode *node) {
    flow_list_.erase(flow_list_.iterator_to(*node));
    return flow_list_.size();
}

// An entry *cannot* be deleted if 
//    - It contains flows
//    - It has seen ADD but not seen any DELETE
bool FlowMgmtEntry::CanDelete() const {
    assert(oper_state_ != INVALID);
    if (flow_list_.size())
        return false;

    return (oper_state_ != OPER_ADD_SEEN);
}

// Handle Add/Change event for DBEntry
bool FlowMgmtEntry::OperEntryAdd(FlowMgmtManager *mgr,
                                 const FlowMgmtRequest *req, FlowMgmtKey *key) {
    oper_state_ = OPER_ADD_SEEN;
    FlowEvent::Event event = req->GetResponseEvent();
    if (event == FlowEvent::INVALID)
        return false;

    FlowList::iterator it = flow_list_.begin();
    while (it != flow_list_.end()) {
        FlowMgmtKeyNode *node = &(*it);
        mgr->DBEntryEvent(event, key, node->flow_entry());
        it++;
    }

    return true;
}

bool FlowMgmtEntry::OperEntryChange(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key) {
    return OperEntryAdd(mgr, req, key);
}

// Handle Delete event for DBEntry
bool FlowMgmtEntry::OperEntryDelete(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key) {
    oper_state_ = OPER_DEL_SEEN;
    gen_id_ = req->gen_id();
    FlowEvent::Event event = req->GetResponseEvent();
    if (event == FlowEvent::INVALID)
        return false;

    FlowList::iterator it = flow_list_.begin();
    while (it != flow_list_.end()) {
        FlowMgmtKeyNode *node = &(*it);
        mgr->DBEntryEvent(event, key, node->flow_entry());
        it++;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Acl Flow Management
/////////////////////////////////////////////////////////////////////////////
string AclFlowMgmtEntry::GetAclFlowSandeshDataKey(const AclDBEntry *acl,
                                                  const int last_count) const {
    string uuid_str = UuidToString(acl->GetUuid());
    stringstream ss;
    ss << uuid_str << ":";
    ss << last_count;
    return ss.str();
}

string AclFlowMgmtEntry::GetAceSandeshDataKey(const AclDBEntry *acl,
                                              int ace_id) {
    string uuid_str = UuidToString(acl->GetUuid());
    stringstream ss;
    ss << uuid_str << ":";
    ss << ace_id;
    return ss.str();
}

void AclFlowMgmtEntry::FillAceFlowSandeshInfo(const AclDBEntry *acl,
                                              AclFlowCountResp &data,
                                              int ace_id) {
    int count = 0;
    bool key_set = false;
    AceIdFlowCntMap::iterator aceid_it = aceid_cnt_map_.upper_bound(ace_id);
    std::vector<AceIdFlowCnt> id_cnt_l;
    while (aceid_it != aceid_cnt_map_.end()) {
        AceIdFlowCnt id_cnt_s;
        id_cnt_s.ace_id = aceid_it->first;
        id_cnt_s.flow_cnt = aceid_it->second;
        id_cnt_l.push_back(id_cnt_s);
        count++;
        ++aceid_it;
        if (count == MaxResponses && aceid_it != aceid_cnt_map_.end()) {
            data.set_iteration_key(GetAceSandeshDataKey(acl, id_cnt_s.ace_id));
            key_set = true;
            break;
        }
    }
    data.set_aceid_cnt_list(id_cnt_l);

    data.set_flow_count(Size());
    data.set_flow_miss(flow_miss_);

    if (!key_set) {
        data.set_iteration_key(GetAceSandeshDataKey(acl, 0));
    }
}

void AclFlowMgmtEntry::FillAclFlowSandeshInfo(const AclDBEntry *acl,
                                              AclFlowResp &data,
                                              const int last_count,
                                              Agent *agent) {
    int count = 0;
    bool key_set = false;
    FlowList::iterator fe_tree_it = flow_list_.begin();
    while (fe_tree_it != flow_list_.end() && (count + 1) < last_count) {
        fe_tree_it++;
        count++;
    }
    data.set_flow_count(Size());
    data.set_flow_miss(flow_miss_);
    std::vector<FlowSandeshData> flow_entries_l;
    while(fe_tree_it != flow_list_.end()) {
        FlowMgmtKeyNode *node = &(*fe_tree_it);
        const FlowEntry *fe = node->flow_entry();
        FlowSandeshData fe_sandesh_data;
        fe->SetAclFlowSandeshData(acl, fe_sandesh_data, agent);

        flow_entries_l.push_back(fe_sandesh_data);
        count++;
        ++fe_tree_it;
        if (count == (MaxResponses + last_count) &&
            fe_tree_it != flow_list_.end()) {
            data.set_iteration_key(GetAclFlowSandeshDataKey(acl, count));
            key_set = true;
            break;
        }
    }
    data.set_flow_entries(flow_entries_l);
    if (!key_set) {
        data.set_iteration_key(GetAclFlowSandeshDataKey(acl, 0));
    }
}

void AclFlowMgmtEntry::DecrementAceIdCountMap(const AclEntryIDList *id_list) {
    AclEntryIDList::const_iterator id_it;
    for (id_it = id_list->begin(); id_it != id_list->end(); ++id_it) {
        aceid_cnt_map_[*id_it] -= 1;
    }
}

bool AclFlowMgmtEntry::Add(const AclEntryIDList *id_list, FlowEntry *flow,
                           const AclEntryIDList *old_id_list,
                           FlowMgmtKeyNode *node) {
    if (old_id_list) {
        DecrementAceIdCountMap(old_id_list);
    }
    if (id_list->size()) {
        AclEntryIDList::const_iterator id_it;
        for (id_it = id_list->begin(); id_it != id_list->end(); ++id_it) {
            aceid_cnt_map_[*id_it] += 1;
        }
    } else {
        flow_miss_++;
    }
    return FlowMgmtEntry::Add(flow, node);
}

bool AclFlowMgmtEntry::Delete(const AclEntryIDList *id_list, FlowEntry *flow,
                              FlowMgmtKeyNode *node) {
    if (id_list->size()) {
        DecrementAceIdCountMap(id_list);
    }
    return FlowMgmtEntry::Delete(flow, node);
}

void AclFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                  const MatchAclParamsList *acl_list) {
    std::list<MatchAclParams>::const_iterator it;
    for (it = acl_list->begin(); it != acl_list->end(); it++) {
        AclFlowMgmtKey *key = new AclFlowMgmtKey(it->acl.get(),
                                                 &it->ace_id_list);
        AddFlowMgmtKey(tree, key);
    }
}

void AclFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
    ExtractKeys(flow, tree, &flow->match_p().m_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_sg_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_out_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_out_sg_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_reverse_sg_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_reverse_out_sg_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_mirror_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_out_mirror_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_vrf_assign_acl_l);
}

FlowMgmtEntry *AclFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new AclFlowMgmtEntry();
}

bool AclFlowMgmtTree::Add(FlowMgmtKey *key, FlowEntry *flow,
                          FlowMgmtKey *old_key, FlowMgmtKeyNode *node) {
    AclFlowMgmtEntry *entry = static_cast<AclFlowMgmtEntry *>(Locate(key));
    if (entry == NULL) {
        return false;
    }

    AclFlowMgmtKey *acl_key = static_cast<AclFlowMgmtKey *>(key);
    const AclEntryIDList *old_ace_id_list = NULL;
    if (old_key) {
        AclFlowMgmtKey *old_acl_key = static_cast<AclFlowMgmtKey *>(old_key);
        old_ace_id_list = old_acl_key->ace_id_list();
    }
    return entry->Add(acl_key->ace_id_list(), flow, old_ace_id_list, node);
}

bool AclFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow,
                             FlowMgmtKeyNode *node) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end()) {
        return false;
    }

    AclFlowMgmtKey *acl_key = static_cast<AclFlowMgmtKey *>(key);
    AclFlowMgmtEntry *entry = static_cast<AclFlowMgmtEntry *>(it->second);
    bool ret = entry->Delete(acl_key->ace_id_list(), flow, node);

    TryDelete(it->first, entry);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VN Flow Management
/////////////////////////////////////////////////////////////////////////////
void VnFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
    if (flow->vn_entry() == NULL)
        return;
    VnFlowMgmtKey *key = new VnFlowMgmtKey(flow->vn_entry());
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *VnFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new VnFlowMgmtEntry();
}

void VnFlowMgmtEntry::UpdateCounterOnAdd(FlowEntry *flow, bool add_flow,
                                         bool local_flow, bool old_ingress) {
    if (add_flow) {
        if (flow->is_flags_set(FlowEntry::LocalFlow)) {
            ingress_flow_count_++;
            egress_flow_count_++;
        } else if (flow->is_flags_set(FlowEntry::IngressDir)) {
            ingress_flow_count_++;
        } else {
            egress_flow_count_++;
        }

        return;
    }

    if (local_flow)
        return;

    bool new_ingress = flow->is_flags_set(FlowEntry::IngressDir);
    if (new_ingress != old_ingress) {
        if (new_ingress) {
            ingress_flow_count_++;
            egress_flow_count_--;
        } else {
            ingress_flow_count_--;
            egress_flow_count_++;
        }
    }
}

void VnFlowMgmtEntry::UpdateCounterOnDel(FlowEntry *flow, bool local_flow,
                                         bool old_ingress) {
    if (local_flow) {
        ingress_flow_count_--;
        egress_flow_count_--;
        return;
    }

    if (old_ingress) {
        ingress_flow_count_--;
    } else {
        egress_flow_count_--;
    }
}

void VnFlowMgmtTree::InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry) {
    tbb::mutex::scoped_lock mutex(mutex_);
    FlowMgmtTree::InsertEntry(key, entry);
}

void VnFlowMgmtTree::RemoveEntry(Tree::iterator it) {
    tbb::mutex::scoped_lock mutex(mutex_);
    FlowMgmtTree::RemoveEntry(it);
}

void VnFlowMgmtTree::VnFlowCounters(const VnEntry *vn,
                                    uint32_t *ingress_flow_count,
                                    uint32_t *egress_flow_count) {
    VnFlowMgmtKey key(vn);
    tbb::mutex::scoped_lock mutex(mutex_);
    VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>(Find(&key));
    if (entry) {
        *ingress_flow_count += entry->ingress_flow_count();
        *egress_flow_count += entry->egress_flow_count();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Interface Flow Management
/////////////////////////////////////////////////////////////////////////////
bool InterfaceFlowMgmtEntry::Add(FlowEntry *flow, FlowMgmtKeyNode *node) {
    bool added = FlowMgmtEntry::Add(flow, node);
    if (added) {
        flow_created_++;
    }
    return added;
}

bool InterfaceFlowMgmtEntry::Delete(FlowEntry *flow, FlowMgmtKeyNode *node) {
    flow_aged_++;
    return FlowMgmtEntry::Delete(flow, node);
}

void InterfaceFlowMgmtTree::InsertEntry(FlowMgmtKey *key, FlowMgmtEntry *entry){
    tbb::mutex::scoped_lock mutex(mutex_);
    FlowMgmtTree::InsertEntry(key, entry);
}

void InterfaceFlowMgmtTree::RemoveEntry(Tree::iterator it) {
    tbb::mutex::scoped_lock mutex(mutex_);
    FlowMgmtTree::RemoveEntry(it);
}

void InterfaceFlowMgmtTree::InterfaceFlowCount(const Interface *itf,
                                               uint64_t *created,
                                               uint64_t *aged,
                                               uint32_t *active_flows) {
    InterfaceFlowMgmtKey key(itf);
    tbb::mutex::scoped_lock mutex(mutex_);
    InterfaceFlowMgmtEntry *entry = static_cast<InterfaceFlowMgmtEntry *>
        (Find(&key));
    if (entry) {
        *created += entry->flow_created();
        *aged += entry->flow_aged();
        *active_flows += entry->Size();
    }
}

void InterfaceFlowMgmtTree::ExtractKeys(FlowEntry *flow,
                                        FlowMgmtKeyTree *tree) {
    if (flow->intf_entry() == NULL)
        return;
    InterfaceFlowMgmtKey *key =
        new InterfaceFlowMgmtKey(flow->intf_entry());
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *InterfaceFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new InterfaceFlowMgmtEntry();
}

/////////////////////////////////////////////////////////////////////////////
// Nh Flow Management
/////////////////////////////////////////////////////////////////////////////
void NhFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
    if (flow->rpf_nh() == NULL)
        return;
    NhFlowMgmtKey *key = new NhFlowMgmtKey(flow->rpf_nh());
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *NhFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new NhFlowMgmtEntry();
}

/////////////////////////////////////////////////////////////////////////////
// Route Flow Management
/////////////////////////////////////////////////////////////////////////////
bool RouteFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow,
                               FlowMgmtKeyNode *node) {
    bool ret = FlowMgmtTree::Delete(key, flow, node);
    RouteFlowMgmtKey *route_key = static_cast<RouteFlowMgmtKey *>(key);
    mgr_->RetryVrfDelete(route_key->vrf_id());
    return ret;
}

void RouteFlowMgmtTree::SetDBEntry(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end()) {
        return;
    }

    if (it->first->db_entry()) {
        assert(it->first->db_entry() == req->db_entry());
        return;
    }
    it->first->set_db_entry(req->db_entry());
    return;
}

bool RouteFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                        FlowMgmtKey *key) {
    // Set the db_entry if it was not set earlier. It is needed to send the
    // FreeDBState message
    SetDBEntry(req, key);
    bool ret = FlowMgmtTree::OperEntryDelete(req, key);
    RouteFlowMgmtKey *route_key = static_cast<RouteFlowMgmtKey *>(key);
    mgr_->RetryVrfDelete(route_key->vrf_id());
    return ret;
}

bool RouteFlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req,
                                     FlowMgmtKey *key) {
    bool ret = FlowMgmtTree::OperEntryAdd(req, key);
    if (req->db_entry() == NULL)
        return ret;

    // Set the DBEntry in the flow-mgmt-entry
    SetDBEntry(req, key);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Inet Route Flow Management
/////////////////////////////////////////////////////////////////////////////
void InetRouteFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                        uint32_t vrf, const IpAddress &ip,
                                        uint8_t plen) {
    // We do not support renewal of VRF, so skip flow if VRF is deleted
    if (mgr_->agent()->vrf_table()->FindVrfFromId(vrf) == NULL) {
        return;
    }

    InetRouteFlowMgmtKey *key = NULL;
    if (ip.is_v4()) {
        Ip4Address ip4 = Address::GetIp4SubnetAddress(ip.to_v4(), plen);
        key = new InetRouteFlowMgmtKey(vrf, ip4, plen);
    } else {
        Ip6Address ip6 = Address::GetIp6SubnetAddress(ip.to_v6(), plen);
        key = new InetRouteFlowMgmtKey(vrf, ip6, plen);
    }
    AddFlowMgmtKey(tree, key);
}

void InetRouteFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                        const IpAddress &ip,
                                        const FlowRouteRefMap *rt_list) {
    FlowRouteRefMap::const_iterator it;
    for (it = rt_list->begin(); it != rt_list->end(); it++) {
        ExtractKeys(flow, tree, it->first, ip, it->second);
    }
}

void InetRouteFlowMgmtTree::ExtractKeys(FlowEntry *flow,
                                        FlowMgmtKeyTree *tree) {

    if (flow->l3_flow() == false) {
        // For l2-flows Track INET route for RPF only
        if (flow->data().rpf_vrf != VrfEntry::kInvalidIndex) {
            ExtractKeys(flow, tree, flow->data().rpf_vrf,
                        flow->key().src_addr, flow->data().rpf_plen);
        }
        return;
    }

    if (flow->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().flow_source_vrf,
                    flow->key().src_addr, flow->data().source_plen);
    }

    if (flow->data().acl_assigned_vrf_index_ != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().acl_assigned_vrf_index_,
                    flow->key().src_addr, flow->data().source_plen);
        ExtractKeys(flow, tree, flow->data().acl_assigned_vrf_index_,
                    flow->key().dst_addr, flow->data().dest_plen);
    }

    ExtractKeys(flow, tree, flow->key().src_addr,
                &flow->data().flow_source_plen_map);

    if (flow->data().flow_dest_vrf != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().flow_dest_vrf,
                    flow->key().dst_addr, flow->data().dest_plen);
    }
    ExtractKeys(flow, tree, flow->key().dst_addr,
                &flow->data().flow_dest_plen_map);
}

FlowMgmtEntry *InetRouteFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new InetRouteFlowMgmtEntry();
}

bool InetRouteFlowMgmtTree::HasVrfFlows(uint32_t vrf,
                                        Agent::RouteTableType type) {
    InetRouteFlowMgmtKey *next_key = NULL;

    if (type == Agent::INET4_UNICAST) {
        InetRouteFlowMgmtKey key(vrf, Ip4Address(0), 0);
        next_key = static_cast<InetRouteFlowMgmtKey *>(LowerBound(&key));
    } else {
        InetRouteFlowMgmtKey key(vrf, Ip6Address(), 0);
        next_key = static_cast<InetRouteFlowMgmtKey *>(LowerBound(&key));
    }

    if (next_key == NULL)
        return false;

    if (next_key->vrf_id() != vrf)
        return false;

    return true;
}

bool InetRouteFlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req,
                                         FlowMgmtKey *key) {
    bool ret = RouteFlowMgmtTree::OperEntryAdd(req, key);

    // A new route is added. This new route can be a longer prefix route for
    // flows using lower prefix-len (covering routes). So, do a LPM match to
    // find the covering route and trigger flow re-compute for flows on the
    // covering route
    InetRouteFlowMgmtKey *rt_key = static_cast<InetRouteFlowMgmtKey *>(key);
    AddToLPMTree(rt_key);
    if (rt_key->plen_ > 0) {
        InetRouteFlowMgmtKey lpm_key(rt_key->vrf_id_, rt_key->ip_,
                                     rt_key->plen_ - 1);
        InetRouteFlowMgmtKey *covering_route = LPM(&lpm_key);
        if (covering_route != NULL) {
            ret = RecomputeCoveringRoute(covering_route, rt_key);
        }
        rt_key->plen_ += 1;
    }

    return ret;
}

bool InetRouteFlowMgmtTree::RecomputeCoveringRoute
(InetRouteFlowMgmtKey *covering_route, InetRouteFlowMgmtKey *key) {
    InetRouteFlowMgmtEntry *entry = dynamic_cast<InetRouteFlowMgmtEntry *>
                                    (Find(covering_route));
    if (entry == NULL) {
        return true;
    }

    return entry->RecomputeCoveringRouteEntry(mgr_, covering_route, key);
}

bool InetRouteFlowMgmtEntry::RecomputeCoveringRouteEntry
(FlowMgmtManager *mgr, InetRouteFlowMgmtKey *covering_route,
 InetRouteFlowMgmtKey *key){
    FlowList::iterator it = flow_list_.begin();
    while (it != flow_list_.end()) {
        FlowMgmtKeyNode *node = &(*it);
        // Queue the DB Event only route key  matches src or dst ip matches.
        if (key->NeedsReCompute(node->flow_entry())) {
            mgr->DBEntryEvent(FlowEvent::RECOMPUTE_FLOW, covering_route,
                              node->flow_entry());
        }
        it++;
    }

    return true;
}

bool InetRouteFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                            FlowMgmtKey *key) {
    InetRouteFlowMgmtKey *rt_key = static_cast<InetRouteFlowMgmtKey *>(key);
    DelFromLPMTree(rt_key);
    return RouteFlowMgmtTree::OperEntryDelete(req, key);
}

bool InetRouteFlowMgmtKey::NeedsReCompute(const FlowEntry *flow) {


    if (Match(flow->key().src_addr)) {
        return true;
    }

    if (Match(flow->key().dst_addr)) {
        return true;
    }

    const FlowEntry *rflow = flow->reverse_flow_entry();
    if (rflow == NULL)
        return true;

    if (Match(rflow->key().src_addr)) {
        return true;
    }

    if (Match(rflow->key().dst_addr)) {
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Bridge Route Flow Management
/////////////////////////////////////////////////////////////////////////////
void BridgeRouteFlowMgmtTree::ExtractKeys(FlowEntry *flow,
                                          FlowMgmtKeyTree *tree) {
    if (flow->l3_flow() == true)
        return;

    VrfTable *table = mgr_->agent()->vrf_table();
    uint32_t vrf = flow->data().flow_source_vrf;
    if (vrf != VrfEntry::kInvalidIndex && table->FindVrfFromId(vrf) != NULL) {
        BridgeRouteFlowMgmtKey *key =
            new BridgeRouteFlowMgmtKey(vrf, flow->data().smac);
        AddFlowMgmtKey(tree, key);
    }

    vrf = flow->data().flow_dest_vrf;
    if (vrf != VrfEntry::kInvalidIndex && table->FindVrfFromId(vrf) != NULL) {
        BridgeRouteFlowMgmtKey *key =
            new BridgeRouteFlowMgmtKey(vrf, flow->data().smac);
        AddFlowMgmtKey(tree, key);
    }
}

FlowMgmtEntry *BridgeRouteFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new BridgeRouteFlowMgmtEntry();
}

bool BridgeRouteFlowMgmtTree::HasVrfFlows(uint32_t vrf,
                                          Agent::RouteTableType type) {
    BridgeRouteFlowMgmtKey key(vrf, MacAddress::ZeroMac());
    BridgeRouteFlowMgmtKey *next_key = static_cast<BridgeRouteFlowMgmtKey *>
        (LowerBound(&key));
    if (next_key == false)
        return false;

    if (next_key->vrf_id() != vrf)
        return false;

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Vrf Flow Management
/////////////////////////////////////////////////////////////////////////////
void VrfFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
}

FlowMgmtEntry *VrfFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(key->db_entry());
    return new VrfFlowMgmtEntry(this, vrf);
}

bool VrfFlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    bool ret = FlowMgmtTree::OperEntryAdd(req, key);

    const VrfEntry *vrf = static_cast<const VrfEntry *>(key->db_entry());
    VrfIdMap::iterator it = id_map_.find(vrf->vrf_id());
    if (it != id_map_.end())
        return ret;

    id_map_.insert(make_pair(vrf->vrf_id(), vrf));
    return ret;
}

void VrfFlowMgmtTree::FreeNotify(FlowMgmtKey *key, uint32_t gen_id) {
    FlowMgmtTree::FreeNotify(key, gen_id);

    const VrfEntry *vrf = static_cast<const VrfEntry *>(key->db_entry());
    VrfIdMap::iterator it = id_map_.find(vrf->vrf_id());
    if (it != id_map_.end()) {
        id_map_.erase(it);
    }
}

void VrfFlowMgmtTree::RetryDelete(uint32_t vrf_id) {
    VrfIdMap::iterator it = id_map_.find(vrf_id);
    if (it == id_map_.end())
        return;

    VrfFlowMgmtKey key(it->second);
    const VrfEntry *vrf = dynamic_cast<const VrfEntry *>(key.db_entry());
    if (vrf && vrf->AllRouteTablesEmpty()) {
        FlowMgmtTree::RetryDelete(&key);
    }
}

VrfFlowMgmtEntry::VrfFlowMgmtEntry(VrfFlowMgmtTree *vrf_tree,
                                   const VrfEntry *vrf) :
    vrf_(vrf), vrf_id_(vrf->vrf_id()),
    inet4_(this, vrf, vrf->GetRouteTable(Agent::INET4_UNICAST)),
    inet6_(this, vrf, vrf->GetRouteTable(Agent::INET6_UNICAST)),
    bridge_(this, vrf, vrf->GetRouteTable(Agent::BRIDGE)),
    vrf_tree_(vrf_tree) {
}

bool VrfFlowMgmtEntry::CanDelete() const {
    if (FlowMgmtEntry::CanDelete() == false)
        return false;

    if (inet4_.deleted() == false || inet6_.deleted() == false ||
        bridge_.deleted() == false) {
        return false;
    }

    return (vrf_tree_->mgr()->HasVrfFlows(vrf_id_) == false);
}

VrfFlowMgmtEntry::Data::Data(VrfFlowMgmtEntry *vrf_mgmt_entry,
                             const VrfEntry *vrf, AgentRouteTable *table) :
    deleted_(false), table_ref_(this, NULL),
    vrf_mgmt_entry_(vrf_mgmt_entry), vrf_(vrf) {
    if (vrf->IsDeleted() == false) {
        table_ref_.Reset(table->deleter());
    } else {
        deleted_ = true;
    }
}

VrfFlowMgmtEntry::Data::~Data() {
    table_ref_.Reset(NULL);
}

void VrfFlowMgmtEntry::Data::ManagedDelete() {
    deleted_ = true;
    vrf_mgmt_entry_->vrf_tree()->mgr()->RetryVrfDeleteEvent(vrf_);
}
