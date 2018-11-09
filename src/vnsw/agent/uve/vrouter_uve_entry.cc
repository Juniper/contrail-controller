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
#include <vrouter/flow_stats/flow_stats_manager.h>

using namespace std;

VrouterUveEntry::VrouterUveEntry(Agent *agent)
    : VrouterUveEntryBase(agent), bandwidth_count_(0), port_bitmap_(),
      flow_info_(), vrf_walk_id_(DBTableWalker::kInvalidWalkerId) {
    start_time_ = UTCTimestampUsec();
}

VrouterUveEntry::~VrouterUveEntry() {
}

bool VrouterUveEntry::SendVrouterMsg() {
    static bool first = true;
    VrouterStatsAgent stats;

    VrouterUveEntryBase::SendVrouterMsg();

    stats.set_name(agent_->agent_name());

    if (prev_stats_.get_in_tpkts() !=
        agent_->stats()->in_pkts() || first) {
        stats.set_in_tpkts(agent_->stats()->in_pkts());
        prev_stats_.set_in_tpkts(agent_->stats()->in_pkts());
    }

    if (prev_stats_.get_in_bytes() !=
        agent_->stats()->in_bytes() || first) {
        stats.set_in_bytes(agent_->stats()->in_bytes());
        prev_stats_.set_in_bytes(agent_->stats()->in_bytes());
    }

    if (prev_stats_.get_out_tpkts() !=
        agent_->stats()->out_pkts() || first) {
        stats.set_out_tpkts(agent_->stats()->out_pkts());
        prev_stats_.set_out_tpkts(agent_->stats()->out_pkts());
    }

    if (prev_stats_.get_out_bytes() !=
        agent_->stats()->out_bytes() || first) {
        stats.set_out_bytes(agent_->stats()->out_bytes());
        prev_stats_.set_out_bytes(agent_->stats()->out_bytes());
    }

    if (prev_stats_.get_exception_packets() !=
        agent_->stats()->pkt_exceptions() || first) {
        stats.set_exception_packets(agent_->stats()->pkt_exceptions());
        prev_stats_.set_exception_packets(agent_->stats()->pkt_exceptions());
    }

    if (prev_stats_.get_exception_packets_dropped() !=
            agent_->stats()->pkt_dropped() || first) {
        stats.set_exception_packets_dropped(agent_->stats()->pkt_dropped());
        prev_stats_.set_exception_packets_dropped(agent_->stats()->
                                                  pkt_dropped());
    }

    uint64_t e_pkts_allowed = (agent_->stats()->pkt_exceptions() -
                               agent_->stats()->pkt_dropped());
    if (prev_stats_.get_exception_packets_allowed() != e_pkts_allowed) {
        stats.set_exception_packets_allowed(e_pkts_allowed);
        prev_stats_.set_exception_packets_allowed(e_pkts_allowed);
    }

    if (prev_stats_.get_total_flows() !=
        agent_->stats()->flow_created() || first) {
        stats.set_total_flows(agent_->stats()->flow_created());
        prev_stats_.set_total_flows(agent_->stats()->
                                    flow_created());
    }

    if (prev_stats_.get_aged_flows() !=
            agent_->stats()->flow_aged() || first) {
        stats.set_aged_flows(agent_->stats()->flow_aged());
        prev_stats_.set_aged_flows(agent_->stats()->flow_aged());
    }
    map<string, PhyIfStats> phy_if_list;
    map<string, PhyIfInfo> phy_if_info;
    map<string, AgentDropStats> phy_if_ds;
    BuildPhysicalInterfaceList(phy_if_list, phy_if_info, phy_if_ds);
    stats.set_raw_phy_if_stats(phy_if_list);
    stats.set_raw_phy_if_drop_stats(phy_if_ds);

    if (prev_stats_.get_phy_if_info() != phy_if_info) {
        stats.set_phy_if_info(phy_if_info);
        prev_stats_.set_phy_if_info(phy_if_info);
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
        map<string,uint64_t> inb,outb;
        BuildPhysicalInterfaceBandwidth(inb, outb, 1, in_util, out_util);
        /* One minute bandwidth has 'tags' annotation and has to be sent
         * always regardless of change in bandwidth or not */
        stats.set_phy_band_in_bps(inb);
        stats.set_phy_band_out_bps(outb);
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
        BuildPhysicalInterfaceBandwidth(phy_if_blist, 5);
        if (prev_stats_.get_phy_if_5min_usage() != phy_if_blist) {
            stats.set_phy_if_5min_usage(phy_if_blist);
            prev_stats_.set_phy_if_5min_usage(phy_if_blist);
        }
    }

    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, boost::uuids::nil_uuid(),
                       agent_->vhost_interface_name());
    const Interface *vhost = static_cast<const Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    const StatsManager::InterfaceStats *s =
        uve->stats_manager()->GetInterfaceStats(vhost);
    if (s != NULL) {
        AgentIfStats vhost_stats;
        AgentDropStats vhost_ds;
        vhost_stats.set_name(agent_->vhost_interface_name());
        vhost_stats.set_in_pkts(s->in_pkts);
        vhost_stats.set_in_bytes(s->in_bytes);
        vhost_stats.set_out_pkts(s->out_pkts);
        vhost_stats.set_out_bytes(s->out_bytes);
        vhost_stats.set_speed(s->speed);
        vhost_stats.set_duplexity(s->duplexity);
        uve->stats_manager()->BuildDropStats(s->drop_stats, vhost_ds);
        stats.set_raw_vhost_stats(vhost_stats);
        stats.set_raw_vhost_drop_stats(vhost_ds);
    }

    SetVrouterPortBitmap(stats);

    AgentDropStats ds;
    FetchDropStats(ds);
    stats.set_raw_drop_stats(ds);

    if (first) {
        stats.set_uptime(start_time_);
    }
    AgentStats::FlowCounters &added =  agent_->stats()->added();
    AgentStats::FlowCounters &deleted =  agent_->stats()->deleted();
    uint32_t active_flows = agent_->pkt()->get_flow_proto()->FlowCount();

    VrouterFlowRate flow_rate;
    bool built = uve->stats_manager()->BuildFlowRate(added, deleted, flow_info_,
                                                     flow_rate);
    if (built) {
        flow_rate.set_active_flows(active_flows);
        stats.set_flow_rate(flow_rate);
    }

    DispatchVrouterStatsMsg(stats);
    first = false;

    //Send VrouterControlStats UVE
    SendVrouterControlStats();
    return true;
}

uint64_t VrouterUveEntry::CalculateBandwitdh(uint64_t bytes, int speed_mbps,
                                             int diff_seconds,
                                             double *utilization_bps) const {
    if (utilization_bps) *utilization_bps = 0;
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
    if (utilization_bps) *utilization_bps = (bps_double * 100)/speed_bps;
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
            default:
                bytes = s->in_bytes - s->prev_5min_in_bytes;
                s->prev_5min_in_bytes = s->in_bytes;
                break;
        }
    } else {
        switch (mins) {
            case 1:
                bytes = s->out_bytes - s->prev_out_bytes;
                s->prev_out_bytes = s->out_bytes;
                break;
            default:
                bytes = s->out_bytes - s->prev_5min_out_bytes;
                s->prev_5min_out_bytes = s->out_bytes;
                break;
        }
    }
    return CalculateBandwitdh(bytes, s->speed, (mins * 60), util);
}

bool VrouterUveEntry::BuildPhysicalInterfaceList(map<string, PhyIfStats> &list,
                                                 map<string, PhyIfInfo> &info,
                                                 map<string, AgentDropStats> &dsmap)
                                                 const {
    bool changed = false;
    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        ++it;
        AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
        StatsManager::InterfaceStats *s =
              uve->stats_manager()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        PhyIfStats phy_stat_entry;
        phy_stat_entry.set_in_pkts(s->in_pkts);
        phy_stat_entry.set_in_bytes(s->in_bytes);
        phy_stat_entry.set_out_pkts(s->out_pkts);
        phy_stat_entry.set_out_bytes(s->out_bytes);
        list.insert(make_pair(intf->name(), phy_stat_entry));

        PhyIfInfo phy_if_info;
        phy_if_info.set_speed(s->speed);
        phy_if_info.set_duplexity(s->duplexity);
        info.insert(make_pair(intf->name(), phy_if_info));

        AgentDropStats ds;
        uve->stats_manager()->BuildDropStats(s->drop_stats, ds);
        dsmap.insert(make_pair(intf->name(), ds));
        changed = true;
    }
    return changed;
}

bool VrouterUveEntry::BuildPhysicalInterfaceBandwidth
    (vector<AgentIfBandwidth> &phy_if_list, uint8_t mins) const {
    uint64_t in_band, out_band;
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
        AgentIfBandwidth phy_stat_entry;
        phy_stat_entry.set_name(intf->name());
        in_band = GetBandwidthUsage(s, true, mins, NULL);
        out_band = GetBandwidthUsage(s, false, mins, NULL);
        phy_stat_entry.set_in_bandwidth_usage(in_band);
        phy_stat_entry.set_out_bandwidth_usage(out_band);
        phy_if_list.push_back(phy_stat_entry);
        changed = true;
        ++it;
    }
    return changed;
}

bool VrouterUveEntry::BuildPhysicalInterfaceBandwidth
    (map<string,uint64_t> &imp, map<string,uint64_t> &omp,
     uint8_t mins, double &in_avg_util,
     double &out_avg_util) const {
    uint64_t in_band, out_band;
    double in_util, out_util;
    bool changed = false;
    int num_intfs = 0;
    in_avg_util = 0;
    out_avg_util = 0;

    PhysicalInterfaceSet::const_iterator it = phy_intf_set_.begin();
    while (it != phy_intf_set_.end()) {
        const Interface *intf = *it;
        ++it;
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
        imp.insert(make_pair(intf->name(),in_band));
        omp.insert(make_pair(intf->name(),out_band));
        changed = true;
        in_avg_util += in_util;
        out_avg_util += out_util;
        num_intfs++;
    }
    if (num_intfs) {
        in_avg_util /= num_intfs;
        out_avg_util /= num_intfs;
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
        s->prev_out_bytes = s->out_bytes;
        s->prev_5min_out_bytes = s->out_bytes;
        ++it;
    }
}

void VrouterUveEntry::FetchDropStats(AgentDropStats &ds) const {
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    const vr_drop_stats_req &req = uve->stats_manager()->drop_stats();
    uve->stats_manager()->BuildDropStats(req, ds);
}

void VrouterUveEntry::FetchIFMapStats(AgentUve::DerivedStatsMap *ds) const {
    IFMapAgentParser *parser = agent_->cfg()->cfg_parser();
    if (parser) {
        ds->insert(AgentUve::DerivedStatsPair("node_update_parse_errors",
                                    parser->node_update_parse_errors()));
        ds->insert(AgentUve::DerivedStatsPair("link_update_parse_errors",
                                    parser->link_update_parse_errors()));
        ds->insert(AgentUve::DerivedStatsPair("node_delete_parse_errors",
                                    parser->node_delete_parse_errors()));
        ds->insert(AgentUve::DerivedStatsPair("link_delete_parse_errors",
                                    parser->link_delete_parse_errors()));
    }
}

void VrouterUveEntry::BuildXmppStatsList
    (std::map<std::string, AgentXmppStats> *xstats) const {
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
            peer.set_reconnects(agent_->stats()->xmpp_reconnects(count));
            peer.set_in_msgs(agent_->stats()->xmpp_in_msgs(count));
            peer.set_out_msgs(agent_->stats()->xmpp_out_msgs(count));
            xstats->insert(std::make_pair(
                               agent_->controller_ifmap_xmpp_server(count),
                               peer));
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

void VrouterUveEntry::VrfWalkDone(DBTableBase *base, RouteTableSizeMapPtr list){
    vrf_walk_id_ = DBTableWalker::kInvalidWalkerId;
    BuildAndSendVrouterControlStats(list);
}

bool VrouterUveEntry::AppendVrf(DBTablePartBase *part, DBEntryBase *entry,
                                RouteTableSizeMapPtr list) {
    VrfEntry *vrf = static_cast<VrfEntry *>(entry);

    if (!vrf->IsDeleted()) {
        RouteTableSize value;
        value.set_inet4_unicast(vrf->GetInet4UnicastRouteTable()->Size());
        value.set_inet4_multicast(vrf->GetInet4MulticastRouteTable()->Size());
        value.set_evpn(vrf->GetEvpnRouteTable()->Size());
        value.set_bridge(vrf->GetBridgeRouteTable()->Size());
        value.set_inet6_unicast(vrf->GetInet6UnicastRouteTable()->Size());
        list.get()->insert(RouteTableSizePair(vrf->GetName(), value));
    }
    return true;
}

bool VrouterUveEntry::StartVrfWalk() {
    if (vrf_walk_id_ != DBTableWalker::kInvalidWalkerId) {
        return false;
    }

    RouteTableSizeMapPtr list(new RouteTableSizeMap());
    DBTableWalker *walker = agent_->db()->GetWalker();
    vrf_walk_id_ = walker->WalkTable(agent_->vrf_table(), NULL,
             boost::bind(&VrouterUveEntry::AppendVrf, this, _1, _2, list),
             boost::bind(&VrouterUveEntry::VrfWalkDone, this, _1, list));
    return true;
}

void VrouterUveEntry::DispatchVrouterControlStats
    (const VrouterControlStats &uve) const {
    VrouterControlStatsTrace::Send(uve);
}

void VrouterUveEntry::SendVrouterControlStats() {
    /* We do VRF walk to collect route table sizes. In Walk Done API we trigger
     * building of all attributes of VrouterControlStats UVE and send it*/
    StartVrfWalk();
}

void VrouterUveEntry::BuildAndSendVrouterControlStats(RouteTableSizeMapPtr
                                                      list) {
    VrouterControlStats stats;
    stats.set_name(agent_->agent_name());

    std::map<std::string, AgentXmppStats> xstats;
    BuildXmppStatsList(&xstats);
    stats.set_raw_xmpp_stats(xstats);

    AgentUve::DerivedStatsMap ifmap_stats;
    FetchIFMapStats(&ifmap_stats);
    stats.set_raw_ifmap_stats(ifmap_stats);

    stats.set_raw_rt_table_size(*(list.get()));
    DispatchVrouterControlStats(stats);
}
