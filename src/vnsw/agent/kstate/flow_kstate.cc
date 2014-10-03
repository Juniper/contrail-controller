/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/task.h>
#include <base/util.h>

#include <cmn/agent_cmn.h>
#include <kstate/kstate.h>
#include <kstate/flow_kstate.h>

#include <vr_flow.h>
#include <vr_mirror.h>

#include <pkt/flow_table.h>

#include <ksync/flowtable_ksync.h>
#include <ksync/ksync_init.h>

using namespace std;

FlowKState::FlowKState(Agent *agent, const string &resp_ctx, int idx) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::FlowResponder")),
            0), response_context_(resp_ctx), flow_idx_(idx), 
    flow_iteration_key_(0), agent_(agent) {
}

FlowKState::FlowKState(Agent *agent, const string &resp_ctx, 
                       const string &iter_idx) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::FlowResponder")),
            0), response_context_(resp_ctx), flow_idx_(-1), 
    flow_iteration_key_(0), agent_(agent) {
    stringToInteger(iter_idx, flow_iteration_key_);
}

void FlowKState::SendResponse(KFlowResp *resp) const {
    resp->set_context(response_context_);
    resp->Response();
}

const string FlowKState::FlagToStr(unsigned int flag) const {
    switch(flag) {
        case VR_FLOW_FLAG_ACTIVE:
            return " ACTIVE ";
        case VR_FLOW_FLAG_MIRROR:
            return " MIRROR ";
        case VR_FLOW_FLAG_VRFT:
            return " VRFT ";
        case VR_FLOW_FLAG_SNAT:
            return " SNAT ";
        case VR_FLOW_FLAG_SPAT:
            return " SPAT ";
        case VR_FLOW_FLAG_DNAT:
            return " DNAT ";
        case VR_FLOW_FLAG_DPAT:
            return " DPAT ";
        default:
            return " INVALID ";
    }
}

void FlowKState::UpdateFlagStr(string &str, bool &set, unsigned sflag, 
                               unsigned cflag) const {
    if (sflag & cflag) {
        if (set) {
            str.append("|" + FlagToStr(cflag));
        } else {
            str.assign(FlagToStr(cflag));
            set = true;
        }
    }
}

void FlowKState::SetFlowData(vector<KFlowInfo> &list, 
                             const vr_flow_entry *k_flow, 
                             const int index) const {
    KFlowInfo data;
    string action_str;
    string flag_str;
    data.set_index((unsigned int)index);
    data.set_sport((unsigned)ntohs(k_flow->fe_key.key_src_port));
    data.set_dport((unsigned)ntohs(k_flow->fe_key.key_dst_port));
    Ip4Address sip(ntohl(k_flow->fe_key.key_src_ip));
    data.set_sip(sip.to_string());
    Ip4Address dip(ntohl(k_flow->fe_key.key_dest_ip));
    data.set_dip(dip.to_string());
    data.set_vrf_id(k_flow->fe_vrf);
    data.set_proto(k_flow->fe_key.key_proto);
    switch (k_flow->fe_action) {
        case VR_FLOW_ACTION_FORWARD:
            action_str.assign("FORWARD");
            break;
        case VR_FLOW_ACTION_DROP:
            action_str.assign("DROP");
            break;
        case VR_FLOW_ACTION_NAT:
            action_str.assign("NAT");
            break;
        case VR_FLOW_ACTION_HOLD:
            action_str.assign("HOLD");
            break;
        default:
            action_str.assign("INVALID");
    }
    data.set_action(action_str);
    bool assigned = false;
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_ACTIVE);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_MIRROR);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_VRFT);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_SNAT);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_SPAT);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_DNAT);
    UpdateFlagStr(flag_str, assigned, k_flow->fe_flags, VR_FLOW_FLAG_SPAT);
    data.set_flags(flag_str);
    data.set_rflow(k_flow->fe_rflow);
    data.set_d_vrf_id(k_flow->fe_dvrf);
    data.set_bytes(k_flow->fe_stats.flow_bytes);
    data.set_pkts(k_flow->fe_stats.flow_packets);
    if (k_flow->fe_mirror_id != VR_MAX_MIRROR_INDICES) {
        data.set_mirror_id(k_flow->fe_mirror_id);
    }
    if (k_flow->fe_sec_mirror_id != VR_MAX_MIRROR_INDICES) {
        data.set_sec_mirror_id(k_flow->fe_sec_mirror_id);
    }
    if (k_flow->fe_ecmp_nh_index != -1) {
        data.set_ecmp_index(k_flow->fe_ecmp_nh_index);
    }
    list.push_back(data);
}

bool FlowKState::Run() {
    int count = 0;
    const vr_flow_entry *k_flow;
    KFlowResp *resp;

    FlowTableKSyncObject *ksync_obj = agent_->ksync()->flowtable_ksync_obj();

    if (flow_idx_ != -1) {
        k_flow = ksync_obj->GetKernelFlowEntry(flow_idx_, false);
        if (k_flow) {
            resp = new KFlowResp();
            vector<KFlowInfo> &list = const_cast<std::vector<KFlowInfo>&>
                                          (resp->get_flow_list());
            SetFlowData(list, k_flow, flow_idx_);
            SendResponse(resp);
        } else {
            ErrResp *resp = new ErrResp();
            resp->set_context(response_context_);
            resp->Response();
        }
        return true;
    }
    uint32_t idx = flow_iteration_key_;
    uint32_t max_flows = ksync_obj->flow_table_entries_count();
    
    resp = new KFlowResp();
    vector<KFlowInfo> &list = const_cast<std::vector<KFlowInfo>&>
                                  (resp->get_flow_list());
    while(idx < max_flows) {
        k_flow = ksync_obj->GetKernelFlowEntry(idx, false);
        if (k_flow) {
            count++;
            SetFlowData(list, k_flow, idx);
        } 
        idx++;
        if (count == KState::kMaxEntriesPerResponse) {
            if (idx != max_flows) {
                resp->set_flow_handle(integerToString(idx));
            } else {
                resp->set_flow_handle(integerToString(0));
            }
            SendResponse(resp);
            return true;
        }
    }

    resp->set_flow_handle(integerToString(0));
    SendResponse(resp);

    return true;
}

