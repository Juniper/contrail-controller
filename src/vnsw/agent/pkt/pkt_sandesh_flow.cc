/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/pkt_sandesh_flow.h>
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>
#include <algorithm>

#include <uve/agent_uve.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

using boost::system::error_code;

#define SET_SANDESH_FLOW_DATA(agent, data, fe, info)                               \
    data.set_vrf(fe->data().vrf);                                           \
    data.set_sip(fe->key().src_addr.to_string());                           \
    data.set_dip(fe->key().dst_addr.to_string());                           \
    data.set_src_port((unsigned)fe->key().src_port);                        \
    data.set_dst_port((unsigned)fe->key().dst_port);                        \
    data.set_protocol(fe->key().protocol);                                  \
    data.set_dest_vrf(fe->data().dest_vrf);                                 \
    data.set_action(fe->match_p().action_info.action);                      \
    std::vector<ActionStr> action_str_l;                                    \
    SetActionStr(fe->match_p().action_info, action_str_l);                  \
    if ((fe->match_p().action_info.action & TrafficAction::DROP_FLAGS) != 0) {\
        data.set_drop_reason(GetFlowDropReason(fe));                        \
    }                                                                       \
    data.set_action_str(action_str_l);                                      \
    std::vector<MirrorActionSpec>::const_iterator mait;                     \
    std::vector<MirrorInfo> mirror_l;                                       \
    for (mait = fe->match_p().action_info.mirror_l.begin();                 \
         mait != fe->match_p().action_info.mirror_l.end();                  \
         ++mait) {                                                          \
         MirrorInfo minfo;                                                  \
         minfo.set_mirror_destination((*mait).ip.to_string());              \
         minfo.set_mirror_port((*mait).port);                               \
         mirror_l.push_back(minfo);                                         \
    }                                                                       \
    data.set_mirror_l(mirror_l);                                            \
    if (fe->is_flags_set(FlowEntry::IngressDir)) {                          \
        data.set_direction("ingress");                                      \
    } else {                                                                \
        data.set_direction("egress");                                       \
    }                                                                       \
    if (info) {                                                             \
        data.set_stats_bytes(info->bytes());                                \
        data.set_stats_packets(info->packets());                            \
        data.set_underlay_source_port(info->underlay_source_port());        \
        data.set_setup_time(                                                \
            integerToString(UTCUsecToPTime(info->setup_time())));           \
        data.set_setup_time_utc(info->setup_time());                        \
        data.set_uuid(UuidToString(info->flow_uuid()));                     \
        if (fe->is_flags_set(FlowEntry::LocalFlow)) {                       \
            data.set_egress_uuid(UuidToString(info->egress_uuid()));        \
        }                                                                   \
    }                                                                       \
    if (fe->is_flags_set(FlowEntry::NatFlow)) {                             \
        data.set_nat("enabled");                                            \
    } else {                                                                \
        data.set_nat("disabled");                                           \
    }                                                                       \
    data.set_flow_handle(fe->flow_handle());                                \
    data.set_refcount(fe->GetRefCount());                                   \
    data.set_implicit_deny(fe->ImplicitDenyFlow() ? "yes" : "no");          \
    data.set_short_flow(                                                    \
            fe->is_flags_set(FlowEntry::ShortFlow) ?                        \
            string("yes (") + GetShortFlowReason(fe->short_flow_reason()) + \
            ")": "no");                                                     \
    data.set_local_flow(fe->is_flags_set(FlowEntry::LocalFlow) ? "yes" : "no");     \
    data.set_src_vn(fe->data().source_vn);                                  \
    data.set_dst_vn(fe->data().dest_vn);                                    \
    if (fe->is_flags_set(FlowEntry::EcmpFlow) &&                            \
        fe->data().component_nh_idx != CompositeNH::kInvalidComponentNHIdx) { \
        data.set_ecmp_index(fe->data().component_nh_idx);                     \
    }                                                                       \
    data.set_reverse_flow(fe->is_flags_set(FlowEntry::ReverseFlow) ? "yes" : "no"); \
    Ip4Address fip(fe->fip());                                              \
    data.set_fip(fip.to_string());                                          \
    uint32_t fip_intf_id = fe->InterfaceKeyToId(agent, fe->fip_vmi());      \
    data.set_fip_vm_interface_idx(fip_intf_id);                             \
    SetAclInfo(data, fe);                                                   \
    data.set_nh(fe->key().nh);                                              \
    if (fe->data().nh.get() != NULL) {                                      \
        data.set_rpf_nh(fe->data().nh.get()->id());                         \
    }                                                                       \
    data.set_peer_vrouter(fe->peer_vrouter());                            \
    data.set_tunnel_type(fe->tunnel_type().ToString());                     \
    data.set_enable_rpf(fe->data().enable_rpf);\
    if (fe->fsc()) {\
       data.set_aging_protocol(fe->fsc()->flow_aging_key().proto);\
       data.set_aging_port(fe->fsc()->flow_aging_key().port);\
    }\

const std::string PktSandeshFlow::start_key = "0-0-0-0-0.0.0.0-0.0.0.0";

////////////////////////////////////////////////////////////////////////////////

static const char * GetShortFlowReason(uint16_t reason) {
    switch (reason) {
    case FlowEntry::SHORT_UNAVIALABLE_INTERFACE:
        return "Interface unavialable";
    case FlowEntry::SHORT_IPV4_FWD_DIS:
        return "Ipv4 forwarding disabled";
    case FlowEntry::SHORT_UNAVIALABLE_VRF:
        return "VRF unavailable";
    case FlowEntry::SHORT_NO_SRC_ROUTE:
        return "No Source route";
    case FlowEntry::SHORT_NO_DST_ROUTE:
        return "No Destination route";
    case FlowEntry::SHORT_AUDIT_ENTRY:
        return "Audit Entry";
    case FlowEntry::SHORT_VRF_CHANGE:
        return "VRF Change";
    case FlowEntry::SHORT_NO_REVERSE_FLOW:
        return "No Reverse flow";
    case FlowEntry::SHORT_REVERSE_FLOW_CHANGE:
        return "Reverse flow change";
    case FlowEntry::SHORT_NAT_CHANGE:
        return "NAT Changed";
    case FlowEntry::SHORT_FLOW_LIMIT:
        return "Flow Limit Reached";
    case FlowEntry::SHORT_LINKLOCAL_SRC_NAT:
        return "Linklocal source NAT failed";
    default:
        break;
    }
    return "Unknown";
}

static const char * GetFlowDropReason(FlowEntry *fe) {
    switch (fe->data().drop_reason) {
    case FlowEntry::DROP_POLICY:
        return "Policy";
    case FlowEntry::DROP_OUT_POLICY:
        return "Out Policy";
    case FlowEntry::DROP_SG:
        return "SG";
    case FlowEntry::DROP_OUT_SG:
        return "Out SG";
    case FlowEntry::DROP_REVERSE_SG:
        return "Reverse SG";
    case FlowEntry::DROP_REVERSE_OUT_SG:
        return "Reverse Out SG";
    default:
        break;
    }
    return GetShortFlowReason(fe->data().drop_reason);;
}

static void SetOneAclInfo(FlowAclInfo *policy, uint32_t action,
                          const MatchAclParamsList &acl_list)  {
    MatchAclParamsList::const_iterator it;
    std::vector<FlowAclUuid> acl;

    for (it = acl_list.begin(); it != acl_list.end(); it++) {
        FlowAclUuid f;
        f.uuid = UuidToString(it->acl->GetUuid());
        acl.push_back(f);
    }
    policy->set_acl(acl);
    policy->set_action(action);

    std::vector<ActionStr> action_str_l;
    for (it = acl_list.begin(); it != acl_list.end(); it++) {
    FlowAction action_info = it->action_info;
        action_info.action = action;
        SetActionStr(action_info, action_str_l);
    }
    policy->set_action_str(action_str_l);
}

static void SetAclInfo(SandeshFlowData &data, FlowEntry *fe) {
    FlowAclInfo policy;

    SetOneAclInfo(&policy, fe->match_p().policy_action, fe->match_p().m_acl_l);
    data.set_policy(policy);

    SetOneAclInfo(&policy, fe->match_p().out_policy_action,
                  fe->match_p().m_out_acl_l);
    data.set_out_policy(policy);

    SetOneAclInfo(&policy, fe->match_p().sg_action, fe->match_p().m_sg_acl_l);
    data.set_sg(policy);

    SetOneAclInfo(&policy, fe->match_p().out_sg_action,
                  fe->match_p().m_out_sg_acl_l);
    data.set_out_sg(policy);

    SetOneAclInfo(&policy, fe->match_p().reverse_sg_action,
                  fe->match_p().m_reverse_sg_acl_l);
    data.set_reverse_sg(policy);

    SetOneAclInfo(&policy, fe->match_p().reverse_out_sg_action,
                  fe->match_p().m_reverse_out_sg_acl_l);
    data.set_reverse_out_sg(policy);

    SetOneAclInfo(&policy, fe->match_p().vrf_assign_acl_action,
                  fe->match_p().m_vrf_assign_acl_l);
    data.set_vrf_assign_acl(policy);

    FlowAction action_info;
    action_info.action = fe->match_p().sg_action_summary;
    std::vector<ActionStr> action_str_l;
    SetActionStr(action_info, action_str_l);
    data.set_sg_action_summary(action_str_l);

    SetOneAclInfo(&policy, fe->match_p().mirror_action,
                  fe->match_p().m_mirror_acl_l);
    data.set_mirror(policy);

    SetOneAclInfo(&policy, fe->match_p().out_mirror_action,
                  fe->match_p().m_out_mirror_acl_l);
    data.set_out_mirror(policy);

    data.set_sg_rule_uuid(fe->sg_rule_uuid());
    data.set_nw_ace_uuid(fe->nw_ace_uuid());
}

////////////////////////////////////////////////////////////////////////////////

PktSandeshFlow::PktSandeshFlow(Agent *agent, FlowRecordsResp *obj,
                               std::string resp_ctx, std::string key) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::PktFlowResponder")),
          0), resp_obj_(obj), resp_data_(resp_ctx), 
    flow_iteration_key_(), key_valid_(false), delete_op_(false), agent_(agent) {
    if (key != agent_->NullString()) {
        if (SetFlowKey(key)) {
            key_valid_ = true;
        }
    }
}

PktSandeshFlow::~PktSandeshFlow() {
}

void PktSandeshFlow::SetSandeshFlowData(std::vector<SandeshFlowData> &list,
                                        FlowEntry *fe, const FlowExportInfo *info) {
    SandeshFlowData data;
    SET_SANDESH_FLOW_DATA(agent_, data, fe, info);
    list.push_back(data);
}

void PktSandeshFlow::SendResponse(SandeshResponse *resp) {
    resp->set_context(resp_data_);
    resp->set_more(false);
    resp->Response();
}

string PktSandeshFlow::GetFlowKey(const FlowKey &key) {
    stringstream ss;
    ss << key.nh << kDelimiter;
    ss << key.src_port << kDelimiter;
    ss << key.dst_port << kDelimiter;
    ss << (uint16_t)key.protocol << kDelimiter;
    ss << key.src_addr.to_string() << kDelimiter;
    ss << key.dst_addr.to_string();
    return ss.str();
}

bool PktSandeshFlow::SetFlowKey(string key) {
    const char ch = kDelimiter;
    size_t n = std::count(key.begin(), key.end(), ch);
    if (n != 5) {
        return false;
    }
    stringstream ss(key);
    string item, sip, dip;
    uint32_t proto;
    if (getline(ss, item, ch)) {
        istringstream(item) >> flow_iteration_key_.nh;
    }
    if (getline(ss, item, ch)) {
        istringstream(item) >> flow_iteration_key_.src_port;
    }
    if (getline(ss, item, ch)) {
        istringstream(item) >> flow_iteration_key_.dst_port;
    }
    if (getline(ss, item, ch)) {
        istringstream(item) >> proto;
    }
    if (getline(ss, item, ch)) {
        sip = item;
    }
    if (getline(ss, item, ch)) {
        dip = item;
    }

    error_code ec;
    flow_iteration_key_.src_addr = IpAddress::from_string(sip.c_str(), ec);
    flow_iteration_key_.dst_addr = IpAddress::from_string(dip.c_str(), ec);
    if (flow_iteration_key_.src_addr.is_v4()) {
        flow_iteration_key_.family = Address::INET;
    } else if (flow_iteration_key_.src_addr.is_v6()) {
        flow_iteration_key_.family = Address::INET6;
    }
    flow_iteration_key_.protocol = proto;
    return true;
}

// FIXME : Should handle multiple flow tables
bool PktSandeshFlow::Run() {
    FlowTable::FlowEntryMap::iterator it;
    std::vector<SandeshFlowData>& list =
        const_cast<std::vector<SandeshFlowData>&>(resp_obj_->get_flow_list());
    int count = 0;
    bool flow_key_set = false;
    FlowTable *flow_obj = agent_->pkt()->flow_table(0);

    if (delete_op_) {
        flow_obj->DeleteAll();
        SendResponse(resp_obj_);
        return true;
    }

    if (key_valid_) {
        it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    } else {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }
    FlowStatsCollector *fec =
        agent_->flow_stats_manager()->default_flow_stats_collector();
    while (it != flow_obj->flow_entry_map_.end()) {
        FlowEntry *fe = it->second;
        const FlowExportInfo *info = fec->FindFlowExportInfo(fe->key());
        SetSandeshFlowData(list, fe, info);
        ++it;
        count++;
        if (count == kMaxFlowResponse) {
            if (it != flow_obj->flow_entry_map_.end()) {
                resp_obj_->set_flow_key(GetFlowKey(fe->key()));
                flow_key_set = true;
            }
            break;
        }
    }
    if (!flow_key_set) {
        resp_obj_->set_flow_key(PktSandeshFlow::start_key);
    }
    SendResponse(resp_obj_);
    return true;
}

////////////////////////////////////////////////////////////////////////////////

void NextFlowRecordsSet::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowRecordsResp *resp = new FlowRecordsResp();
    PktSandeshFlow *task = new PktSandeshFlow(agent, resp, context(),
                                              get_flow_key());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void FetchAllFlowRecords::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowRecordsResp *resp = new FlowRecordsResp();
    PktSandeshFlow *task = new PktSandeshFlow(agent, resp, context(),
                                              PktSandeshFlow::start_key);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void DeleteAllFlowRecords::HandleRequest() const {
    FlowRecordsResp *resp = new FlowRecordsResp();

    PktSandeshFlow *task = new PktSandeshFlow(Agent::GetInstance(), resp,
                                              context(),
                                              PktSandeshFlow::start_key);
    task->set_delete_op(true);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void FetchFlowRecord::HandleRequest() const {
    FlowKey key;
    Agent *agent = Agent::GetInstance();
    key.nh = get_nh();
    error_code ec;
    key.src_addr = IpAddress::from_string(get_sip(), ec);
    key.dst_addr = IpAddress::from_string(get_dip(), ec);
    if (key.src_addr.is_v4()) {
        key.family = Address::INET;
    } else if (key.src_addr.is_v6()) {
        key.family = Address::INET6;
    }
    key.src_port = (unsigned)get_src_port();
    key.dst_port = (unsigned)get_dst_port();
    key.protocol = get_protocol();

    FlowTable::FlowEntryMap::iterator it;
    FlowTable *flow_obj = agent->pkt()->flow_table(0);
    FlowStatsCollector *fec =
        agent->flow_stats_manager()->default_flow_stats_collector();
    it = flow_obj->flow_entry_map_.find(key);
    SandeshResponse *resp;
    if (it != flow_obj->flow_entry_map_.end()) {
        FlowRecordResp *flow_resp = new FlowRecordResp();
        FlowEntry *fe = it->second;
        FlowExportInfo *info = fec->FindFlowExportInfo(fe->key());
        SandeshFlowData data;
        SET_SANDESH_FLOW_DATA(agent, data, fe, info);
        flow_resp->set_record(data);
        resp = flow_resp;
    } else {
        resp = new FlowErrorResp();
    }
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

// Sandesh interface to modify flow aging interval
// Intended for use in testing only
void FlowAgeTimeReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    uint32_t age_time = get_new_age_time();

    FlowStatsCollector *collector =
        agent->flow_stats_manager()->default_flow_stats_collector();

    FlowAgeTimeResp *resp = new FlowAgeTimeResp();
    if (collector == NULL) {
        goto done;
    }
    resp->set_old_age_time(collector->flow_age_time_intvl_in_secs());

    if (age_time && age_time != resp->get_old_age_time()) {
        collector->UpdateFlowAgeTimeInSecs(age_time);
        resp->set_new_age_time(age_time);
    } else {
        resp->set_new_age_time(resp->get_old_age_time());
    }
done:
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void FetchLinkLocalFlowInfo::HandleRequest() const {
    LinkLocalFlowInfoResp *resp = new LinkLocalFlowInfoResp();
    std::vector<LinkLocalFlowInfo> &list =
        const_cast<std::vector<LinkLocalFlowInfo>&>
        (resp->get_linklocal_flow_list());

    const FlowTable::LinkLocalFlowInfoMap &flow_map =
        Agent::GetInstance()->pkt()->flow_table(0)->linklocal_flow_info_map();
    FlowTable::LinkLocalFlowInfoMap::const_iterator it = flow_map.begin();
    while (it != flow_map.end()) {
        LinkLocalFlowInfo info;
        info.fd = it->first;
        info.flow_index = it->second.flow_index;
        info.source_addr = it->second.flow_key.src_addr.to_string();
        info.dest_addr = it->second.flow_key.dst_addr.to_string();
        info.protocol = it->second.flow_key.protocol;
        info.source_port = it->second.flow_key.src_port;
        info.dest_port = it->second.flow_key.dst_port;
        info.timestamp = integerToString(UTCUsecToPTime(it->second.timestamp));
        list.push_back(info);
        ++it;
    }

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

bool PktSandeshFlowStats::Run() {
    std::vector<SandeshFlowData>& list =
        const_cast<std::vector<SandeshFlowData>&>(resp_->get_flow_list());
    int count = 0;
    bool flow_key_set = false;

    FlowTable *flow_obj = agent_->pkt()->flow_table(0);
    FlowStatsManager *fm = agent_->flow_stats_manager();
    const FlowStatsCollector *fsc = fm->Find(proto_, port_);
    if (!fsc) {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }

    FlowTable::FlowEntryMap::iterator it;
    if (key_valid_) {
        it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    } else {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }
 
    while (it != flow_obj->flow_entry_map_.end()) {
        FlowEntry *fe = it->second;
        const FlowExportInfo *info = fsc->FindFlowExportInfo(fe->key());
        SetSandeshFlowData(list, fe, info);
        ++it;
        count++;
        if (count == kMaxFlowResponse) {
            if (it != flow_obj->flow_entry_map_.end()) {
                ostringstream ostr;
                ostr << proto_ << ":" << port_ << ":"
                    << GetFlowKey(fe->key());
                resp_->set_flow_key(ostr.str());
                flow_key_set = true;
            }
            break;
        }
    }

    if (!flow_key_set) {
        ostringstream ostr;
        ostr << proto_ << ":" << port_ << ":" << "0x0";
        resp_->set_flow_key(ostr.str());
    }
    SendResponse(resp_);
    return true;
}

bool PktSandeshFlowStats::SetProto(string &key) {
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 3) {
        return false;
    }
    stringstream ss(key);
    string item;
    if (getline(ss, item, ':')) {
        istringstream(item) >> proto_;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> port_;
    }

    long flow_ptr;
    if (getline(ss, item)) {
        istringstream(item) >> flow_ptr;
    }

    flow_ptr_ = (FlowEntry *)(flow_ptr);
    return true;
}

PktSandeshFlowStats::PktSandeshFlowStats(Agent *agent, FlowStatsCollectorRecordsResp *obj,
                                         std::string resp_ctx, std::string key):
    PktSandeshFlow(agent, NULL, resp_ctx, key), resp_(obj), flow_ptr_(NULL) {
    if (key != agent_->NullString()) {
        if (SetProto(key)) {
            key_valid_ = true;
        }
    }
}

void ShowFlowStatsCollector::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowStatsCollectorRecordsResp *resp = new FlowStatsCollectorRecordsResp();

    ostringstream ostr;
    ostr << get_protocol() << ":" << get_port() << ":" <<
        PktSandeshFlow::start_key;
    PktSandeshFlowStats *task = new PktSandeshFlowStats(agent, resp, context(),
                                                        ostr.str());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void NextFlowStatsRecordsSet::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowStatsCollectorRecordsResp *resp = new FlowStatsCollectorRecordsResp();

    PktSandeshFlow *task = new PktSandeshFlowStats(agent, resp, context(),
                                                   get_flow_key());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}
////////////////////////////////////////////////////////////////////////////////
