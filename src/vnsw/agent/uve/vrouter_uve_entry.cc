/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <fstream>
#include <uve/vrouter_uve_entry.h>
#include <cfg/cfg_init.h>
#include <cmn/agent_param.h>
#include <oper/interface_common.h>
#include <oper/interface.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <controller/controller_peer.h>
#include <uve/agent_uve.h>
#include <pkt/agent_stats.h>
#include <base/cpuinfo.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

using namespace std;

VrouterUveEntry::VrouterUveEntry(Agent *agent)
    : prev_stats_(), bandwidth_count_(0), cpu_stats_count_(0), port_bitmap_(),
      agent_(agent), phy_intf_set_(), vn_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId), prev_vrouter_() {
    start_time_ = UTCTimestampUsec();
}

VrouterUveEntry::~VrouterUveEntry() {
}

void VrouterUveEntry::RegisterDBClients() {
    VnTable *vn_table = agent_->vn_table();
    vn_listener_id_ = vn_table->Register
                  (boost::bind(&VrouterUveEntry::VnNotify, this, _1, _2));

    VmTable *vm_table = agent_->vm_table();
    vm_listener_id_ = vm_table->Register
        (boost::bind(&VrouterUveEntry::VmNotify, this, _1, _2));

    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
               (boost::bind(&VrouterUveEntry::InterfaceNotify, this, _1, _2));
}

void VrouterUveEntry::Shutdown(void) {
    agent_->interface_table()->Unregister(intf_listener_id_);
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);
}

void VrouterUveEntry::DispatchVrouterMsg(const VrouterAgent &uve) {
    UveVrouterAgent::Send(uve);
}

void VrouterUveEntry::DispatchVrouterStatsMsg(const VrouterStatsAgent &uve) {
    VrouterStats::Send(uve);
}

void VrouterUveEntry::DispatchComputeCpuStateMsg(const ComputeCpuState &ccs) {
    ComputeCpuStateTrace::Send(ccs);
}

void VrouterUveEntry::VmWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->host_name());
    vrouter_agent.set_virtual_machine_list(*(list.get()));
    DispatchVrouterMsg(vrouter_agent);
}

bool VrouterUveEntry::AppendVm(DBTablePartBase *part, DBEntryBase *entry,
                               StringVectorPtr list) {
    VmEntry *vm = static_cast<VmEntry *>(entry);

    if (!vm->IsDeleted()) {
        std::ostringstream ostr;
        ostr << vm->GetUuid();
        list.get()->push_back(ostr.str());
    }
    return true;
}

void VrouterUveEntry::VmNotifyHandler(const VmEntry *vm) {
    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->vm_table(), NULL,
        boost::bind(&VrouterUveEntry::AppendVm, this, _1, _2, list),
        boost::bind(&VrouterUveEntry::VmWalkDone, this, _1, list));
}

void VrouterUveEntry::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmEntry *vm = static_cast<const VmEntry *>(e);

    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            VmNotifyHandler(vm);
            e->ClearState(partition->parent(), vm_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vm_listener_id_, state);
        //Send vrouter object only for a add/delete
        VmNotifyHandler(vm);
    }
}

void VrouterUveEntry::VnWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->host_name());
    vrouter_agent.set_connected_networks(*(list.get()));
    DispatchVrouterMsg(vrouter_agent);
}

bool VrouterUveEntry::AppendVn(DBTablePartBase *part, DBEntryBase *entry,
                               StringVectorPtr list) {
    VnEntry *vn = static_cast<VnEntry *>(entry);

    if (!vn->IsDeleted()) {
        list.get()->push_back(vn->GetName());
    }
    return true;
}

void VrouterUveEntry::VnNotifyHandler(const VnEntry *vn) {
    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->vn_table(), NULL, 
             boost::bind(&VrouterUveEntry::AppendVn, this, _1, _2, list),
             boost::bind(&VrouterUveEntry::VnWalkDone, this, _1, list));
}

void VrouterUveEntry::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VnEntry *vn = static_cast<const VnEntry *>(e);

    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vn_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            VnNotifyHandler(vn);
            e->ClearState(partition->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vn_listener_id_, state);
        VnNotifyHandler(vn);
    }
}

void VrouterUveEntry::InterfaceWalkDone(DBTableBase *base,
                                        StringVectorPtr if_list,
                                        StringVectorPtr err_if_list,
                                        StringVectorPtr nova_if_list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->host_name());
    vrouter_agent.set_interface_list(*(if_list.get()));
    vrouter_agent.set_error_intf_list(*(err_if_list.get()));
    vrouter_agent.set_no_config_intf_list(*(nova_if_list.get()));
    vrouter_agent.set_total_interface_count((if_list.get()->size() +
                                             nova_if_list.get()->size()));
    vrouter_agent.set_down_interface_count((err_if_list.get()->size() +
                                            nova_if_list.get()->size()));
    DispatchVrouterMsg(vrouter_agent);
}

bool VrouterUveEntry::AppendInterface(DBTablePartBase *part,
                                      DBEntryBase *entry,
                                      StringVectorPtr intf_list,
                                      StringVectorPtr err_if_list,
                                      StringVectorPtr nova_if_list) {
    Interface *intf = static_cast<Interface *>(entry);

    if (intf->type() == Interface::VM_INTERFACE) {
        const VmInterface *port = static_cast<const VmInterface *>(intf);
        if (!entry->IsDeleted()) {
            if (port->cfg_name() == agent_->NullString()) {
                nova_if_list.get()->push_back(UuidToString(port->GetUuid()));
            } else {
                intf_list.get()->push_back(port->cfg_name());
                if (!intf->ipv4_active() && !intf->l2_active()) {
                    err_if_list.get()->push_back(port->cfg_name());
                }
            }
        }
    }
    return true;
}

void VrouterUveEntry::InterfaceNotifyHandler(const Interface *intf) {
    StringVectorPtr intf_list(new std::vector<std::string>());
    StringVectorPtr err_if_list(new std::vector<std::string>());
    StringVectorPtr nova_if_list(new std::vector<std::string>());

    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->interface_table(), NULL,
        boost::bind(&VrouterUveEntry::AppendInterface, this, _1, _2, intf_list,
                    err_if_list, nova_if_list),
        boost::bind(&VrouterUveEntry::InterfaceWalkDone, this, _1, intf_list,
                    err_if_list, nova_if_list));
}

void VrouterUveEntry::InterfaceNotify(DBTablePartBase *partition,
                                      DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    bool set_state = false, reset_state = false;

    VrouterUveInterfaceState *state = static_cast<VrouterUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    bool vmport_ipv4_active = false;
    bool vmport_l2_active = false;
    const VmInterface *vm_port = NULL;
    switch(intf->type()) {
    case Interface::VM_INTERFACE:
        vm_port = static_cast<const VmInterface*>(intf);
        if (!e->IsDeleted() && !state) {
            set_state = true;
            vmport_ipv4_active = vm_port->ipv4_active();
            vmport_l2_active = vm_port->l2_active();
            InterfaceNotifyHandler(intf);
        } else if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
                InterfaceNotifyHandler(intf);
            }
        } else {
            if (state && vm_port->ipv4_active() != state->vmport_ipv4_active_) {
                InterfaceNotifyHandler(intf);
                state->vmport_ipv4_active_ = vm_port->ipv4_active();
            }
            if (state && vm_port->l2_active() != state->vmport_l2_active_) {
                InterfaceNotifyHandler(intf);
                state->vmport_l2_active_ = vm_port->l2_active();
            }
        }
        break;
    case Interface::PHYSICAL:
        if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
                phy_intf_set_.erase(intf);
            }
        } else {
            if (!state) {
                set_state = true;
                phy_intf_set_.insert(intf);
            }
        }
        break;
    default:
        break;
    }
    if (set_state) {
        state = new VrouterUveInterfaceState(vmport_ipv4_active, vmport_l2_active);
        e->SetState(partition->parent(), intf_listener_id_, state);
    } else if (reset_state) {
        e->ClearState(partition->parent(), intf_listener_id_);
        delete state;
    }
    return;
}

void VrouterUveEntry::BuildAndSendComputeCpuStateMsg(const CpuLoadInfo &info) {
    ComputeCpuState astate;
    VrouterCpuInfo ainfo;
    vector<VrouterCpuInfo> aciv;

    astate.set_name(agent_->host_name());
    ainfo.set_cpu_share(info.get_cpu_share());
    ainfo.set_mem_virt(info.get_meminfo().get_virt());
    ainfo.set_used_sys_mem(info.get_sys_mem_info().get_used());
    ainfo.set_one_min_cpuload(info.get_cpuload().get_one_min_avg());
    aciv.push_back(ainfo);
    astate.set_cpu_info(aciv);
    DispatchComputeCpuStateMsg(astate);
}

bool VrouterUveEntry::SendVrouterMsg() {
    static bool first = true;
    bool change = false;
    VrouterStatsAgent stats;

    SendVrouterUve();

    stats.set_name(agent_->host_name());

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

    cpu_stats_count_++;
    if ((cpu_stats_count_ % 6) == 0) {
        static bool cpu_first = true;
        CpuLoadInfo cpu_load_info;
        CpuLoadData::FillCpuInfo(cpu_load_info, true);
        if (prev_stats_.get_cpu_info() != cpu_load_info || cpu_first) {
            stats.set_cpu_info(cpu_load_info);
            prev_stats_.set_cpu_info(cpu_load_info);
            change = true;
            cpu_first = false;
        }
        //Cpu and mem stats needs to be sent always regardless of whether stats
        //have changed since last send
        stats.set_cpu_share(cpu_load_info.get_cpu_share());
        stats.set_virt_mem(cpu_load_info.get_meminfo().get_virt());
        stats.set_used_sys_mem(cpu_load_info.get_sys_mem_info().get_used());
        stats.set_one_min_avg_cpuload(
                cpu_load_info.get_cpuload().get_one_min_avg());

        //Stats oracle interface for cpu and mem stats. Needs to be sent
        //always regardless of whether the stats have changed since last send
        BuildAndSendComputeCpuStateMsg(cpu_load_info);
        cpu_stats_count_ = 0;
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
    const AgentStatsCollector::InterfaceStats *s =
        agent_->uve()->agent_stats_collector()->GetInterfaceStats(vhost);
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

void VrouterUveEntry::SubnetToStringList
    (VirtualGatewayConfig::SubnetList &source_list, vector<string> &target_list) {
    VirtualGatewayConfig::SubnetList::iterator subnet_it =
        source_list.begin();
    while (subnet_it != source_list.end()) {
        string subnet_str = subnet_it->ip_.to_string() + "/" +
            integerToString(subnet_it->plen_);
        target_list.push_back(subnet_str);
        ++subnet_it;
    }
}

void VrouterUveEntry::BuildAgentConfig(VrouterAgent &vrouter_agent) {
    AgentVhostConfig vhost_cfg;
    AgentXenConfig xen_cfg;
    AgentVmwareConfig vmware_cfg;
    string hypervisor;
    vector<string> dns_list;
    vector<string> control_node_list;
    vector<AgentVgwConfig> gw_cfg_list;

    AgentParam *param = agent_->params();

    vrouter_agent.set_log_file(param->log_file());
    vrouter_agent.set_config_file(param->config_file());
    vrouter_agent.set_log_local(param->log_local());
    vrouter_agent.set_log_flow(param->log_flow());
    vrouter_agent.set_log_category(param->log_category());
    vrouter_agent.set_log_level(param->log_level());
    vrouter_agent.set_sandesh_http_port(param->http_server_port());
    vrouter_agent.set_tunnel_type(param->tunnel_type());
    vrouter_agent.set_hostname_cfg(param->host_name());
    vrouter_agent.set_flow_cache_timeout_cfg(param->flow_cache_timeout());

    dns_list.push_back(param->dns_server_1().to_string());
    dns_list.push_back(param->dns_server_2().to_string());
    vrouter_agent.set_dns_server_list_cfg(dns_list);

    control_node_list.push_back(param->xmpp_server_1().to_string());
    control_node_list.push_back(param->xmpp_server_2().to_string());
    vrouter_agent.set_control_node_list_cfg(control_node_list);

    vrouter_agent.set_ll_max_system_flows_cfg(param->linklocal_system_flows());
    vrouter_agent.set_ll_max_vm_flows_cfg(param->linklocal_vm_flows());
    vrouter_agent.set_max_vm_flows_cfg((uint32_t)param->max_vm_flows());
    vrouter_agent.set_control_ip(param->mgmt_ip().to_string());

    vhost_cfg.set_name(param->vhost_name());
    vhost_cfg.set_ip(param->vhost_addr().to_string());
    vhost_cfg.set_ip_prefix_len(param->vhost_plen());
    vhost_cfg.set_gateway(param->vhost_gw().to_string());
    vrouter_agent.set_vhost_cfg(vhost_cfg);

    vrouter_agent.set_eth_name(param->eth_port());

    if (param->isKvmMode()) {
        hypervisor = "kvm";
    } else if (param->isXenMode()) {
        hypervisor = "xen";
        xen_cfg.set_xen_ll_port(param->xen_ll_name());
        xen_cfg.set_xen_ll_ip(param->xen_ll_addr().to_string());
        xen_cfg.set_xen_ll_prefix_len(param->xen_ll_plen());
        vrouter_agent.set_xen_cfg(xen_cfg);
    } else if (param->isVmwareMode()) {
        hypervisor = "vmware";
        vmware_cfg.set_vmware_port(param->vmware_physical_port());
        vrouter_agent.set_vmware_cfg(vmware_cfg);
    }
    vrouter_agent.set_hypervisor(hypervisor);

    vrouter_agent.set_ds_addr(param->discovery_server().to_string());
    vrouter_agent.set_ds_xs_instances(param->xmpp_instance_count());

    VirtualGatewayConfigTable *table = param->vgw_config_table();
    VirtualGatewayConfigTable::Table::iterator it = table->table().begin();
    while (it != table->table().end()) {
        AgentVgwConfig  gw_cfg;
        VirtualGatewayConfig::SubnetList subnet_list = it->subnets();
        VirtualGatewayConfig::SubnetList route_list = it->routes();
        vector<string> ip_blocks_list;
        vector<string> route_str_list;

        SubnetToStringList(subnet_list, ip_blocks_list);
        SubnetToStringList(route_list, route_str_list);

        gw_cfg.set_interface_name(it->interface_name());
        gw_cfg.set_vrf_name(it->vrf_name());
        gw_cfg.set_ip_blocks_list(ip_blocks_list);
        gw_cfg.set_route_list(route_str_list);

        gw_cfg_list.push_back(gw_cfg);
        ++it;
    }
    vrouter_agent.set_gateway_cfg_list(gw_cfg_list);
    vrouter_agent.set_headless_mode_cfg(param->headless_mode());
    vrouter_agent.set_collector_server_list_cfg(param->collector_server_list());
}


void VrouterUveEntry::SendVrouterUve() {
    VrouterAgent vrouter_agent;
    bool changed = false;
    static bool first = true, build_info = false;
    vrouter_agent.set_name(agent_->host_name());
    Ip4Address rid = agent_->router_id();
    vector<string> ip_list;
    vector<string> dns_list;

    if (first) {
        //Physical interface list
        vector<AgentInterface> phy_if_list;
        PhysicalInterfaceSet::iterator it = phy_intf_set_.begin();
        while (it != phy_intf_set_.end()) {
            AgentInterface pitf;
            const Interface *intf = *it;
            const PhysicalInterface *port = static_cast
                                            <const PhysicalInterface *>(intf);
            pitf.set_name(intf->name());
            pitf.set_mac_address(GetMacAddress(port->mac()));
            phy_if_list.push_back(pitf);
            ++it;
        }
        vrouter_agent.set_phy_if(phy_if_list);

        //vhost attributes
        InetInterfaceKey key(agent_->vhost_interface_name());
        const Interface *vhost = static_cast<const Interface *>(
             agent_->interface_table()->FindActiveEntry(&key));
        if (vhost) {
            AgentInterface vitf;
            vitf.set_name(vhost->name());
            vitf.set_mac_address(GetMacAddress(vhost->mac()));
            vrouter_agent.set_vhost_if(vitf);
        }

        //Configuration. Needs to be sent only once because whenever config
        //changes agent will be restarted
        BuildAgentConfig(vrouter_agent);

        first = false;
        changed = true;
    }

    if (!build_info) {
        string build_info_str;
        build_info = GetBuildInfo(build_info_str);
        if (prev_vrouter_.get_build_info() != build_info_str) {
            vrouter_agent.set_build_info(build_info_str);
            prev_vrouter_.set_build_info(build_info_str);
            changed = true;
        }

    }

    std::vector<AgentXmppPeer> xmpp_list;
    for (int count = 0; count < MAX_XMPP_SERVERS; count++) {
        AgentXmppPeer peer;
        if (!agent_->controller_ifmap_xmpp_server(count).empty()) {
            peer.set_ip(agent_->controller_ifmap_xmpp_server(count));
            AgentXmppChannel *ch = agent_->controller_xmpp_channel(count);
            if (ch == NULL) {
                continue;
            }
            XmppChannel *xc = ch->GetXmppChannel();
            if (xc == NULL) {
                continue;
            }
            if (ch->bgp_peer_id() && xc->GetPeerState() == xmps::READY) {
                peer.set_status(true);
            } else {
                peer.set_status(false);
            }
            peer.set_setup_time(agent_->controller_xmpp_channel_setup_time(count));
            if (agent_->ifmap_active_xmpp_server_index() == count) {
                peer.set_primary(true);
            } else {
                peer.set_primary(false);
            }
            xmpp_list.push_back(peer);
        }
    }

    if (!prev_vrouter_.__isset.xmpp_peer_list ||
        prev_vrouter_.get_xmpp_peer_list() != xmpp_list) {
        vrouter_agent.set_xmpp_peer_list(xmpp_list);
        prev_vrouter_.set_xmpp_peer_list(xmpp_list);
        changed = true;
    }
    ip_list.push_back(rid.to_string());
    if (!prev_vrouter_.__isset.self_ip_list ||
        prev_vrouter_.get_self_ip_list() != ip_list) {

        vrouter_agent.set_self_ip_list(ip_list);
        prev_vrouter_.set_self_ip_list(ip_list);
        changed = true;
    }



    for (int idx = 0; idx < MAX_XMPP_SERVERS; idx++) {
        if (!agent_->dns_server(idx).empty()) {
            dns_list.push_back(agent_->dns_server(idx));
        }
    }

    if (!prev_vrouter_.__isset.dns_servers ||
        prev_vrouter_.get_dns_servers() != dns_list) {
        vrouter_agent.set_dns_servers(dns_list);
        prev_vrouter_.set_dns_servers(dns_list);
        changed = true;
    }

    if (changed) {
        DispatchVrouterMsg(vrouter_agent);
    }
}

std::string VrouterUveEntry::GetMacAddress(const MacAddress &mac) const {
    return mac.ToString();
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
        AgentStatsCollector::InterfaceStats *s = 
              agent_->uve()->agent_stats_collector()->GetInterfaceStats(intf);
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
        AgentStatsCollector::InterfaceStats *s = 
              agent_->uve()->agent_stats_collector()->GetInterfaceStats(intf);
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
        AgentStatsCollector::InterfaceStats *s = 
              agent_->uve()->agent_stats_collector()->GetInterfaceStats(intf);
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
    vr_drop_stats_req stats = agent_->uve()->agent_stats_collector()
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

