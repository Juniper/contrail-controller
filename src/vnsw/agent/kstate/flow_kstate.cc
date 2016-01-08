/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/task.h>
#include <net/address_util.h>

#include <cmn/agent_cmn.h>
#include <kstate/kstate.h>
#include <kstate/flow_kstate.h>

#include <vr_flow.h>
#include <vr_mirror.h>

#include <pkt/flow_table.h>

#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/ksync_init.h>

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
        case VR_FLOW_FLAG_LINK_LOCAL:
            return " LINK_LOCAL ";
        case VR_FLOW_FLAG_EVICTED:
            return " EVICTED ";
        case VR_FLOW_FLAG_EVICT_CANDIDATE:
            return " EVICT_CANDIDATE ";
        case VR_FLOW_FLAG_NEW_FLOW:
            return " NEW_FLOW ";
        case VR_FLOW_FLAG_MODIFIED:
            return " MODIFIED ";
        case VR_RFLOW_VALID:
            return " RFLOW_VALID ";
        default:
            return " INVALID ";
    }
}

const string FlowKState::TcpFlagToStr(unsigned int flag) const {
    switch(flag) {
        case VR_FLOW_TCP_SYN:
            return " SYN ";
        case VR_FLOW_TCP_SYN_R:
            return " SYN_R ";
        case VR_FLOW_TCP_ESTABLISHED:
            return " ESTB ";
        case VR_FLOW_TCP_ESTABLISHED_R:
            return " ESTB_R ";
        case VR_FLOW_TCP_FIN:
            return " FIN ";
        case VR_FLOW_TCP_FIN_R:
            return " FIN_R ";
        case VR_FLOW_TCP_RST:
            return " RST ";
        case VR_FLOW_TCP_HALF_CLOSE:
            return " HALF_CLOSE ";
        case VR_FLOW_TCP_DEAD:
            return " DEAD ";
        default:
            return " INVALID ";
    }
}

void FlowKState::UpdateFlagStr(string &str, bool &set, bool tcp, unsigned sflag,
                               unsigned cflag) const {
    string flag_str;
    if (sflag & cflag) {
        if (tcp) {
            flag_str.assign(TcpFlagToStr(cflag));
        } else {
            flag_str.assign(FlagToStr(cflag));
        }
        if (set) {
            str.append("|" + flag_str);
        } else {
            str.assign(flag_str);
            set = true;
        }
    }
}

const std::string FlowKState::DropCodeToStr(uint8_t drop_code) const {
    switch (drop_code) {
    case VR_FLOW_DR_UNKNOWN:
        return "Unknown";
    case VR_FLOW_DR_UNAVIALABLE_INTF:
        return "IntfErr";
    case VR_FLOW_DR_IPv4_FWD_DIS:
        return "Ipv4Dis";
    case VR_FLOW_DR_UNAVAILABLE_VRF:
        return "VrfErr";
    case VR_FLOW_DR_NO_SRC_ROUTE:
        return "NoSrcRt";
    case VR_FLOW_DR_NO_DST_ROUTE:
        return "NoDstRt";
    case VR_FLOW_DR_AUDIT_ENTRY:
        return "Audit";
    case VR_FLOW_DR_VRF_CHANGE:
        return "VrfChange";
    case VR_FLOW_DR_NO_REVERSE_FLOW:
        return "NoRevFlow";
    case VR_FLOW_DR_REVERSE_FLOW_CHANGE:
        return "RevFlowChng";
    case VR_FLOW_DR_NAT_CHANGE:
        return "NatChng";
    case VR_FLOW_DR_FLOW_LIMIT:
        return "FlowLim";
    case VR_FLOW_DR_LINKLOCAL_SRC_NAT:
        return "LinkSrcNatErr";
    case VR_FLOW_DR_POLICY:
        return "Policy";
    case VR_FLOW_DR_OUT_POLICY:
        return "OutPolicy";
    case VR_FLOW_DR_SG:
        return "SG";
    case VR_FLOW_DR_OUT_SG:
        return "OutSG";
    case VR_FLOW_DR_REVERSE_SG:
        return "RevSG";
    case VR_FLOW_DR_REVERSE_OUT_SG:
        return "RevOutSG";
    default:
        return "Unknown";
    }
}

void FlowKState::SetFlowData(vector<KFlowInfo> &list, 
                             const vr_flow_entry *k_flow, 
                             const int index) const {
    KFlowInfo data;
    bool action_drop = false;
    int family = (k_flow->fe_key.flow_family == AF_INET)? Address::INET :
        Address::INET6;
    IpAddress sip, dip;
    CharArrayToIp(k_flow->fe_key.flow_ip, sizeof(k_flow->fe_key.flow_ip),
                  family, &sip, &dip);

    string action_str;
    string flag_str, tcp_flags;
    data.set_index((unsigned int)index);
    data.set_sport((unsigned)ntohs(k_flow->fe_key.flow4_sport));
    data.set_dport((unsigned)ntohs(k_flow->fe_key.flow4_dport));
    data.set_sip(sip.to_string());
    data.set_dip(dip.to_string());
    data.set_vrf_id(k_flow->fe_vrf);
    data.set_proto(k_flow->fe_key.flow4_proto);
    data.set_nhid(k_flow->fe_key.flow_nh_id);
    switch (k_flow->fe_action) {
        case VR_FLOW_ACTION_FORWARD:
            action_str.assign("FORWARD");
            break;
        case VR_FLOW_ACTION_DROP:
            action_str.assign("DROP");
            action_drop = true;
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
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_ACTIVE);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_MIRROR);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_VRFT);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_SNAT);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_SPAT);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_DNAT);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_SPAT);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_LINK_LOCAL);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_EVICTED);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_EVICT_CANDIDATE);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_NEW_FLOW);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags,
                  VR_FLOW_FLAG_MODIFIED);
    UpdateFlagStr(flag_str, assigned, false, k_flow->fe_flags, VR_RFLOW_VALID);
    data.set_flags(flag_str);

    if (k_flow->fe_key.flow4_proto == IPPROTO_TCP) {
        assigned = false;
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_SYN);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_SYN_R);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_ESTABLISHED);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_ESTABLISHED_R);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_FIN);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_FIN_R);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_RST);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_HALF_CLOSE);
        UpdateFlagStr(tcp_flags, assigned, true, k_flow->fe_tcp_flags,
                      VR_FLOW_TCP_DEAD);
        data.set_tcp_flags(tcp_flags);
    }
    if (action_drop) {
        data.set_drop_reason(DropCodeToStr(k_flow->fe_drop_reason));
    }
    data.set_underlay_udp_sport(k_flow->fe_udp_src_port);
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

    KSyncFlowMemory *ksync_obj = agent_->ksync()->ksync_flow_memory();

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

