/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <pkt/pkt_sandesh_flow.h>
#include <vector>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/lexical_cast.hpp>
#include <sstream>
#include <algorithm>
using boost::system::error_code;

#define SET_SANDESH_FLOW_DATA(data, fe)                  \
    data.set_vrf(fe->key.vrf);				 \
    Ip4Address sip(fe->key.src.ipv4);			 \
    data.set_sip(sip.to_string());			 \
    Ip4Address dip(fe->key.dst.ipv4);			 \
    data.set_dip(dip.to_string());			 \
    data.set_src_port((unsigned)fe->key.src_port);       \
    data.set_dst_port((unsigned)fe->key.dst_port);	 \
    data.set_protocol(fe->key.protocol);		 \
    data.set_dest_vrf(fe->data.dest_vrf);		 \
    data.set_action(fe->data.match_p.action_info.action);	   \
    std::vector<MirrorActionSpec>::iterator mait;                    \
    std::vector<MirrorInfo> mirror_l;                                \
    for (mait = fe->data.match_p.action_info.mirror_l.begin();       \
         mait != fe->data.match_p.action_info.mirror_l.end();        \
         ++mait) {                                                   \
         MirrorInfo minfo;                                           \
         minfo.set_mirror_destination((*mait).ip.to_string());       \
         minfo.set_mirror_port((*mait).port);                        \
         mirror_l.push_back(minfo);                                  \
    }                                                                \
    data.set_mirror_l(mirror_l);                                     \
    if (fe->data.ingress) {				 \
        data.set_direction("ingress");			 \
    } else {						 \
        data.set_direction("egress");			 \
    }							 \
    data.set_stats_bytes(fe->data.bytes);		 \
    data.set_stats_packets(fe->data.packets); 		 \
    data.set_uuid(boost::lexical_cast<std::string>(fe->flow_uuid)); \
    if (fe->nat) {					 \
        data.set_nat("enabled");			 \
    } else {						 \
        data.set_nat("disabled");			 \
    }							 \
    data.set_flow_handle(fe->flow_handle);		 \
    data.set_interface_idx(fe->intf_in);		 \
    data.set_setup_time(				 \
                    boost::lexical_cast<std::string>(UTCUsecToPTime(fe->setup_time))); \
    data.set_refcount(fe->GetRefCount());		 \
    data.set_implicit_deny(fe->ImplicitDenyFlow() ? "yes" : "no"); \
    data.set_short_flow(fe->ShortFlow() ? "yes" : "no");           \
    data.set_local_flow(fe->local_flow ? "yes" : "no");           \
    if (fe->local_flow) {                                         \
        data.set_egress_uuid(boost::lexical_cast<std::string>(fe->egress_uuid));        \
    }                                                             \
    data.set_src_vn(fe->data.source_vn);                          \
    data.set_dst_vn(fe->data.dest_vn);                            \
    data.set_setup_time_utc(fe->setup_time); \
    if (fe->data.ecmp && \
        fe->data.component_nh_idx != CompositeNH::kInvalidComponentNHIdx) {\
        data.set_ecmp_index(fe->data.component_nh_idx);\
    }                                                           \
    data.set_reverse_flow(fe->is_reverse_flow ? "yes" : "no");           \
    SetAclInfo(data, fe);                                     \

const std::string PktSandeshFlow::start_key = "0:0:0:0:0.0.0.0:0.0.0.0";

static void SetAclInfo(SandeshFlowData &data, FlowEntry *fe) {
    std::list<MatchAclParams>::const_iterator it;
    FlowAclInfo policy;
    std::vector<FlowAclUuid> acl;

    acl.clear();
    for (it = fe->data.match_p.m_acl_l.begin();
         it != fe->data.match_p.m_acl_l.end(); it++) {
        FlowAclUuid f;
        f.uuid = UuidToString(it->acl->GetUuid());
        acl.push_back(f);
    }
    policy.set_action(fe->data.match_p.policy_action);
    policy.set_acl(acl);
    data.set_policy(policy);

    acl.clear();
    for (it = fe->data.match_p.m_out_acl_l.begin();
         it != fe->data.match_p.m_out_acl_l.end(); it++) {
        FlowAclUuid f;
        f.uuid = UuidToString(it->acl->GetUuid());
        acl.push_back(f);
    }
    policy.set_action(fe->data.match_p.out_policy_action);
    policy.set_acl(acl);
    data.set_out_policy(policy);

    acl.clear();
    for (it = fe->data.match_p.m_sg_acl_l.begin();
         it != fe->data.match_p.m_sg_acl_l.end(); it++) {
        FlowAclUuid f;
        f.uuid = UuidToString(it->acl->GetUuid());
        acl.push_back(f);
    }
    policy.set_action(fe->data.match_p.sg_action);
    policy.set_acl(acl);
    data.set_sg(policy);

    acl.clear();
    for (it = fe->data.match_p.m_out_sg_acl_l.begin();
         it != fe->data.match_p.m_out_sg_acl_l.end(); it++) {
        FlowAclUuid f;
        f.uuid = UuidToString(it->acl->GetUuid());
        acl.push_back(f);
    }
    policy.set_action(fe->data.match_p.out_sg_action);
    policy.set_acl(acl);
    data.set_out_sg(policy);
}

void PktSandeshFlow::SetSandeshFlowData(std::vector<SandeshFlowData> &list, FlowEntry *fe) {

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
    ss << key.vrf << ":";
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
        istringstream(item) >> flow_iteration_key_.vrf;
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
    std::vector<SandeshFlowData>& list = const_cast<std::vector<SandeshFlowData>&>(resp_obj_->get_flow_list());
    int count = 0;
    bool flow_key_set = false;
    FlowTable *flow_obj = FlowTable::GetFlowTableObject();

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
        if (count == max_flow_response) {
            if (it != flow_obj->flow_entry_map_.end()) {
                resp_obj_->set_flow_key(GetFlowKey(fe->key));
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

void FetchFlowRecord::HandleRequest() const {
    FlowKey key;
    key.vrf = get_vrf();
    error_code ec;
    key.src.ipv4 = Ip4Address::from_string(get_sip(), ec).to_ulong();
    key.dst.ipv4 = Ip4Address::from_string(get_dip(), ec).to_ulong();
    key.src_port = (unsigned)get_src_port();
    key.dst_port = (unsigned)get_dst_port();
    key.protocol = get_protocol();

    FlowTable::FlowEntryMap::iterator it;
    FlowTable *flow_obj = FlowTable::GetFlowTableObject();
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

