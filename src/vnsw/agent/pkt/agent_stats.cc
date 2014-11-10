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

#include <pkt/agent_stats.h>
#include <pkt/pkt_init.h>
#include <pkt/flow_table.h>
#include <uve/agent_uve_base.h>

AgentStats *AgentStats::singleton_;

void AgentStats::Reset() {
    sandesh_reconnects_ = sandesh_in_msgs_ = sandesh_out_msgs_ = 0;
    sandesh_http_sessions_ = nh_count_ = pkt_exceptions_ = 0;
    pkt_invalid_agent_hdr_ = pkt_invalid_interface_ = 0;
    pkt_no_handler_ = pkt_dropped_ = flow_created_ = 0;
    flow_aged_ = flow_active_ = flow_drop_due_to_max_limit_ = 0;
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

    PktTrapStatsResp *pkt = new PktTrapStatsResp();
    pkt->set_exceptions(stats->pkt_exceptions());
    pkt->set_invalid_agent_hdr(stats->pkt_invalid_agent_hdr());
    pkt->set_invalid_interface(stats->pkt_invalid_interface());
    pkt->set_no_handler(stats->pkt_no_handler());
    pkt->set_pkt_dropped(stats->pkt_dropped());
    pkt->set_context(context());
    pkt->set_more(true);
    pkt->Response();

    FlowStatsResp *flow = new FlowStatsResp();
    flow->set_flow_active(agent->pkt()->flow_table()->Size());
    flow->set_flow_created(stats->flow_created());
    flow->set_flow_aged(stats->flow_aged());
    flow->set_flow_drop_due_to_max_limit(stats->flow_drop_due_to_max_limit());
    flow->set_flow_drop_due_to_linklocal_limit(
            stats->flow_drop_due_to_linklocal_limit());
    flow->set_flow_max_system_flows(agent->flow_table_size());
    flow->set_flow_max_vm_flows(agent->pkt()->flow_table()->max_vm_flows());
    flow->set_context(context());
    flow->set_more(true);
    flow->Response();

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
