/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/pkt_sandesh_flow.h>
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <sstream>
#include <algorithm>

#include <cmn/agent_stats.h>
#include <uve/agent_uve.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>

using boost::system::error_code;

#define SET_SANDESH_FLOW_DATA(agent, data, fe, info)                        \
    data.set_vrf(fe->data().vrf);                                           \
    data.set_sip(fe->key().src_addr.to_string());                           \
    data.set_dip(fe->key().dst_addr.to_string());                           \
    data.set_src_port((unsigned)fe->key().src_port);                        \
    data.set_dst_port((unsigned)fe->key().dst_port);                        \
    data.set_protocol(fe->key().protocol);                                  \
    data.set_dest_vrf(fe->data().dest_vrf);                                 \
    data.set_uuid(UuidToString(fe->uuid()));                                \
    data.set_action(fe->match_p().action_info.action);                      \
    std::vector<ActionStr> action_str_l;                                    \
    SetActionStr(fe->match_p().action_info, action_str_l);                  \
    if ((fe->match_p().action_info.action & TrafficAction::DROP_FLAGS) != 0) {\
        data.set_drop_reason(fe->DropReasonStr(fe->data().drop_reason));    \
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
        if (fe->is_flags_set(FlowEntry::LocalFlow)) {                       \
            data.set_egress_uuid(UuidToString(info->egress_uuid()));        \
        }                                                                   \
    }                                                                       \
    if (fe->is_flags_set(FlowEntry::NatFlow)) {                             \
        data.set_nat("enabled");                                            \
    } else {                                                                \
        data.set_nat("disabled");                                           \
    }                                                                       \
    data.set_gen_id(fe->gen_id());                                \
    data.set_flow_handle(fe->flow_handle());                                \
    data.set_refcount(fe->GetRefCount());                                   \
    data.set_implicit_deny(fe->ImplicitDenyFlow() ? "yes" : "no");          \
    data.set_short_flow(                                                    \
            fe->is_flags_set(FlowEntry::ShortFlow) ?                        \
            string("yes (") + fe->DropReasonStr(fe->short_flow_reason()) + \
            ")": "no");                                                     \
    data.set_local_flow(fe->is_flags_set(FlowEntry::LocalFlow) ? "yes" : "no");     \
    data.set_src_vn_list(fe->data().SourceVnList());                        \
    data.set_dst_vn_list(fe->data().DestinationVnList());                   \
    data.set_src_vn_match(fe->data().source_vn_match);                      \
    data.set_dst_vn_match(fe->data().dest_vn_match);                        \
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
    if (fe->data().src_ip_nh.get() != NULL) {                                      \
        data.set_src_ip_nh(fe->data().src_ip_nh.get()->id());                         \
    }                                                                       \
    if (fe->data().rpf_nh.get() != NULL) {                                      \
        data.set_rpf_nh(fe->data().rpf_nh.get()->id());                         \
    }                                                                       \
    data.set_peer_vrouter(fe->peer_vrouter());                            \
    data.set_tunnel_type(fe->tunnel_type().ToString());                     \
    data.set_enable_rpf(fe->data().enable_rpf);\
    if (fe->fsc()) {\
       data.set_aging_protocol(fe->fsc()->flow_aging_key().proto);\
       data.set_aging_port(fe->fsc()->flow_aging_key().port);\
    }\
    data.set_l3_flow(fe->l3_flow());\
    uint16_t id = fe->flow_table()? fe->flow_table()->table_index() : 0xFFFF;\
    data.set_table_id(id);\
    data.set_deleted(fe->deleted());\
    SandeshFlowIndexInfo flow_index_info;\
    fe->SetEventSandeshData(&flow_index_info);\
    data.set_flow_index_info(flow_index_info);

const std::string PktSandeshFlow::start_key = "0-0-0-0-0-0.0.0.0-0.0.0.0";

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

    data.set_sg_rule_uuid(fe->sg_rule_uuid());
    data.set_nw_ace_uuid(fe->nw_ace_uuid());
}

////////////////////////////////////////////////////////////////////////////////

PktSandeshFlow::PktSandeshFlow(Agent *agent, FlowRecordsResp *obj,
                               std::string resp_ctx, std::string key):
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::PktFlowResponder")),
          0), resp_obj_(obj), resp_data_(resp_ctx),
    flow_iteration_key_(), key_valid_(false), delete_op_(false), agent_(agent),
    partition_id_(0) {
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

string PktSandeshFlow::GetFlowKey(const FlowKey &key, uint16_t partition_id) {
    stringstream ss;
    ss << partition_id << kDelimiter;
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
    if (n != 6) {
        return false;
    }
    stringstream ss(key);
    string item, sip, dip;
    uint32_t proto;

    if (getline(ss, item, ch)) {
        istringstream(item) >> partition_id_;
    }
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

bool PktSandeshFlow::Run() {
    FlowTable::FlowEntryMap::iterator it;
    std::vector<SandeshFlowData>& list =
        const_cast<std::vector<SandeshFlowData>&>(resp_obj_->get_flow_list());
    int count = 0;
    bool flow_key_set = false;

    if (partition_id_ >= agent_->flow_thread_count()) {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }

    FlowTable *flow_obj = agent_->pkt()->flow_table(partition_id_);

    if (delete_op_) {
        for (int i =0; i < agent_->flow_thread_count(); i++){
            flow_obj = agent_->pkt()->flow_table(i);
            flow_obj->DeleteAll();
        }
        SendResponse(resp_obj_);
        return true;
    }

    if (key_valid_)  {
        it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    } else {
         FlowErrorResp *resp = new FlowErrorResp();
         SendResponse(resp);
         return true;
    }

    while (it == flow_obj->flow_entry_map_.end() &&
          ++partition_id_ < agent_->flow_thread_count()) {
         flow_obj = agent_->pkt()->flow_table(partition_id_);
         it =  flow_obj->flow_entry_map_.begin();
    }

    while (it != flow_obj->flow_entry_map_.end()) {
        FlowEntry *fe = it->second;
        FlowStatsCollector *fec = fe->fsc();
        const FlowExportInfo *info = NULL;
        if (fec) {
            info = fec->FindFlowExportInfo(fe);
        }
        SetSandeshFlowData(list, fe, info);
        ++it;
        count++;
        if (count == kMaxFlowResponse) {
            if (it != flow_obj->flow_entry_map_.end()) {
                resp_obj_->set_flow_key(GetFlowKey(fe->key(), partition_id_));
                flow_key_set = true;

            } else {
                FlowKey key;
                resp_obj_->set_flow_key(GetFlowKey(key, ++partition_id_));
                flow_key_set = true;
            }
            break;
        }

        while (it == flow_obj->flow_entry_map_.end()) {
            if (++partition_id_ < agent_->flow_thread_count()) {
                flow_obj = agent_->pkt()->flow_table(partition_id_);
                it = flow_obj->flow_entry_map_.begin();
                if (it != flow_obj->flow_entry_map_.end()) {
                    break;
                }
            } else {
              break;
           }
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
    FlowTable *flow_obj = NULL;

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
    for (int i = 0; i < agent->flow_thread_count(); i++) {
        flow_obj = agent->pkt()->flow_table(i);
        it = flow_obj->flow_entry_map_.find(key);
        if (it != flow_obj->flow_entry_map_.end())
            break;
    }

    SandeshResponse *resp;
    if (flow_obj && it != flow_obj->flow_entry_map_.end()) {
       FlowRecordResp *flow_resp = new FlowRecordResp();
       FlowEntry *fe = it->second;
       FlowStatsCollector *fec = fe->fsc();
       const FlowExportInfo *info = NULL;
       if (fec) {
           info = fec->FindFlowExportInfo(fe);
       }
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

    FlowStatsCollectorObject *obj =
        agent->flow_stats_manager()->default_flow_stats_collector_obj();

    FlowAgeTimeResp *resp = new FlowAgeTimeResp();
    if (obj == NULL) {
        goto done;
    }
    resp->set_old_age_time(obj->GetAgeTimeInSeconds());

    if (age_time && age_time != resp->get_old_age_time()) {
        obj->UpdateAgeTimeInSeconds(age_time);
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

    if (partition_id_ > agent_->flow_thread_count()) {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }

    FlowTable *flow_obj = agent_->pkt()->flow_table(partition_id_);
    FlowStatsManager *fm = agent_->flow_stats_manager();
    const FlowStatsCollectorObject *fsc_obj = fm->Find(proto_, port_);
    if (!fsc_obj) {
        FlowErrorResp *resp = new FlowErrorResp();
        SendResponse(resp);
        return true;
    }

    FlowTable::FlowEntryMap::iterator it;
    if (key_valid_)  {
        it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    } else {
         FlowErrorResp *resp = new FlowErrorResp();
         SendResponse(resp);
         return true;
    }

    while (it == flow_obj->flow_entry_map_.end() &&
          ++partition_id_ < agent_->flow_thread_count()) {
         flow_obj = agent_->pkt()->flow_table(partition_id_);
         it =  flow_obj->flow_entry_map_.begin();
    }

    while (it != flow_obj->flow_entry_map_.end()) {
        FlowEntry *fe = it->second;
        const FlowExportInfo *info = NULL;
        if (fe->fsc()) {
            info = fe->fsc()->FindFlowExportInfo(fe);
        }
        SetSandeshFlowData(list, fe, info);
        ++it;
        count++;
        if (count == kMaxFlowResponse) {
            if (it != flow_obj->flow_entry_map_.end()) {
                ostringstream ostr;
                ostr << proto_ << ":" << port_ << ":"
                    << GetFlowKey(fe->key(), partition_id_);
                resp_->set_flow_key(ostr.str());
                flow_key_set = true;
            } else {
                ostringstream ostr;
                FlowKey key;
                ostr << proto_ << ":" << port_ << ":"
                    << GetFlowKey(key, ++partition_id_);
                resp_->set_flow_key(ostr.str());
                flow_key_set = true;
            }
            break;
        }

        while (it == flow_obj->flow_entry_map_.end()) {
            if (++partition_id_ < agent_->flow_thread_count()) {
                flow_obj = agent_->pkt()->flow_table(partition_id_);
                it = flow_obj->flow_entry_map_.begin();
                if (it != flow_obj->flow_entry_map_.end()) {
                    break;
                }
            } else {
              break;
           }
         }

    }

    if (!flow_key_set) {
       ostringstream ostr;
       ostr << proto_ << ":" << port_ << ":" <<PktSandeshFlow::start_key;
       resp_->set_flow_key(ostr.str());
    }
    SendResponse(resp_);
    return true;
}

bool PktSandeshFlowStats::SetProto(string &key) {
    size_t n = std::count(key.begin(), key.end(), ':');
    if (n != 2) {
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
    if (getline(ss, item)) {
        SetFlowKey(item);
    }
    return true;
}

PktSandeshFlowStats::PktSandeshFlowStats(Agent *agent, FlowStatsCollectorRecordsResp *obj,
                                         std::string resp_ctx, std::string key):
    PktSandeshFlow(agent, NULL, resp_ctx, key), resp_(obj) {
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


void SandeshFlowTableInfoRequest::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowProto *proto = agent->pkt()->get_flow_proto();
    SandeshFlowTableInfoResp *resp = new SandeshFlowTableInfoResp();
    resp->set_flow_count(proto->FlowCount());
    resp->set_total_added(agent->stats()->flow_created());
    resp->set_max_flows(agent->stats()->max_flow_count());
    resp->set_total_deleted(agent->stats()->flow_aged());
    std::vector<SandeshFlowTableInfo> info_list;
    for (uint16_t i = 0; i < proto->flow_table_count(); i++) {
        FlowTable *table = proto->GetTable(i);
        SandeshFlowTableInfo info;
        info.set_index(table->table_index());
        info.set_count(table->Size());
        info.set_total_add(table->free_list()->total_alloc());
        info.set_total_del(table->free_list()->total_free());
        info.set_freelist_count(table->free_list()->free_count());
        info_list.push_back(info);
    }
    resp->set_table_list(info_list);
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}
////////////////////////////////////////////////////////////////////////////////
