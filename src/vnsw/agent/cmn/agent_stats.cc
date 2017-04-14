/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <string>
#include <vector>
#include <base/logging.h>

#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>
#include <xmpp/xmpp_channel.h>
#include <controller/controller_peer.h>

#include <cmn/agent_stats.h>
#include <cmn/stats_types.h>
#include <uve/agent_uve_base.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

AgentStats *AgentStats::singleton_;

void AgentStats::Reset() {
    sandesh_reconnects_ = sandesh_in_msgs_ = sandesh_out_msgs_ = 0;
    sandesh_http_sessions_ = nh_count_ = pkt_exceptions_ = 0;
    pkt_invalid_agent_hdr_ = pkt_invalid_interface_ = 0;
    pkt_no_handler_ = pkt_dropped_ = flow_created_ = 0;
    pkt_fragments_dropped_ = 0;
    flow_aged_ = flow_drop_due_to_max_limit_ = 0;
    flow_drop_due_to_linklocal_limit_ = ipc_in_msgs_ = 0;
    ipc_out_msgs_ = in_tpkts_ = in_bytes_ = out_tpkts_ = 0;
    out_bytes_ = 0;
}

void AgentStatsReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    AgentStats *stats = agent->stats();
    IpcStatsResp *ipc = new IpcStatsResp();

    ipc->set_ipc_in_msgs(stats->ipc_in_msgs());
    ipc->set_ipc_out_msgs(stats->ipc_out_msgs());
    ipc->set_context(context());
    ipc->set_more(true);
    ipc->Response();

    /* If Pkt module is not set (for tor-agents for example), no need to send
     * PktTrapStatsResp and FlowStatsResp */
    if (agent->pkt()) {
        PktTrapStatsResp *pkt = new PktTrapStatsResp();
        pkt->set_exceptions(stats->pkt_exceptions());
        pkt->set_invalid_agent_hdr(stats->pkt_invalid_agent_hdr());
        pkt->set_invalid_interface(stats->pkt_invalid_interface());
        pkt->set_no_handler(stats->pkt_no_handler());
        pkt->set_pkt_dropped(stats->pkt_dropped());
        pkt->set_pkt_fragments_dropped(stats->pkt_fragments_dropped());
        pkt->set_context(context());
        pkt->set_more(true);
        pkt->Response();

        FlowStatsResp *flow = new FlowStatsResp();
        flow->set_flow_active(stats->FlowCount());
        flow->set_flow_created(stats->flow_created());
        flow->set_flow_aged(stats->flow_aged());
        flow->set_flow_drop_due_to_max_limit(
                stats->flow_drop_due_to_max_limit());
        flow->set_flow_drop_due_to_linklocal_limit(
                stats->flow_drop_due_to_linklocal_limit());
        flow->set_flow_max_system_flows(agent->flow_table_size());
        flow->set_flow_max_vm_flows(agent->max_vm_flows());
        flow->set_flow_export_disable_drops(
                agent->flow_stats_manager()->flow_export_disable_drops());
        flow->set_flow_export_sampling_drops(
                agent->flow_stats_manager()->flow_export_sampling_drops());
        flow->set_flow_export_drops(
                agent->flow_stats_manager()->flow_export_drops());
        flow->set_flow_sample_export_count(
                agent->flow_stats_manager()->flow_sample_exports());
        flow->set_flow_msg_export_count(
                agent->flow_stats_manager()->flow_msg_exports());
        flow->set_flow_export_count(
                agent->flow_stats_manager()->flow_exports());
        flow->set_context(context());
        flow->set_more(true);
        flow->Response();
    }

    XmppStatsResp *xmpp_resp = new XmppStatsResp();
    vector<XmppStatsInfo> list;
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        XmppStatsInfo peer;
        if (!agent->controller_ifmap_xmpp_server(count).empty()) {
            peer.set_ip(agent->controller_ifmap_xmpp_server(count));
            AgentXmppChannel *ch = agent->controller_xmpp_channel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            peer.set_reconnect(stats->xmpp_reconnects(count));
            peer.set_in_msgs(stats->xmpp_in_msgs(count));
            peer.set_out_msgs(stats->xmpp_out_msgs(count));
            peer.set_config_in_msgs(stats->xmpp_config_in_msgs(count));
            list.push_back(peer);
        }
    }
    xmpp_resp->set_xmpp_list(list);
    xmpp_resp->set_context(context());
    xmpp_resp->set_more(true);
    xmpp_resp->Response();

    SandeshStatsResp *sandesh = new SandeshStatsResp();
    sandesh->set_sandesh_in_msgs(stats->sandesh_in_msgs());
    sandesh->set_sandesh_out_msgs(stats->sandesh_out_msgs());
    sandesh->set_sandesh_http_sessions(stats->sandesh_http_sessions());
    sandesh->set_sandesh_reconnects(stats->sandesh_reconnects());
    sandesh->set_context(context());
    sandesh->set_more(false);
    sandesh->Response();
}

void AgentStats::UpdateFlowMinMaxStats(uint64_t total_flows,
                                       FlowCounters &stat) const {
    uint64_t count = total_flows - stat.prev_flow_count;
    if ((stat.max_flows_per_second == kInvalidFlowCount) ||
        (count > stat.max_flows_per_second)) {
        stat.max_flows_per_second = count;
    }
    if ((stat.min_flows_per_second == kInvalidFlowCount) ||
        (count < stat.min_flows_per_second)) {
        stat.min_flows_per_second = count;
    }
    stat.prev_flow_count = total_flows;
}

void AgentStats::ResetFlowMinMaxStats(FlowCounters &stat) const {
    stat.max_flows_per_second = kInvalidFlowCount;
    stat.min_flows_per_second = kInvalidFlowCount;
}

void AgentStats::RegisterFlowCountFn(FlowCountFn cb) {
    flow_count_fn_ = cb;
}

uint32_t AgentStats::FlowCount() const {
    if (flow_count_fn_.empty())
        return 0;
    return flow_count_fn_();
}
