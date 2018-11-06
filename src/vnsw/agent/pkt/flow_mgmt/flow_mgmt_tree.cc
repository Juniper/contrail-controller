/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/flow_mgmt/flow_mgmt_tree.h>
#include <pkt/flow_mgmt/flow_mgmt_key.h>
#include <pkt/flow_mgmt/flow_mgmt_entry.h>
#include <pkt/flow_mgmt/flow_mgmt_request.h>
#include <pkt/flow_mgmt.h>

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

void BgpAsAServiceFlowMgmtTree::FreeNotify(FlowMgmtKey *key, uint32_t gen_id) {
    assert(key->db_entry() == NULL);
}

void BgpAsAServiceFlowMgmtTree::ExtractKeys(FlowEntry *flow,
                                            FlowMgmtKeyTree *tree) {
    if (flow->is_flags_set(FlowEntry::BgpRouterService) == false)
        return;
    const VmInterface *vm_intf =
        dynamic_cast<const VmInterface *>(flow->intf_entry());
    if (!vm_intf || (flow->bgp_as_a_service_sport() == 0))
        return;

    BgpAsAServiceFlowMgmtKey *key =
        new BgpAsAServiceFlowMgmtKey(vm_intf->GetUuid(),
                                 flow->bgp_as_a_service_sport(),
                                 index_, NULL, NULL);
    AddFlowMgmtKey(tree, key);
}

FlowMgmtEntry *BgpAsAServiceFlowMgmtTree::Allocate(const FlowMgmtKey *key) {
    return new BgpAsAServiceFlowMgmtEntry();
}

// Update health check on the BgpAsAService entry
bool BgpAsAServiceFlowMgmtTree::BgpAsAServiceHealthCheckUpdate
    (Agent *agent, BgpAsAServiceFlowMgmtKey &key,
     BgpAsAServiceFlowMgmtRequest *req) {
    FlowMgmtEntry *entry = Find(&key);
    if (entry == NULL) {
        return true;
    }

    BgpAsAServiceFlowMgmtEntry *bgpaas_entry =
        static_cast<BgpAsAServiceFlowMgmtEntry *>(entry);
    return bgpaas_entry->HealthCheckUpdate(agent, mgr_, key, req);
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

/////////////////////////////////////////////////////////////////////////////
// Acl Flow Management
/////////////////////////////////////////////////////////////////////////////
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
    ExtractKeys(flow, tree, &flow->match_p().m_out_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().sg_policy.m_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().sg_policy.m_out_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().sg_policy.m_reverse_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().sg_policy.m_reverse_out_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_mirror_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_out_mirror_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().m_vrf_assign_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().aps_policy.m_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().aps_policy.m_out_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().fwaas_policy.m_acl_l);
    ExtractKeys(flow, tree, &flow->match_p().fwaas_policy.m_out_acl_l);
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

    if (req->db_entry() == NULL) {
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
    VrfEntry *vrfp = mgr_->agent()->vrf_table()->FindVrfFromId(vrf);
    if (vrfp == NULL) {
        return;
    }

    InetRouteFlowMgmtKey *key = NULL;
    if (flow->l3_flow()) {
        if (ip.is_v4()) {
            Ip4Address ip4 = Address::GetIp4SubnetAddress(ip.to_v4(), plen);
            key = new InetRouteFlowMgmtKey(vrf, ip4, plen);
        } else {
            Ip6Address ip6 = Address::GetIp6SubnetAddress(ip.to_v6(), plen);
            key = new InetRouteFlowMgmtKey(vrf, ip6, plen);
        }
    } else {
        InetUnicastRouteEntry *rt = vrfp->GetUcRoute(ip);
        if (rt) {
            key = new InetRouteFlowMgmtKey(vrf, rt->addr(), rt->plen());
        }
    }

    if (key) {
        AddFlowMgmtKey(tree, key);
    }
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

    if (flow->data().src_policy_vrf != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().src_policy_vrf,
                    flow->key().src_addr, flow->data().src_policy_plen);
    }

    if (flow->data().dst_policy_vrf != VrfEntry::kInvalidIndex) {
        ExtractKeys(flow, tree, flow->data().dst_policy_vrf,
                    flow->key().dst_addr, flow->data().dst_policy_plen);
    }

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
    } else if (type == Agent::INET6_UNICAST) {
        InetRouteFlowMgmtKey key(vrf, Ip6Address(), 0);
        next_key = static_cast<InetRouteFlowMgmtKey *>(LowerBound(&key));
    } else {
        return false;
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

bool InetRouteFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                            FlowMgmtKey *key) {
    InetRouteFlowMgmtKey *rt_key = static_cast<InetRouteFlowMgmtKey *>(key);
    DelFromLPMTree(rt_key);
    return RouteFlowMgmtTree::OperEntryDelete(req, key);
}

bool InetRouteFlowMgmtTree::RouteNHChangeEvent(const FlowMgmtRequest *req,
                                               FlowMgmtKey *key) {
    InetRouteFlowMgmtEntry *entry = static_cast<InetRouteFlowMgmtEntry*>
        (Find(key));
    if (entry == NULL) {
        return true;
    }

    return entry->HandleNhChange(mgr_, req, key);
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

void VrfFlowMgmtTree::DeleteDefaultRoute(const VrfEntry *vrf) {
    //If VMI is associated to FIP, then all non floating-ip
    //traffic would also be dependent FIP VRF route. This is
    //to ensure that if more specific route gets added preference
    //would be given to floating-ip
    //
    //Assume a sceanrio where traffic is not NATed, then flow would
    //add a dependency on default route(assume no default route is
    //present in FIP VRF). Now if FIP VRF is deleted there is no explicit
    //trigger to delete this dependencyi and hence delay in releasing VRF
    //reference, hence if default route DB entry is not present
    //impliticly delete the default route so that flow could get
    InetRouteFlowMgmtKey key(vrf->vrf_id(), Ip4Address(0), 0);
    FlowMgmtEntry *route_entry = mgr_->ip4_route_flow_mgmt_tree()->Find(&key);
    if (route_entry == NULL ||
        route_entry->oper_state() != FlowMgmtEntry::OPER_NOT_SEEN) {
        //If entry is not present on it has corresponding DB entry
        //no need for implicit delete
        return;
    }

    FlowMgmtRequest route_req(FlowMgmtRequest::IMPLICIT_ROUTE_DELETE);
    FlowMgmtManager::ProcessEvent(
        &route_req, &key, mgr_->ip4_route_flow_mgmt_tree());
}

bool VrfFlowMgmtTree::OperEntryDelete(const FlowMgmtRequest *req,
                                      FlowMgmtKey *key) {
    const VrfEntry* vrf = static_cast<const VrfEntry *>(req->db_entry());
    DeleteDefaultRoute(vrf);

    return FlowMgmtTree::OperEntryDelete(req, key);
}
