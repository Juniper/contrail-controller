#include <bitset>
#include "pkt/flow_proto.h"
#include "pkt/flow_mgmt.h"
#include "pkt/flow_mgmt_request.h"
#include "pkt/flow_mgmt_dbclient.h"
#include "vrouter/flow_stats/flow_stats_collector.h"
const string FlowMgmtManager::kFlowMgmtTask = "Flow::Management";

/////////////////////////////////////////////////////////////////////////////
// FlowMgmtManager methods
/////////////////////////////////////////////////////////////////////////////
FlowMgmtManager::FlowMgmtManager(Agent *agent) :
    agent_(agent),
    acl_flow_mgmt_tree_(this),
    interface_flow_mgmt_tree_(this),
    vn_flow_mgmt_tree_(this),
    ip4_route_flow_mgmt_tree_(this),
    ip6_route_flow_mgmt_tree_(this),
    bridge_route_flow_mgmt_tree_(this),
    vrf_flow_mgmt_tree_(this),
    nh_flow_mgmt_tree_(this),
    flow_mgmt_dbclient_(new FlowMgmtDbClient(agent, this)),
    request_queue_(agent_->task_scheduler()->GetTaskId(kFlowMgmtTask), 1,
                   boost::bind(&FlowMgmtManager::RequestHandler, this, _1)) {
    request_queue_.set_name("Flow management");
}

void FlowMgmtManager::Init() {
    flow_mgmt_dbclient_->Init();
    agent_->acl_table()->set_ace_flow_sandesh_data_cb
        (boost::bind(&FlowMgmtManager::SetAceSandeshData, this, _1, _2, _3));
    agent_->acl_table()->set_acl_flow_sandesh_data_cb
        (boost::bind(&FlowMgmtManager::SetAclFlowSandeshData, this, _1, _2,
                     _3));
}

void FlowMgmtManager::Shutdown() {
    request_queue_.Shutdown();
    flow_mgmt_dbclient_->Shutdown();
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
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_FLOW, flow_ptr));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_FLOW, flow_ptr));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::FlowIndexUpdateEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::UPDATE_FLOW_INDEX, flow_ptr));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const DBEntry *entry, uint32_t gen_id) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_DBENTRY, entry, gen_id));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::ChangeEvent(const DBEntry *entry, uint32_t gen_id) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::CHANGE_DBENTRY, entry,
                                gen_id));
    request_queue_.Enqueue(req);
}
void FlowMgmtManager::DeleteEvent(const DBEntry *entry, uint32_t gen_id) {
    boost::shared_ptr<FlowMgmtRequest>req
        (new FlowMgmtRequest(FlowMgmtRequest::DELETE_DBENTRY, entry, gen_id));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::RetryVrfDeleteEvent(const VrfEntry *vrf) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::RETRY_DELETE_VRF, vrf, 0));
    request_queue_.Enqueue(req);
}

void FlowMgmtManager::EnqueueFlowEvent(const FlowEvent &event) {
    agent_->pkt()->get_flow_proto()->EnqueueFlowEvent(event);
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

bool FlowMgmtManager::DBEntryRequestHandler(FlowMgmtRequest *req,
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

bool FlowMgmtManager::RequestHandler(boost::shared_ptr<FlowMgmtRequest> req) {
    switch (req->event()) {
    case FlowMgmtRequest::ADD_FLOW: {
        //Handle the Add request for flow-mgmt
        AddFlow(req->flow());
        break;
    }

    case FlowMgmtRequest::DELETE_FLOW: {
        //Handle the Delete request for flow-mgmt
        DeleteFlow(req->flow());
        // On return from here reference to the flow is removed which can
        // result in deletion of flow from the tree. But, flow management runs
        // in parallel to flow processing. As a result, it can result in tree
        // being modified by two threads. Avoid the concurrency issue by
        // enqueuing a dummy request to flow-table queue. The reference will
        // be removed in flow processing context
        FlowEvent flow_resp(FlowEvent::FREE_FLOW_REF, req->flow().get());
        EnqueueFlowEvent(flow_resp);
        break;
    }

    case FlowMgmtRequest::UPDATE_FLOW_INDEX: {
        //Handle Flow index update for flow-mgmt
        UpdateFlowIndex(req->flow());
        break;
    }

    case FlowMgmtRequest::ADD_DBENTRY:
    case FlowMgmtRequest::CHANGE_DBENTRY:
    case FlowMgmtRequest::DELETE_DBENTRY: {
        DBEntryRequestHandler(req.get(), req->db_entry());
        break;
    }

    case FlowMgmtRequest::RETRY_DELETE_VRF: {
        RetryVrfDelete(req->vrf_id());
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
void FlowMgmtManager::LogFlow(FlowEntry *flow, const std::string &op) {
    FlowInfo trace;
    tbb::mutex::scoped_lock mutex(flow->mutex());
    flow->FillFlowInfo(trace);
    FLOW_TRACE(Trace, op, trace);
}

// Extract all the FlowMgmtKey for a flow
void FlowMgmtManager::MakeFlowMgmtKeyTree(FlowEntry *flow,
                                          FlowMgmtKeyTree *tree) {
    tbb::mutex::scoped_lock mutex(flow->mutex());
    acl_flow_mgmt_tree_.ExtractKeys(flow, tree);
    interface_flow_mgmt_tree_.ExtractKeys(flow, tree);
    vn_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip4_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip6_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    bridge_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    nh_flow_mgmt_tree_.ExtractKeys(flow, tree);
}

void FlowMgmtManager::AddFlow(FlowEntryPtr &flow) {
    LogFlow(flow.get(), "ADD");

    //Enqueue Add request to flow-stats-collector
    agent_->flow_stats_manager()->AddEvent(flow);

    // Trace the flow add/change
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
        FlowMgmtKey *new_key = *new_it;
        FlowMgmtKey *old_key = *old_it;
        if (new_key->IsLess(old_key)) {
            AddFlowMgmtKey(flow.get(), old_info, new_key, NULL);
            new_it++;
        } else if (old_key->IsLess(new_key)) {
            DeleteFlowMgmtKey(flow.get(), old_info, old_key);
            FlowMgmtKeyTree::iterator tmp = old_it++;
            FlowMgmtKey *key = *tmp;
            old_tree->erase(tmp);
            delete key;
        } else {
            AddFlowMgmtKey(flow.get(), old_info, new_key, old_key);
            old_it++;
            new_it++;
        }
    }

    while (new_it != new_tree.end()) {
        FlowMgmtKey *new_key = *new_it;
        AddFlowMgmtKey(flow.get(), old_info, new_key, NULL);
        new_it++;
    }

    while (old_it != old_tree->end()) {
        FlowMgmtKey *old_key = *old_it;
        DeleteFlowMgmtKey(flow.get(), old_info, old_key);
        FlowMgmtKeyTree::iterator tmp = old_it++;
        FlowMgmtKey *key = *tmp;
        old_tree->erase(tmp);
        delete key;
    }

    new_it = new_tree.begin();
    while (new_it != new_tree.end()) {
        FlowMgmtKeyTree::iterator tmp = new_it++;
        FlowMgmtKey *key = *tmp;
        new_tree.erase(tmp);
        delete key;
    }

}

void FlowMgmtManager::DeleteFlow(FlowEntryPtr &flow) {
    LogFlow(flow.get(), "DEL");

    // Delete entries for flow from the tree
    FlowEntryInfo *old_info = FindFlowEntryInfo(flow);
    if (old_info == NULL)
        return;

    //Enqueue Delete request to flow-stats-collector
    agent_->flow_stats_manager()->DeleteEvent(flow);

    FlowMgmtKeyTree *old_tree = &old_info->tree_;
    assert(old_tree);
    old_info->count_++;

    FlowMgmtKeyTree::iterator old_it = old_tree->begin();
    while (old_it != old_tree->end()) {
        DeleteFlowMgmtKey(flow.get(), old_info, *old_it);
        FlowMgmtKeyTree::iterator tmp = old_it++;
        FlowMgmtKey *key = *tmp;
        old_tree->erase(tmp);
        delete key;
    }

    assert(old_tree->size() == 0);
    DeleteFlowEntryInfo(flow);
}

void FlowMgmtManager::UpdateFlowIndex(FlowEntryPtr &flow) {
    //Enqueue Flow Index Update Event request to flow-stats-collector
    agent_->flow_stats_manager()->FlowIndexUpdateEvent(flow);
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

FlowMgmtManager::FlowEntryInfo *
FlowMgmtManager::FindFlowEntryInfo(const FlowEntryPtr &flow) {
    FlowEntryTree::iterator it = flow_tree_.find(flow);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

FlowMgmtManager::FlowEntryInfo *
FlowMgmtManager::LocateFlowEntryInfo(FlowEntryPtr &flow) {
    FlowEntryInfo *info = FindFlowEntryInfo(flow);
    if (info != NULL)
        return info;

    flow_tree_.insert(make_pair(flow, FlowEntryInfo()));
    return FindFlowEntryInfo(flow);
}

void FlowMgmtManager::DeleteFlowEntryInfo(FlowEntryPtr &flow) {
    FlowEntryTree::iterator it = flow_tree_.find(flow);
    if (it == flow_tree_.end())
        return;

    assert(it->second.tree_.size() == 0);
    flow_tree_.erase(it);
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
    std::pair<FlowMgmtKeyTree::iterator, bool> ret = info->tree_.insert(tmp);
    if (ret.second == false) {
        delete tmp;
        if (key->type() == FlowMgmtKey::ACL) {
            /* Copy the ACE Id list to existing key from new Key */
            FlowMgmtKey *existing_key = *(ret.first);
            AclFlowMgmtKey *akey = static_cast<AclFlowMgmtKey *>(existing_key);
            AclFlowMgmtKey *new_key = static_cast<AclFlowMgmtKey *>(key);
            akey->set_ace_id_list(new_key->ace_id_list());
        }
    }

    switch (key->type()) {
    case FlowMgmtKey::INTERFACE:
        interface_flow_mgmt_tree_.Add(key, flow);
        break;

    case FlowMgmtKey::ACL:
        acl_flow_mgmt_tree_.Add(key, flow, old_key);
        break;

    case FlowMgmtKey::VN: {
        bool new_flow = vn_flow_mgmt_tree_.Add(key, flow);
        VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>
            (vn_flow_mgmt_tree_.Find(key));
        entry->UpdateCounterOnAdd(flow, new_flow, info->local_flow_,
                                  info->ingress_);
        info->local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
        info->ingress_ = flow->is_flags_set(FlowEntry::IngressDir);
        break;
    }

    case FlowMgmtKey::INET4:
        ip4_route_flow_mgmt_tree_.Add(key, flow);
        break;

    case FlowMgmtKey::INET6:
        ip6_route_flow_mgmt_tree_.Add(key, flow);
        break;

    case FlowMgmtKey::BRIDGE:
        bridge_route_flow_mgmt_tree_.Add(key, flow);
        break;

    case FlowMgmtKey::NH:
        nh_flow_mgmt_tree_.Add(key, flow);
        break;

    default:
        assert(0);
    }
}

// Delete a FlowMgmtKey from FlowMgmtKeyTree for an object
// The FlowMgmtKeyTree for object is passed as argument
void FlowMgmtManager::DeleteFlowMgmtKey(FlowEntry *flow, FlowEntryInfo *info,
                                        FlowMgmtKey *key) {
    FlowMgmtKeyTree::iterator it = info->tree_.find(key);
    assert(it != info->tree_.end());

    switch (key->type()) {
    case FlowMgmtKey::INTERFACE:
        interface_flow_mgmt_tree_.Delete(key, flow);
        break;

    case FlowMgmtKey::ACL:
        acl_flow_mgmt_tree_.Delete(key, flow);
        break;

    case FlowMgmtKey::VN: {
        vn_flow_mgmt_tree_.Delete(key, flow);
        VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>
            (vn_flow_mgmt_tree_.Find(key));
        if (entry)
            entry->UpdateCounterOnDel(flow, info->local_flow_, info->ingress_);
        info->local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
        info->ingress_ = flow->is_flags_set(FlowEntry::IngressDir);
        break;
    }

    case FlowMgmtKey::INET4:
        ip4_route_flow_mgmt_tree_.Delete(key, flow);
        break;

    case FlowMgmtKey::INET6:
        ip6_route_flow_mgmt_tree_.Delete(key, flow);
        break;

    case FlowMgmtKey::BRIDGE:
        bridge_route_flow_mgmt_tree_.Delete(key, flow);
        break;

    case FlowMgmtKey::NH:
        nh_flow_mgmt_tree_.Delete(key, flow);
        break;

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
        tree_[key->Clone()] = entry;
    }

    return entry;
}

FlowMgmtKey *FlowMgmtTree::UpperBound(FlowMgmtKey *key) {
    Tree::iterator it = tree_.upper_bound(key);
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
    tree_.erase(it);
    delete entry;
    delete first;

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Generic Event handler on tree for add/delete of a flow
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtTree::AddFlowMgmtKey(FlowMgmtKeyTree *tree, FlowMgmtKey *key) {
    std::pair<FlowMgmtKeyTree::iterator, bool> ret = tree->insert(key);
    if (ret.second == false)
        delete key;
    return ret.second;
}

// Adds Flow to a FlowMgmtEntry defined by key. Does not allocate FlowMgmtEntry
// if its not already present
bool FlowMgmtTree::Add(FlowMgmtKey *key, FlowEntry *flow) {
    FlowMgmtEntry *entry = Locate(key);
    if (entry == NULL) {
        return false;
    }

    return entry->Add(flow);
}

bool FlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end()) {
        return false;
    }

    FlowMgmtEntry *entry = it->second;
    bool ret = entry->Delete(flow);

    TryDelete(it->first, entry);
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Event handler for add/delete/change of an object
/////////////////////////////////////////////////////////////////////////////

// Send DELETE Entry message to FlowTable module
void FlowMgmtTree::FreeNotify(FlowMgmtKey *key, uint32_t gen_id) {
    assert(key->db_entry() != NULL);
    FlowEvent::Event event = key->FreeDBEntryEvent();
    if (event == FlowEvent::INVALID)
        return;

    FlowEvent resp(event, key->db_entry(), gen_id);
    mgr_->EnqueueFlowEvent(resp);
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
bool FlowMgmtEntry::Add(FlowEntry *flow) {
    std::pair<Tree::iterator, bool> ret = tree_.insert(flow);
    return ret.second;
}

bool FlowMgmtEntry::Delete(FlowEntry *flow) {
    tree_.erase(flow);
    return tree_.size();
}

// An entry *cannot* be deleted if 
//    - It contains flows
//    - It has seen ADD but not seen any DELETE
bool FlowMgmtEntry::CanDelete() const {
    assert(oper_state_ != INVALID);
    if (tree_.size())
        return false;

    return (oper_state_ != OPER_ADD_SEEN);
}

// Handle Add/Change event for the object
bool FlowMgmtEntry::OperEntryAdd(FlowMgmtManager *mgr,
                                 const FlowMgmtRequest *req, FlowMgmtKey *key) {
    oper_state_ = OPER_ADD_SEEN;
    FlowEvent::Event event = req->GetResponseEvent();
    if (event == FlowEvent::INVALID)
        return false;

    FlowEvent flow_resp(event, NULL, key->db_entry());
    key->KeyToFlowRequest(&flow_resp);
    Tree::iterator it = tree_.begin();
    while (it != tree_.end()) {
        flow_resp.set_flow(*it);
        mgr->EnqueueFlowEvent(flow_resp);
        it++;
    }

    return true;
}

bool FlowMgmtEntry::OperEntryChange(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key) {
    return OperEntryAdd(mgr, req, key);
}

// Handle Delete event for the object
bool FlowMgmtEntry::OperEntryDelete(FlowMgmtManager *mgr,
                                    const FlowMgmtRequest *req,
                                    FlowMgmtKey *key) {
    oper_state_ = OPER_DEL_SEEN;
    gen_id_ = req->gen_id();
    FlowEvent::Event event = req->GetResponseEvent();
    if (event == FlowEvent::INVALID)
        return false;

    FlowEvent flow_resp(event, NULL, key->db_entry());
    key->KeyToFlowRequest(&flow_resp);
    Tree::iterator it = tree_.begin();
    while (it != tree_.end()) {
        flow_resp.set_flow(*it);
        mgr->EnqueueFlowEvent(flow_resp);
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
    FlowMgmtEntry::Tree::iterator fe_tree_it = tree_.begin();
    while (fe_tree_it != tree_.end() && (count + 1) < last_count) {
        fe_tree_it++;
        count++;
    }
    data.set_flow_count(Size());
    data.set_flow_miss(flow_miss_);
    std::vector<FlowSandeshData> flow_entries_l;
    while(fe_tree_it != tree_.end()) {
        const FlowEntry *fe = *fe_tree_it;
        FlowSandeshData fe_sandesh_data;
        fe->SetAclFlowSandeshData(acl, fe_sandesh_data, agent);

        flow_entries_l.push_back(fe_sandesh_data);
        count++;
        ++fe_tree_it;
        if (count == (MaxResponses + last_count) && fe_tree_it != tree_.end()) {
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
                           const AclEntryIDList *old_id_list) {
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
    return FlowMgmtEntry::Add(flow);
}

bool AclFlowMgmtEntry::Delete(const AclEntryIDList *id_list, FlowEntry *flow) {
    if (id_list->size()) {
        DecrementAceIdCountMap(id_list);
    }
    return FlowMgmtEntry::Delete(flow);
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
                          FlowMgmtKey *old_key) {
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
    return entry->Add(acl_key->ace_id_list(), flow, old_ace_id_list);
}

bool AclFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow) {
    Tree::iterator it = tree_.find(key);
    if (it == tree_.end()) {
        return false;
    }

    AclFlowMgmtKey *acl_key = static_cast<AclFlowMgmtKey *>(key);
    AclFlowMgmtEntry *entry = static_cast<AclFlowMgmtEntry *>(it->second);
    bool ret = entry->Delete(acl_key->ace_id_list(), flow);

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

bool VnFlowMgmtTree::Add(FlowMgmtKey *key, FlowEntry *flow) {
    tbb::mutex::scoped_lock mutex(mutex_);
    return FlowMgmtTree::Add(key, flow);
}

bool VnFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow) {
    tbb::mutex::scoped_lock mutex(mutex_);
    return FlowMgmtTree::Delete(key, flow);
}

bool VnFlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req,
                                  FlowMgmtKey *key) {
    tbb::mutex::scoped_lock mutex(mutex_);
    return FlowMgmtTree::OperEntryAdd(req, key);
}

bool VnFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                     FlowMgmtKey *key) {
    tbb::mutex::scoped_lock mutex(mutex_);
    return FlowMgmtTree::OperEntryDelete(req, key);
}

void VnFlowMgmtTree::VnFlowCounters(const VnEntry *vn,
                                    uint32_t *ingress_flow_count,
                                    uint32_t *egress_flow_count) {
    tbb::mutex::scoped_lock mutex(mutex_);
    *ingress_flow_count = 0;
    *egress_flow_count = 0;
    VnFlowMgmtKey key(vn);
    VnFlowMgmtEntry *entry = static_cast<VnFlowMgmtEntry *>(Find(&key));
    if (entry) {
        *ingress_flow_count = entry->ingress_flow_count();
        *egress_flow_count = entry->egress_flow_count();
    }
}

/////////////////////////////////////////////////////////////////////////////
// Interface Flow Management
/////////////////////////////////////////////////////////////////////////////
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
    if (flow->nh() == NULL)
        return;
    NhFlowMgmtKey *key = new NhFlowMgmtKey(flow->nh());
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *NhFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new NhFlowMgmtEntry();
}

/////////////////////////////////////////////////////////////////////////////
// Route Flow Management
/////////////////////////////////////////////////////////////////////////////
bool RouteFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow) {
    bool ret = FlowMgmtTree::Delete(key, flow);
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
        if (flow->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
            ExtractKeys(flow, tree, flow->data().flow_source_vrf,
                        flow->key().src_addr, flow->data().l2_rpf_plen);
        }
        return;
    }

    if (flow->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().flow_source_vrf,
                    flow->key().src_addr, flow->data().source_plen);
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
        next_key = static_cast<InetRouteFlowMgmtKey *>(UpperBound(&key));
    } else {
        InetRouteFlowMgmtKey key(vrf, Ip6Address(), 0);
        next_key = static_cast<InetRouteFlowMgmtKey *>(UpperBound(&key));
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
            FlowMgmtRequest rt_req(FlowMgmtRequest::ADD_DBENTRY, NULL, 0);
            RouteFlowMgmtTree::OperEntryAdd(&rt_req, covering_route);
        }
        rt_key->plen_ += 1;
    }

    return ret;
}

bool InetRouteFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                            FlowMgmtKey *key) {
    InetRouteFlowMgmtKey *rt_key = static_cast<InetRouteFlowMgmtKey *>(key);
    DelFromLPMTree(rt_key);
    return RouteFlowMgmtTree::OperEntryDelete(req, key);
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
        (UpperBound(&key));
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
    FlowMgmtTree::RetryDelete(&key);
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
    deleted_(false), table_ref_(this, table->deleter()),
    vrf_mgmt_entry_(vrf_mgmt_entry), vrf_(vrf) {
}

VrfFlowMgmtEntry::Data::~Data() {
    table_ref_.Reset(NULL);
}

void VrfFlowMgmtEntry::Data::ManagedDelete() {
    deleted_ = true;
    if (vrf_mgmt_entry_->CanDelete()) {
        vrf_mgmt_entry_->vrf_tree()->mgr()->RetryVrfDeleteEvent(vrf_);
    }
}
