/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <fstream>
#include <uve/vrouter_uve_entry.h>
#include <cfg/cfg_init.h>
#include <init/agent_param.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <controller/controller_peer.h>
#include <uve/agent_uve.h>
#include <uve/vrouter_uve_entry.h>
#include <pkt/agent_stats.h>
#include <base/cpuinfo.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

using namespace std;

VrouterUveEntry::VrouterUveEntry(Agent *agent)
    : VrouterUveEntryBase(agent), bandwidth_count_(0), port_bitmap_() {
    start_time_ = UTCTimestampUsec();
}

VrouterUveEntry::~VrouterUveEntry() {
}

bool VrouterUveEntry::SendVrouterMsg() {
    static bool first = true;
    bool change = false;
    VrouterStatsAgent stats;

    VrouterUveEntryBase::SendVrouterMsg();

    stats.set_name(agent_->agent_name());

    if (prev_stats_.get_in_tpkts() !=
        agent_->stats()->in_pkts() || first) {
        stats.set_in_tpkts(agent_->stats()->in_pkts());
        prev_stats_.set_in_tpkts(agent_->stats()->in_pkts());
        change = true;
    }

    if (prev_stats_.get_in_bytes() !=
        agent_->stats()->in_bytes() || first) {
        stats.set_in_bytes(agent_->stats()->in_bytes());
        prev_stats_.set_in_bytes(agent_->stats()->in_bytes());
        change = true;
    }

    if (prev_stats_.get_out_tpkts() !=
        agent_->stats()->out_pkts() || first) {
        stats.set_out_tpkts(agent_->stats()->out_pkts());
        prev_stats_.set_out_tpkts(agent_->stats()->out_pkts());
        change = true;
    }

    if (prev_stats_.get_out_bytes() !=
        agent_->stats()->out_bytes() || first) {
        stats.set_out_bytes(agent_->stats()->out_bytes());
        prev_stats_.set_out_bytes(agent_->stats()->out_bytes());
        change = true;
    }

    vector<AgentXmppStats> xmpp_list;
    BuildXmppStatsList(xmpp_list);
    if (prev_stats_.get_xmpp_stats_list() != xmpp_list) {
        stats.set_xmpp_stats_list(xmpp_list);
        prev_stats_.set_xmpp_stats_list(xmpp_list);
        change = true;
    }

    if (prev_stats_.get_exception_packets() !=
        agent_->stats()->pkt_exceptions() || first) {
        stats.set_exception_packets(agent_->stats()->pkt_exceptions());
        prev_stats_.set_exception_packets(agent_->stats()->pkt_exceptions());
        change = true;
    }

    if (prev_stats_.get_exception_packets_dropped() !=
            agent_->stats()->pkt_dropped() || first) {
        stats.set_exception_packets_dropped(agent_->stats()->pkt_dropped());
        prev_stats_.set_exception_packets_dropped(agent_->stats()->
                                                  pkt_dropped());
        change = true;
    }

    uint64_t e_pkts_allowed = (agent_->stats()->pkt_exceptions() -
                               agent_->stats()->pkt_dropped());
    if (prev_stats_.get_exception_packets_allowed() != e_pkts_allowed) {
        stats.set_exception_packets_allowed(e_pkts_allowed);
        prev_stats_.set_exception_packets_allowed(e_pkts_allowed);
        change = true;
    }

    if (prev_stats_.get_total_flows() !=
            agent_->stats()->flow_created() || first) {
        stats.set_total_flows(agent_->stats()->flow_created());
        prev_stats_.set_total_flows(agent_->stats()->
                                    flow_created());
        change = true;
    }

    uint64_t active_flow_count = agent_->pkt()->flow_table()->Size();
    if (prev_stats_.get_active_flows() != active_flow_count || first) {
        stats.set_active_flows(active_flow_count);
        prev_stats_.set_active_flows(active_flow_count);
        change = true;
    }

    if (prev_stats_.get_aged_flows() !=
            agent_->stats()->flow_aged() || first) {
        stats.set_aged_flows(agent_->stats()->flow_aged());
        prev_stats_.set_aged_flows(agent_->stats()->flow_aged());
        change = true;
    }

    vector<AgentIfStats> phy_if_list;
    BuildPhysicalInterfaceList(phy_if_list);
    if (prev_stats_.get_phy_if_stats_list() != phy_if_list) {
        stats.set_phy_if_stats_list(phy_if_list);
        prev_stats_.set_phy_if_stats_list(phy_if_list);
        change = true;
    }
    bandwidth_count_++;
    if (first) {
        InitPrevStats();
        //First sample of bandwidth is sent after 1.5, 5.5 and 10.5 minutes
        bandwidth_count_ = 0;
    }
    // 1 minute bandwidth
    if (bandwidth_count_ && ((bandwidth_count_ % bandwidth_mod_1min) == 0)) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 1);
        if (prev_stats_.get_phy_if_1min_usage() != phy_if_blist) {
            stats.set_phy_if_1min_usage(phy_if_blist);
            prev_stats_.set_phy_if_1min_usage(phy_if_blist);
            change = true;

            vector<AgentIfBandwidth>::iterator it = phy_if_blist.begin();
            int num_intfs = 0, in_band = 0, out_band = 0;
            while(it != phy_if_blist.end()) {
                AgentIfBandwidth band = *it;
                in_band += band.get_in_bandwidth_usage();
                out_band += band.get_out_bandwidth_usage();
                num_intfs++;
                ++it;
            }
            stats.set_total_in_bandwidth_utilization((in_band/num_intfs));
            stats.set_total_out_bandwidth_utilization((out_band/num_intfs));
        }
    }

    // 5 minute bandwidth
    if (bandwidth_count_ && ((bandwidth_count_ % bandwidth_mod_5min) == 0)) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 5);
        if (prev_stats_.get_phy_if_5min_usage() != phy_if_blist) {
            stats.set_phy_if_5min_usage(phy_if_blist);
            prev_stats_.set_phy_if_5min_usage(phy_if_blist);
            change = true;
        }
    }

    // 10 minute bandwidth
    if (bandwidth_count_ && ((bandwidth_count_ % bandwidth_mod_10min) == 0)) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 10);
        if (prev_stats_.get_phy_if_10min_usage() != phy_if_blist) {
            stats.set_phy_if_10min_usage(phy_if_blist);
            prev_stats_.set_phy_if_10min_usage(phy_if_blist);
            change = true;
        }
        //The following avoids handling of count overflow cases.
        bandwidth_count_ = 0;
    }
    InetInterfaceKey key(agent_->vhost_interface_name());
    const Interface *vhost = static_cast<const Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
    const AgentStatsCollector::InterfaceStats *s =
        uve->agent_stats_collector()->GetInterfaceStats(vhost);
    if (s != NULL) {
        AgentIfStats vhost_stats;
        vhost_stats.set_name(agent_->vhost_interface_name());
        vhost_stats.set_in_pkts(s->in_pkts);
        vhost_stats.set_in_bytes(s->in_bytes);
        vhost_stats.set_out_pkts(s->out_pkts);
        vhost_stats.set_out_bytes(s->out_bytes);
        vhost_stats.set_speed(s->speed);
        vhost_stats.set_duplexity(s->duplexity);
        if (prev_stats_.get_vhost_stats() != vhost_stats) {
            stats.set_vhost_stats(vhost_stats);
            prev_stats_.set_vhost_stats(vhost_stats);
            change = true;
        }
    }

    if (SetVrouterPortBitmap(stats)) {
        change = true;
    }

    AgentDropStats drop_stats;
    FetchDropStats(drop_stats);
    if (prev_stats_.get_drop_stats() != drop_stats) {
        stats.set_drop_stats(drop_stats);
        prev_stats_.set_drop_stats(drop_stats);
        change = true;
    }
    if (first) {
        stats.set_uptime(start_time_);
    }

    if (change) {
        DispatchVrouterStatsMsg(stats);
    }
    first = false;
    return true;
}

uint8_t VrouterUveEntry::CalculateBandwitdh(uint64_t bytes, int speed_mbps,
                                            int diff_seconds) const {
    if (bytes == 0 || speed_mbps == 0) {
        return 0;
    }
    uint64_t bits = bytes * 8;
    if (diff_seconds == 0) {
        return 0;
    }
    uint64_t speed_bps = speed_mbps * 1024 * 1024;
    uint64_t bps = bits/diff_seconds;
    return (bps * 100)/speed_bps;
}

uint8_t VrouterUveEntry::GetBandwidthUsage
    (AgentStatsCollector::InterfaceStats *s, bool dir_in, int mins) const {

    uint64_t bytes;
    if (dir_in) {
        switch (mins) {
            case 1:
                bytes = s->in_bytes - s->prev_in_bytes;
                s->prev_in_bytes = s->in_bytes;
                break;
            case 5:
                bytes = s->in_bytes - s->prev_5min_in_bytes;
                s->prev_5min_in_bytes = s->in_bytes;
                break;
            default:
                bytes = s->in_bytes - s->prev_10min_in_bytes;
                s->prev_10min_in_bytes = s->in_bytes;
                break;
        }
    } else {
        switch (mins) {
            case 1:
                bytes = s->out_bytes - s->prev_out_bytes;
                s->prev_out_bytes = s->out_bytes;
                break;
            case 5:
                bytes = s->out_bytes - s->prev_5min_out_bytes;
                s->prev_5min_out_bytes = s->out_bytes;
                break;
            default:
                bytes = s->out_bytes - s->prev_10min_out_bytes;
                s->prev_10min_out_bytes = s->out_bytes;
                break;
        }
    }
    return CalculateBandwitdh(bytes, s->speed, (mins * 60));
}

bool VrouterUveEntry::BuildPhysicalInterfaceList(vector<AgentIfStats> &list)
                                                 const {
    bool changed = false;
    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
        AgentStatsCollector::InterfaceStats *s =
              uve->agent_stats_collector()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        AgentIfStats phy_stat_entry;
        phy_stat_entry.set_name(intf->name());
        phy_stat_entry.set_in_pkts(s->in_pkts);
        phy_stat_entry.set_in_bytes(s->in_bytes);
        phy_stat_entry.set_out_pkts(s->out_pkts);
        phy_stat_entry.set_out_bytes(s->out_bytes);
        phy_stat_entry.set_speed(s->speed);
        phy_stat_entry.set_duplexity(s->duplexity);
        list.push_back(phy_stat_entry);
        changed = true;
        ++it;
    }
    return changed;
}

bool VrouterUveEntry::BuildPhysicalInterfaceBandwidth
    (vector<AgentIfBandwidth> &phy_if_list, uint8_t mins) const {
    uint8_t in_band, out_band;
    bool changed = false;

    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
        AgentStatsCollector::InterfaceStats *s =
              uve->agent_stats_collector()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        AgentIfBandwidth phy_stat_entry;
        phy_stat_entry.set_name(intf->name());
        in_band = GetBandwidthUsage(s, true, mins);
        out_band = GetBandwidthUsage(s, false, mins);
        phy_stat_entry.set_in_bandwidth_usage(in_band);
        phy_stat_entry.set_out_bandwidth_usage(out_band);
        phy_if_list.push_back(phy_stat_entry);
        changed = true;
        ++it;
    }
    return changed;
}

void VrouterUveEntry::InitPrevStats() const {
    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
        AgentStatsCollector::InterfaceStats *s =
              uve->agent_stats_collector()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        s->prev_in_bytes = s->in_bytes;
        s->prev_5min_in_bytes = s->in_bytes;
        s->prev_10min_in_bytes = s->in_bytes;
        s->prev_out_bytes = s->out_bytes;
        s->prev_5min_out_bytes = s->out_bytes;
        s->prev_10min_out_bytes = s->out_bytes;
        ++it;
    }
}

void VrouterUveEntry::FetchDropStats(AgentDropStats &ds) const {
    AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
    vr_drop_stats_req stats = uve->agent_stats_collector()
                                           ->drop_stats();
    ds.ds_discard = stats.get_vds_discard();
    ds.ds_pull = stats.get_vds_pull();
    ds.ds_invalid_if = stats.get_vds_invalid_if();
    ds.ds_arp_not_me = stats.get_vds_arp_not_me();
    ds.ds_garp_from_vm = stats.get_vds_garp_from_vm();
    ds.ds_invalid_arp = stats.get_vds_invalid_arp();
    ds.ds_trap_no_if = stats.get_vds_trap_no_if();
    ds.ds_nowhere_to_go = stats.get_vds_nowhere_to_go();
    ds.ds_flow_queue_limit_exceeded = stats.
                                        get_vds_flow_queue_limit_exceeded();
    ds.ds_flow_no_memory = stats.get_vds_flow_no_memory();
    ds.ds_flow_invalid_protocol = stats.get_vds_flow_invalid_protocol();
    ds.ds_flow_nat_no_rflow = stats.get_vds_flow_nat_no_rflow();
    ds.ds_flow_action_drop = stats.get_vds_flow_action_drop();
    ds.ds_flow_action_invalid = stats.get_vds_flow_action_invalid();
    ds.ds_flow_unusable = stats.get_vds_flow_unusable();
    ds.ds_flow_table_full = stats.get_vds_flow_table_full();
    ds.ds_interface_tx_discard = stats.get_vds_interface_tx_discard();
    ds.ds_interface_drop = stats.get_vds_interface_drop();
    ds.ds_duplicated = stats.get_vds_duplicated();
    ds.ds_push = stats.get_vds_push();
    ds.ds_ttl_exceeded = stats.get_vds_ttl_exceeded();
    ds.ds_invalid_nh = stats.get_vds_invalid_nh();
    ds.ds_invalid_label = stats.get_vds_invalid_label();
    ds.ds_invalid_protocol = stats.get_vds_invalid_protocol();
    ds.ds_interface_rx_discard = stats.get_vds_interface_rx_discard();
    ds.ds_invalid_mcast_source = stats.get_vds_invalid_mcast_source();
    ds.ds_head_alloc_fail = stats.get_vds_head_alloc_fail();
    ds.ds_head_space_reserve_fail = stats.get_vds_head_space_reserve_fail();
    ds.ds_pcow_fail = stats.get_vds_pcow_fail();
    ds.ds_flood = stats.get_vds_flood();
    ds.ds_mcast_clone_fail = stats.get_vds_mcast_clone_fail();
    ds.ds_composite_invalid_interface = stats.
                                        get_vds_composite_invalid_interface();
    ds.ds_rewrite_fail = stats.get_vds_rewrite_fail();
    ds.ds_misc = stats.get_vds_misc();
    ds.ds_invalid_packet = stats.get_vds_invalid_packet();
    ds.ds_cksum_err = stats.get_vds_cksum_err();
    ds.ds_clone_fail = stats.get_vds_clone_fail();
    ds.ds_no_fmd = stats.get_vds_no_fmd();
    ds.ds_cloned_original = stats.get_vds_cloned_original();
    ds.ds_invalid_vnid = stats.get_vds_invalid_vnid();
    ds.ds_frag_err = stats.get_vds_frag_err();
}

void VrouterUveEntry::BuildXmppStatsList(vector<AgentXmppStats> &list) const {
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppStats peer;
        if (!agent_->controller_ifmap_xmpp_server(count).empty()) {
            AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            peer.set_ip(agent_->controller_ifmap_xmpp_server(count));
            peer.set_reconnects(agent_->stats()->xmpp_reconnects(count));
            peer.set_in_msgs(agent_->stats()->xmpp_in_msgs(count));
            peer.set_out_msgs(agent_->stats()->xmpp_out_msgs(count));
            list.push_back(peer);
        }
    }
}

bool VrouterUveEntry::SetVrouterPortBitmap(VrouterStatsAgent &vr_stats) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap_.tcp_sport_.Sync(tcp_sport)) {
        vr_stats.set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap_.tcp_dport_.Sync(tcp_dport)) {
        vr_stats.set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap_.udp_sport_.Sync(udp_sport)) {
        vr_stats.set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap_.udp_dport_.Sync(udp_dport)) {
        vr_stats.set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

void VrouterUveEntry::UpdateBitmap(uint8_t proto, uint16_t sport,
                                   uint16_t dport) {
    port_bitmap_.AddPort(proto, sport, dport);
}

uint32_t VrouterUveEntry::GetCpuCount() {
    return prev_stats_.get_cpu_info().get_num_cpu();
}

