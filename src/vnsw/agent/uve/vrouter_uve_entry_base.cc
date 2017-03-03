/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <fstream>
#include <sandesh/common/vns_types.h>
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
#include <cmn/agent_stats.h>
#include <base/cpuinfo.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

using namespace std;

VrouterUveEntryBase::VrouterUveEntryBase(Agent *agent)
    : agent_(agent), phy_intf_set_(), prev_stats_(), prev_vrouter_(),
      cpu_stats_count_(0), do_vn_walk_(false), do_vm_walk_(false),
      do_interface_walk_(false),
      vn_walk_id_(DBTableWalker::kInvalidWalkerId),
      vm_walk_id_(DBTableWalker::kInvalidWalkerId),
      interface_walk_id_(DBTableWalker::kInvalidWalkerId),
      vn_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId),
      physical_device_listener_id_(DBTableBase::kInvalidId),
      timer_(TimerManager::CreateTimer(
                 *(agent_->event_manager())->io_service(), "UveDBWalkTimer",
                 TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude), 0)) {
    StartTimer();
}

VrouterUveEntryBase::~VrouterUveEntryBase() {
}

void VrouterUveEntryBase::StartTimer() {
    timer_->Cancel();
    timer_->Start(AgentUveBase::kDefaultInterval,
                  boost::bind(&VrouterUveEntryBase::TimerExpiry, this));
}

bool VrouterUveEntryBase::TimerExpiry() {
    bool restart = Run();
    return restart;
}

bool VrouterUveEntryBase::Run() {
    /* We don't do vn, vm and interface walks simultaneously to avoid creation
     * of multiple threads (caused by start of walks). After all the walks are
     * done we re-start the timer */
    bool walk_started = StartVnWalk();

    /* If VN walk is not started, start VM walk */
    if (!walk_started) {
        walk_started = StartVmWalk();

        /* If neither VN nor VM walks have started, start interface walk */
        if (!walk_started) {
            walk_started = StartInterfaceWalk();

            /* If none of the walks are started, return true to trigger
             * auto restart of timer */
            if (!walk_started) {
                return true;
            }
        }
    }

    return false;
}

void VrouterUveEntryBase::PhysicalDeviceNotify(DBTablePartBase *partition,
                                               DBEntryBase *e) {
    const PhysicalDevice *pr = static_cast<const PhysicalDevice *>(e);
    VrouterPhysicalDeviceState *state = static_cast<VrouterPhysicalDeviceState *>
        (e->GetState(partition->parent(), physical_device_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            e->ClearState(partition->parent(), physical_device_listener_id_);
            delete state;
        }
    } else {
        if (!state) {
            state = new VrouterPhysicalDeviceState();
            e->SetState(partition->parent(), physical_device_listener_id_,
                        state);
            do_interface_walk_ = true;
        } else {
            if (state->master_ != pr->master()) {
                do_interface_walk_ = true;
            }
        }
        state->master_ = pr->master();
    }
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

    PhysicalDeviceTable *pd_table = agent_->physical_device_table();
    physical_device_listener_id_ = pd_table->Register
        (boost::bind(&VrouterUveEntryBase::PhysicalDeviceNotify, this, _1, _2));
}

void VrouterUveEntryBase::Shutdown(void) {
    if (physical_device_listener_id_ != DBTableBase::kInvalidId)
        agent_->physical_device_table()->
            Unregister(physical_device_listener_id_);
    if (intf_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(intf_listener_id_);
    if (vm_listener_id_ != DBTableBase::kInvalidId)
        agent_->vm_table()->Unregister(vm_listener_id_);
    if (vn_listener_id_ != DBTableBase::kInvalidId)
        agent_->vn_table()->Unregister(vn_listener_id_);
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (walker) {
        if (vn_walk_id_ != DBTableWalker::kInvalidWalkerId) {
            walker->WalkCancel(vn_walk_id_);
        }
        if (vm_walk_id_ != DBTableWalker::kInvalidWalkerId) {
            walker->WalkCancel(vm_walk_id_);
        }
        if (interface_walk_id_ != DBTableWalker::kInvalidWalkerId) {
            walker->WalkCancel(interface_walk_id_);
        }
    }
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;
    }
}

void VrouterUveEntryBase::DispatchVrouterMsg(const VrouterAgent &uve) {
    UveVrouterAgent::Send(uve);
}

void VrouterUveEntryBase::VmWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->agent_name());
    vrouter_agent.set_virtual_machine_list(*(list.get()));
    VrouterAgentObjectCount vm_count;
    vm_count.set_active(list.get()->size());
    vrouter_agent.set_vm_count(vm_count);
    DispatchVrouterMsg(vrouter_agent);
    vm_walk_id_ = DBTableWalker::kInvalidWalkerId;

    /* Start Interface Walk after we are done with Vm Walk */
    bool walk_started = StartInterfaceWalk();

    /* If interface walk has not started, restart the timer */
    if (!walk_started) {
        StartTimer();
    }
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

bool VrouterUveEntryBase::StartVmWalk() {
    if (!do_vm_walk_) {
        /* There is no change in VM list. No need of walk */
        return false;
    }
    assert(vm_walk_id_ == DBTableWalker::kInvalidWalkerId);

    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    vm_walk_id_ = walker->WalkTable(agent_->vm_table(), NULL,
        boost::bind(&VrouterUveEntryBase::AppendVm, this, _1, _2, list),
        boost::bind(&VrouterUveEntryBase::VmWalkDone, this, _1, list));
    do_vm_walk_ = false;
    return true;
}

void VrouterUveEntryBase::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            do_vm_walk_ = true;
            e->ClearState(partition->parent(), vm_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vm_listener_id_, state);
        //Send vrouter object only for a add/delete
        do_vm_walk_ = true;
    }
}

void VrouterUveEntryBase::VnWalkDone(DBTableBase *base, StringVectorPtr list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->agent_name());
    vrouter_agent.set_connected_networks(*(list.get()));
    vrouter_agent.set_vn_count((*list).size());
    DispatchVrouterMsg(vrouter_agent);

    //Update prev_vrouter_ fields. Currently used only in UT
    prev_vrouter_.set_connected_networks(*(list.get()));
    prev_vrouter_.set_vn_count((*list).size());
    vn_walk_id_ = DBTableWalker::kInvalidWalkerId;

    /* Start Vm Walk after we are done with Vn Walk */
    bool walk_started = StartVmWalk();

    /* If VM walk has not started, start interface walk */
    if (!walk_started) {
        walk_started = StartInterfaceWalk();

        /* If interface walk has not started, restart the timer */
        if (!walk_started) {
            StartTimer();
        }
    }
}

bool VrouterUveEntryBase::AppendVn(DBTablePartBase *part, DBEntryBase *entry,
                               StringVectorPtr list) {
    VnEntry *vn = static_cast<VnEntry *>(entry);

    if (!vn->IsDeleted()) {
        list.get()->push_back(vn->GetName());
    }
    return true;
}

bool VrouterUveEntryBase::StartVnWalk() {
    if (!do_vn_walk_) {
        /* There is no change in VN list. No need of walk */
        return false;
    }
    assert(vn_walk_id_ == DBTableWalker::kInvalidWalkerId);

    StringVectorPtr list(new vector<string>());
    DBTableWalker *walker = agent_->db()->GetWalker();
    vn_walk_id_ = walker->WalkTable(agent_->vn_table(), NULL,
             boost::bind(&VrouterUveEntryBase::AppendVn, this, _1, _2, list),
             boost::bind(&VrouterUveEntryBase::VnWalkDone, this, _1, list));
    do_vn_walk_ = false;
    return true;
}

void VrouterUveEntryBase::VnNotify(DBTablePartBase *partition, DBEntryBase *e) {
    DBState *state = static_cast<DBState *>
        (e->GetState(partition->parent(), vn_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            do_vn_walk_ = true;
            e->ClearState(partition->parent(), vn_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new DBState();
        e->SetState(partition->parent(), vn_listener_id_, state);
        do_vn_walk_ = true;
    }
}

void VrouterUveEntryBase::InterfaceWalkDone(DBTableBase *base,
                                        StringVectorPtr if_list,
                                        StringVectorPtr err_if_list,
                                        StringVectorPtr nova_if_list,
                                        StringVectorPtr unmanaged_list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->agent_name());
    vrouter_agent.set_interface_list(*(if_list.get()));
    vrouter_agent.set_error_intf_list(*(err_if_list.get()));
    vrouter_agent.set_no_config_intf_list(*(nova_if_list.get()));
    if (agent_->tsn_enabled()) {
        vrouter_agent.set_unmanaged_if_list(*(unmanaged_list.get()));
        prev_vrouter_.set_unmanaged_if_list(*(unmanaged_list.get()));
    }

    VrouterAgentObjectCount vmi_count;
    vmi_count.set_active((if_list.get()->size() + nova_if_list.get()->size()));
    vrouter_agent.set_vmi_count(vmi_count);
    vrouter_agent.set_down_interface_count((err_if_list.get()->size() +
                                            nova_if_list.get()->size()));
    DispatchVrouterMsg(vrouter_agent);
    interface_walk_id_ = DBTableWalker::kInvalidWalkerId;

    //Update prev_vrouter_ fields. This is being used now only for UT
    prev_vrouter_.set_interface_list(*(if_list.get()));
    prev_vrouter_.set_error_intf_list(*(err_if_list.get()));
    prev_vrouter_.set_no_config_intf_list(*(nova_if_list.get()));

    /* Restart the timer after we are done with the walk */
    StartTimer();
}

bool VrouterUveEntryBase::AppendInterface(DBTablePartBase *part,
                                      DBEntryBase *entry,
                                      StringVectorPtr intf_list,
                                      StringVectorPtr err_if_list,
                                      StringVectorPtr nova_if_list,
                                      StringVectorPtr unmanaged_list) {
    Interface *intf = static_cast<Interface *>(entry);

    if (intf->type() == Interface::VM_INTERFACE) {
        const VmInterface *port = static_cast<const VmInterface *>(intf);
        if (!entry->IsDeleted()) {
            if (port->cfg_name() == agent_->NullString()) {
                nova_if_list.get()->push_back(UuidToString(port->GetUuid()));
            } else {
                if (agent_->tsn_enabled()) {
                    /* For TSN nodes send VMI in interface_list if the VMI's
                     * physical device has tsn_enabled set to true. Otherwise
                     * send the VMI in unmanaged_list */
                    PhysicalDevice *pd = VmiToPhysicalDevice(port);
                    if (!pd || !pd->master()) {
                        unmanaged_list.get()->push_back(port->cfg_name());
                        return true;
                    }
                    AppendInterfaceInternal(port, intf_list, err_if_list);
                } else {
                    AppendInterfaceInternal(port, intf_list, err_if_list);
                }
            }
        }
    }
    return true;
}

void VrouterUveEntryBase::AppendInterfaceInternal(const VmInterface *port,
                                                  StringVectorPtr intf_list,
                                                  StringVectorPtr err_if_list) {
    intf_list.get()->push_back(port->cfg_name());
    if (!port->IsUveActive()) {
        err_if_list.get()->push_back(port->cfg_name());
    }
}

PhysicalDevice *VrouterUveEntryBase::VmiToPhysicalDevice
    (const VmInterface *port) {
    const boost::uuids::uuid u = port->logical_interface();
    if (u == nil_uuid()) {
        return NULL;
    }
    LogicalInterface *intf;
    VlanLogicalInterfaceKey key(u, "");
    intf = static_cast<LogicalInterface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    if (!intf || !intf->physical_interface()) {
        return NULL;
    }
    return InterfaceToPhysicalDevice(intf->physical_interface());
}

PhysicalDevice *VrouterUveEntryBase::InterfaceToPhysicalDevice(Interface *intf) {
    PhysicalDevice *pde = NULL;
    const RemotePhysicalInterface *rpintf;
    const PhysicalInterface *pintf;
    if (intf->type() == Interface::REMOTE_PHYSICAL) {
        rpintf = static_cast<const RemotePhysicalInterface *>(intf);
        pde = rpintf->physical_device();
    } else if (intf->type() == Interface::PHYSICAL) {
        pintf = static_cast<const PhysicalInterface *>(intf);
        pde = pintf->physical_device();
    }
    return pde;
}

bool VrouterUveEntryBase::StartInterfaceWalk() {
    if (!do_interface_walk_) {
        /* There is no change in interface list. No need of walk */
        return false;
    }
    assert(interface_walk_id_ == DBTableWalker::kInvalidWalkerId);

    StringVectorPtr intf_list(new std::vector<std::string>());
    StringVectorPtr err_if_list(new std::vector<std::string>());
    StringVectorPtr nova_if_list(new std::vector<std::string>());
    StringVectorPtr unmanaged_list(new std::vector<std::string>());

    DBTableWalker *walker = agent_->db()->GetWalker();
    interface_walk_id_ = walker->WalkTable(agent_->interface_table(), NULL,
        boost::bind(&VrouterUveEntryBase::AppendInterface, this, _1, _2,
                    intf_list, err_if_list, nova_if_list, unmanaged_list),
        boost::bind(&VrouterUveEntryBase::InterfaceWalkDone, this, _1, intf_list
                    , err_if_list, nova_if_list, unmanaged_list));
    do_interface_walk_ = false;
    return true;
}

void VrouterUveEntryBase::InterfaceNotify(DBTablePartBase *partition,
                                          DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    bool set_state = false, reset_state = false;

    VrouterUveInterfaceState *state = static_cast<VrouterUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    bool vmport_active = false;
    const VmInterface *vm_port = NULL;
    switch(intf->type()) {
    case Interface::VM_INTERFACE:
        vm_port = static_cast<const VmInterface*>(intf);
        if (!e->IsDeleted() && !state) {
            set_state = true;
            vmport_active = vm_port->IsUveActive();
            do_interface_walk_ = true;
        } else if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
                do_interface_walk_ = true;
            }
        } else {
            if (state && vm_port->IsUveActive() != state->vmport_active_) {
                do_interface_walk_ = true;
                state->vmport_active_ = vm_port->IsUveActive();
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
            const PhysicalInterface* phy_if =
                static_cast<const PhysicalInterface*>(intf);
            /* Ignore PhysicalInterface notifications if it is not of subtype
             * FABRIC */
            if (phy_if->subtype() != PhysicalInterface::FABRIC) {
                return;
            }
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
        state = new VrouterUveInterfaceState(vmport_active);
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

    vrouter_agent.set_dns_server_list_cfg(param->dns_server_list());
    vrouter_agent.set_control_node_list_cfg(param->controller_server_list());

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
    vrouter_agent.set_headless_mode_cfg(true);
    vrouter_agent.set_collector_server_list_cfg(param->collector_server_list());
}


bool VrouterUveEntryBase::SendVrouterMsg() {
    VrouterAgent vrouter_agent;
    bool changed = false;
    static bool first = true, build_info = false;
    vrouter_agent.set_name(agent_->agent_name());
    Ip4Address rid = agent_->router_id();
    vector<string> ip_list;
    vector<string> dns_list;

    if (first) {
        //Physical interface list
        vnsConstants vnsVrouterType;
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

        //Set the Agent mode
        if (agent_->tor_agent_enabled()) {
            vrouter_agent.set_mode(vnsVrouterType.VrouterAgentTypeMap.at
                (VrouterAgentType::VROUTER_AGENT_TOR));
        } else if (agent_->tsn_enabled()) {
            vrouter_agent.set_mode(vnsVrouterType.VrouterAgentTypeMap.at
                (VrouterAgentType::VROUTER_AGENT_TSN));
        } else {
            vrouter_agent.set_mode(vnsVrouterType.VrouterAgentTypeMap.at
                (VrouterAgentType::VROUTER_AGENT_EMBEDDED));
        }

        if (agent_->vrouter_on_nic_mode()) {
            vrouter_agent.set_platform(vnsVrouterType.
                                       VrouterAgentPlatformTypeMap.at
                                       (VrouterAgentPlatformType::
                                        VROUTER_AGENT_ON_NIC));
        } else if (agent_->vrouter_on_host_dpdk()) {
            vrouter_agent.set_platform(vnsVrouterType.
                                       VrouterAgentPlatformTypeMap.at
                                       (VrouterAgentPlatformType::
                                        VROUTER_AGENT_ON_HOST_DPDK));
        } else if (agent_->vrouter_on_host()) {
            vrouter_agent.set_platform(vnsVrouterType.
                                       VrouterAgentPlatformTypeMap.at
                                       (VrouterAgentPlatformType::
                                        VROUTER_AGENT_ON_HOST));
        }

        VrouterObjectLimits vr_limits = agent_->GetVrouterObjectLimits();
        vrouter_agent.set_vr_limits(vr_limits);
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
    if (prev_vrouter_.get_phy_if() != phy_if_list) {
        vrouter_agent.set_phy_if(phy_if_list);
        prev_vrouter_.set_phy_if(phy_if_list);
        changed = true;
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
    /* Self IP list should be populated only if router_id is  configured */
    if (agent_->router_id_configured()) {
        ip_list.push_back(rid.to_string());
        if (!prev_vrouter_.__isset.self_ip_list ||
                prev_vrouter_.get_self_ip_list() != ip_list) {

            vrouter_agent.set_self_ip_list(ip_list);
            prev_vrouter_.set_self_ip_list(ip_list);
            changed = true;
        }
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

    VrouterStatsAgent stats;
    stats.set_name(agent_->agent_name());
    cpu_stats_count_++;
    /* CPU stats needs to be sent every minute. We are using '% 2' below
     * because timer is fired every 30 secs. If the timer interval is changed
     * we need to fix the '%' value below accordingly */
    if ((cpu_stats_count_ % 2) == 0) {
        static bool cpu_first = true;
        CpuLoadInfo cpu_load_info;
        CpuLoadData::FillCpuInfo(cpu_load_info, true);
        if (prev_stats_.get_cpu_info() != cpu_load_info || cpu_first) {
            stats.set_cpu_info(cpu_load_info);
            prev_stats_.set_cpu_info(cpu_load_info);
            changed = true;
            cpu_first = false;
            DispatchVrouterStatsMsg(stats);
        }

        //Stats oracle interface for cpu and mem stats. Needs to be sent
        //always regardless of whether the stats have changed since last send
        BuildAndSendComputeCpuStateMsg(cpu_load_info);
        cpu_stats_count_ = 0;
    }
    return changed;
}

void VrouterUveEntryBase::SendVrouterProuterAssociation
    (const vector<string> &list) {
    VrouterAgent vrouter_agent;
    vrouter_agent.set_name(agent_->agent_name());
    if (agent_->tor_agent_enabled()) {
        vrouter_agent.set_tor_prouter_list(list);
    } else if (agent_->tsn_enabled()) {
        vrouter_agent.set_tsn_prouter_list(list);
    } else {
        vrouter_agent.set_embedded_prouter_list(list);
    }
    DispatchVrouterMsg(vrouter_agent);
}

string VrouterUveEntryBase::GetMacAddress(const MacAddress &mac) const {
    return mac.ToString();
}

void VrouterUveEntryBase::DispatchVrouterStatsMsg(const VrouterStatsAgent &uve) {
    VrouterStats::Send(uve);
}

void VrouterUveEntryBase::DispatchComputeCpuStateMsg(const ComputeCpuState &ccs) {
    ComputeCpuStateTrace::Send(ccs);
}

void VrouterUveEntryBase::BuildAndSendComputeCpuStateMsg(const CpuLoadInfo &info) {
    ComputeCpuState astate;
    VrouterCpuInfo ainfo;
    vector<VrouterCpuInfo> aciv;

    astate.set_name(agent_->agent_name());
    ainfo.set_cpu_share(info.get_cpu_share());
    ainfo.set_mem_virt(info.get_meminfo().get_virt());
    ainfo.set_mem_res(info.get_meminfo().get_res());
    const SysMemInfo &sys_mem_info(info.get_sys_mem_info());
    ainfo.set_used_sys_mem(sys_mem_info.get_used() -
        sys_mem_info.get_buffers() - sys_mem_info.get_cached());
    ainfo.set_one_min_cpuload(info.get_cpuload().get_one_min_avg());
    aciv.push_back(ainfo);
    astate.set_cpu_info(aciv);
    DispatchComputeCpuStateMsg(astate);
}
