/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/pkt_sandesh_flow.h>
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>
#include <algorithm>

using boost::system::error_code;

#define SET_SANDESH_FLOW_DATA(data, fe)                                     \
    data.set_vrf(fe->data().vrf);                                           \
    Ip4Address sip(fe->key().src.ipv4);                                     \
    data.set_sip(sip.to_string());                                          \
    Ip4Address dip(fe->key().dst.ipv4);                                     \
    data.set_dip(dip.to_string());                                          \
    data.set_src_port((unsigned)fe->key().src_port);                        \
    data.set_dst_port((unsigned)fe->key().dst_port);                        \
    data.set_protocol(fe->key().protocol);                                  \
    data.set_dest_vrf(fe->data().dest_vrf);                                 \
    data.set_action(fe->match_p().action_info.action);                      \
    std::vector<ActionStr> action_str_l;                                    \
    SetActionStr(fe->match_p().action_info, action_str_l);                  \
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
    data.set_stats_bytes(fe->stats().bytes);                                \
    data.set_stats_packets(fe->stats().packets);                            \
    data.set_uuid(UuidToString(fe->flow_uuid()));                           \
    if (fe->is_flags_set(FlowEntry::NatFlow)) {                             \
        data.set_nat("enabled");                                            \
    } else {                                                                \
        data.set_nat("disabled");                                           \
    }                                                                       \
    data.set_flow_handle(fe->flow_handle());                                \
    data.set_interface_idx(fe->stats().intf_in);                            \
    data.set_setup_time(                                                    \
                    integerToString(UTCUsecToPTime(fe->stats().setup_time)));       \
    data.set_refcount(fe->GetRefCount());                                   \
    data.set_implicit_deny(fe->ImplicitDenyFlow() ? "yes" : "no");          \
    data.set_short_flow(fe->is_flags_set(FlowEntry::ShortFlow) ? "yes" : "no");     \
    data.set_local_flow(fe->is_flags_set(FlowEntry::LocalFlow) ? "yes" : "no");     \
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {                           \
        data.set_egress_uuid(UuidToString(fe->egress_uuid()));              \
    }                                                                       \
    data.set_src_vn(fe->data().source_vn);                                  \
    data.set_dst_vn(fe->data().dest_vn);                                    \
    data.set_setup_time_utc(fe->stats().setup_time);                        \
    if (fe->is_flags_set(FlowEntry::EcmpFlow) &&                            \
        fe->data().component_nh_idx != CompositeNH::kInvalidComponentNHIdx) { \
        data.set_ecmp_index(fe->data().component_nh_idx);                     \
    }                                                                       \
    data.set_reverse_flow(fe->is_flags_set(FlowEntry::ReverseFlow) ? "yes" : "no"); \
    SetAclInfo(data, fe);                                                   \
    data.set_nh(fe->key().nh);                                              \

const std::string PktSandeshFlow::start_key = "0:0:0:0:0.0.0.0:0.0.0.0";

////////////////////////////////////////////////////////////////////////////////

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
}

////////////////////////////////////////////////////////////////////////////////

PktSandeshFlow::PktSandeshFlow(FlowRecordsResp *obj, std::string resp_ctx,
                               std::string key) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::PktFlowResponder")),
          0), resp_obj_(obj), resp_data_(resp_ctx), 
    flow_iteration_key_(), key_valid_(false), delete_op_(false) {
    if (key != Agent::GetInstance()->NullString()) {
        if (SetFlowKey(key)) {
            key_valid_ = true;
        }
    }
}

PktSandeshFlow::~PktSandeshFlow() {
}

void PktSandeshFlow::SetSandeshFlowData(std::vector<SandeshFlowData> &list,
                                        FlowEntry *fe) {
    SandeshFlowData data;
    SET_SANDESH_FLOW_DATA(data, fe);
    list.push_back(data);
}

void PktSandeshFlow::SendResponse(SandeshResponse *resp) {
    resp->set_context(resp_data_);
    resp->set_more(false);
    resp->Response();
}

string PktSandeshFlow::GetFlowKey(const FlowKey &key) {
    stringstream ss;
    ss << key.nh << ":";
    ss << key.src_port << ":";
    ss << key.dst_port << ":";
    ss << (uint16_t)key.protocol << ":";
    ss << Ip4Address(key.src.ipv4).to_string() << ":";
    ss << Ip4Address(key.dst.ipv4).to_string();
    return ss.str();
}

bool PktSandeshFlow::SetFlowKey(string key) {
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 5) {
        return false;
    }
    stringstream ss(key);
    string item, sip, dip;
    uint32_t proto;
    if (getline(ss, item, ':')) {
        istringstream(item) >> flow_iteration_key_.nh;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> flow_iteration_key_.src_port;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> flow_iteration_key_.dst_port;
    }
    if (getline(ss, item, ':')) {
        istringstream(item) >> proto;
    }
    if (getline(ss, item, ':')) {
        sip = item;
    }
    if (getline(ss, item, ':')) {
        dip = item;
    }

    flow_iteration_key_.src.ipv4 = ntohl(inet_addr(sip.c_str()));
    flow_iteration_key_.dst.ipv4 = ntohl(inet_addr(dip.c_str()));
    flow_iteration_key_.protocol = proto;
    return true;
}

bool PktSandeshFlow::Run() {
    FlowTable::FlowEntryMap::iterator it;
    std::vector<SandeshFlowData>& list =
        const_cast<std::vector<SandeshFlowData>&>(resp_obj_->get_flow_list());
    int count = 0;
    bool flow_key_set = false;
    FlowTable *flow_obj = Agent::GetInstance()->pkt()->flow_table();

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
    while (it != flow_obj->flow_entry_map_.end()) {
        FlowEntry *fe = it->second;
        SetSandeshFlowData(list, fe);
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
    FlowRecordsResp *resp = new FlowRecordsResp();
    
    PktSandeshFlow *task = new PktSandeshFlow(resp, context(), get_flow_key());
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void FetchAllFlowRecords::HandleRequest() const {
    FlowRecordsResp *resp = new FlowRecordsResp();
    
    PktSandeshFlow *task = new PktSandeshFlow(resp, context(), 
                                              PktSandeshFlow::start_key);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void DeleteAllFlowRecords::HandleRequest() const {
    FlowRecordsResp *resp = new FlowRecordsResp();

    PktSandeshFlow *task = new PktSandeshFlow(resp, context(),
                                              PktSandeshFlow::start_key);
    task->set_delete_op(true);
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void FetchFlowRecord::HandleRequest() const {
    FlowKey key;
    key.nh = get_nh();
    error_code ec;
    key.src.ipv4 = Ip4Address::from_string(get_sip(), ec).to_ulong();
    key.dst.ipv4 = Ip4Address::from_string(get_dip(), ec).to_ulong();
    key.src_port = (unsigned)get_src_port();
    key.dst_port = (unsigned)get_dst_port();
    key.protocol = get_protocol();

    FlowTable::FlowEntryMap::iterator it;
    FlowTable *flow_obj = Agent::GetInstance()->pkt()->flow_table();
    it = flow_obj->flow_entry_map_.find(key);
    SandeshResponse *resp;
    if (it != flow_obj->flow_entry_map_.end()) {
        FlowRecordResp *flow_resp = new FlowRecordResp();
        FlowEntry *fe = it->second;
        SandeshFlowData data;
        SET_SANDESH_FLOW_DATA(data, fe);
        flow_resp->set_record(data);
        resp = flow_resp;
    } else {
        resp = new FlowErrorResp();
    }
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

////////////////////////////////////////////////////////////////////////////////
