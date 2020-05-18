/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/route_common.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/metadata_ip.h>
#include <oper/interface_common.h>
#include <oper/health_check.h>
#include <oper/vrf_assign.h>
#include <oper/vxlan.h>
#include <oper/oper_dhcp_options.h>
#include <oper/physical_device_vn.h>
#include <oper/global_vrouter.h>
#include <oper/qos_config.h>
#include <oper/bridge_domain.h>
#include <oper/sg.h>
#include <oper/bgp_as_service.h>
#include <oper/tag.h>

#include <filter/acl.h>
#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

/////////////////////////////////////////////////////////////////////////////
// VM Port Key routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceKey::VmInterfaceKey(AgentKey::DBSubOperation sub_op,
                   const boost::uuids::uuid &uuid, const std::string &name) :
    InterfaceKey(sub_op, Interface::VM_INTERFACE, uuid, name, false) {
}

Interface *VmInterfaceKey::AllocEntry(const InterfaceTable *table) const {
    Agent *agent = table->agent();
    /* OS oper state is disabled by default in Vmware mode */
    bool os_oper_state = !agent->isVmwareMode();
    return new VmInterface(uuid_, name_, os_oper_state, nil_uuid());
}

Interface *VmInterfaceKey::AllocEntry(const InterfaceTable *table,
                                      const InterfaceData *data) const {
    const VmInterfaceData *vm_data =
        static_cast<const VmInterfaceData *>(data);

    VmInterface *vmi = vm_data->OnAdd(table, this);
    return vmi;
}

InterfaceKey *VmInterfaceKey::Clone() const {
    return new VmInterfaceKey(*this);
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceConfigData routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceConfigData::VmInterfaceConfigData(Agent *agent, IFMapNode *node) :
    VmInterfaceData(agent, node, CONFIG, Interface::TRANSPORT_INVALID),
    addr_(0), ip6_addr_(), vm_mac_(""),
    cfg_name_(""), vm_uuid_(), vm_name_(), vn_uuid_(), vrf_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), bridging_(true),
    layer3_forwarding_(true), mirror_enable_(false), ecmp_(false),
    ecmp6_(false), dhcp_enable_(true),
    proxy_arp_mode_(VmInterface::PROXY_ARP_NONE), admin_state_(true),
    disable_policy_(false), analyzer_name_(""),
    local_preference_(0), oper_dhcp_options_(),
    mirror_direction_(Interface::UNKNOWN),
    cfg_igmp_enable_(false), igmp_enabled_(false), max_flows_(0), sg_list_(),
    floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    instance_ipv4_list_(true), instance_ipv6_list_(false),
    bridge_domain_list_(),
    device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    physical_interface_(""), parent_vmi_(), subnet_(0), subnet_plen_(0),
    rx_vlan_id_(VmInterface::kInvalidVlanId),
    tx_vlan_id_(VmInterface::kInvalidVlanId),
    logical_interface_(nil_uuid()), ecmp_load_balance_(),
    service_health_check_ip_(), service_ip_(0),
    service_ip_ecmp_(false), service_ip6_(), service_ip_ecmp6_(false),
    qos_config_uuid_(), learning_enabled_(false),
    vhostuser_mode_(VmInterface::vHostUserClient), is_left_si_(false), service_mode_(VmInterface::SERVICE_MODE_ERROR),
    si_other_end_vmi_(nil_uuid()), vmi_cfg_uuid_(nil_uuid()),
    service_intf_type_("") {
}

VmInterface *VmInterfaceConfigData::OnAdd(const InterfaceTable *table,
                                          const VmInterfaceKey *key) const {

    Interface *parent = NULL;
    if (device_type_ == VmInterface::REMOTE_VM_VLAN_ON_VMI) {
        if (rx_vlan_id_ == VmInterface::kInvalidVlanId ||
            tx_vlan_id_ == VmInterface::kInvalidVlanId)
            return NULL;

        if (physical_interface_ != Agent::NullString()) {
            PhysicalInterfaceKey key_1(physical_interface_);
            parent = static_cast<Interface *>
                (table->agent()->interface_table()->FindActiveEntry(&key_1));
        }

        if (parent == NULL)
            return NULL;
    }

    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString(vm_mac_, &ec);
    if (ec.value() != 0) {
        mac.Zero();
    }

    Agent *agent = table->agent();
    /* OS oper state is disabled by default in Vmware mode */
    bool os_oper_state = !agent->isVmwareMode();
    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, addr_, mac, vm_name_,
                        nil_uuid(), tx_vlan_id_, rx_vlan_id_, parent,
                        ip6_addr_, device_type_, vmi_type_, vhostuser_mode_,
                        os_oper_state, logical_router_uuid_);
    vmi->SetConfigurer(VmInterface::CONFIG);
    vmi->set_hbs_intf_type(hbs_intf_type_);
    return vmi;
}

bool VmInterfaceConfigData::OnDelete(const InterfaceTable *table,
                                     VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::CONFIG) == false)
        return true;

    VmInterfaceConfigData data(NULL, NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::CONFIG);
    return true;
}

bool VmInterfaceConfigData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi,
                                     bool *force_update) const {
    bool sg_changed = false;
    bool ecmp_changed = false;
    bool local_pref_changed = false;
    bool ecmp_load_balance_changed = false;
    bool static_route_config_changed = false;
    bool etree_leaf_mode_changed = false;
    bool tag_changed = false;
    bool ret = false;

    ret = vmi->CopyConfig(table, this, &sg_changed, &ecmp_changed,
                          &local_pref_changed, &ecmp_load_balance_changed,
                          &static_route_config_changed,
                          &etree_leaf_mode_changed, &tag_changed);
    if (sg_changed || ecmp_changed || local_pref_changed ||
        ecmp_load_balance_changed || static_route_config_changed
        || etree_leaf_mode_changed || tag_changed )
        *force_update = true;

    vmi->SetConfigurer(VmInterface::CONFIG);
    return ret;
}

autogen::VirtualMachineInterface *VmInterfaceConfigData::GetVmiCfg() const {
    IFMapNode *node = ifmap_node();
    if (node != NULL) {
        if (node->GetObject() != NULL) {
            return static_cast <autogen::VirtualMachineInterface *>
                        (node->GetObject());
        }
    }
    return NULL;
}

//Configuration of vhost interface to be filled from
//config file. This include
//1> IP of vhost
//2> Default route to be populated if any
//3> Resolve route to be added
//4> Multicast receive route
void VmInterfaceConfigData::CopyVhostData(const Agent *agent) {
    transport_ = static_cast<Interface::Transport>
        (agent->GetInterfaceTransport());

    proxy_arp_mode_ = VmInterface::PROXY_ARP_NONE;
    device_type_ = VmInterface::LOCAL_DEVICE;
    vmi_type_ = VmInterface::VHOST;

    vrf_name_ = agent->fabric_policy_vrf_name();
    if (agent->router_id() != Ip4Address(0)) {
        addr_ = agent->router_id();
        instance_ipv4_list_.list_.insert(
            VmInterface::InstanceIp(agent->router_id(), 32, false, true,
                                    false, false, false, Ip4Address(0)));
    }

    boost::system::error_code ec;
    IpAddress mc_addr =
        Ip4Address::from_string(IPV4_MULTICAST_BASE_ADDRESS, ec);
    receive_route_list_.list_.insert(
            VmInterface::VmiReceiveRoute(mc_addr, MULTICAST_BASE_ADDRESS_PLEN,
                                         false));

    mc_addr = Ip6Address::from_string(IPV6_MULTICAST_BASE_ADDRESS, ec);
    receive_route_list_.list_.insert(
            VmInterface::VmiReceiveRoute(mc_addr, MULTICAST_BASE_ADDRESS_PLEN,
                                         false));

    if (agent->params()->subnet_hosts_resolvable() == true) {
        //Add resolve route
        subnet_ = agent->router_id();
        subnet_plen_ = agent->vhost_prefix_len();
    }

    physical_interface_ = agent->fabric_interface_name();

    PhysicalInterfaceKey physical_key(agent->fabric_interface_name());
    const Interface *pif =
        static_cast<const Interface *>(agent->interface_table()->
                                           FindActiveEntry(&physical_key));
    vm_mac_ = pif->mac().ToString();

    //Add default route pointing to gateway
    static_route_list_.list_.insert(
            VmInterface::StaticRoute(Ip4Address(0), 0,
                                     agent->params()->vhost_gw(),
                                     CommunityList()));
}


bool VmInterface::CopyIp6Address(const Ip6Address &addr) {
    bool ret = false;

    // Retain the old if new IP could not be got
    if (addr.is_unspecified()) {
        return false;
    }

    if (primary_ip6_addr_ != addr) {
        primary_ip6_addr_ = addr;
        ret = true;
    }

    return ret;
}

// Copies configuration from DB-Request data. The actual applying of
// configuration, like adding/deleting routes must be done with ApplyConfig()
bool VmInterface::CopyConfig(const InterfaceTable *table,
                             const VmInterfaceConfigData *data,
                             bool *sg_changed,
                             bool *ecmp_changed,
                             bool *local_pref_changed,
                             bool *ecmp_load_balance_changed,
                             bool *static_route_config_changed,
                             bool *etree_leaf_mode_changed,
                             bool *tag_changed) {
    bool ret = false;
    if (table) {
        VmEntry *vm = table->FindVmRef(data->vm_uuid_);
        if (vm_.get() != vm) {
            vm_ = vm;
            ret = true;
        }

        bool drop_new_flows =
            (vm_.get() != NULL) ? vm_->drop_new_flows() : false;
        if (drop_new_flows_ != drop_new_flows) {
            drop_new_flows_ = drop_new_flows;
            ret = true;
        }

        VrfEntry *vrf = table->FindVrfRef(data->vrf_name_);
        if (vrf_.get() != vrf) {
            vrf_ = vrf;
            ret = true;
        }

        VrfEntry *forwarding_vrf = vrf;
        if (vrf && vrf_->forwarding_vrf()) {
            forwarding_vrf = vrf_->forwarding_vrf();
        }

        if (forwarding_vrf != forwarding_vrf_) {
            forwarding_vrf_ = forwarding_vrf;
            ret = true;
        }

        MirrorEntry *mirror = table->FindMirrorRef(data->analyzer_name_);
        if (mirror_entry_.get() != mirror) {
            mirror_entry_ = mirror;
            ret = true;
        }
    }

    if (vmi_type_ != data->vmi_type_) {
        *etree_leaf_mode_changed = true;
        vmi_type_ = data->vmi_type_;
        ret = true;
    }

    if (vmi_cfg_uuid_ != data->vmi_cfg_uuid_) {
        vmi_cfg_uuid_ = data->vmi_cfg_uuid_;
        ret = true;
    }

    if (vhostuser_mode_ != data->vhostuser_mode_) {
        vhostuser_mode_ = data->vhostuser_mode_;
        ret = true;
    }

    MirrorDirection mirror_direction = data->mirror_direction_;
    if (mirror_direction_ != mirror_direction) {
        mirror_direction_ = mirror_direction;
        ret = true;
    }

    string cfg_name = data->cfg_name_;
    if (cfg_name_ != cfg_name) {
        cfg_name_ = cfg_name;
        ret = true;
    }

    // Read ifindex for the interface
    if (table) {
        VnEntry *vn = table->FindVnRef(data->vn_uuid_);
        if (vn_.get() != vn) {
            vn_ = vn;
            ret = true;
        }

        bool val = vn ? vn->layer3_forwarding() : false;
        if (vmi_type_ == VHOST) {
            val = true;
        }

        if (layer3_forwarding_ != val) {
            layer3_forwarding_ = val;
            ret = true;
        }

        val = vn ? vn->bridging() : false;
        if (vmi_type_ == VHOST) {
            //Bridging not supported on VHOST vmi
            val = false;
        }

        if (bridging_ != val) {
            bridging_ = val;
            ret = true;
        }

        int vxlan_id = vn ? vn->GetVxLanId() : 0;
        if (vxlan_id_ != vxlan_id) {
            vxlan_id_ = vxlan_id;
            ret = true;
        }

        bool is_etree_leaf = false;
        if (vn) {
            is_etree_leaf = vn->pbb_etree_enable();
        }

        if (etree_leaf_ != is_etree_leaf) {
            etree_leaf_ = is_etree_leaf;
            *etree_leaf_mode_changed = true;
            ret = true;
        }

        bool flood_unknown_unicast =
            vn ? vn->flood_unknown_unicast(): false;
        if (flood_unknown_unicast_ != flood_unknown_unicast) {
            flood_unknown_unicast_ = flood_unknown_unicast;
            ret = true;
        }

        bool layer2_control_word = false;
        if (vn) {
            layer2_control_word = vn->layer2_control_word();
        }
        if (layer2_control_word_ != layer2_control_word) {
            layer2_control_word_ = layer2_control_word;
            *etree_leaf_mode_changed = true;
            ret = true;
        }

        //  global_qos_config is applicable for vmi of VHOST type.
        //  Skip checking for qos for vmi of VHOST type, since cfg_qos_table
        //  is not applicable.
        if (vmi_type_ != VHOST) {
            AgentQosConfigTable *qos_table = table->agent()->qos_config_table();
            AgentQosConfigKey qos_key(data->qos_config_uuid_);
            const AgentQosConfig *qos_config =  static_cast<AgentQosConfig *>
                (qos_table->FindActiveEntry(&qos_key));
            bool is_vn_qos_config = false;

            if (qos_config == NULL) {
                if (vn && vn->qos_config()) {
                    qos_config = vn->qos_config();
                    is_vn_qos_config = true;
                }
            }

            if (qos_config_ != qos_config) {
                qos_config_ = qos_config;
                ret = true;
            }

            if (is_vn_qos_config != is_vn_qos_config_) {
                is_vn_qos_config_ = is_vn_qos_config;
                ret = true;
            }
        }

        if (max_flows_ != data->max_flows_) {
            if(data->max_flows_ != 0) {
                max_flows_ = data->max_flows_;
            } else {
                uint32_t max_flow = 0;
                if (vn) {
                    max_flow = vn->vn_max_flows();
                }
                max_flows_ = max_flow;
            }
            ret = true;
        }

    }

    if (local_preference_ != data->local_preference_) {
        local_preference_ = data->local_preference_;
        *local_pref_changed = true;
        ret = true;
    }

    if (need_linklocal_ip_ != data->need_linklocal_ip_) {
        need_linklocal_ip_ = data->need_linklocal_ip_;
        ret = true;
    }

    // CopyIpAddress uses fabric_port_. So, set it before CopyIpAddresss
    if (fabric_port_ != data->fabric_port_) {
        fabric_port_ = data->fabric_port_;
        ret = true;
    }

    if (service_health_check_ip_ != data->service_health_check_ip_) {
        service_health_check_ip_ = data->service_health_check_ip_;
        ret = true;
    }

    //If nova gives a instance-ip then retain that
    //ip address as primary ip address
    //Else choose the ip address to be old
    //primary ip address as long as its present in
    //new configuration also
    Ip4Address ipaddr = data->addr_;
    if (nova_ip_addr_ != Ip4Address(0)) {
        ipaddr = nova_ip_addr_;
    }
    if (CopyIpAddress(ipaddr)) {
        ret = true;
    }

    Ip6Address ip6_addr = data->ip6_addr_;
    if (nova_ip6_addr_ != Ip6Address()) {
        ip6_addr = nova_ip6_addr_;
    }

    if (CopyIp6Address(ip6_addr)) {
        ret = true;
    }

    if (dhcp_enable_ != data->dhcp_enable_) {
        dhcp_enable_ = data->dhcp_enable_;
        ret = true;
    }

    if (cfg_igmp_enable_ != data->cfg_igmp_enable_) {
        cfg_igmp_enable_ = data->cfg_igmp_enable_;
        ret = true;
    }

    if (igmp_enabled_ != data->igmp_enabled_) {
        igmp_enabled_ = data->igmp_enabled_;
        ret = true;
    }

    if (proxy_arp_mode_ != data->proxy_arp_mode_) {
        proxy_arp_mode_ = data->proxy_arp_mode_;
        ret = true;
    }

    if (disable_policy_ != data->disable_policy_) {
        disable_policy_ = data->disable_policy_;
        ret = true;
    }

    if (is_left_si_ != data->is_left_si_) {
        is_left_si_ = data->is_left_si_;
        ret = true;
    }

    if (service_mode_ != data->service_mode_) {
        service_mode_ = data->service_mode_;
        ret = true;
    }

    if (si_other_end_vmi_ != data->si_other_end_vmi_) {
        si_other_end_vmi_ = data->si_other_end_vmi_;
        ret = true;
    }

    // Update MAC address if not set already. We dont allow modification
    // of mac-address
    bool mac_set = true;
    if (vm_mac_ == MacAddress::kZeroMac) {
        mac_set = false;
    }
    if (mac_set_ != mac_set) {
        mac_set_ = mac_set;
    }

    if (mac_set_ == false) {
        boost::system::error_code ec;
        mac_set_ = true;
        vm_mac_ = MacAddress::FromString(data->vm_mac_, &ec);
        if (ec.value() != 0) {
            vm_mac_ = MacAddress();
            mac_set_ = false;
        }

        if (vmi_type_ == VHOST) {
            vm_mac_ = GetVifMac(table->agent());
            if (vm_mac_ != MacAddress()) {
                mac_set_ = true;
            }
        }
        ret = true;
    }

    if (admin_state_ != data->admin_state_) {
        admin_state_ = data->admin_state_;
        ret = true;
    }

    if (subnet_ != data->subnet_ || subnet_plen_ != data->subnet_plen_) {
        subnet_ = data->subnet_;
        subnet_plen_ = data->subnet_plen_;
    }

    if (learning_enabled_ != data->learning_enabled_) {
        learning_enabled_ = data->learning_enabled_;
        *etree_leaf_mode_changed = true;
        ret = true;
    }

    if (service_intf_type_ != data->service_intf_type_) {
        service_intf_type_ = data->service_intf_type_;
        ret = true;
    }
    // Copy DHCP options; ret is not modified as there is no dependent action
    oper_dhcp_options_ = data->oper_dhcp_options_;

    // Audit operational and config floating-ip list
    FloatingIpSet &old_fip_list = floating_ip_list_.list_;
    const FloatingIpSet &new_fip_list = data->floating_ip_list_.list_;
    if (AuditList<FloatingIpList, FloatingIpSet::iterator>
        (floating_ip_list_, old_fip_list.begin(), old_fip_list.end(),
         new_fip_list.begin(), new_fip_list.end())) {
        ret = true;
        assert(floating_ip_list_.list_.size() ==
               (floating_ip_list_.v4_count_ + floating_ip_list_.v6_count_));
    }

    // Audit operational and config alias-ip list
    AliasIpSet &old_aip_list = alias_ip_list_.list_;
    const AliasIpSet &new_aip_list = data->alias_ip_list_.list_;
    if (AuditList<AliasIpList, AliasIpSet::iterator>
        (alias_ip_list_, old_aip_list.begin(), old_aip_list.end(),
         new_aip_list.begin(), new_aip_list.end())) {
        ret = true;
        assert(alias_ip_list_.list_.size() ==
               (alias_ip_list_.v4_count_ + alias_ip_list_.v6_count_));
    }

    // Audit operational and config Service VLAN list
    ServiceVlanSet &old_service_list = service_vlan_list_.list_;
    const ServiceVlanSet &new_service_list = data->service_vlan_list_.list_;
    if (AuditList<ServiceVlanList, ServiceVlanSet::iterator>
        (service_vlan_list_, old_service_list.begin(), old_service_list.end(),
         new_service_list.begin(), new_service_list.end())) {
        ret = true;
    }

    // Audit operational and config Static Route list
    StaticRouteSet &old_route_list = static_route_list_.list_;
    const StaticRouteSet &new_route_list = data->static_route_list_.list_;
    if (AuditList<StaticRouteList, StaticRouteSet::iterator>
        (static_route_list_, old_route_list.begin(), old_route_list.end(),
         new_route_list.begin(), new_route_list.end())) {
        *static_route_config_changed = true;
        ret = true;
    }

    // Audit operational and config allowed address pair
    AllowedAddressPairSet &old_aap_list = allowed_address_pair_list_.list_;
    const AllowedAddressPairSet &new_aap_list = data->
        allowed_address_pair_list_.list_;
    if (AuditList<AllowedAddressPairList, AllowedAddressPairSet::iterator>
       (allowed_address_pair_list_, old_aap_list.begin(), old_aap_list.end(),
        new_aap_list.begin(), new_aap_list.end())) {
        ret = true;
    }

    // Audit operational and config Security Group list
    SecurityGroupEntrySet &old_sg_list = sg_list_.list_;
    const SecurityGroupEntrySet &new_sg_list = data->sg_list_.list_;
    *sg_changed =
        AuditList<SecurityGroupEntryList, SecurityGroupEntrySet::iterator>
        (sg_list_, old_sg_list.begin(), old_sg_list.end(),
         new_sg_list.begin(), new_sg_list.end());
    if (*sg_changed) {
        ret = true;
    }

    VrfAssignRuleSet &old_vrf_assign_list = vrf_assign_rule_list_.list_;
    const VrfAssignRuleSet &new_vrf_assign_list = data->
        vrf_assign_rule_list_.list_;
    if (AuditList<VrfAssignRuleList, VrfAssignRuleSet::iterator>
        (vrf_assign_rule_list_, old_vrf_assign_list.begin(),
         old_vrf_assign_list.end(), new_vrf_assign_list.begin(),
         new_vrf_assign_list.end())) {
        ret = true;
     }

    FatFlowEntrySet &old_fat_flow_entry_list = fat_flow_list_.list_;
    const FatFlowEntrySet &new_fat_flow_entry_list =
        data->fat_flow_list_.list_;
    if (AuditList<FatFlowList, FatFlowEntrySet::iterator>
        (fat_flow_list_, old_fat_flow_entry_list.begin(),
         old_fat_flow_entry_list.end(), new_fat_flow_entry_list.begin(),
         new_fat_flow_entry_list.end())) {
        ret = true;
    }

    InstanceIpSet &old_ipv4_list = instance_ipv4_list_.list_;
    InstanceIpSet new_ipv4_list = data->instance_ipv4_list_.list_;
    //Native ip of instance should be advertised even if
    //config is not present, so manually add that entry
    if (nova_ip_addr_ != Ip4Address(0) &&
        data->vrf_name_ != Agent::NullString()) {
        new_ipv4_list.insert(
            VmInterface::InstanceIp(nova_ip_addr_, Address::kMaxV4PrefixLen,
                                    data->ecmp_, true, false, false, false,
                                    Ip4Address(0)));
    }
    if (AuditList<InstanceIpList, InstanceIpSet::iterator>
        (instance_ipv4_list_, old_ipv4_list.begin(), old_ipv4_list.end(),
         new_ipv4_list.begin(), new_ipv4_list.end())) {
        ret = true;
    }

    InstanceIpSet &old_ipv6_list = instance_ipv6_list_.list_;
    InstanceIpSet new_ipv6_list = data->instance_ipv6_list_.list_;
    if (nova_ip6_addr_ != Ip6Address() &&
            data->vrf_name_ != Agent::NullString()) {
        new_ipv6_list.insert(
            VmInterface::InstanceIp(nova_ip6_addr_, Address::kMaxV6PrefixLen,
                                    data->ecmp6_, true, false, false, false,
                                    Ip4Address(0)));
    }

    if (AuditList<InstanceIpList, InstanceIpSet::iterator>
        (instance_ipv6_list_, old_ipv6_list.begin(), old_ipv6_list.end(),
         new_ipv6_list.begin(), new_ipv6_list.end())) {
        ret = true;
    }

    BridgeDomainEntrySet &old_bd_list = bridge_domain_list_.list_;
    const BridgeDomainEntrySet &new_bd_list = data->bridge_domain_list_.list_;
    if (AuditList<BridgeDomainList, BridgeDomainEntrySet::iterator>
            (bridge_domain_list_, old_bd_list.begin(), old_bd_list.end(),
             new_bd_list.begin(), new_bd_list.end())) {
        ret = true;
    }


    TagEntrySet &old_tag_list = tag_list_.list_;
    const TagEntrySet &new_tag_list = data->tag_list_.list_;
    *tag_changed = AuditList<TagEntryList, TagEntrySet::iterator>(tag_list_,
                                                           old_tag_list.begin(),
                                                           old_tag_list.end(),
                                                           new_tag_list.begin(),
                                                           new_tag_list.end());
    if (*tag_changed) {
        ret = true;
    }

    VmiReceiveRouteSet &old_recv_list = receive_route_list_.list_;
    const VmiReceiveRouteSet &new_recv_list = data->receive_route_list_.list_;
    *tag_changed = AuditList<VmiReceiveRouteList,
                             VmiReceiveRouteSet::iterator>(receive_route_list_,
                                                           old_recv_list.begin(),
                                                           old_recv_list.end(),
                                                           new_recv_list.begin(),
                                                           new_recv_list.end());
    if (*tag_changed) {
        ret = true;
    }
    bool pbb_interface = new_bd_list.size() ? true: false;
    if (pbb_interface_ != pbb_interface) {
        pbb_interface_ = pbb_interface;
        *etree_leaf_mode_changed = true;
        ret = true;
    }

    if (data->addr_ != Ip4Address(0) && ecmp_ != data->ecmp_) {
        ecmp_ = data->ecmp_;
        *ecmp_changed = true;
    }

    if (!data->ip6_addr_.is_unspecified() && ecmp6_ != data->ecmp6_) {
        ecmp6_ = data->ecmp6_;
        *ecmp_changed = true;
    }

    if (service_ip_ecmp_ != data->service_ip_ecmp_ ||
        service_ip_ != data->service_ip_) {
        service_ip_ecmp_ = data->service_ip_ecmp_;
        service_ip_ = data->service_ip_;
        *ecmp_changed = true;
        ret = true;
    }

    if (service_ip_ecmp6_ != data->service_ip_ecmp6_ ||
        service_ip6_ != data->service_ip6_) {
        service_ip_ecmp6_ = data->service_ip_ecmp6_;
        service_ip6_ = data->service_ip6_;
        *ecmp_changed = true;
        ret = true;
    }

    if (data->device_type_ !=  VmInterface::DEVICE_TYPE_INVALID &&
        device_type_ != data->device_type_) {
        device_type_= data->device_type_;
        ret = true;
    }

    if (device_type_ == LOCAL_DEVICE || device_type_ == REMOTE_VM_VLAN_ON_VMI ||
        device_type_ == VM_VLAN_ON_VMI) {
        if (rx_vlan_id_ != data->rx_vlan_id_) {
            rx_vlan_id_ = data->rx_vlan_id_;
            ret = true;
        }
        if (tx_vlan_id_ != data->tx_vlan_id_) {
            tx_vlan_id_ = data->tx_vlan_id_;
            ret = true;
        }
    }

    if (hbs_intf_type_ != data->hbs_intf_type_) {
        hbs_intf_type_ = data->hbs_intf_type_;
        ret = true;
    }

    if (logical_interface_ != data->logical_interface_) {
        logical_interface_ = data->logical_interface_;
        ret = true;
    }
    Interface *new_parent = NULL;
    if (data->physical_interface_.empty() == false) {
        PhysicalInterfaceKey key(data->physical_interface_);
        new_parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
    } else if (data->parent_vmi_.is_nil() == false) {
        VmInterfaceKey key(AgentKey::RESYNC, data->parent_vmi_, "");
        new_parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key));
    } else {
        new_parent = parent_.get();
    }

    if (parent_ != new_parent) {
        parent_ = new_parent;
        ret = true;
    }

    if (table) {
        if (os_index() == kInvalidIndex) {
            GetOsParams(table->agent());
            if (os_index() != kInvalidIndex)
                ret = true;
        }
    }

    if (ecmp_load_balance_ != data->ecmp_load_balance_) {
        ecmp_load_balance_.Copy(data->ecmp_load_balance_);
        *ecmp_load_balance_changed = true;
        ret = true;
    }

    if (slo_list_ != data->slo_list_) {
        slo_list_ = data->slo_list_;
    }

    if (logical_router_uuid() != data->logical_router_uuid_) {
        set_logical_router_uuid(data->logical_router_uuid_);
        ret = true;
    }
    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceNovaData routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceNovaData::VmInterfaceNovaData() :
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, Interface::TRANSPORT_INVALID),
    ipv4_addr_(),
    ipv6_addr_(),
    mac_addr_(),
    vm_name_(),
    vm_uuid_(),
    vm_project_uuid_(),
    physical_interface_(),
    tx_vlan_id_(),
    rx_vlan_id_(),
    vhostuser_mode_() {
}

VmInterfaceNovaData::VmInterfaceNovaData(const Ip4Address &ipv4_addr,
                                         const Ip6Address &ipv6_addr,
                                         const std::string &mac_addr,
                                         const std::string vm_name,
                                         boost::uuids::uuid vm_uuid,
                                         boost::uuids::uuid vm_project_uuid,
                                         const std::string &physical_interface,
                                         uint16_t tx_vlan_id,
                                         uint16_t rx_vlan_id,
                                         VmInterface::DeviceType device_type,
                                         VmInterface::VmiType vmi_type,
                                         uint8_t vhostuser_mode,
                                         Interface::Transport transport,
                                         uint8_t link_state) :
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, transport),
    ipv4_addr_(ipv4_addr),
    ipv6_addr_(ipv6_addr),
    mac_addr_(mac_addr),
    vm_name_(vm_name),
    vm_uuid_(vm_uuid),
    vm_project_uuid_(vm_project_uuid),
    physical_interface_(physical_interface),
    tx_vlan_id_(tx_vlan_id),
    rx_vlan_id_(rx_vlan_id),
    device_type_(device_type),
    vmi_type_(vmi_type),
    vhostuser_mode_(vhostuser_mode), link_state_(link_state) {
}

VmInterfaceNovaData::~VmInterfaceNovaData() {
}

VmInterface *VmInterfaceNovaData::OnAdd(const InterfaceTable *table,
                                        const VmInterfaceKey *key) const {
    Interface *parent = NULL;
    if (tx_vlan_id_ != VmInterface::kInvalidVlanId &&
        rx_vlan_id_ != VmInterface::kInvalidVlanId &&
        physical_interface_ != Agent::NullString()) {
        PhysicalInterfaceKey key_1(physical_interface_);
        parent = static_cast<Interface *>
            (table->agent()->interface_table()->FindActiveEntry(&key_1));
        assert(parent != NULL);
    }

    boost::system::error_code ec;
    MacAddress mac = MacAddress::FromString(mac_addr_, &ec);
    if (ec.value() != 0) {
        mac.Zero();
    }

    /* OS oper state is passed by PortIpc module (VmiSubscribeEntry) which
     * invokes NovaAdd in case of vmware */
    bool os_oper_state = link_state_;
    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, ipv4_addr_, mac, vm_name_,
                        vm_project_uuid_, tx_vlan_id_, rx_vlan_id_,
                        parent, ipv6_addr_, device_type_, vmi_type_,
                        vhostuser_mode_, os_oper_state, logical_router_uuid_);
    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);
    vmi->nova_ip_addr_ = ipv4_addr_;
    vmi->nova_ip6_addr_ = ipv6_addr_;

    return vmi;
}

bool VmInterfaceNovaData::OnDelete(const InterfaceTable *table,
                                   VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::INSTANCE_MSG) == false)
        return true;

    VmInterfaceConfigData data(NULL, NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::CONFIG);
    vmi->ResetConfigurer(VmInterface::INSTANCE_MSG);
    table->operdb()->bgp_as_a_service()->DeleteVmInterface(vmi->GetUuid());
    table->agent()->interface_table()->DelPhysicalDeviceVnEntry(vmi->GetUuid());
    return true;
}

bool VmInterfaceNovaData::OnResync(const InterfaceTable *table,
                                   VmInterface *vmi,
                                   bool *force_update) const {
    bool ret = false;

    if (vmi->vm_project_uuid_ != vm_project_uuid_) {
        vmi->vm_project_uuid_ = vm_project_uuid_;
        ret = true;
    }

    if (vmi->tx_vlan_id_ != tx_vlan_id_) {
        vmi->tx_vlan_id_ = tx_vlan_id_;
        ret = true;
    }

    if (vmi->rx_vlan_id_ != rx_vlan_id_) {
        vmi->rx_vlan_id_ = rx_vlan_id_;
        ret = true;
    }

    if (vmi->nova_ip_addr_ != ipv4_addr_) {
        vmi->nova_ip_addr_ = ipv4_addr_;
        ret = true;
    }

    if (vmi->nova_ip6_addr_ != ipv6_addr_) {
        vmi->nova_ip6_addr_ = ipv6_addr_;
        ret = true;
    }

    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);

    return ret;
}

// Add a VM-Interface
void VmInterface::NovaAdd(InterfaceTable *table, const uuid &intf_uuid,
                          const string &os_name, const Ip4Address &addr,
                          const string &mac, const string &vm_name,
                          const uuid &vm_project_uuid, uint16_t tx_vlan_id,
                          uint16_t rx_vlan_id, const std::string &parent,
                          const Ip6Address &ip6,
                          uint8_t vhostuser_mode,
                          Interface::Transport transport,
                          uint8_t link_state) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid,
                                     os_name));

    req.data.reset(new VmInterfaceNovaData(addr, ip6, mac, vm_name,
                                           nil_uuid(), vm_project_uuid, parent,
                                           tx_vlan_id, rx_vlan_id,
                                           VmInterface::VM_ON_TAP,
                                           VmInterface::INSTANCE,
                                           vhostuser_mode,
                                           transport, link_state));
    table->Enqueue(&req);
}

// Delete a VM-Interface
void VmInterface::Delete(InterfaceTable *table, const uuid &intf_uuid,
                         VmInterface::Configurer configurer) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, intf_uuid, ""));

    if (configurer == VmInterface::CONFIG) {
        req.data.reset(new VmInterfaceConfigData(NULL, NULL));
    } else if (configurer == VmInterface::INSTANCE_MSG) {
        req.data.reset(new VmInterfaceNovaData());
    } else {
        assert(0);
    }
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceMirrorData routines
/////////////////////////////////////////////////////////////////////////////
bool VmInterfaceMirrorData::OnResync(const InterfaceTable *table,
                                     VmInterface *vmi,
                                     bool *force_update) const {
    bool ret = false;

    MirrorEntry *mirror_entry = NULL;
    if (mirror_enable_ == true) {
        mirror_entry = table->FindMirrorRef(analyzer_name_);
    }

    if (vmi->mirror_entry_ != mirror_entry) {
        vmi->mirror_entry_ = mirror_entry;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceIpAddressData routines
/////////////////////////////////////////////////////////////////////////////
// Update for VM IP address only
// For interfaces in IP Fabric VRF, we send DHCP requests to external servers
// if config doesnt provide an address. This address is updated here.
bool VmInterfaceIpAddressData::OnResync(const InterfaceTable *table,
                                        VmInterface *vmi,
                                        bool *force_update) const {
    bool ret = false;

    if (vmi->os_index() == VmInterface::kInvalidIndex) {
        vmi->GetOsParams(table->agent());
        if (vmi->os_index() != VmInterface::kInvalidIndex)
            ret = true;
    }

    // Ignore IP address change if L3 Forwarding not enabled
    if (!vmi->layer3_forwarding_) {
        return ret;
    }

    Ip4Address addr = Ip4Address(0);
    if (vmi->CopyIpAddress(addr)) {
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceOsOperStateData routines
/////////////////////////////////////////////////////////////////////////////
// Resync oper-state for the interface
bool VmInterfaceOsOperStateData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    bool ret = false;
    Agent *agent = table->agent();

    uint32_t old_os_index = vmi->os_index();
    bool old_ipv4_active = vmi->ipv4_active_;
    bool old_ipv6_active = vmi->ipv6_active_;

    vmi->GetOsParams(agent);
    /* In DPDK mode (where we have interfaces of type TRANSPORT_PMD), oper_state
     * is updated based on Netlink notification received from vrouter */
    if ((vmi->transport_ == Interface::TRANSPORT_PMD) ||
        vmi->NeedDefaultOsOperStateDisabled(agent)) {
        if (vmi->os_params_.os_oper_state_ != oper_state_) {
            vmi->os_params_.os_oper_state_ = oper_state_;
            ret = true;
        }
    }
    if (vmi->os_index() != old_os_index)
        ret = true;

    vmi->ipv4_active_ = vmi->IsIpv4Active();
    if (vmi->ipv4_active_ != old_ipv4_active)
        ret = true;

    vmi->ipv6_active_ = vmi->IsIpv6Active();
    if (vmi->ipv6_active_ != old_ipv6_active)
        ret = true;
    // Update the Oper data for SubInterfaces if attached to parent interface.
    if (ret == true)
        vmi->UpdateOperStateOfSubIntf(table);

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Global VRouter config update
/////////////////////////////////////////////////////////////////////////////
bool VmInterfaceGlobalVrouterData::OnResync(const InterfaceTable *table,
                                            VmInterface *vmi,
                                            bool *force_update) const {
    bool ret = false;

    if (layer3_forwarding_ != vmi->layer3_forwarding_) {
        vmi->layer3_forwarding_ = layer3_forwarding_;
        *force_update = true;
        ret = true;
    }

    if (bridging_ != vmi->bridging_) {
        vmi->bridging_= bridging_;
        *force_update = true;
        ret = true;
    }

    if (vxlan_id_ != vmi->vxlan_id_)
        ret = true;

    if (vmi->ecmp_load_balance().use_global_vrouter()) {
        *force_update = true;
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// Health Check update
/////////////////////////////////////////////////////////////////////////////
VmInterfaceHealthCheckData::VmInterfaceHealthCheckData() :
    VmInterfaceData(NULL, NULL, HEALTH_CHECK, Interface::TRANSPORT_INVALID) {
}

VmInterfaceHealthCheckData::~VmInterfaceHealthCheckData() {
}

bool VmInterfaceHealthCheckData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    return vmi->UpdateIsHealthCheckActive();
}

VmInterfaceNewFlowDropData::VmInterfaceNewFlowDropData(bool drop_new_flows) :
    VmInterfaceData(NULL, NULL, DROP_NEW_FLOWS, Interface::TRANSPORT_INVALID),
    drop_new_flows_(drop_new_flows) {
}

VmInterfaceNewFlowDropData::~VmInterfaceNewFlowDropData() {
}

bool VmInterfaceNewFlowDropData::OnResync(const InterfaceTable *table,
                                          VmInterface *vmi,
                                          bool *force_update) const {
    if (vmi->drop_new_flows_ != drop_new_flows_) {
        vmi->drop_new_flows_ = drop_new_flows_;
        return true;
    }

    return false;
}

bool VmInterface::UpdateIsHealthCheckActive() {
    bool is_hc_active = true;
    HealthCheckInstanceSet::iterator it = hc_instance_set_.begin();
    while (it != hc_instance_set_.end()) {
        if ((*it)->active() == false) {
            // if any of the health check instance reports not active
            // status mark interface health check status inactive
            is_hc_active = false;
            break;
        }
        it++;
    }

    if (is_hc_active_ != is_hc_active) {
        is_hc_active_ = is_hc_active;
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceIfNameData routines
// The request is used to add/delete interface with only ifname as data
/////////////////////////////////////////////////////////////////////////////
VmInterfaceIfNameData::VmInterfaceIfNameData() :
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, Interface::TRANSPORT_INVALID),
    ifname_() {
}

VmInterfaceIfNameData::VmInterfaceIfNameData
(const std::string &ifname):
    VmInterfaceData(NULL, NULL, INSTANCE_MSG, Interface::TRANSPORT_ETHERNET),
    ifname_(ifname) {
}

VmInterfaceIfNameData::~VmInterfaceIfNameData() {
}

VmInterface *VmInterfaceIfNameData::OnAdd(const InterfaceTable *table,
                                          const VmInterfaceKey *key) const {
    Agent *agent = table->agent();
    /* OS oper state is disabled by default in Vmware mode */
    bool os_oper_state = !agent->isVmwareMode();
    VmInterface *vmi =
        new VmInterface(key->uuid_, key->name_, Ip4Address(), MacAddress(), "",
                        nil_uuid(), VmInterface::kInvalidVlanId,
                        VmInterface::kInvalidVlanId, NULL, Ip6Address(),
                        VmInterface::VM_ON_TAP, VmInterface::INSTANCE,
                        VmInterface::vHostUserClient, os_oper_state,
                        logical_router_uuid_);
    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);
    return vmi;
}

bool VmInterfaceIfNameData::OnDelete(const InterfaceTable *table,
                                     VmInterface *vmi) const {
    if (vmi->IsConfigurerSet(VmInterface::INSTANCE_MSG) == false)
        return true;

    VmInterfaceConfigData data(NULL, NULL);
    vmi->Resync(table, &data);
    vmi->ResetConfigurer(VmInterface::CONFIG);
    vmi->ResetConfigurer(VmInterface::INSTANCE_MSG);
    table->operdb()->bgp_as_a_service()->DeleteVmInterface(vmi->GetUuid());
    table->agent()->interface_table()->DelPhysicalDeviceVnEntry(vmi->GetUuid());
    return true;
}

bool VmInterfaceIfNameData::OnResync(const InterfaceTable *table,
                                   VmInterface *vmi,
                                   bool *force_update) const {
    bool ret = false;
    vmi->SetConfigurer(VmInterface::INSTANCE_MSG);
    return ret;
}

// Utility methods to enqueue add/delete requests
void VmInterface::SetIfNameReq(InterfaceTable *table, const uuid &uuid,
                               const string &ifname) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, uuid,
                                     ifname));

    req.data.reset(new VmInterfaceIfNameData(ifname));
    table->Enqueue(&req);
}

void VmInterface::DeleteIfNameReq(InterfaceTable *table, const uuid &uuid) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, uuid, ""));
    req.data.reset(new VmInterfaceIfNameData());
    table->Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// QoS Config change - used from introspect
/////////////////////////////////////////////////////////////////////////////
void AddVmiQosConfig::HandleRequest() const {
    QosResponse *resp = new QosResponse();
    resp->set_context(context());

    boost::uuids::uuid vmi_uuid = StringToUuid(std::string(get_vmi_uuid()));
    boost::uuids::uuid qos_config_uuid =
        StringToUuid(std::string(get_qos_config_uuid()));
    DBTable *table = Agent::GetInstance()->interface_table();

    DBRequest req;
    req.oper =  DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, vmi_uuid, ""));
    req.data.reset(new InterfaceQosConfigData(NULL, NULL, qos_config_uuid));
    table->Enqueue(&req);
    resp->set_resp("Success");
    resp->Response();
}
