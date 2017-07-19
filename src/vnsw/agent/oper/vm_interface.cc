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
// VM-Interface entry routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::VmInterface(const boost::uuids::uuid &uuid) :
    Interface(Interface::VM_INTERFACE, uuid, "", NULL), vm_(NULL, this),
    vn_(NULL), primary_ip_addr_(0), mdata_ip_(NULL), subnet_bcast_addr_(0),
    primary_ip6_addr_(), vm_mac_(MacAddress::kZeroMac), policy_enabled_(false),
    mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX), cfg_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), drop_new_flows_(false),
    dhcp_enable_(true), do_dhcp_relay_(false), proxy_arp_mode_(PROXY_ARP_NONE),
    vm_name_(), vm_project_uuid_(nil_uuid()), vxlan_id_(0), bridging_(false),
    layer3_forwarding_(true), flood_unknown_unicast_(false),
    mac_set_(false), ecmp_(false), ecmp6_(false), disable_policy_(false),
    tx_vlan_id_(kInvalidVlanId), rx_vlan_id_(kInvalidVlanId), parent_(NULL, this),
    local_preference_(0), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(), fat_flow_list_(),
    vrf_assign_rule_list_(), vrf_assign_acl_(NULL), vm_ip_service_addr_(0),
    device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    configurer_(0), subnet_(0), subnet_plen_(0), ethernet_tag_(0),
    logical_interface_(nil_uuid()), nova_ip_addr_(0), nova_ip6_addr_(),
    dhcp_addr_(0), metadata_ip_map_(), hc_instance_set_(),
    ecmp_load_balance_(), service_health_check_ip_(), is_vn_qos_config_(false),
    learning_enabled_(false), etree_leaf_(false), layer2_control_word_(false) {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
    l3_interface_nh_policy_.reset();
    l2_interface_nh_policy_.reset();
    l3_interface_nh_no_policy_.reset();
    l2_interface_nh_no_policy_.reset();
}

VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         const Ip4Address &addr, const MacAddress &mac,
                         const std::string &vm_name,
                         const boost::uuids::uuid &vm_project_uuid,
                         uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                         Interface *parent, const Ip6Address &a6,
                         DeviceType device_type, VmiType vmi_type) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL), vm_(NULL, this),
    vn_(NULL), primary_ip_addr_(addr), mdata_ip_(NULL), subnet_bcast_addr_(0),
    primary_ip6_addr_(a6), vm_mac_(mac), policy_enabled_(false),
    mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX), cfg_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), drop_new_flows_(false),
    dhcp_enable_(true), do_dhcp_relay_(false), proxy_arp_mode_(PROXY_ARP_NONE),
    vm_name_(vm_name), vm_project_uuid_(vm_project_uuid), vxlan_id_(0),
    bridging_(false), layer3_forwarding_(true),
    flood_unknown_unicast_(false), mac_set_(false),
    ecmp_(false), ecmp6_(false), disable_policy_(false),
    tx_vlan_id_(tx_vlan_id), rx_vlan_id_(rx_vlan_id), parent_(parent, this),
    local_preference_(0), oper_dhcp_options_(),
    sg_list_(), floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(), vrf_assign_rule_list_(),
    vrf_assign_acl_(NULL), device_type_(device_type),
    vmi_type_(vmi_type), configurer_(0), subnet_(0),
    subnet_plen_(0), ethernet_tag_(0), logical_interface_(nil_uuid()),
    nova_ip_addr_(0), nova_ip6_addr_(), dhcp_addr_(0), metadata_ip_map_(),
    hc_instance_set_(), service_health_check_ip_(), is_vn_qos_config_(false),
    learning_enabled_(false), etree_leaf_(false), layer2_control_word_(false) {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
}

VmInterface::~VmInterface() {
    mdata_ip_.reset(NULL);
    assert(metadata_ip_map_.empty());
    assert(hc_instance_set_.empty());
}

void VmInterface::SetConfigurer(VmInterface::Configurer type) {
    configurer_ |= (1 << type);
}

void VmInterface::ResetConfigurer(VmInterface::Configurer type) {
    configurer_ &= ~(1 << type);
}

bool VmInterface::IsConfigurerSet(VmInterface::Configurer type) {
    return ((configurer_ & (1 << type)) != 0);
}

bool VmInterface::CmpInterface(const DBEntry &rhs) const {
    const VmInterface &intf=static_cast<const VmInterface &>(rhs);
    return uuid_ < intf.uuid_;
}

string VmInterface::ToString() const {
    return "VM-PORT <" + name() + ">";
}

DBEntryBase::KeyPtr VmInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, uuid_,
                                           name_);
    return DBEntryBase::KeyPtr(key);
}

void VmInterface::Add() {
    peer_.reset(new LocalVmPortPeer(LOCAL_VM_PORT_PEER_NAME, id_));
}

bool VmInterface::Delete(const DBRequest *req) {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    const VmInterfaceData *vm_data = static_cast<const VmInterfaceData *>
        (req->data.get());
    vm_data->OnDelete(table, this);
    if (configurer_) {
        return false;
    }
    table->DeleteDhcpSnoopEntry(name_);
    return true;
}

// When VMInterface is added from Config (sub-interface, gateway interface etc.)
// the RESYNC is not called and some of the config like VN and VRF are not
// applied on the interface (See Add() API above). Force change to ensure
// RESYNC is called
void VmInterface::PostAdd() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    IFMapNode *node = ifmap_node();
    if (node == NULL) {
        PortSubscribeTable *subscribe_table =
            table->agent()->port_ipc_handler()->port_subscribe_table();
        if (subscribe_table)
            node = subscribe_table->UuidToIFNode(GetUuid());
        if (node == NULL)
            return;
    }

    // Config notification would have been ignored till Nova message is
    // received. Update config now
    IFMapAgentTable *ifmap_table =
        static_cast<IFMapAgentTable *>(node->table());
    DBRequest req(DBRequest::DB_ENTRY_NOTIFY);
    req.key = node->GetDBRequestKey();
    IFMapTable::RequestKey *key =
        dynamic_cast<IFMapTable::RequestKey *>(req.key.get());
    key->id_type = "virtual-machine-interface";
    ifmap_table->Enqueue(&req);
}

bool VmInterface::OnChange(VmInterfaceData *data) {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    return Resync(table, data);
}

// Handle RESYNC DB Request. Handles multiple sub-types,
// - CONFIG : RESYNC from config message
// - IP_ADDR: RESYNC due to learning IP from DHCP
// - MIRROR : RESYNC due to change in mirror config
bool VmInterface::Resync(const InterfaceTable *table,
                         const VmInterfaceData *data) {
    bool ret = false;

    // Copy old values used to update config below
    bool old_ipv4_active = ipv4_active_;
    bool old_ipv6_active = ipv6_active_;
    bool old_l2_active = l2_active_;
    bool old_policy = policy_enabled_;
    VrfEntryRef old_vrf = vrf_;
    Ip4Address old_addr = primary_ip_addr_;
    Ip6Address old_v6_addr = primary_ip6_addr_;
    bool old_need_linklocal_ip = need_linklocal_ip_;
    bool force_update = false;
    Ip4Address old_subnet = subnet_;
    uint8_t  old_subnet_plen = subnet_plen_;
    int old_ethernet_tag = ethernet_tag_;
    bool old_dhcp_enable = dhcp_enable_;
    bool old_layer3_forwarding = layer3_forwarding_;
    Ip4Address old_dhcp_addr = dhcp_addr_;
    bool old_metadata_ip_active = metadata_ip_active_;
    bool old_metadata_l2_active = metadata_l2_active_;
    bool old_bridging = bridging_;

    if (data) {
        ret = data->OnResync(table, this, &force_update);
    }

    metadata_ip_active_ = IsMetaDataIPActive();
    metadata_l2_active_ = IsMetaDataL2Active();
    ipv4_active_ = IsIpv4Active();
    ipv6_active_ = IsIpv6Active();
    l2_active_ = IsL2Active();

    if (metadata_ip_active_ != old_metadata_ip_active) {
        ret = true;
    }

    if (metadata_l2_active_ != old_metadata_l2_active) {
        ret = true;
    }

    if (ipv4_active_ != old_ipv4_active) {
        InterfaceTable *intf_table = static_cast<InterfaceTable *>(get_table());
        if (ipv4_active_)
            intf_table->incr_active_vmi_count();
        else
            intf_table->decr_active_vmi_count();
        ret = true;
    }

    if (ipv6_active_ != old_ipv6_active) {
        ret = true;
    }

    if (l2_active_ != old_l2_active) {
        ret = true;
    }

    policy_enabled_ = PolicyEnabled();
    if (policy_enabled_ != old_policy) {
        ret = true;
    }

    // Apply config based on old and new values
    ApplyConfig(old_ipv4_active, old_l2_active, old_policy, old_vrf.get(), 
                old_addr, old_ethernet_tag, old_need_linklocal_ip,
                old_ipv6_active, old_v6_addr, old_subnet, old_subnet_plen,
                old_dhcp_enable, old_layer3_forwarding, force_update,
                old_dhcp_addr, old_metadata_ip_active, old_bridging);

    return ret;
}

// Apply the latest configuration
void VmInterface::ApplyConfig(bool old_ipv4_active, bool old_l2_active,
                              bool old_policy, VrfEntry *old_vrf,
                              const Ip4Address &old_addr,
                              int old_ethernet_tag, bool old_need_linklocal_ip,
                              bool old_ipv6_active,
                              const Ip6Address &old_v6_addr,
                              const Ip4Address &old_subnet,
                              uint8_t old_subnet_plen,
                              bool old_dhcp_enable,
                              bool old_layer3_forwarding,
                              bool force_update,
                              const Ip4Address &old_dhcp_addr,
                              bool old_metadata_ip_active,
                              bool old_bridging) {

    //For SRIOV we dont generate any things lile l2 routes, l3 routes
    //etc
    if (device_type_ == VmInterface::VM_SRIOV) {
        return;
    }

    // Appy common changes first
    //DHCP MAC IP binding
    ApplyMacVmBindingConfig(old_vrf, old_l2_active,  old_dhcp_enable);
    //Security Group update
    if (IsActive()) {
        UpdateSecurityGroup();
        UpdateFatFlow();
        UpdateBridgeDomain();
    } else {
        DeleteSecurityGroup();
        DeleteFatFlow();
        DeleteBridgeDomain();
    }

    //Need not apply config for TOR VMI as it is more of an inidicative
    //interface. No route addition or NH addition happens for this interface.
    //Also, when parent is not updated for a non-Nova interface, device type
    //remains invalid.
    if ((device_type_ == VmInterface::TOR ||
         device_type_ == VmInterface::DEVICE_TYPE_INVALID) &&
        (old_subnet.is_unspecified() && old_subnet_plen == 0)) {
        return;
    }

    bool policy_change = (policy_enabled_ != old_policy);

    if (vrf_ && vmi_type() == GATEWAY) {
        vrf_->CreateTableLabel(false, false, false, false);
    }

    //Update common prameters
    if (IsActive()) {
        UpdateCommonNextHop(force_update);
    }
    // Add/Update L3 Metadata
    if (metadata_ip_active_) {
        UpdateL3MetadataIp(old_vrf, force_update, policy_change,
                           old_metadata_ip_active);
    }

    // Add/Del/Update L3 
    if ((ipv4_active_ || ipv6_active_) && layer3_forwarding_) {
        UpdateL3(old_ipv4_active, old_vrf, old_addr, old_ethernet_tag,
                 force_update, policy_change, old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen, old_dhcp_addr);
    } else {
        DeleteL3(old_ipv4_active, old_vrf, old_addr, old_need_linklocal_ip,
                 old_ipv6_active, old_v6_addr, old_subnet, old_subnet_plen,
                 old_ethernet_tag, old_dhcp_addr,
                 (force_update || policy_change));
    }

    // Del L3 Metadata after deleting L3
    if (!metadata_ip_active_ && old_metadata_ip_active) {
        DeleteL3MetadataIp(old_vrf, force_update, policy_change,
                           old_metadata_ip_active, old_need_linklocal_ip);
    }

    // Add/Update L2 
    if (l2_active_) {
        UpdateL2(old_l2_active, policy_change);
    }

    if (InstallBridgeRoutes()) {
        UpdateBridgeRoutes(old_l2_active, old_vrf, old_ethernet_tag,
                           force_update, policy_change, old_addr, old_v6_addr,
                           old_layer3_forwarding);
    } else {
        DeleteBridgeRoutes(old_l2_active, old_vrf, old_ethernet_tag,
                           old_addr, old_v6_addr, old_layer3_forwarding,
                           (force_update || policy_change));
    }

    // Delete L2
    if (!l2_active_ && old_l2_active) {
        DeleteL2(old_l2_active);
    }

    UpdateFlowKeyNextHop();

    // Remove floating-ip entries marked for deletion
    CleanupFloatingIpList();

    // Remove Alias-ip entries marked for deletion
    CleanupAliasIpList();

    if (!IsActive()) {
        DeleteCommonNextHop();
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    if (old_l2_active != l2_active_) {
        if (l2_active_) {
            SendTrace(table, ACTIVATED_L2);
        } else {
            SendTrace(table, DEACTIVATED_L2);
        }
    }

    if (old_ipv4_active != ipv4_active_) {
        if (ipv4_active_) {
            SendTrace(table, ACTIVATED_IPV4);
        } else {
            SendTrace(table, DEACTIVATED_IPV4);
        }
    }

    if (old_ipv6_active != ipv6_active_) {
        if (ipv6_active_) {
            SendTrace(table, ACTIVATED_IPV6);
        } else {
            SendTrace(table, DEACTIVATED_IPV6);
        }
    }
}

/*
 * L2 nexthops:
 * These L2 nexthop are used by multicast and bridge.
 * Presence of multicast forces it to be present in
 * ipv4 mode(l3-only).
 *
 * L3 nexthops:
 * Also creates L3 interface NH, if layer3_forwarding is set.
 * It does not depend on oper state of ip forwarding.
 * Needed as health check can disable oper ip_active and will result in flow key
 * pointing to L2 interface NH. This has to be avoided as interface still points
 * to l3 nh and flow should use same. For this fix it is also required that l3
 * i/f nh is also created on seeing config and not oper state of l3. Reason
 * being if vmi(irrespective of health check) is coming up and transitioning
 * from ipv4_inactive to ip4_active and during this transition a flow is added
 * then flow_key in vmi will return null because l3 config is set and interface
 * nh not created yet.
 */
void VmInterface::UpdateCommonNextHop(bool force_update) {
    UpdateL2NextHop(force_update);
    UpdateL3NextHop();
}

void VmInterface::DeleteCommonNextHop() {
    DeleteL2NextHop();
    DeleteL3NextHop();
}

void VmInterface::UpdateL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                                     bool policy_change,
                                     bool old_metadata_ip_active) {
    assert(metadata_ip_active_);
    UpdateMetadataRoute(old_metadata_ip_active, old_vrf);
}

// Delete meta-data route
void VmInterface::DeleteMetadataRoute(bool old_active, VrfEntry *old_vrf,
                                      bool old_need_linklocal_ip) {
    if (!old_need_linklocal_ip) {
        return;
    }
    if (mdata_ip_.get() != NULL) {
        mdata_ip_->set_active(false);
    }
}

void VmInterface::DeleteL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                                     bool policy_change,
                                     bool old_metadata_ip_active,
                                     bool old_need_linklocal_ip) {
    assert(!metadata_ip_active_);
    DeleteL3TunnelId();
    DeleteMetadataRoute(old_metadata_ip_active, old_vrf,
                        old_need_linklocal_ip);
}

void VmInterface::UpdateFlowKeyNextHop() {
    if (!IsActive()) {
        flow_key_nh_.reset();
        return;
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();

    //If Layer3 forwarding is configured irrespective of ipv4/v6 status,
    //flow_key_nh should be l3 based.
    if (layer3_forwarding()) {
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           true, InterfaceNHFlags::INET4, vm_mac_);
        flow_key_nh_ = static_cast<const NextHop *>(
                agent->nexthop_table()->FindActiveEntry(&key));
        return;
    }

    //L2 mode is identified if layer3_forwarding is diabled.
    InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                          GetUuid(), ""),
                       true, InterfaceNHFlags::BRIDGE, vm_mac_);
    flow_key_nh_ = static_cast<const NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key));
}

void VmInterface::ApplyMacVmBindingConfig(const VrfEntry *old_vrf,
                                          bool old_active,
                                          bool old_dhcp_enable) {
    if (!IsActive() || old_vrf != vrf()) {
        DeleteMacVmBinding(old_vrf);
    }

    //Update DHCP and DNS flag in Interface Class.
    if (dhcp_enable_) {
        dhcp_enabled_ = true;
        dns_enabled_ = true;
    } else {
        dhcp_enabled_ = false;
        dns_enabled_ = false;
    }

    if (!IsActive())
        return;

    UpdateMacVmBinding();
}

void VmInterface::UpdateMacVmBinding() {
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    Agent *agent = table->agent();
    table->AddMacVmBindingRoute(agent->mac_vm_binding_peer(), vrf_->GetName(),
                                vm_mac_, this);
}

void VmInterface::DeleteMacVmBinding(const VrfEntry *old_vrf) {
    if (old_vrf == NULL)
        return;
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (old_vrf->GetBridgeRouteTable());
    if (table == NULL)
        return;
    Agent *agent = table->agent();
    table->DeleteMacVmBindingRoute(agent->mac_vm_binding_peer(),
                                   old_vrf->GetName(), vm_mac_, this);
}

/////////////////////////////////////////////////////////////////////////////
// L3 related routines
/////////////////////////////////////////////////////////////////////////////
void VmInterface::UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr, int old_ethernet_tag,
                           bool force_update, bool policy_change,
                           bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen,
                           const Ip4Address &old_dhcp_addr) {
    if (ipv4_active_) {
        if (do_dhcp_relay_) {
            UpdateIpv4InterfaceRoute(old_ipv4_active,
                                     force_update||policy_change, old_vrf,
                                     old_dhcp_addr);
        }
        UpdateIpv4InstanceIp(force_update, policy_change, false,
                             old_ethernet_tag, old_vrf);
        UpdateFloatingIp(force_update, policy_change, false, old_ethernet_tag);
        UpdateAliasIp(force_update, policy_change);
        UpdateResolveRoute(old_ipv4_active, force_update, policy_change, 
                           old_vrf, old_subnet, old_subnet_plen);
    }
    if (ipv6_active_) {
        UpdateIpv6InstanceIp(force_update, policy_change, false,
                             old_ethernet_tag);
    }
    UpdateServiceVlan(force_update, policy_change, old_ipv4_active,
                      old_ipv6_active);
    UpdateAllowedAddressPair(force_update, policy_change, false, false, false);
    UpdateVrfAssignRule();
    UpdateStaticRoute(force_update||policy_change);
}

void VmInterface::DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                           const Ip4Address &old_addr,
                           bool old_need_linklocal_ip, bool old_ipv6_active,
                           const Ip6Address &old_v6_addr,
                           const Ip4Address &old_subnet,
                           const uint8_t old_subnet_plen,
                           int old_ethernet_tag,
                           const Ip4Address &old_dhcp_addr,
                           bool force_update) {
    // trigger delete instance ip v4/v6 unconditionally, it internally
    // handles cases where delete is already processed
    DeleteIpv4InstanceIp(false, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv4InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(false, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(true, old_ethernet_tag, old_vrf, force_update);

    if (old_ipv4_active) {
        if (old_dhcp_addr != Ip4Address(0)) {
            DeleteIpv4InterfaceRoute(old_vrf, old_dhcp_addr);
        }
    }
    DeleteServiceVlan();
    // Process following deletes only if any of old ipv4 or ipv6
    // was active
    if ((old_ipv4_active || old_ipv6_active)) {
        DeleteFloatingIp(false, old_ethernet_tag);
        DeleteAliasIp();
        DeleteStaticRoute();
        DeleteAllowedAddressPair(false);
        DeleteVrfAssignRule();
        DeleteResolveRoute(old_vrf, old_subnet, old_subnet_plen);
    }

}

void VmInterface::UpdateL3NextHop() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    //layer3_forwarding config is not set, so delete as i/f is active.
    if (!layer3_forwarding_) {
        if ((l3_interface_nh_policy_.get() != NULL) ||
            (l3_interface_nh_no_policy_.get() != NULL)) {
            DeleteL3NextHop();
        }
        return;
    }

    InterfaceNH::CreateL3VmInterfaceNH(GetUuid(), vm_mac_, vrf_->GetName(),
                                       learning_enabled_);
    InterfaceNHKey key1(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, GetUuid(),
                                           ""),
                        true, InterfaceNHFlags::INET4, vm_mac_);
    l3_interface_nh_policy_ =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key1));
    InterfaceNHKey key2(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, GetUuid(),
                                           ""),
                        false, InterfaceNHFlags::INET4, vm_mac_);
    l3_interface_nh_no_policy_ =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key2));

    bool new_entry = false;
    if (label_ == MplsTable::kInvalidLabel) {
        new_entry = true;
    }

    // Update L3 mpls label from nh entry
    if (policy_enabled()) {
        label_ = l3_interface_nh_policy_->mpls_label()->label();
    } else {
        label_ = l3_interface_nh_no_policy_->mpls_label()->label();
    }

    if (new_entry) {
        UpdateMetaDataIpInfo();
    }
}

void VmInterface::DeleteL3NextHop() {
    InterfaceNH::DeleteL3InterfaceNH(GetUuid(), vm_mac_);
    l3_interface_nh_policy_.reset();
    l3_interface_nh_no_policy_.reset();
}

// Add/Update route. Delete old route if VRF or address changed
void VmInterface::UpdateIpv4InterfaceRoute(bool old_ipv4_active,
                                           bool force_update,
                                           VrfEntry * old_vrf,
                                           const Ip4Address &old_addr) {
    Ip4Address ip = GetServiceIp(primary_ip_addr_).to_v4();

    // If interface was already active earlier and there is no force_update or
    // policy_change, return
    if (old_ipv4_active == true && force_update == false
        && old_addr == primary_ip_addr_ &&
        vm_ip_service_addr_ == ip) {
        return;
    }

    // We need to have valid IP and VRF to add route
    if (primary_ip_addr_.to_ulong() != 0 && vrf_.get() != NULL) {
        // Add route if old was inactive or force_update is set
        if (old_ipv4_active == false || force_update == true ||
            old_addr != primary_ip_addr_ || vm_ip_service_addr_ != ip) {
            vm_ip_service_addr_ = ip;
            AddRoute(vrf_->GetName(), primary_ip_addr_, 32, vn_->GetName(),
                     false, ecmp_, false, false, vm_ip_service_addr_,
                     Ip4Address(0), CommunityList(), label_);
        }
    }

    // If there is change in VRF or IP address, delete old route
    if (old_vrf != vrf_.get() || primary_ip_addr_ != old_addr) {
        DeleteIpv4InterfaceRoute(old_vrf, old_addr);
    }
}

void VmInterface::DeleteIpv4InterfaceRoute(VrfEntry *old_vrf,
                                           const Ip4Address &old_addr) {
    if ((old_vrf == NULL) || (old_addr.to_ulong() == 0))
        return;

    DeleteRoute(old_vrf->GetName(), old_addr, 32);
}

void VmInterface::UpdateResolveRoute(bool old_ipv4_active, bool force_update,
                                     bool policy_change, VrfEntry * old_vrf,
                                     const Ip4Address &old_addr,
                                     uint8_t old_plen) {
    if (old_ipv4_active == true && force_update == false
        && policy_change == false && old_addr == subnet_ &&
        subnet_plen_ == old_plen) {
        return;
    }

    if (old_vrf && (old_vrf != vrf_.get() || 
        old_addr != subnet_ ||
        subnet_plen_ != old_plen)) {
        DeleteResolveRoute(old_vrf, old_addr, old_plen);
    }

    if (subnet_.to_ulong() != 0 && vrf_.get() != NULL && vn_.get() != NULL) {
        SecurityGroupList sg_id_list;
        CopySgIdList(&sg_id_list);
        VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE, GetUuid(), "");

        InetUnicastAgentRouteTable::AddResolveRoute
            (peer_.get(), vrf_->GetName(),
             Address::GetIp4SubnetAddress(subnet_, subnet_plen_), subnet_plen_,
             vm_intf_key, vrf_->table_label(), policy_enabled_, vn_->GetName(),
             sg_id_list);
    }
}

void VmInterface::DeleteResolveRoute(VrfEntry *old_vrf,
                                     const Ip4Address &old_addr,
                                     const uint8_t plen) {
    DeleteRoute(old_vrf->GetName(), old_addr, plen);
}

void VmInterface::UpdateIpv4InstanceIp(bool force_update, bool policy_change,
                                       bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv4_list_.list_.begin();
    while (it != instance_ipv4_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, vrf(), old_ethernet_tag);
            if (prev->installed() == false) {
                instance_ipv4_list_.list_.erase(prev);
            }
        } else {
            if (old_vrf && (old_vrf != vrf())) {
                prev->DeActivate(this, l2, old_vrf, old_ethernet_tag);
            }
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteIpv4InstanceIp(bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf_entry,
                                       bool force_update) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv4_list_.list_.begin();
    while (it != instance_ipv4_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->is_service_health_check_ip_) {
            // for service health check instance ip do not withdraw
            // the route till it is marked del_pending
            // interface itself is not active
            bool interface_active = false;
            if (l2) {
                interface_active = metadata_l2_active_;
            } else {
                interface_active = metadata_ip_active_;
            }
            if (!prev->del_pending_ && interface_active) {
                prev->Activate(this, force_update, l2,
                               old_ethernet_tag);
                continue;
            }
        }
        prev->DeActivate(this, l2, old_vrf_entry, old_ethernet_tag);
        if (prev->del_pending_ && prev->installed() == false) {
            instance_ipv4_list_.list_.erase(prev);
        }
    }
}

void VmInterface::UpdateIpv6InstanceIp(bool force_update, bool policy_change,
                                       bool l2, uint32_t old_ethernet_tag) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv6_list_.list_.begin();
    while (it != instance_ipv6_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, vrf(), old_ethernet_tag);
            if (prev->installed() == false) {
                instance_ipv6_list_.list_.erase(prev);
            }
        } else {
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteIpv6InstanceIp(bool l2, uint32_t old_ethernet_tag,
                                       VrfEntry *old_vrf_entry,
                                       bool force_update) {
    if (l2 && old_ethernet_tag != ethernet_tag()) {
        force_update = true;
    }

    InstanceIpSet::iterator it = instance_ipv6_list_.list_.begin();
    while (it != instance_ipv6_list_.list_.end()) {
        InstanceIpSet::iterator prev = it++;
        if (prev->is_service_health_check_ip_) {
            // for service health check instance ip do not withdraw
            // the route till it is marked del_pending
            // interface itself is not active
            bool interface_active = false;
            if (l2) {
                interface_active = metadata_l2_active_;
            } else {
                interface_active = metadata_ip_active_;
            }
            if (!prev->del_pending_ && interface_active) {
                prev->Activate(this, force_update, l2,
                               old_ethernet_tag);
                continue;
            }
        }
        prev->DeActivate(this, l2, old_vrf_entry, old_ethernet_tag);
        if (prev->del_pending_ && prev->installed() == false) {
            instance_ipv6_list_.list_.erase(prev);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// L2 related routines
/////////////////////////////////////////////////////////////////////////////
void VmInterface::AddL2ReceiveRoute(bool old_bridging) {
    if (BridgingActivated(old_bridging)) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();
        BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>
            (vrf_->GetRouteTable(Agent::BRIDGE));

        l2_table->AddBridgeReceiveRoute(peer_.get(), vrf_->GetName(), 0,
                                        GetVifMac(agent), vn_->GetName());
    }
}

void VmInterface::DeleteL2ReceiveRoute(const VrfEntry *old_vrf,
                                       bool old_bridging) {
    if (old_vrf) {
        InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
        Agent *agent = table->agent();
        BridgeAgentRouteTable::Delete(peer_.get(), old_vrf->GetName(),
                                      GetVifMac(agent), 0);
    }
}

void VmInterface::UpdateBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                                     int old_ethernet_tag,
                                     bool force_update, bool policy_change,
                                     const Ip4Address &old_v4_addr,
                                     const Ip6Address &old_v6_addr,
                                     bool old_layer3_forwarding) {
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID)
        return;

    UpdateL2InterfaceRoute(old_bridging, force_update, old_vrf, Ip4Address(),
                           Ip6Address(), old_ethernet_tag,
                           old_layer3_forwarding, policy_change, Ip4Address(),
                           Ip6Address(), vm_mac_, Ip4Address(0));
    UpdateIpv4InstanceIp(force_update, policy_change, true, old_ethernet_tag,
                         old_vrf);
    UpdateIpv6InstanceIp(force_update, policy_change, true, old_ethernet_tag);
    UpdateFloatingIp(force_update, policy_change, true, old_ethernet_tag);
    UpdateAllowedAddressPair(force_update, policy_change, true, old_bridging,
                             old_layer3_forwarding);
    //If the interface is Gateway we need to add a receive route,
    //such the packet gets routed. Bridging on gateway
    //interface is not supported
    if ((vmi_type() == GATEWAY || vmi_type() == REMOTE_VM) &&
        BridgingActivated(old_bridging)) {
        AddL2ReceiveRoute(old_bridging);
    }
}

void VmInterface::DeleteBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                                     int old_ethernet_tag,
                                     const Ip4Address &old_v4_addr,
                                     const Ip6Address &old_v6_addr,
                                     bool old_layer3_forwarding,
                                     bool force_update) {
    DeleteIpv4InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    DeleteIpv6InstanceIp(true, old_ethernet_tag, old_vrf, force_update);
    if (old_bridging) {
        DeleteL2InterfaceRoute(old_bridging, old_vrf, Ip4Address(0),
                               Ip6Address(), old_ethernet_tag, vm_mac_);
        DeleteFloatingIp(true, old_ethernet_tag);
        DeleteL2ReceiveRoute(old_vrf, old_bridging);
        DeleteAllowedAddressPair(true);
    }
}

void VmInterface::UpdateVxLan() {
    int new_vxlan_id = vn_.get() ? vn_->GetVxLanId() : 0;
    if (l2_active_ && ((vxlan_id_ == 0) ||
                       (vxlan_id_ != new_vxlan_id))) {
        vxlan_id_ = new_vxlan_id;
    }
    ethernet_tag_ = IsVxlanMode() ? vxlan_id_ : 0;
}

void VmInterface::UpdateL2(bool old_l2_active, bool policy_change) {
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID)
        return;

    UpdateVxLan();
}

void VmInterface::DeleteL2(bool old_l2_active) {
    l2_label_ = MplsTable::kInvalidLabel;
}

//Create these NH irrespective of mode, as multicast uses l2 NH.
void VmInterface::UpdateL2NextHop(bool force_update) {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    if (l2_interface_nh_policy_.get() == NULL || force_update) {
        InterfaceNH::CreateL2VmInterfaceNH(GetUuid(), vm_mac_, vrf_->GetName(),
                                           learning_enabled_, etree_leaf_,
                                           layer2_control_word_);
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           true, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_policy_ = static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    }
    if (l2_interface_nh_no_policy_.get() == NULL || force_update) {
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           false, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_no_policy_ =static_cast<NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
    }

    // Update L2 mpls label from nh entry
    if (policy_enabled()) {
        l2_label_ = l2_interface_nh_policy_->mpls_label()->label();
    } else {
        l2_label_ = l2_interface_nh_no_policy_->mpls_label()->label();
    }
}

void VmInterface::DeleteL2NextHop() {
    InterfaceNH::DeleteL2InterfaceNH(GetUuid(),
                                     vm_mac_);
    if (l2_interface_nh_policy_.get() != NULL)
        l2_interface_nh_policy_.reset();
    if (l2_interface_nh_no_policy_.get() != NULL)
        l2_interface_nh_no_policy_.reset();
}

void VmInterface::UpdateL2InterfaceRoute(bool old_bridging, bool force_update,
                                         VrfEntry *old_vrf,
                                         const Ip4Address &old_v4_addr,
                                         const Ip6Address &old_v6_addr,
                                         int old_ethernet_tag,
                                         bool old_layer3_forwarding,
                                         bool policy_changed,
                                         const Ip4Address &new_ip_addr,
                                         const Ip6Address &new_ip6_addr,
                                         const MacAddress &mac,
                                         const IpAddress &dependent_ip) const {
    if (ethernet_tag_ != old_ethernet_tag) {
        force_update = true;
    }

    if (old_layer3_forwarding != layer3_forwarding_) {
        force_update = true;
    }

    if (old_vrf && old_vrf != vrf()) {
        force_update = true;
    }

    //Encap change will result in force update of l2 routes.
    if (force_update) {
        DeleteL2InterfaceRoute(true, old_vrf, old_v4_addr,
                               old_v6_addr, old_ethernet_tag, mac);
    } else {
        if (new_ip_addr != old_v4_addr) {
            force_update = true;
            DeleteL2InterfaceRoute(true, old_vrf, old_v4_addr, Ip6Address(),
                                   old_ethernet_tag, mac);
        }

        if (new_ip6_addr != old_v6_addr) {
            force_update = true;
            DeleteL2InterfaceRoute(true, old_vrf, Ip4Address(), old_v6_addr,
                                   old_ethernet_tag, mac);
        }
    }

    assert(peer_.get());
    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, false, dependent_ip);

    if (policy_changed == true) {
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(), mac, new_ip_addr,
                             ethernet_tag_, NULL);
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(), mac, new_ip6_addr,
                             ethernet_tag_, NULL);
    }

    if (old_bridging && force_update == false)
        return;

    uint32_t label = l2_label_;
    if (pbb_interface()) {
        label = GetPbbLabel();
    }

    if (new_ip_addr.is_unspecified() || layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(), mac, this,
                               new_ip_addr, label, vn_->GetName(), sg_id_list,
                               path_preference, ethernet_tag_, etree_leaf_);
    }

    if (new_ip6_addr.is_unspecified() == false && layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(), mac, this,
                               new_ip6_addr, label, vn_->GetName(), sg_id_list,
                               path_preference, ethernet_tag_, etree_leaf_);
    }
}

void VmInterface::DeleteL2InterfaceRoute(bool old_bridging, VrfEntry *old_vrf,
                                         const Ip4Address &old_v4_addr,
                                         const Ip6Address &old_v6_addr,
                                         int old_ethernet_tag,
                                         const MacAddress &mac) const {
    if (old_bridging == false)
        return;

    if (old_vrf == NULL)
        return;

    EvpnAgentRouteTable *table = static_cast<EvpnAgentRouteTable *>
        (old_vrf->GetEvpnRouteTable());
    if (table == NULL)
        return;
    table->DelLocalVmRoute(peer_.get(), old_vrf->GetName(), mac,
                           this, old_v4_addr,
                           old_ethernet_tag);
    table->DelLocalVmRoute(peer_.get(), old_vrf->GetName(), mac,
                           this, old_v6_addr,
                           old_ethernet_tag);
}

/////////////////////////////////////////////////////////////////////////////
// InstanceIp routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::InstanceIp::InstanceIp() :
    ListEntry(), ip_(), plen_(), ecmp_(false), l2_installed_(false),
    old_ecmp_(false), is_primary_(false), is_service_health_check_ip_(false),
    is_local_(false), old_tracking_ip_(), tracking_ip_() {
}

VmInterface::InstanceIp::InstanceIp(const InstanceIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    ip_(rhs.ip_), plen_(rhs.plen_), ecmp_(rhs.ecmp_),
    l2_installed_(rhs.l2_installed_), old_ecmp_(rhs.old_ecmp_),
    is_primary_(rhs.is_primary_),
    is_service_health_check_ip_(rhs.is_service_health_check_ip_),
    is_local_(rhs.is_local_), old_tracking_ip_(rhs.old_tracking_ip_),
    tracking_ip_(rhs.tracking_ip_) {
}

VmInterface::InstanceIp::InstanceIp(const IpAddress &addr, uint8_t plen,
                                    bool ecmp, bool is_primary,
                                    bool is_service_health_check_ip,
                                    bool is_local,
                                    const IpAddress &tracking_ip) :
    ListEntry(), ip_(addr), plen_(plen), ecmp_(ecmp),
    l2_installed_(false), old_ecmp_(false), is_primary_(is_primary),
    is_service_health_check_ip_(is_service_health_check_ip),
    is_local_(is_local), tracking_ip_(tracking_ip) {
}

VmInterface::InstanceIp::~InstanceIp() {
}

bool VmInterface::InstanceIp::operator() (const InstanceIp &lhs,
                                          const InstanceIp &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::InstanceIp::IsLess(const InstanceIp *rhs) const {
    return ip_ < rhs->ip_;
}

void VmInterface::InstanceIp::L3Activate(VmInterface *interface,
                                         bool force_update) const {
    if (old_ecmp_ != ecmp_) {
        force_update = true;
        old_ecmp_ = ecmp_;
    }

    if (old_tracking_ip_ != tracking_ip_) {
        force_update = true;
    }

    // Add route if not installed or if force requested
    if (installed_ && force_update == false) {
        return;
    }

    // Add route only when vn IPAM exists
    if (!interface->vn()->GetIpam(ip_)) {
        return;
    }

    // Set prefix len for instance_ip based on Alloc-unit in VnIPAM
    SetPrefixForAllocUnitIpam(interface);

    if (ip_.is_v4()) {
        interface->AddRoute(interface->vrf()->GetName(), ip_.to_v4(), plen_,
                            interface->vn()->GetName(), is_force_policy(),
                            ecmp_, is_local_, is_service_health_check_ip_,
                            interface->GetServiceIp(ip_), tracking_ip_,
                            CommunityList(), interface->label());
    } else if (ip_.is_v6()) {
        interface->AddRoute(interface->vrf()->GetName(), ip_.to_v6(), plen_,
                            interface->vn()->GetName(), is_force_policy(),
                            ecmp_, is_local_, is_service_health_check_ip_,
                            interface->GetServiceIp(ip_), tracking_ip_,
                            CommunityList(), interface->label());
    }
    installed_ = true;
}

void VmInterface::InstanceIp::L3DeActivate(VmInterface *interface,
                                           VrfEntry *old_vrf) const {
    if (installed_ == false) {
        return;
    }

    if (ip_.is_v4()) {
        interface->DeleteRoute(old_vrf->GetName(), ip_, plen_);
    } else if (ip_.is_v6()) {
        interface->DeleteRoute(old_vrf->GetName(), ip_, plen_);
    }
    installed_ = false;
}

void VmInterface::InstanceIp::L2Activate(VmInterface *interface,
                                         bool force_update,
                                         uint32_t old_ethernet_tag) const {

    Ip4Address ipv4(0);
    Ip6Address ipv6;

    if (IsL3Only()) {
        return;
    }

    if (ip_.is_v4()) {
        // check if L3 route is already installed or not 
        if (installed_ == false) {
            return;
        }
        ipv4 = ip_.to_v4();
    } else {
        // check if L3 route is already installed or not 
        if (installed_ == false) {
            return;
        }
        ipv6 = ip_.to_v6();
    }

    if (tracking_ip_ != old_tracking_ip_) {
        force_update = true;
    }

    if (l2_installed_ == false || force_update) {
        interface->UpdateL2InterfaceRoute
            (false, force_update, interface->vrf(), ipv4, ipv6,
             old_ethernet_tag, false, false, ipv4, ipv6, interface->vm_mac(),
             tracking_ip_);
        l2_installed_ = true;
    }
}

void VmInterface::InstanceIp::L2DeActivate(VmInterface *interface,
                                           VrfEntry *old_vrf,
                                           uint32_t old_ethernet_tag) const {
    if (l2_installed_ == false) {
        return;
    }

    Ip4Address ipv4(0);
    Ip6Address ipv6;

    if (ip_.is_v4()) {
        ipv4 = ip_.to_v4();
    } else {
        ipv6 = ip_.to_v6();
    }

    interface->DeleteL2InterfaceRoute(true, old_vrf, ipv4, ipv6,
                                      old_ethernet_tag, interface->vm_mac());
    l2_installed_ = false;
}

void VmInterface::InstanceIp::Activate(VmInterface *interface,
                                       bool force_update, bool l2,
                                       int old_ethernet_tag) const {
    if (l2) {
        L2Activate(interface, force_update, old_ethernet_tag);
    } else {
        L3Activate(interface, force_update);
    }
}

void VmInterface::InstanceIp::DeActivate(VmInterface *interface, bool l2,
                                         VrfEntry *old_vrf,
                                         uint32_t old_ethernet_tag) const {
    if (l2) {
        L2DeActivate(interface, old_vrf, old_ethernet_tag);
    } else {
        L3DeActivate(interface, old_vrf);
    }
}

void VmInterface::InstanceIp::SetPrefixForAllocUnitIpam
(VmInterface *interface) const {
    uint32_t alloc_unit = interface->vn()->GetAllocUnitFromIpam(ip_);

    uint8_t alloc_prefix = 0;
    if (alloc_unit > 0) {
        alloc_prefix = log2(alloc_unit);
    }

    if (ip_.is_v4()) {
        plen_ = Address::kMaxV4PrefixLen - alloc_prefix;
    } else if (ip_.is_v6()) {
        plen_ = Address::kMaxV6PrefixLen - alloc_prefix;
    }
}

void VmInterface::InstanceIpList::Insert(const InstanceIp *rhs) {
    list_.insert(*rhs);
}

void VmInterface::InstanceIpList::Update(const InstanceIp *lhs,
                                         const InstanceIp *rhs) {
    if (lhs->ecmp_ != rhs->ecmp_) {
        lhs->ecmp_ = rhs->ecmp_;
    }

    lhs->is_service_health_check_ip_ = rhs->is_service_health_check_ip_;
    lhs->is_local_ = rhs->is_local_;

    lhs->old_tracking_ip_ = lhs->tracking_ip_;
    lhs->tracking_ip_ = rhs->tracking_ip_;

    lhs->set_del_pending(false);
}

void VmInterface::InstanceIpList::Remove(InstanceIpSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::FatFlowList::Insert(const FatFlowEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::FatFlowList::Update(const FatFlowEntry *lhs,
                                      const FatFlowEntry *rhs) {
}

void VmInterface::FatFlowList::Remove(FatFlowEntrySet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateFatFlow() {
    DeleteFatFlow();
}

void VmInterface::DeleteFatFlow() { 
    FatFlowEntrySet::iterator it = fat_flow_list_.list_.begin();
    while (it != fat_flow_list_.list_.end()) {
        FatFlowEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            fat_flow_list_.list_.erase(prev);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// FloatingIp routines
/////////////////////////////////////////////////////////////////////////////

VmInterface::FloatingIp::FloatingIp() : 
    ListEntry(), floating_ip_(), vn_(NULL),
    vrf_(NULL, this), vrf_name_(""), vn_uuid_(), l2_installed_(false),
    fixed_ip_(), force_l3_update_(false), force_l2_update_(false),
    direction_(DIRECTION_BOTH),
    port_map_enabled_(false), src_port_map_(), dst_port_map_() {
}

VmInterface::FloatingIp::FloatingIp(const FloatingIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    floating_ip_(rhs.floating_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_),
    l2_installed_(rhs.l2_installed_), fixed_ip_(rhs.fixed_ip_),
    force_l3_update_(rhs.force_l3_update_),
    force_l2_update_(rhs.force_l2_update_),
    direction_(rhs.direction_),
    port_map_enabled_(rhs.port_map_enabled_), src_port_map_(rhs.src_port_map_),
    dst_port_map_(rhs.dst_port_map_) {
}

VmInterface::FloatingIp::FloatingIp(const IpAddress &addr,
                                    const std::string &vrf,
                                    const boost::uuids::uuid &vn_uuid,
                                    const IpAddress &fixed_ip,
                                    Direction direction,
                                    bool port_map_enabled,
                                    const PortMap &src_port_map,
                                    const PortMap &dst_port_map) :
    ListEntry(), floating_ip_(addr), vn_(NULL), vrf_(NULL, this),
    vrf_name_(vrf), vn_uuid_(vn_uuid), l2_installed_(false),
    fixed_ip_(fixed_ip), force_l3_update_(false), force_l2_update_(false),
    direction_(direction),
    port_map_enabled_(port_map_enabled), src_port_map_(src_port_map),
    dst_port_map_(dst_port_map) {
}

VmInterface::FloatingIp::~FloatingIp() {
}

bool VmInterface::FloatingIp::operator() (const FloatingIp &lhs,
                                          const FloatingIp &rhs) const {
    return lhs.IsLess(&rhs);
}

// Compare key for FloatingIp. Key is <floating_ip_ and vrf_name_> for both
// Config and Operational processing
bool VmInterface::FloatingIp::IsLess(const FloatingIp *rhs) const {
    if (floating_ip_ != rhs->floating_ip_)
        return floating_ip_ < rhs->floating_ip_;

    return (vrf_name_ < rhs->vrf_name_);
}

void VmInterface::FloatingIp::L3Activate(VmInterface *interface,
                                         bool force_update) const {
    // Add route if not installed or if force requested
    if (installed_ && force_update == false && force_l3_update_ == false) {
        return;
    }

    fixed_ip_ = GetFixedIp(interface);

    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (floating_ip_.is_v4()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v4(),
                        Address::kMaxV4PrefixLen, vn_->GetName(), false,
                        interface->ecmp(), false, false, Ip4Address(0),
                        fixed_ip_, CommunityList(),
                        interface->label());
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), false);
        }
    } else if (floating_ip_.is_v6()) {
        interface->AddRoute(vrf_.get()->GetName(), floating_ip_.to_v6(),
                            Address::kMaxV6PrefixLen, vn_->GetName(), false,
                            interface->ecmp6(), false, false, Ip6Address(),
                            fixed_ip_, CommunityList(), interface->label());
        //TODO:: callback for DNS handling
    }

    installed_ = true;
    force_l3_update_ = false;
}

void VmInterface::FloatingIp::L3DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;

    if (floating_ip_.is_v4()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_,
                               Address::kMaxV4PrefixLen);
        InterfaceTable *table =
            static_cast<InterfaceTable *>(interface->get_table());
        if (table->update_floatingip_cb().empty() == false) {
            table->update_floatingip_cb()(interface, vn_.get(),
                                          floating_ip_.to_v4(), true);
        }
    } else if (floating_ip_.is_v6()) {
        interface->DeleteRoute(vrf_.get()->GetName(), floating_ip_,
                               Address::kMaxV6PrefixLen);
        //TODO:: callback for DNS handling
    }
    installed_ = false;
}

void VmInterface::FloatingIp::L2Activate(VmInterface *interface,
                                         bool force_update,
                                         uint32_t old_ethernet_tag) const {
    // Add route if not installed or if force requested
    if (l2_installed_ && force_update == false &&
            force_l2_update_ == false) {
        return;
    }

    SecurityGroupList sg_id_list;
    interface->CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    interface->SetPathPreference(&path_preference, false, GetFixedIp(interface));

    EvpnAgentRouteTable *evpn_table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());
    if (old_ethernet_tag != interface->ethernet_tag()) {
        L2DeActivate(interface, old_ethernet_tag);
    }
    evpn_table->AddReceiveRoute(interface->peer_.get(), vrf_->GetName(),
                                interface->l2_label(),
                                interface->vm_mac(),
                                floating_ip_, interface->ethernet_tag(),
                                vn_->GetName(), path_preference);
    l2_installed_ = true;
    force_l2_update_ = false;
}

void VmInterface::FloatingIp::L2DeActivate(VmInterface *interface,
                                           uint32_t ethernet_tag) const {
    if (l2_installed_ == false)
        return;

    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(vrf_->GetEvpnRouteTable());
    if (evpn_table) {
        evpn_table->DelLocalVmRoute(interface->peer_.get(), vrf_->GetName(),
                                    interface->vm_mac(),
                                    interface, floating_ip_, ethernet_tag);
    }
    //Reset the interface ethernet_tag
    l2_installed_ = false;
}

void VmInterface::FloatingIp::Activate(VmInterface *interface,
                                       bool force_update, bool l2,
                                       uint32_t old_ethernet_tag) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (vn_.get() == NULL) {
        vn_ = table->FindVnRef(vn_uuid_);
        assert(vn_.get());
    }

    if (vrf_.get() == NULL) {
        vrf_ = table->FindVrfRef(vrf_name_);
        assert(vrf_.get());
    }

    if (l2)
        L2Activate(interface, force_update, old_ethernet_tag);
    else
        L3Activate(interface, force_update);
}

void VmInterface::FloatingIp::DeActivate(VmInterface *interface, bool l2,
                                         uint32_t old_ethernet_tag) const{
    if (l2)
        L2DeActivate(interface, old_ethernet_tag);
    else
        L3DeActivate(interface);

    if (installed_ == false && l2_installed_ == false)
        vrf_ = NULL;
}

const IpAddress
VmInterface::FloatingIp::GetFixedIp(const VmInterface *interface) const {
    if (fixed_ip_.is_unspecified()) {
        if (floating_ip_.is_v4() == true) {
            return interface->primary_ip_addr();
        } else {
            return interface->primary_ip6_addr();
        }
    }
    return fixed_ip_;
}

bool VmInterface::FloatingIp::port_map_enabled() const {
    return port_map_enabled_;
}

uint32_t VmInterface::FloatingIp::PortMappingSize() const {
    return src_port_map_.size();
}

int32_t VmInterface::FloatingIp::GetSrcPortMap(uint8_t protocol,
                                               uint16_t src_port) const {
    PortMapIterator it = src_port_map_.find(PortMapKey(protocol, src_port));
    if (it == src_port_map_.end())
        return -1;
    return it->second;
}

int32_t VmInterface::FloatingIp::GetDstPortMap(uint8_t protocol,
                                               uint16_t dst_port) const {
    PortMapIterator it = dst_port_map_.find(PortMapKey(protocol, dst_port));
    if (it == dst_port_map_.end())
        return -1;
    return it->second;
}

void VmInterface::FloatingIpList::Insert(const FloatingIp *rhs) {
    std::pair<FloatingIpSet::iterator, bool> ret = list_.insert(*rhs);
    if (ret.second) {
        if (rhs->floating_ip_.is_v4()) {
            v4_count_++;
        } else {
            v6_count_++;
        }
    }
}

void VmInterface::FloatingIpList::Update(const FloatingIp *lhs,
                                         const FloatingIp *rhs) {
    if (lhs->fixed_ip_ != rhs->fixed_ip_) {
        lhs->fixed_ip_ = rhs->fixed_ip_;
        lhs->force_l3_update_ = true;
        lhs->force_l2_update_ = true;
    }

    lhs->direction_ = rhs->direction_;
    lhs->port_map_enabled_ = rhs->port_map_enabled_;
    lhs->src_port_map_ = rhs->src_port_map_;
    lhs->dst_port_map_ = rhs->dst_port_map_;
    lhs->set_del_pending(false);
}

void VmInterface::FloatingIpList::Remove(FloatingIpSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::CleanupFloatingIpList() {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_ == false)
            continue;

        if (prev->floating_ip_.is_v4()) {
            floating_ip_list_.v4_count_--;
            assert(floating_ip_list_.v4_count_ >= 0);
        } else {
            floating_ip_list_.v6_count_--;
            assert(floating_ip_list_.v6_count_ >= 0);
        }
        floating_ip_list_.list_.erase(prev);
    }
}

void VmInterface::UpdateFloatingIp(bool force_update, bool policy_change,
                                   bool l2, uint32_t old_ethernet_tag) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this, l2, old_ethernet_tag);
        } else {
            prev->Activate(this, force_update||policy_change, l2,
                           old_ethernet_tag);
        }
    }
}

void VmInterface::DeleteFloatingIp(bool l2, uint32_t old_ethernet_tag) {
    FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
    while (it != floating_ip_list_.list_.end()) {
        FloatingIpSet::iterator prev = it++;
        prev->DeActivate(this, l2, old_ethernet_tag);
    }
}

/////////////////////////////////////////////////////////////////////////////
// AliasIp routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::AliasIp::AliasIp() :
    ListEntry(), alias_ip_(), vn_(NULL),
    vrf_(NULL, this), vrf_name_(""), vn_uuid_(), force_update_(false) {
}

VmInterface::AliasIp::AliasIp(const AliasIp &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_),
    alias_ip_(rhs.alias_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_),
    force_update_(rhs.force_update_) {
}

VmInterface::AliasIp::AliasIp(const IpAddress &addr,
                              const std::string &vrf,
                              const boost::uuids::uuid &vn_uuid) :
    ListEntry(), alias_ip_(addr), vn_(NULL), vrf_(NULL, this), vrf_name_(vrf),
    vn_uuid_(vn_uuid), force_update_(false) {
}

VmInterface::AliasIp::~AliasIp() {
}

bool VmInterface::AliasIp::operator() (const AliasIp &lhs,
                                       const AliasIp &rhs) const {
    return lhs.IsLess(&rhs);
}

// Compare key for AliasIp. Key is <alias_ip_ and vrf_name_> for both
// Config and Operational processing
bool VmInterface::AliasIp::IsLess(const AliasIp *rhs) const {
    if (alias_ip_ != rhs->alias_ip_)
        return alias_ip_ < rhs->alias_ip_;

    return (vrf_name_ < rhs->vrf_name_);
}

void VmInterface::AliasIp::Activate(VmInterface *interface,
                                    bool force_update) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());

    if (vn_.get() == NULL) {
        vn_ = table->FindVnRef(vn_uuid_);
        assert(vn_.get());
    }

    if (vrf_.get() == NULL) {
        vrf_ = table->FindVrfRef(vrf_name_);
        assert(vrf_.get());
    }

    // Add route if not installed or if force requested
    if (installed_ && force_update == false && force_update_ == false) {
        return;
    }

    if (alias_ip_.is_v4()) {
        interface->AddRoute(vrf_.get()->GetName(), alias_ip_.to_v4(), 32,
                            vn_->GetName(), false, interface->ecmp(), false,
                            false, Ip4Address(0), Ip4Address(0),
                            CommunityList(), interface->label());
    } else if (alias_ip_.is_v6()) {
        interface->AddRoute(vrf_.get()->GetName(), alias_ip_.to_v6(), 128,
                            vn_->GetName(), false, interface->ecmp6(), false,
                            false, Ip6Address(), Ip6Address(), CommunityList(),
                            interface->label());
    }

    installed_ = true;
    force_update_ = false;
}

void VmInterface::AliasIp::DeActivate(VmInterface *interface) const {
    if (installed_) {
        if (alias_ip_.is_v4()) {
            interface->DeleteRoute(vrf_.get()->GetName(), alias_ip_, 32);
        } else if (alias_ip_.is_v6()) {
            interface->DeleteRoute(vrf_.get()->GetName(), alias_ip_, 128);
        }
        installed_ = false;
    }

    vrf_ = NULL;
}

void VmInterface::AliasIpList::Insert(const AliasIp *rhs) {
    std::pair<AliasIpSet::iterator, bool> ret = list_.insert(*rhs);
    if (ret.second) {
        if (rhs->alias_ip_.is_v4()) {
            v4_count_++;
        } else {
            v6_count_++;
        }
    }
}

void VmInterface::AliasIpList::Update(const AliasIp *lhs,
                                      const AliasIp *rhs) {
    lhs->set_del_pending(false);
}

void VmInterface::AliasIpList::Remove(AliasIpSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::CleanupAliasIpList() {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        if (prev->del_pending_ == false)
            continue;

        if (prev->alias_ip_.is_v4()) {
            alias_ip_list_.v4_count_--;
            assert(alias_ip_list_.v4_count_ >= 0);
        } else {
            alias_ip_list_.v6_count_--;
            assert(alias_ip_list_.v6_count_ >= 0);
        }
        alias_ip_list_.list_.erase(prev);
    }
}

void VmInterface::UpdateAliasIp(bool force_update, bool policy_change) {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->DeActivate(this);
        } else {
            prev->Activate(this, force_update||policy_change);
        }
    }
}

void VmInterface::DeleteAliasIp() {
    AliasIpSet::iterator it = alias_ip_list_.list_.begin();
    while (it != alias_ip_list_.list_.end()) {
        AliasIpSet::iterator prev = it++;
        prev->DeActivate(this);
    }
}

/////////////////////////////////////////////////////////////////////////////
// StaticRoute routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::StaticRoute::StaticRoute() :
    ListEntry(), vrf_(""), addr_(), plen_(0), gw_(), communities_() {
}

VmInterface::StaticRoute::StaticRoute(const StaticRoute &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), vrf_(rhs.vrf_),
    addr_(rhs.addr_), plen_(rhs.plen_), gw_(rhs.gw_),
    communities_(rhs.communities_) {
}

VmInterface::StaticRoute::StaticRoute(const std::string &vrf,
                                      const IpAddress &addr,
                                      uint32_t plen, const IpAddress &gw,
                                      const CommunityList &communities) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), gw_(gw),
    communities_(communities) {
}

VmInterface::StaticRoute::~StaticRoute() {
}

bool VmInterface::StaticRoute::operator() (const StaticRoute &lhs,
                                           const StaticRoute &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::StaticRoute::IsLess(const StaticRoute *rhs) const {
#if 0
    //Enable once we can add static routes across vrf
    if (vrf_name_ != rhs->vrf_name_)
        return vrf_name_ < rhs->vrf_name_;
#endif

    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return gw_ < rhs->gw_;
}

void VmInterface::StaticRoute::Activate(VmInterface *interface,
                                        bool force_update) const {
    if (installed_ && force_update == false)
        return;

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == false || force_update) {
        Ip4Address gw_ip(0);
        if (gw_.is_v4() && addr_.is_v4() && gw_.to_v4() != gw_ip) {
            SecurityGroupList sg_id_list;
            interface->CopySgIdList(&sg_id_list);
            InetUnicastAgentRouteTable::AddGatewayRoute
                (interface->peer_.get(), vrf_, addr_.to_v4(), plen_,
                 gw_.to_v4(), interface->vn_->GetName(),
                 interface->vrf_->table_label(), sg_id_list, communities_);
        } else {
            IpAddress dependent_ip;
            bool ecmp = false;
            if (addr_.is_v4()) {
                dependent_ip = interface->primary_ip_addr();
                ecmp = interface->ecmp();
            } else if (addr_.is_v6()) {
                dependent_ip = interface->primary_ip6_addr();
                ecmp = interface->ecmp6();
            }
            interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                                false, ecmp, false, false,
                                interface->GetServiceIp(addr_),dependent_ip,
                                communities_, interface->label());
        }
    }

    installed_ = true;
}

void VmInterface::StaticRoute::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;
    interface->DeleteRoute(vrf_, addr_, plen_);
    installed_ = false;
}

void VmInterface::StaticRouteList::Insert(const StaticRoute *rhs) {
    list_.insert(*rhs);
}

void VmInterface::StaticRouteList::Update(const StaticRoute *lhs,
                                          const StaticRoute *rhs) {
    if (lhs->communities_ != rhs->communities_) {
        (const_cast<StaticRoute *>(lhs))->communities_ = rhs->communities_;
    }
    lhs->set_del_pending(false);
}

void VmInterface::StaticRouteList::Remove(StaticRouteSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateStaticRoute(bool force_update) {
    StaticRouteSet::iterator it = static_route_list_.list_.begin();
    while (it != static_route_list_.list_.end()) {
        StaticRouteSet::iterator prev = it++;
        /* V4 static routes should be enabled only if ipv4_active_ is true
         * V6 static routes should be enabled only if ipv6_active_ is true
         */
        if ((!ipv4_active_ && prev->addr_.is_v4()) ||
            (!ipv6_active_ && prev->addr_.is_v6())) {
            continue;
        }
        if (prev->del_pending_) {
            prev->DeActivate(this);
            static_route_list_.list_.erase(prev);
        } else {
            prev->Activate(this, force_update);
        }
    }
}

void VmInterface::DeleteStaticRoute() {
    StaticRouteSet::iterator it = static_route_list_.list_.begin();
    while (it != static_route_list_.list_.end()) {
        StaticRouteSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            static_route_list_.list_.erase(prev);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
//Allowed addresss pair route
///////////////////////////////////////////////////////////////////////////////
VmInterface::AllowedAddressPair::AllowedAddressPair() :
    ListEntry(), vrf_(""), addr_(), plen_(0), ecmp_(false), mac_(),
    l2_entry_installed_(false), ecmp_config_changed_(false), ethernet_tag_(0),
    vrf_ref_(NULL, this), service_ip_(), label_(MplsTable::kInvalidLabel),
    policy_enabled_nh_(NULL), policy_disabled_nh_(NULL) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(
    const AllowedAddressPair &rhs) : ListEntry(rhs.installed_,
    rhs.del_pending_), vrf_(rhs.vrf_), addr_(rhs.addr_), plen_(rhs.plen_),
    ecmp_(rhs.ecmp_), mac_(rhs.mac_),
    l2_entry_installed_(rhs.l2_entry_installed_),
    ecmp_config_changed_(rhs.ecmp_config_changed_),
    ethernet_tag_(rhs.ethernet_tag_), vrf_ref_(rhs.vrf_ref_, this),
    service_ip_(rhs.service_ip_), label_(rhs.label_),
    policy_enabled_nh_(rhs.policy_enabled_nh_),
    policy_disabled_nh_(rhs.policy_disabled_nh_) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(const std::string &vrf,
                                                    const IpAddress &addr,
                                                    uint32_t plen, bool ecmp,
                                                    const MacAddress &mac) :
    ListEntry(), vrf_(vrf), addr_(addr), plen_(plen), ecmp_(ecmp), mac_(mac),
    l2_entry_installed_(false), ecmp_config_changed_(false), ethernet_tag_(0),
    vrf_ref_(NULL, this), label_(MplsTable::kInvalidLabel),
    policy_enabled_nh_(NULL), policy_disabled_nh_(NULL) {
}

VmInterface::AllowedAddressPair::~AllowedAddressPair() {
}

bool VmInterface::AllowedAddressPair::operator() (const AllowedAddressPair &lhs,
                                                  const AllowedAddressPair &rhs) 
                                                  const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::AllowedAddressPair::IsLess(const AllowedAddressPair *rhs) const {
#if 0
    //Enable once we can add static routes across vrf
    if (vrf_name_ != rhs->vrf_name_)
        return vrf_name_ < rhs->vrf_name_;
#endif

    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return mac_ < rhs->mac_;
}

void VmInterface::AllowedAddressPair::L2Activate(VmInterface *interface,
                                                 bool force_update,
                                                 bool policy_change,
                                                 bool old_layer2_forwarding,
                                                 bool old_layer3_forwarding) const {
    if (mac_ == MacAddress::kZeroMac) {
        return;
    }

    if (l2_entry_installed_ && force_update == false &&
        policy_change == false && ethernet_tag_ == interface->ethernet_tag() &&
        old_layer3_forwarding == interface->layer3_forwarding() &&
        ecmp_config_changed_ == false) {
        return;
    }

    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    vrf_ref_ = interface->vrf();
    if (old_layer3_forwarding != interface->layer3_forwarding() ||
        l2_entry_installed_ == false || ecmp_config_changed_) {
        force_update = true;
    }

    if (ethernet_tag_ != interface->ethernet_tag()) {
        force_update = true;
    }

    if (l2_entry_installed_ == false || force_update || policy_change) {
        IpAddress dependent_rt;
        Ip4Address v4ip(0);
        Ip6Address v6ip;
        if (addr_.is_v4()) {
            dependent_rt = Ip4Address(0);
            if (plen_ == 32) {
                v4ip = addr_.to_v4();
            } else {
                v4ip = Ip4Address(0);
            }
        } else if (addr_.is_v6()) {
            dependent_rt = Ip6Address();
            if (plen_ == 128) {
                v6ip = addr_.to_v6();
            } else {
                v6ip = Ip6Address();
            }
        }

        if (interface->bridging()) {
            interface->UpdateL2InterfaceRoute(old_layer2_forwarding,
                                   force_update, interface->vrf(), v4ip, v6ip,
                                   ethernet_tag_, old_layer3_forwarding,
                                   policy_change, v4ip, v6ip, mac_,
                                   dependent_rt);
        }
        ethernet_tag_ = interface->ethernet_tag();
        //If layer3 forwarding is disabled
        //  * IP + mac allowed address pair should not be published
        //  * Only mac allowed address pair should be published
        //Logic for same is present in UpdateL2InterfaceRoute
        if (interface->layer3_forwarding() || addr_.is_unspecified() == true) {
            l2_entry_installed_ = true;
        } else {
            l2_entry_installed_ = false;
        }
        ecmp_config_changed_ = false;
    }
}

void VmInterface::AllowedAddressPair::L2DeActivate(VmInterface *interface)
    const {
    if (mac_ == MacAddress::kZeroMac) {
        return;
    }

    if (l2_entry_installed_ == false) {
        return;
    }

    Ip4Address v4ip(0);
    Ip6Address v6ip;
    if (addr_.is_v4()) {
        if (plen_ == 32) {
            v4ip = addr_.to_v4();
        } else {
            v4ip = Ip4Address(0);
        }
    } else if (addr_.is_v6()) {
        if (plen_ == 128) {
            v6ip = addr_.to_v6();
        } else {
            v6ip = Ip6Address();
        }
    }
    interface->DeleteL2InterfaceRoute(true, vrf_ref_.get(), v4ip,
                                      v6ip, ethernet_tag_, mac_);
    l2_entry_installed_ = false;
    vrf_ref_ = NULL;
}

void VmInterface::AllowedAddressPair::CreateLabelAndNH(Agent *agent,
                                                       VmInterface *interface,
                                                       bool policy_change)
                                                       const {
    InterfaceNH::CreateL3VmInterfaceNH(interface->GetUuid(), mac_,
                                       interface->vrf_->GetName(),
                                       interface->learning_enabled_);

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, interface->GetUuid(),
                           interface->name());
    InterfaceNHKey key(vmi_key.Clone(), false, InterfaceNHFlags::INET4, mac_);
    InterfaceNH *nh = static_cast<InterfaceNH *>(agent->nexthop_table()->
                                                 FindActiveEntry(&key));
    policy_disabled_nh_ = nh;
    //Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);

    InterfaceNHKey key1(vmi_key.Clone(), true, InterfaceNHFlags::INET4, mac_);
    nh = static_cast<InterfaceNH *>(agent->
                                    nexthop_table()->FindActiveEntry(&key1));
    //Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);
    policy_enabled_nh_ = nh;

    // Update AAP mpls label from nh entry
    if (interface->policy_enabled()) {
        label_ = policy_enabled_nh_->mpls_label()->label();
    } else {
        label_ = policy_disabled_nh_->mpls_label()->label();
    }
}

void VmInterface::AllowedAddressPair::Activate(VmInterface *interface,
                                               bool force_update,
                                               bool policy_change) const {
    IpAddress ip = interface->GetServiceIp(addr_);

    if (installed_ && force_update == false && service_ip_ == ip &&
        policy_change == false && ecmp_config_changed_ == false) {
        return;
    }

    Agent *agent = interface->agent();
    if (vrf_ != interface->vrf()->GetName()) {
        vrf_ = interface->vrf()->GetName();
    }

    if (installed_ == false || force_update || service_ip_ != ip ||
        policy_change || ecmp_config_changed_) {
        service_ip_ = ip;
        if (mac_ == MacAddress::kZeroMac ||
            mac_ == interface->vm_mac_) {
            interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                                false, ecmp_, false, false, service_ip_,
                                Ip4Address(0), CommunityList(),
                                interface->label());
        } else {
            CreateLabelAndNH(agent, interface, policy_change);
            interface->AddRoute(vrf_, addr_, plen_, interface->vn_->GetName(),
                                false, ecmp_, false, false, service_ip_,
                                Ip6Address(), CommunityList(), label_);
        }
    }
    installed_ = true;
    ecmp_config_changed_ = false;
}

void VmInterface::AllowedAddressPair::DeActivate(VmInterface *interface) const {
    if (installed_ == false)
        return;
    interface->DeleteRoute(vrf_, addr_, plen_);
    if (label_ != MplsTable::kInvalidLabel) {
        label_ = MplsTable::kInvalidLabel;
    }
    policy_enabled_nh_ = NULL;
    policy_disabled_nh_ = NULL;
    installed_ = false;
}

void VmInterface::AllowedAddressPairList::Insert
(const AllowedAddressPair *rhs){
    list_.insert(*rhs);
}

void VmInterface::AllowedAddressPairList::Update
(const AllowedAddressPair *lhs, const AllowedAddressPair *rhs) {
    lhs->set_del_pending(false);
    if (lhs->ecmp_ != rhs->ecmp_) {
        lhs->ecmp_ = rhs->ecmp_;
        lhs->ecmp_config_changed_ = true;
    }
}

void VmInterface::AllowedAddressPairList::Remove
(AllowedAddressPairSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateAllowedAddressPair(bool force_update,
                                           bool policy_change, bool l2,
                                           bool old_layer2_forwarding,
                                           bool old_layer3_forwarding) {
    AllowedAddressPairSet::iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (prev->del_pending_) {
            prev->L2DeActivate(this);
            prev->DeActivate(this);
            allowed_address_pair_list_.list_.erase(prev);
        }
    }

    for (it = allowed_address_pair_list_.list_.begin();
         it != allowed_address_pair_list_.list_.end(); it++) {
        /* V4 AAP entries should be enabled only if ipv4_active_ is true
         * V6 AAP entries should be enabled only if ipv6_active_ is true
         */
        if ((!ipv4_active_ && it->addr_.is_v4()) ||
            (!ipv6_active_ && it->addr_.is_v6())) {
            continue;
        }
        if (l2) {
            it->L2Activate(this, force_update, policy_change,
                    old_layer2_forwarding, old_layer3_forwarding);
        } else {
           it->Activate(this, force_update, policy_change);
        }
    }
}

void VmInterface::DeleteAllowedAddressPair(bool l2) {
    AllowedAddressPairSet::iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (l2) {
            prev->L2DeActivate(this);
        } else {
            prev->DeActivate(this);
        }

        if (prev->del_pending_) {
            prev->L2DeActivate(this);
            prev->DeActivate(this);
            allowed_address_pair_list_.list_.erase(prev);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// SecurityGroup routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::SecurityGroupEntry::SecurityGroupEntry() : 
    ListEntry(), uuid_(nil_uuid()) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry
    (const SecurityGroupEntry &rhs) : 
        ListEntry(rhs.installed_, rhs.del_pending_), uuid_(rhs.uuid_) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry(const uuid &u) : 
    ListEntry(), uuid_(u) {
}

VmInterface::SecurityGroupEntry::~SecurityGroupEntry() {
}

bool VmInterface::SecurityGroupEntry::operator ==
    (const SecurityGroupEntry &rhs) const {
    return uuid_ == rhs.uuid_;
}

bool VmInterface::SecurityGroupEntry::operator() 
    (const SecurityGroupEntry &lhs, const SecurityGroupEntry &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::SecurityGroupEntry::IsLess
    (const SecurityGroupEntry *rhs) const {
    return uuid_ < rhs->uuid_;
}

void VmInterface::SecurityGroupEntry::Activate(VmInterface *interface) const {
    if (sg_.get() != NULL)
        return; 

    Agent *agent = static_cast<InterfaceTable *>
        (interface->get_table())->agent();
    SgKey sg_key(uuid_);
    sg_ = static_cast<SgEntry *> 
        (agent->sg_table()->FindActiveEntry(&sg_key));
}

void VmInterface::SecurityGroupEntry::DeActivate(VmInterface *interface) const {
}

void VmInterface::SecurityGroupEntryList::Insert
    (const SecurityGroupEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::SecurityGroupEntryList::Update
        (const SecurityGroupEntry *lhs, const SecurityGroupEntry *rhs) {
}

void VmInterface::SecurityGroupEntryList::Remove
        (SecurityGroupEntrySet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateSecurityGroup() {
    SecurityGroupEntrySet::iterator it = sg_list_.list_.begin();
    while (it != sg_list_.list_.end()) {
        SecurityGroupEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            sg_list_.list_.erase(prev);
        } else {
            prev->Activate(this);
        }
    }
}

void VmInterface::DeleteSecurityGroup() {
    SecurityGroupEntrySet::iterator it = sg_list_.list_.begin();
    while (it != sg_list_.list_.end()) {
        SecurityGroupEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            sg_list_.list_.erase(prev);
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// ServiceVlan routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::ServiceVlan::ServiceVlan() :
    ListEntry(), tag_(0), vrf_name_(""), addr_(0), old_addr_(0),
    addr6_(), old_addr6_(), smac_(), dmac_(),
    vrf_(NULL, this), label_(MplsTable::kInvalidLabel), v4_rt_installed_(false),
    v6_rt_installed_(false), del_add_(false) {
}

VmInterface::ServiceVlan::ServiceVlan(const ServiceVlan &rhs) :
    ListEntry(rhs.installed_, rhs.del_pending_), tag_(rhs.tag_),
    vrf_name_(rhs.vrf_name_), addr_(rhs.addr_), old_addr_(rhs.old_addr_),
    addr6_(rhs.addr6_), old_addr6_(rhs.old_addr6_),
    vrf_(rhs.vrf_, this), label_(rhs.label_), v4_rt_installed_(rhs.v4_rt_installed_),
    v6_rt_installed_(rhs.v6_rt_installed_), del_add_(rhs.del_add_) {
    smac_ = rhs.smac_;
    dmac_ = rhs.dmac_;
}

VmInterface::ServiceVlan::ServiceVlan(uint16_t tag, const std::string &vrf_name,
                                      const Ip4Address &addr,
                                      const Ip6Address &addr6,
                                      const MacAddress &smac,
                                      const MacAddress &dmac) :
    ListEntry(), tag_(tag), vrf_name_(vrf_name), addr_(addr), old_addr_(addr),
    addr6_(addr6), old_addr6_(addr6),
    smac_(smac), dmac_(dmac), vrf_(NULL, this), label_(MplsTable::kInvalidLabel)
    , v4_rt_installed_(false), v6_rt_installed_(false), del_add_(false) {
}

VmInterface::ServiceVlan::~ServiceVlan() {
}

bool VmInterface::ServiceVlan::operator() (const ServiceVlan &lhs,
                                           const ServiceVlan &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::ServiceVlan::IsLess(const ServiceVlan *rhs) const {
    return tag_ < rhs->tag_;
}

void VmInterface::ServiceVlan::Activate(VmInterface *interface,
                                        bool force_update,
                                        bool old_ipv4_active,
                                        bool old_ipv6_active) const {
    InterfaceTable *table =
        static_cast<InterfaceTable *>(interface->get_table());
    VrfEntry *vrf = table->FindVrfRef(vrf_name_);
    if (!vrf) {
        /* If vrf is delete marked VMI will eventually get delete of link which
         * will trigger ServiceVlan::DeActivate */
        return;
    }

    //Change in VRF, delete and readd the entry
    if (vrf_.get() != vrf) {
        DeActivate(interface);
        vrf_ = vrf;
    }

    if (label_ == MplsTable::kInvalidLabel) {
        Agent *agent = interface->agent();
        VlanNH::Create(interface->GetUuid(), tag_, vrf_name_, smac_, dmac_);
        VrfAssignTable::CreateVlan(interface->GetUuid(), vrf_name_, tag_);
        // Assign label_ from vlan NH db entry
        VlanNHKey key(interface->GetUuid(), tag_);
        const NextHop *nh = static_cast<const NextHop *>
            (agent->nexthop_table()->FindActiveEntry(&key));
        label_ = nh->mpls_label()->label();
    }

    if (!interface->ipv4_active() && !interface->ipv6_active() &&
        (old_ipv4_active || old_ipv6_active)) {
        V4RouteDelete(interface->peer());
        V6RouteDelete(interface->peer());
    }

    if (!old_ipv4_active && !old_ipv6_active &&
        (interface->ipv4_active() || interface->ipv6_active())) {
        installed_ = false;
    }

    if (installed_ && force_update == false)
        return;

    interface->ServiceVlanRouteAdd(*this, force_update);
    installed_ = true;
}

void VmInterface::ServiceVlan::V4RouteDelete(const Peer *peer) const {
    if (v4_rt_installed_) {
        InetUnicastAgentRouteTable::Delete(peer, vrf_->GetName(), old_addr_,
                                           Address::kMaxV4PrefixLen);
        v4_rt_installed_ = false;
        old_addr_ = addr_;
    }
}

void VmInterface::ServiceVlan::V6RouteDelete(const Peer *peer) const {
    if (v6_rt_installed_) {
        InetUnicastAgentRouteTable::Delete(peer, vrf_->GetName(), addr6_,
                                           Address::kMaxV6PrefixLen);
        v6_rt_installed_ = false;
        old_addr6_ = addr6_;
    }
}

void VmInterface::ServiceVlan::DeActivate(VmInterface *interface) const {
    if (label_ != MplsTable::kInvalidLabel) {
        VrfAssignTable::DeleteVlan(interface->GetUuid(), tag_);
        interface->ServiceVlanRouteDel(*this);
        label_ = MplsTable::kInvalidLabel;
        VlanNH::Delete(interface->GetUuid(), tag_);
        vrf_ = NULL;
    }
    del_add_ = false;
    installed_ = false;
    return;
}

void VmInterface::ServiceVlanList::Insert(const ServiceVlan *rhs) {
    list_.insert(*rhs);
}

void VmInterface::ServiceVlanList::Update(const ServiceVlan *lhs,
                                          const ServiceVlan *rhs) {
    if (lhs->vrf_name_ != rhs->vrf_name_) {
        lhs->vrf_name_ = rhs->vrf_name_;
        lhs->del_add_ = true;
    }

    if (lhs->addr_ != rhs->addr_) {
        lhs->addr_ = rhs->addr_;
        lhs->del_add_ = true;
    }

    if (lhs->addr6_ != rhs->addr6_) {
        lhs->addr6_ = rhs->addr6_;
        lhs->del_add_ = true;
    }

    if (lhs->smac_ != rhs->smac_) {
        lhs->smac_ = rhs->smac_;
        lhs->del_add_ = true;
    }

    if (lhs->dmac_ != rhs->dmac_) {
        lhs->dmac_ = rhs->dmac_;
        lhs->del_add_ = true;
    }

    lhs->set_del_pending(false);
}

void VmInterface::ServiceVlanList::Remove(ServiceVlanSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::ServiceVlanRouteAdd(const ServiceVlan &entry,
                                      bool force_update) {
    if (vrf_.get() == NULL ||
        vn_.get() == NULL) {
        return;
    }

    if (!ipv4_active_ && !ipv6_active_) {
        return;
    }

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    // With IRB model, add L2 Receive route for SMAC and DMAC to ensure
    // packets from service vm go thru routing
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    table->AddBridgeReceiveRoute(peer_.get(), entry.vrf_->GetName(),
                                 0, entry.dmac_, vn()->GetName());
    table->AddBridgeReceiveRoute(peer_.get(), entry.vrf_->GetName(),
                                 0, entry.smac_, vn()->GetName());
    VnListType vn_list;
    vn_list.insert(vn()->GetName());
    if (force_update ||
        (!entry.v4_rt_installed_ && !entry.addr_.is_unspecified())) {
        PathPreference path_preference;
        SetServiceVlanPathPreference(&path_preference, entry.addr_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (peer_.get(), entry.vrf_->GetName(), entry.addr_,
             Address::kMaxV4PrefixLen, GetUuid(), entry.tag_, entry.label_,
             vn_list, sg_id_list, path_preference);
        entry.v4_rt_installed_ = true;
    }
    if ((!entry.v6_rt_installed_ && !entry.addr6_.is_unspecified()) ||
        force_update) {
        PathPreference path_preference;
        SetServiceVlanPathPreference(&path_preference, entry.addr6_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (peer_.get(), entry.vrf_->GetName(), entry.addr6_,
             Address::kMaxV6PrefixLen, GetUuid(), entry.tag_, entry.label_,
             vn_list, sg_id_list, path_preference);
        entry.v6_rt_installed_ = true;
    }

    entry.installed_ = true;
    return;
}

void VmInterface::ServiceVlanRouteDel(const ServiceVlan &entry) {
    if (entry.installed_ == false) {
        return;
    }

    entry.V4RouteDelete(peer_.get());
    entry.V6RouteDelete(peer_.get());

    // Delete the L2 Recive routes added for smac_ and dmac_
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (entry.vrf_->GetBridgeRouteTable());
    if (table) {
        table->Delete(peer_.get(), entry.vrf_->GetName(), entry.dmac_, 0);
        table->Delete(peer_.get(), entry.vrf_->GetName(), entry.smac_, 0);
    }
    entry.installed_ = false;
    return;
}

void VmInterface::UpdateServiceVlan(bool force_update, bool policy_change,
                                    bool old_ipv4_active,
                                    bool old_ipv6_active) {
    ServiceVlanSet::iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        if (prev->del_pending_ || prev->del_add_) {
            prev->DeActivate(this);
            if (prev->del_pending_) {
                service_vlan_list_.list_.erase(prev);
            }
        }
    }

    it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        prev->Activate(this, force_update, old_ipv4_active,
                       old_ipv6_active);
    }
}

void VmInterface::DeleteServiceVlan() {
    ServiceVlanSet::iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        prev->DeActivate(this);
        if (prev->del_pending_) {
            service_vlan_list_.list_.erase(prev);
        }
    }
} 

////////////////////////////////////////////////////////////////////////////
// VRF assign rule routines
////////////////////////////////////////////////////////////////////////////
VmInterface::VrfAssignRule::VrfAssignRule():
    ListEntry(), id_(0), vrf_name_(" "), ignore_acl_(false) {
}

VmInterface::VrfAssignRule::VrfAssignRule(const VrfAssignRule &rhs):
    ListEntry(rhs.installed_, rhs.del_pending_), id_(rhs.id_),
    vrf_name_(rhs.vrf_name_), ignore_acl_(rhs.ignore_acl_),
    match_condition_(rhs.match_condition_) {
}

VmInterface::VrfAssignRule::VrfAssignRule(uint32_t id,
    const autogen::MatchConditionType &match_condition, 
    const std::string &vrf_name,
    bool ignore_acl):
    ListEntry(), id_(id), vrf_name_(vrf_name),
    ignore_acl_(ignore_acl), match_condition_(match_condition) {
}

VmInterface::VrfAssignRule::~VrfAssignRule() {
}

bool VmInterface::VrfAssignRule::operator() (const VrfAssignRule &lhs,
                                             const VrfAssignRule &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::VrfAssignRule::IsLess(const VrfAssignRule *rhs) const {
    return id_ < rhs->id_;
}

void VmInterface::VrfAssignRuleList::Insert(const VrfAssignRule *rhs) {
    list_.insert(*rhs);
}

void VmInterface::VrfAssignRuleList::Update(const VrfAssignRule *lhs,
                                            const VrfAssignRule *rhs) {
    lhs->set_del_pending(false);
    lhs->match_condition_ = rhs->match_condition_;
    lhs->ignore_acl_ = rhs->ignore_acl_;
    lhs->vrf_name_ = rhs->vrf_name_;
}

void VmInterface::VrfAssignRuleList::Remove(VrfAssignRuleSet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateVrfAssignRule() {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    //Erase all delete marked entry
    VrfAssignRuleSet::iterator it = vrf_assign_rule_list_.list_.begin();
    while (it != vrf_assign_rule_list_.list_.end()) {
        VrfAssignRuleSet::iterator prev = it++;
        if (prev->del_pending_) {
            vrf_assign_rule_list_.list_.erase(prev);
        }
    }

    if (vrf_assign_rule_list_.list_.size() == 0 && 
        vrf_assign_acl_.get() != NULL) {
        DeleteVrfAssignRule();
        return;
    }

    if (vrf_assign_rule_list_.list_.size() == 0) {
        return;
    }

    AclSpec acl_spec;
    acl_spec.acl_id = uuid_;
    //Create the ACL
    it = vrf_assign_rule_list_.list_.begin();
    uint32_t id = 0;
    for (;it != vrf_assign_rule_list_.list_.end();it++) {
        //Go thru all match condition and create ACL entry
        AclEntrySpec ace_spec;
        ace_spec.id = id++;
        if (ace_spec.Populate(&(it->match_condition_)) == false) {
            continue;
        }
        /* Add both v4 and v6 rules regardless of whether interface is
         * ipv4_active_/ipv6_active_
         */
        ActionSpec vrf_translate_spec;
        vrf_translate_spec.ta_type = TrafficAction::VRF_TRANSLATE_ACTION;
        vrf_translate_spec.simple_action = TrafficAction::VRF_TRANSLATE;
        vrf_translate_spec.vrf_translate.set_vrf_name(it->vrf_name_);
        vrf_translate_spec.vrf_translate.set_ignore_acl(it->ignore_acl_);
        ace_spec.action_l.push_back(vrf_translate_spec);
        acl_spec.acl_entry_specs_.push_back(ace_spec);
    }

    DBRequest req;
    AclKey *key = new AclKey(acl_spec.acl_id);
    AclData *data = new AclData(agent, NULL, acl_spec);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    agent->acl_table()->Process(req);

    AclKey entry_key(uuid_);
    AclDBEntry *acl = static_cast<AclDBEntry *>(
        agent->acl_table()->FindActiveEntry(&entry_key));
    assert(acl);
    vrf_assign_acl_ = acl;
}

void VmInterface::DeleteVrfAssignRule() {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    VrfAssignRuleSet::iterator it = vrf_assign_rule_list_.list_.begin();
    while (it != vrf_assign_rule_list_.list_.end()) {
        VrfAssignRuleSet::iterator prev = it++;
        if (prev->del_pending_) {
            vrf_assign_rule_list_.list_.erase(prev);
        }
    }

    if (vrf_assign_acl_ != NULL) {
        vrf_assign_acl_ = NULL;
        DBRequest req;
        AclKey *key = new AclKey(uuid_);
        req.oper = DBRequest::DB_ENTRY_DELETE;
        req.key.reset(key);
        req.data.reset(NULL);
        agent->acl_table()->Process(req);
    }
}

////////////////////////////////////////////////////////////////////////////
// Bridge Domain List
////////////////////////////////////////////////////////////////////////////
void VmInterface::BridgeDomainList::Insert(const BridgeDomain *rhs) {
    list_.insert(*rhs);
}

void VmInterface::BridgeDomainList::Update(const BridgeDomain *lhs,
                                           const BridgeDomain *rhs) {
}

void VmInterface::BridgeDomainList::Remove(BridgeDomainEntrySet::iterator &it) {
    it->set_del_pending(true);
}

void VmInterface::UpdateBridgeDomain() {
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    BridgeDomainEntrySet::iterator it = bridge_domain_list_.list_.begin();
    while (it != bridge_domain_list_.list_.end()) {
        if (it->del_pending_ == false) {
            BridgeDomainKey key(it->uuid_);
            it->bridge_domain_ = static_cast<const BridgeDomainEntry *>(
                  table->agent()->bridge_domain_table()->FindActiveEntry(&key));
            if (it->bridge_domain_->vrf() == NULL) {
                //Ignore bridge domain without VRF
                it->del_pending_ = true;
            }
        }
        it++;
    }

    DeleteBridgeDomain();

    if (bridge_domain_list_.list_.size() == 0) {
        pbb_interface_ = false;
    }
}

void VmInterface::DeleteBridgeDomain() {
    BridgeDomainEntrySet::iterator it = bridge_domain_list_.list_.begin();
    while (it != bridge_domain_list_.list_.end()) {
        BridgeDomainEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            bridge_domain_list_.list_.erase(prev);
        }
    }
}
