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
#include <uve/agent_uve_base.h>
#include <pkt/agent_stats.h>
#include <base/cpuinfo.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

using namespace std;

VrouterUveEntryBase::VrouterUveEntryBase(Agent *agent)
    : agent_(agent), phy_intf_set_(), vn_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId), prev_vrouter_() {
}

VrouterUveEntryBase::~VrouterUveEntryBase() {
}

void VrouterUveEntryBase::RegisterDBClients() {
    VnTable *vn_table = agent_->vn_table();
    vn_listener_id_ = vn_table->Register
                  (boost::bind(&VrouterUveEntryBase::VnNotify, this, _1, _2));

    VmTable *vm_table = agent_->vm_table();
    vm_listener_id_ = vm_table->Register
        (boost::bind(&VrouterUveEntryBase::VmNotify, this, _1, _2));

    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
               (boost::bind(&VrouterUveEntryBase::InterfaceNotify, this, _1, _2));
}

void VrouterUveEntryBase::Shutdown(void) {
    agent_->interface_table()->Unregister(intf_listener_id_);
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->vn_table()->Unregister(vn_listener_id_);
}

void VrouterUveEntryBase::DispatchVrouterMsg(const VrouterAgent &uve) {
    UveVrouterAgent::Send(uve);
}

void VrouterUveEntryBase::VmWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->host_name());
    vrouter_agent.set_virtual_machine_list(*(list.get()));
    DispatchVrouterMsg(vrouter_agent);
}

bool VrouterUveEntryBase::AppendVm(DBTablePartBase *part, DBEntryBase *entry,
                               StringVectorPtr list) {
    VmEntry *vm = static_cast<VmEntry *>(entry);

    if (!vm->IsDeleted()) {
        std::ostringstream ostr;
        ostr << vm->GetUuid();
        list.get()->push_back(ostr.str());
    }
    return true;
}

void VrouterUveEntryBase::VmNotifyHandler(const VmEntry *vm) {
    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->vm_table(), NULL,
        boost::bind(&VrouterUveEntryBase::AppendVm, this, _1, _2, list),
        boost::bind(&VrouterUveEntryBase::VmWalkDone, this, _1, list));
}

void VrouterUveEntryBase::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
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

void VrouterUveEntryBase::VnWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->host_name());
    vrouter_agent.set_connected_networks(*(list.get()));
    DispatchVrouterMsg(vrouter_agent);
}

bool VrouterUveEntryBase::AppendVn(DBTablePartBase *part, DBEntryBase *entry,
                               StringVectorPtr list) {
    VnEntry *vn = static_cast<VnEntry *>(entry);

    if (!vn->IsDeleted()) {
        list.get()->push_back(vn->GetName());
    }
    return true;
}

void VrouterUveEntryBase::VnNotifyHandler(const VnEntry *vn) {
    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->vn_table(), NULL,
             boost::bind(&VrouterUveEntryBase::AppendVn, this, _1, _2, list),
             boost::bind(&VrouterUveEntryBase::VnWalkDone, this, _1, list));
}

void VrouterUveEntryBase::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
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

void VrouterUveEntryBase::InterfaceWalkDone(DBTableBase *base,
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

bool VrouterUveEntryBase::AppendInterface(DBTablePartBase *part,
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

void VrouterUveEntryBase::InterfaceNotifyHandler(const Interface *intf) {
    StringVectorPtr intf_list(new std::vector<std::string>());
    StringVectorPtr err_if_list(new std::vector<std::string>());
    StringVectorPtr nova_if_list(new std::vector<std::string>());

    DBTableWalker *walker = agent_->db()->GetWalker();
    walker->WalkTable(agent_->interface_table(), NULL,
        boost::bind(&VrouterUveEntryBase::AppendInterface, this, _1, _2, intf_list,
                    err_if_list, nova_if_list),
        boost::bind(&VrouterUveEntryBase::InterfaceWalkDone, this, _1, intf_list,
                    err_if_list, nova_if_list));
}

void VrouterUveEntryBase::InterfaceNotify(DBTablePartBase *partition,
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

void VrouterUveEntryBase::SubnetToStringList
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

void VrouterUveEntryBase::BuildAgentConfig(VrouterAgent &vrouter_agent) {
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


void VrouterUveEntryBase::SendVrouterUve() {
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

string VrouterUveEntryBase::GetMacAddress(const MacAddress &mac) const {
    return mac.ToString();
}
