/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/flow_mgmt/flow_mgmt_entry.h>
#include <pkt/flow_event.h>
#include <pkt/flow_mgmt/flow_mgmt_request.h>
#include <pkt/flow_mgmt.h>

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
    if (req->event() != FlowMgmtRequest::IMPLICIT_ROUTE_DELETE) {
        //If the delete is implicit there is no DB entry
        //and hence no free notify should be sent, hence
        //dont update the state to DEL SEEN
        oper_state_ = OPER_DEL_SEEN;
        gen_id_ = req->gen_id();
    }

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

string AclFlowMgmtEntry::GetAclFlowSandeshDataKey(const AclDBEntry *acl,
                                                  const int last_count) const {
    string uuid_str = UuidToString(acl->GetUuid());
    std::stringstream ss;
    ss << uuid_str << ":";
    ss << last_count;
    return ss.str();
}

string AclFlowMgmtEntry::GetAceSandeshDataKey(const AclDBEntry *acl,
                                              const std::string &ace_id) {
    string uuid_str = UuidToString(acl->GetUuid());
    std::stringstream ss;
    ss << uuid_str << ":";
    ss << ace_id;
    return ss.str();
}

void AclFlowMgmtEntry::FillAceFlowSandeshInfo(const AclDBEntry *acl,
                                              AclFlowCountResp &data,
                                              const std::string& ace_id) {
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
        data.set_iteration_key(GetAceSandeshDataKey(acl, Agent::NullString()));
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
        aceid_cnt_map_[id_it->id_] -= 1;
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
            aceid_cnt_map_[id_it->id_] += 1;
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

// Update health check on all the BgpAsAService flows
bool BgpAsAServiceFlowMgmtEntry::HealthCheckUpdate(
        Agent *agent, FlowMgmtManager *mgr,
        BgpAsAServiceFlowMgmtKey &key,
        BgpAsAServiceFlowMgmtRequest *req) {
    for (FlowList::iterator it = flow_list_.begin();
         it != flow_list_.end(); ++it) {
        FlowMgmtKeyNode *node = &(*it);
        BgpAsAServiceFlowMgmtKey *bkey =
            mgr->FindBgpAsAServiceInfo(node->flow_entry(), key);
        if (bkey == NULL)
            continue;

        if (req->type() == BgpAsAServiceFlowMgmtRequest::HEALTH_CHECK_ADD)
            bkey->StartHealthCheck(agent, node->flow_entry(),
                                   req->health_check_uuid());
        else
            bkey->StopHealthCheck(node->flow_entry());
    }
    return true;
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

bool InetRouteFlowMgmtEntry::HandleNhChange(FlowMgmtManager *mgr,
                                            const FlowMgmtRequest *req,
                                            FlowMgmtKey *key) {
    assert(req->event() == FlowMgmtRequest::DELETE_LAYER2_FLOW);

    FlowList::iterator it = flow_list_.begin();
    while (it != flow_list_.end()) {
        FlowEvent::Event event;
        FlowMgmtKeyNode *node = &(*it);
        it++;
        FlowEntry *fe = node->flow_entry();
        if (fe->l3_flow()) {
            event = FlowEvent::RECOMPUTE_FLOW;
        } else {
            event = FlowEvent::DELETE_FLOW;
        }

        mgr->DBEntryEvent(event, key, fe);
    }

    return true;
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
