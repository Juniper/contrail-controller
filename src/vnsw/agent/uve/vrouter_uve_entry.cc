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
#include <uve/agent_uve_stats.h>
#include <uve/vrouter_uve_entry.h>
#include <cmn/agent_stats.h>
#include <base/cpuinfo.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

using namespace std;

VrouterUveEntry::VrouterUveEntry(Agent *agent)
    : VrouterUveEntryBase(agent), bandwidth_count_(0), port_bitmap_(),
      prev_flow_setup_rate_export_time_(0), prev_flow_created_(0),
      prev_flow_aged_(0) {
    start_time_ = UTCTimestampUsec();
}

VrouterUveEntry::~VrouterUveEntry() {
}

bool VrouterUveEntry::SendVrouterMsg() {
    static bool first = true;
    uint64_t max_add_rate = 0, min_add_rate = 0;
    uint64_t max_del_rate = 0, min_del_rate = 0;
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
        double in_util = 0, out_util = 0;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 1, &in_util, &out_util);
        /* One minute bandwidth has 'tags' annotation and has to be sent
         * always regardless of change in bandwidth or not */
        stats.set_phy_if_band(phy_if_blist);
        change = true;
        if (in_util != prev_stats_.get_total_in_bandwidth_utilization()) {
            stats.set_total_in_bandwidth_utilization(in_util);
            prev_stats_.set_total_in_bandwidth_utilization(in_util);
        }
        if (out_util != prev_stats_.get_total_out_bandwidth_utilization()) {
            stats.set_total_out_bandwidth_utilization(out_util);
            prev_stats_.set_total_out_bandwidth_utilization(out_util);
        }
    }

    // 5 minute bandwidth
    if (bandwidth_count_ && ((bandwidth_count_ % bandwidth_mod_5min) == 0)) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 5, NULL, NULL);
        if (prev_stats_.get_phy_if_5min_usage() != phy_if_blist) {
            stats.set_phy_if_5min_usage(phy_if_blist);
            prev_stats_.set_phy_if_5min_usage(phy_if_blist);
            change = true;
        }
    }

    // 10 minute bandwidth
    if (bandwidth_count_ && ((bandwidth_count_ % bandwidth_mod_10min) == 0)) {
        vector<AgentIfBandwidth> phy_if_blist;
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 10, NULL, NULL);
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
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    const StatsManager::InterfaceStats *s =
        uve->stats_manager()->GetInterfaceStats(vhost);
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
    uint64_t cur_time = UTCTimestampUsec();
    if (prev_flow_setup_rate_export_time_) {
        uint64_t diff_time = cur_time - prev_flow_setup_rate_export_time_;
        uint64_t created_flows = agent_->stats()->flow_created() -
            prev_flow_created_;
        uint64_t aged_flows = agent_->stats()->flow_aged() - prev_flow_aged_;
        uint64_t diff_secs = diff_time / 1000000;
        if (diff_secs) {
            //Flow setup/delete rate are always sent
            if (created_flows) {
                max_add_rate = agent_->stats()->max_flow_adds_per_second();
                min_add_rate = agent_->stats()->min_flow_adds_per_second();
            }
            if (aged_flows) {
                max_del_rate = agent_->stats()->max_flow_deletes_per_second();
                min_del_rate = agent_->stats()->min_flow_deletes_per_second();
            }

            VrouterFlowRate flow_rate;
            flow_rate.set_added_flows(created_flows);
            flow_rate.set_max_flow_adds_per_second(max_add_rate);
            flow_rate.set_min_flow_adds_per_second(min_add_rate);
            flow_rate.set_deleted_flows(aged_flows);
            flow_rate.set_max_flow_deletes_per_second(max_del_rate);
            flow_rate.set_min_flow_deletes_per_second(min_del_rate);
            flow_rate.set_active_flows(agent_->pkt()->get_flow_proto()->
                                       FlowCount());
            stats.set_flow_rate(flow_rate);
            change = true;
            agent_->stats()->ResetFlowAddMinMaxStats(cur_time);
            agent_->stats()->ResetFlowDelMinMaxStats(cur_time);
            prev_flow_setup_rate_export_time_ = cur_time;
            prev_flow_created_ = agent_->stats()->flow_created();
            prev_flow_aged_ = agent_->stats()->flow_aged();
        }
    } else {
        prev_flow_setup_rate_export_time_ = cur_time;
    }

    if (change) {
        DispatchVrouterStatsMsg(stats);
    }
    first = false;
    return true;
}

uint64_t VrouterUveEntry::CalculateBandwitdh(uint64_t bytes, int speed_mbps,
                                             int diff_seconds,
                                             double *utilization_bps) const {
    *utilization_bps = 0;
    if (bytes == 0 || speed_mbps == 0) {
        return 0;
    }
    uint64_t bits = bytes * 8;
    if (diff_seconds == 0) {
        return 0;
    }
    /* Compute bandwidth in bps */
    uint64_t bps = bits/diff_seconds;

    /* Compute network utilization in percentage */
    uint64_t speed_bps = speed_mbps * 1024 * 1024;
    double bps_double = bits/diff_seconds;
    *utilization_bps = (bps_double * 100)/speed_bps;
    return bps;
}

uint64_t VrouterUveEntry::GetBandwidthUsage(StatsManager::InterfaceStats *s,
                                           bool dir_in, int mins,
                                           double *util) const {

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
    return CalculateBandwitdh(bytes, s->speed, (mins * 60), util);
}

bool VrouterUveEntry::BuildPhysicalInterfaceList(vector<AgentIfStats> &list)
                                                 const {
    bool changed = false;
    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
        StatsManager::InterfaceStats *s =
              uve->stats_manager()->GetInterfaceStats(intf);
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
    (vector<AgentIfBandwidth> &phy_if_list, uint8_t mins, double *in_avg_util,
     double *out_avg_util) const {
    uint64_t in_band, out_band;
    double in_util, out_util;
    bool changed = false;
    int num_intfs = 0;
    if (in_avg_util != NULL) {
        *in_avg_util = 0;
    }
    if (out_avg_util != NULL) {
        *out_avg_util = 0;
    }

    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
        StatsManager::InterfaceStats *s =
              uve->stats_manager()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        AgentIfBandwidth phy_stat_entry;
        phy_stat_entry.set_name(intf->name());
        in_band = GetBandwidthUsage(s, true, mins, &in_util);
        out_band = GetBandwidthUsage(s, false, mins, &out_util);
        phy_stat_entry.set_in_bandwidth_usage(in_band);
        phy_stat_entry.set_out_bandwidth_usage(out_band);
        phy_if_list.push_back(phy_stat_entry);
        changed = true;
        if (in_avg_util != NULL) {
            *in_avg_util += in_util;
        }
        if (out_avg_util != NULL) {
            *out_avg_util += out_util;
        }
        ++it;
        num_intfs++;
    }
    if ((in_avg_util != NULL) && num_intfs) {
        *in_avg_util /= num_intfs;
    }
    if ((out_avg_util != NULL) && num_intfs) {
        *out_avg_util /= num_intfs;
    }
    return changed;
}

void VrouterUveEntry::InitPrevStats() const {
    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
        StatsManager::InterfaceStats *s =
              uve->stats_manager()->GetInterfaceStats(intf);
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
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    ds = uve->stats_manager()->drop_stats();
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
