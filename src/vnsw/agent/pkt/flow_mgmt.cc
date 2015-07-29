#include "pkt/flow_mgmt.h"
const string FlowMgmtManager::kFlowMgmtTask = "Flow::Management";

FlowMgmtManager::FlowMgmtManager(Agent *agent) : agent_(agent),
    event_queue_(agent_->task_scheduler()->GetTaskId(kFlowMgmtTask), 0,
                 boost::bind(&FlowMgmtManager::Run, this, _1)),
    acl_flow_mgmt_tree_(this), ace_id_flow_mgmt_tree_(this),
    interface_flow_mgmt_tree_(this), vn_flow_mgmt_tree_(this),
    vm_flow_mgmt_tree_(this), ip4_route_flow_mgmt_tree_(this),
    ip6_route_flow_mgmt_tree_(this), bridge_route_flow_mgmt_tree_(this),
    vrf_flow_mgmt_tree_(this), nh_flow_mgmt_tree_(this) {
}

void FlowMgmtManager::Init() {
}

void FlowMgmtManager::Shutdown() {
    event_queue_.Shutdown();
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void FlowMgmtManager::AddEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_FLOW, flow_ptr));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_FLOW, flow_ptr));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const VmInterface *vmi) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_INTERFACE, vmi));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const VmInterface *vmi) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_INTERFACE, vmi));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const AclDBEntry *acl) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_ACL, acl));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const AclDBEntry *acl) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_ACL, acl));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const VnEntry *vn) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_VN, vn));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const VnEntry *vn) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_VN, vn));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const NextHop *nh) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_NH, nh));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const NextHop *nh) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_NH, nh));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const AgentRoute *rt) {
    boost::shared_ptr<FlowMgmtRequest> req;
    const BridgeRouteEntry *bridge_rt =
        dynamic_cast<const BridgeRouteEntry *>(rt);
    if (bridge_rt) {
        req.reset(new FlowMgmtRequest(FlowMgmtRequest::ADD_BRIDGE_ROUTE, rt));
    }

    const InetUnicastRouteEntry *inet_uc_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (inet_uc_rt) {
        if (inet_uc_rt->addr().is_v4()) {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::ADD_INET4_ROUTE,rt));
        } else {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::ADD_INET6_ROUTE,rt));
        }
    }
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::ChangeEvent(const AgentRoute *rt) {
    boost::shared_ptr<FlowMgmtRequest> req;
    const BridgeRouteEntry *bridge_rt =
        dynamic_cast<const BridgeRouteEntry *>(rt);
    if (bridge_rt) {
        req.reset(new FlowMgmtRequest(FlowMgmtRequest::ADD_BRIDGE_ROUTE, rt));
    }

    const InetUnicastRouteEntry *inet_uc_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (inet_uc_rt) {
        if (inet_uc_rt->addr().is_v4()) {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::CHANGE_INET4_ROUTE,
                                          rt));
        } else {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::CHANGE_INET6_ROUTE,
                                          rt));
        }
    }
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const AgentRoute *rt) {
    boost::shared_ptr<FlowMgmtRequest> req;
    const BridgeRouteEntry *bridge_rt =
        dynamic_cast<const BridgeRouteEntry *>(rt);
    if (bridge_rt) {
        req.reset(new FlowMgmtRequest(FlowMgmtRequest::DELETE_BRIDGE_ROUTE,rt));
    }

    const InetUnicastRouteEntry *inet_uc_rt =
        dynamic_cast<const InetUnicastRouteEntry *>(rt);
    if (inet_uc_rt) {
        if (inet_uc_rt->addr().is_v4()) {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::DELETE_INET4_ROUTE,
                                          rt));
        } else {
            req.reset(new FlowMgmtRequest(FlowMgmtRequest::DELETE_INET6_ROUTE,
                                          rt));
        }
    }
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::AddEvent(const VrfEntry *vrf) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_VRF, vrf));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::DeleteEvent(const VrfEntry *vrf) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_VRF, vrf));
    event_queue_.Enqueue(req);
}

void FlowMgmtManager::RetryVrfDeleteEvent(const VrfEntry *vrf) {
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::RETRY_DELETE_VRF, vrf));
    event_queue_.Enqueue(req);
}

/////////////////////////////////////////////////////////////////////////////
// Handlers for events from the work-queue
/////////////////////////////////////////////////////////////////////////////
bool FlowMgmtManager::Run(boost::shared_ptr<FlowMgmtRequest> req) {
    switch (req->event()) {
    case FlowMgmtRequest::ADD_FLOW: {
        AddFlow(req->flow());
        break;
    }

    case FlowMgmtRequest::DELETE_FLOW: {
        DeleteFlow(req->flow());
        // On return from here reference to the flow is removed which can
        // result in deletion of flow from the tree. But, flow management runs
        // in parallel to flow processing. As a result, it can result in tree
        // being modified by two threads. Avoid the concurrency issue by
        // enqueuing a dummy request to flow-table queue. The reference will
        // be removed in flow processing context
        FlowTableRequest flow_req(FlowTableRequest::FREE_FLOW_REF,
                                  req->flow().get(), NULL);
        agent()->pkt()->flow_table()->FlowManagerMessageEnqueue(flow_req);
        break;
    }

    case FlowMgmtRequest::DELETE_INTERFACE: {
        InterfaceFlowMgmtKey key(static_cast<const Interface *>
                                 (req->db_entry()));
        interface_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_INTERFACE: {
        InterfaceFlowMgmtKey key(static_cast<const Interface *>
                                 (req->db_entry()));
        interface_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_VN: {
        VnFlowMgmtKey key(static_cast<const VnEntry *>(req->db_entry()));
        vn_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_VN: {
        VnFlowMgmtKey key(static_cast<const VnEntry *>(req->db_entry()));
        vn_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_ACL: {
        AclFlowMgmtKey key(static_cast<const AclDBEntry *>(req->db_entry()));
        acl_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_ACL: {
        AclFlowMgmtKey key(static_cast<const AclDBEntry *>(req->db_entry()));
        acl_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_NH: {
        NhFlowMgmtKey key(static_cast<const NextHop *>(req->db_entry()));
        nh_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_NH: {
        NhFlowMgmtKey key(static_cast<const NextHop *>(req->db_entry()));
        nh_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_INET4_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip4_route_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_INET4_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip4_route_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::CHANGE_INET4_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip4_route_flow_mgmt_tree_.OperEntryChange(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_INET6_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip6_route_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_INET6_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip6_route_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::CHANGE_INET6_ROUTE: {
        InetRouteFlowMgmtKey key(static_cast<const InetUnicastRouteEntry *>
                                 (req->db_entry()));
        ip6_route_flow_mgmt_tree_.OperEntryChange(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_BRIDGE_ROUTE: {
        BridgeRouteFlowMgmtKey key(static_cast<const BridgeRouteEntry *>
                                   (req->db_entry()));
        bridge_route_flow_mgmt_tree_.OperEntryAdd(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::DELETE_BRIDGE_ROUTE: {
        BridgeRouteFlowMgmtKey key(static_cast<const BridgeRouteEntry *>
                                   (req->db_entry()));
        bridge_route_flow_mgmt_tree_.OperEntryDelete(req.get(), &key);
        break;
    }

    case FlowMgmtRequest::ADD_VRF: {
        vrf_flow_mgmt_tree_.OperEntryAdd
            (static_cast<const VrfEntry *>(req->db_entry()));
        break;
    }

    case FlowMgmtRequest::DELETE_VRF: {
        vrf_flow_mgmt_tree_.OperEntryDelete
            (static_cast<const VrfEntry *>(req->db_entry()));
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
void FlowMgmtManager::MakeFlowMgmtKeyTree(FlowEntry *flow,
                                          FlowMgmtKeyTree *tree) {
    tbb::mutex::scoped_lock mutex(flow->mutex());
    acl_flow_mgmt_tree_.ExtractKeys(flow, tree);
    interface_flow_mgmt_tree_.ExtractKeys(flow, tree);
    vn_flow_mgmt_tree_.ExtractKeys(flow, tree);
    vm_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip4_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    ip6_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    bridge_route_flow_mgmt_tree_.ExtractKeys(flow, tree);
    nh_flow_mgmt_tree_.ExtractKeys(flow, tree);
}

void FlowMgmtManager::AddFlow(FlowEntryPtr &flow) {
    FlowInfo trace;
    flow->FillFlowInfo(trace);
    FLOW_TRACE(Trace, "ADD", trace);

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
            AddFlowMgmtKey(flow.get(), old_info, new_key);
            new_it++;
        } else if (old_key->IsLess(new_key)) {
            DeleteFlowMgmtKey(flow.get(), old_info, old_key);
            FlowMgmtKeyTree::iterator tmp = old_it++;
            FlowMgmtKey *key = *tmp;
            old_tree->erase(tmp);
            delete key;
        } else {
            AddFlowMgmtKey(flow.get(), old_info, new_key);
            old_it++;
            new_it++;
        }
    }

    while (new_it != new_tree.end()) {
        FlowMgmtKey *new_key = *new_it;
        AddFlowMgmtKey(flow.get(), old_info, new_key);
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
    FlowInfo trace;
    flow->FillFlowInfo(trace);
    FLOW_TRACE(Trace, "DEL", trace);

    // Delete entries for flow from the tree
    FlowEntryInfo *old_info = FindFlowEntryInfo(flow);
    if (old_info == NULL)
        return;

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

bool FlowMgmtManager::HasVrfFlows(uint32_t vrf_id) {
    if (ip4_route_flow_mgmt_tree_.HasVrfFlows(vrf_id)) {
        return true;
    }

    if (ip6_route_flow_mgmt_tree_.HasVrfFlows(vrf_id)) {
        return true;
    }

    if (bridge_route_flow_mgmt_tree_.HasVrfFlows(vrf_id)) {
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
                                     FlowMgmtKey *key) {
    FlowMgmtKey *tmp = key->Clone();
    std::pair<FlowMgmtKeyTree::iterator, bool> ret = info->tree_.insert(tmp);
    if (ret.second == false) {
        delete tmp;
    }

    switch (key->type()) {
    case FlowMgmtKey::INTERFACE:
        interface_flow_mgmt_tree_.Add(key, flow);
        break;

    case FlowMgmtKey::ACL:
        acl_flow_mgmt_tree_.Add(key, flow);
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

    case FlowMgmtKey::VM:
        vm_flow_mgmt_tree_.Add(key, flow);
        break;

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

    case FlowMgmtKey::VM:
        vm_flow_mgmt_tree_.Delete(key, flow);
        break;

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
        entry = Allocate();
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
        DeleteNotify(key);
    }

    Tree::iterator it = tree_.find(key);
    tree_.erase(it);
    delete entry;
    delete it->first;

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
void FlowMgmtTree::DeleteNotify(FlowMgmtKey *key) {
    assert(key->db_entry() != NULL);
    FlowTableRequest::Event event = key->FreeFlowTableEvent();
    if (event == FlowTableRequest::INVALID)
        return;

    FlowTableRequest req(event, NULL, key->db_entry());
    mgr_->agent()->pkt()->flow_table()->FlowManagerMessageEnqueue(req);
}

// An object is added/updated. Enqueue REVALUATE for flows dependent on it
bool FlowMgmtTree::OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Locate(key);
    entry->OperEntryAdd(req, key, mgr_->agent()->pkt()->flow_table());
    return true;
}

bool FlowMgmtTree::OperEntryChange(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    FlowMgmtEntry *entry = Find(key);
    if (entry) {
        entry->OperEntryChange(req, key, mgr_->agent()->pkt()->flow_table());
    }
    return true;
}

bool FlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                   FlowMgmtKey *key) {
    FlowTable *table = mgr_->agent()->pkt()->flow_table();
    FlowMgmtEntry *entry = Find(key);
    if (entry == NULL) {
        DeleteNotify(key);
        return true;
    }

    entry->OperEntryDelete(req, key, table);
    TryDelete(key, entry);
    return true;
}

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
bool FlowMgmtEntry::OperEntryAdd(const FlowMgmtRequest *req, FlowMgmtKey *key,
                                 FlowTable *table) {
    oper_state_ = OPER_ADD_SEEN;
    FlowTableRequest::Event event = req->ToFlowTableEvent();
    if (event == FlowTableRequest::INVALID)
        return false;

    FlowTableRequest flow_req(event, NULL, key->db_entry());
    key->KeyToFlowRequest(&flow_req);
    Tree::iterator it = tree_.begin();
    while (it != tree_.end()) {
        flow_req.flow_ = *it;
        table->FlowManagerMessageEnqueue(flow_req);
        it++;
    }

    return true;
}

bool FlowMgmtEntry::OperEntryChange(const FlowMgmtRequest *req, FlowMgmtKey *key,
                                    FlowTable *table) {
    return OperEntryAdd(req, key, table);
}

// Handle Delete event for the object
bool FlowMgmtEntry::OperEntryDelete(const FlowMgmtRequest *req,
                                    FlowMgmtKey *key, FlowTable *table) {
    oper_state_ = OPER_DEL_SEEN;
    FlowTableRequest::Event event = req->ToFlowTableEvent();
    if (event == FlowTableRequest::INVALID)
        return false;

    FlowTableRequest flow_req(event, NULL, key->db_entry());
    key->KeyToFlowRequest(&flow_req);
    Tree::iterator it = tree_.begin();
    while (it != tree_.end()) {
        flow_req.flow_ = *it;
        table->FlowManagerMessageEnqueue(flow_req);
        it++;
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Acl Flow Management
/////////////////////////////////////////////////////////////////////////////
void AclFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                  const MatchAclParamsList *acl_list) {
    std::list<MatchAclParams>::const_iterator it;
    for (it = acl_list->begin(); it != acl_list->end(); it++) {
        AclFlowMgmtKey *key = new AclFlowMgmtKey(it->acl.get());
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

FlowMgmtEntry *AclFlowMgmtTree::Allocate() {
    return new AclFlowMgmtEntry();
}

/////////////////////////////////////////////////////////////////////////////
// AclId Flow Management
/////////////////////////////////////////////////////////////////////////////
void AceIdFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                    const AclEntryIDList *ace_id_list) {
    AclEntryIDList::const_iterator it;
    for (it = ace_id_list->begin(); it != ace_id_list->end(); ++it) {
        AceIdFlowMgmtKey *key = new AceIdFlowMgmtKey(*it);
        AddFlowMgmtKey(tree, key);
    }
}

void AceIdFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                    const MatchAclParamsList *acl_list) {
    std::list<MatchAclParams>::const_iterator it;
    for (it = acl_list->begin(); it != acl_list->end(); it++) {
        ExtractKeys(flow, tree, &it->ace_id_list);
    }
}

void AceIdFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
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

FlowMgmtEntry *AceIdFlowMgmtTree::Allocate() {
    return new AceIdFlowMgmtEntry();
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

FlowMgmtEntry *VnFlowMgmtTree::Allocate() {
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

bool VnFlowMgmtTree::OperEntryAdd(FlowMgmtRequest *req, FlowMgmtKey *key) {
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
// VM Flow Management
/////////////////////////////////////////////////////////////////////////////
void VmFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree) {
    if (flow->in_vm_entry()) {
        VmFlowMgmtKey *key = new VmFlowMgmtKey(flow->in_vm_entry());
        AddFlowMgmtKey(tree, key);
    }

    if (flow->out_vm_entry()) {
        VmFlowMgmtKey *key = new VmFlowMgmtKey(flow->out_vm_entry());
        AddFlowMgmtKey(tree, key);
    }
}

FlowMgmtEntry *VmFlowMgmtTree::Allocate() {
    return new VmFlowMgmtEntry();
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

FlowMgmtEntry *InterfaceFlowMgmtTree::Allocate() {
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

FlowMgmtEntry *NhFlowMgmtTree::Allocate() {
    return new NhFlowMgmtEntry();
}
