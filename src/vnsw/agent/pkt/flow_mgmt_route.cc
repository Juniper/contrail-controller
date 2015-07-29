#include <pkt/flow_mgmt.h>

/////////////////////////////////////////////////////////////////////////////
// Route Flow Management
/////////////////////////////////////////////////////////////////////////////
bool RouteFlowMgmtTree::Delete(FlowMgmtKey *key, FlowEntry *flow) {
    bool ret = FlowMgmtTree::Delete(key, flow);
    RouteFlowMgmtKey *route_key = static_cast<RouteFlowMgmtKey *>(key);
    mgr_->RetryVrfDelete(route_key->vrf_id());
    return ret;
}

bool RouteFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                        FlowMgmtKey *key) {
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

    Tree::iterator it = tree_.find(key);
    if (it != tree_.end()) {
        it->first->set_db_entry(req->db_entry());
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Inet Route Flow Management
/////////////////////////////////////////////////////////////////////////////
void InetRouteFlowMgmtTree::ExtractKeys(FlowEntry *flow, FlowMgmtKeyTree *tree,
                                        uint32_t vrf, const IpAddress &ip,
                                        uint8_t plen) {
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

FlowMgmtEntry *InetRouteFlowMgmtTree::Allocate() {
    return new InetRouteFlowMgmtEntry();
}

bool InetRouteFlowMgmtTree::HasVrfFlows(uint32_t vrf) {
    InetRouteFlowMgmtKey key(vrf, Ip4Address(0), 0);
    InetRouteFlowMgmtKey *next_key = static_cast<InetRouteFlowMgmtKey *>
        (UpperBound(&key));
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
            FlowMgmtRequest rt_req(FlowMgmtRequest::ADD_INET4_ROUTE, NULL);
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

    if (flow->data().flow_source_vrf != VrfEntry::kInvalidIndex) {
        BridgeRouteFlowMgmtKey *key =
            new BridgeRouteFlowMgmtKey(flow->data().flow_source_vrf,
                                       flow->data().smac);
        AddFlowMgmtKey(tree, key);
    }
    if (flow->data().flow_dest_vrf != VrfEntry::kInvalidIndex) {
        BridgeRouteFlowMgmtKey *key =
            new BridgeRouteFlowMgmtKey(flow->data().flow_dest_vrf,
                                       flow->data().smac);
        AddFlowMgmtKey(tree, key);
    }
}

FlowMgmtEntry *BridgeRouteFlowMgmtTree::Allocate() {
    return new InetRouteFlowMgmtEntry();
}

bool BridgeRouteFlowMgmtTree::HasVrfFlows(uint32_t vrf) {
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
void VrfFlowMgmtTree::OperEntryAdd(const VrfEntry *vrf) {
    Tree::iterator it = tree_.find(vrf->vrf_id());
    if (it != tree_.end())
        return;

    tree_.insert(make_pair(vrf->vrf_id(), new VrfFlowMgmtEntry(this, vrf)));
}

void VrfFlowMgmtTree::OperEntryDelete(const VrfEntry *vrf) {
    RetryDelete(vrf->vrf_id());
}

void VrfFlowMgmtTree::RetryDelete(uint32_t vrf_id) {
    Tree::iterator it = tree_.find(vrf_id);
    if (it == tree_.end())
        return;

    if (it->second->CanDelete() == false) {
        return;
    }

    FlowTable *table = mgr_->agent()->pkt()->flow_table();
    FlowTableRequest req(FlowTableRequest::DELETE_OBJECT_VRF, NULL,
                         it->second->vrf_);
    table->FlowManagerMessageEnqueue(req);

    tree_.erase(it);
    delete it->second;

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
