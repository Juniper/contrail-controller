/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sys/types.h>
#include <net/ethernet.h>
#include <boost/uuid/uuid_io.hpp>
#include <boost/algorithm/string.hpp>

#include "base/logging.h"
#include "db/db.h"
#include "db/db_entry.h"
#include "db/db_table.h"
#include "ifmap/ifmap_node.h"
#include "net/address_util.h"

#include <cfg/cfg_init.h>
#include <cmn/agent.h>
#include <init/agent_param.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/config_manager.h>
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
#include <oper/inet_unicast_route.h>
#include <oper/physical_device_vn.h>
#include <oper/ecmp_load_balance.h>
#include <oper/global_vrouter.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/qos_config.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>
#include <oper/bridge_domain.h>

#include <vnc_cfg_types.h>
#include <oper/agent_sandesh.h>
#include <oper/sg.h>
#include <oper/bgp_as_service.h>
#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>
#include <bgp_schema_types.h>
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include <filter/acl.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

/////////////////////////////////////////////////////////////////////////////
// Routines to manage VmiToPhysicalDeviceVnTree
/////////////////////////////////////////////////////////////////////////////
VmiToPhysicalDeviceVnData::VmiToPhysicalDeviceVnData
(const boost::uuids::uuid &dev, const boost::uuids::uuid &vn) :
    dev_(dev), vn_(vn) {
}

VmiToPhysicalDeviceVnData::~VmiToPhysicalDeviceVnData() {
}

void InterfaceTable::UpdatePhysicalDeviceVnEntry(const boost::uuids::uuid &vmi,
                                                 boost::uuids::uuid &dev,
                                                 boost::uuids::uuid &vn,
                                                 IFMapNode *vn_node) {
    VmiToPhysicalDeviceVnTree::iterator iter =
        vmi_to_physical_device_vn_tree_.find(vmi);
    if (iter == vmi_to_physical_device_vn_tree_.end()) {
        vmi_to_physical_device_vn_tree_.insert
            (make_pair(vmi,VmiToPhysicalDeviceVnData(nil_uuid(), nil_uuid())));
        iter = vmi_to_physical_device_vn_tree_.find(vmi);
    }

    if (iter->second.dev_ != dev || iter->second.vn_ != vn) {
        agent()->physical_device_vn_table()->DeleteConfigEntry
            (vmi, iter->second.dev_, iter->second.vn_);
    }

    iter->second.dev_ = dev;
    iter->second.vn_ = vn;
    agent()->physical_device_vn_table()->AddConfigEntry(vmi, dev, vn);
}

void InterfaceTable::DelPhysicalDeviceVnEntry(const boost::uuids::uuid &vmi) {
    VmiToPhysicalDeviceVnTree::iterator iter =
        vmi_to_physical_device_vn_tree_.find(vmi);
    if (iter == vmi_to_physical_device_vn_tree_.end())
        return;

    agent()->physical_device_vn_table()->DeleteConfigEntry
        (vmi, iter->second.dev_, iter->second.vn_);
    vmi_to_physical_device_vn_tree_.erase(iter);
}

/////////////////////////////////////////////////////////////////////////////
// VM Port Key routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceKey::VmInterfaceKey(AgentKey::DBSubOperation sub_op,
                   const boost::uuids::uuid &uuid, const std::string &name) :
    InterfaceKey(sub_op, Interface::VM_INTERFACE, uuid, name, false) {
}

Interface *VmInterfaceKey::AllocEntry(const InterfaceTable *table) const {
    return new VmInterface(uuid_);
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
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
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
    local_preference_(VmInterface::INVALID), oper_dhcp_options_(),
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
        UpdateL3(old_ipv4_active, old_vrf, old_addr, old_ethernet_tag, force_update,
                 policy_change, old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen, old_dhcp_addr);
    } else {
        DeleteL3(old_ipv4_active, old_vrf, old_addr, old_need_linklocal_ip,
                 old_ipv6_active, old_v6_addr,
                 old_subnet, old_subnet_plen, old_ethernet_tag, old_dhcp_addr,
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
                                              GetUuid(), ""), true,
                                              InterfaceNHFlags::INET4,
                                              vm_mac_);
        flow_key_nh_ = static_cast<const NextHop *>(
                agent->nexthop_table()->FindActiveEntry(&key));
        return;
    }

    //L2 mode is identified if layer3_forwarding is diabled.
    InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                          GetUuid(), ""), true,
                                          InterfaceNHFlags::BRIDGE,
                                          vm_mac_);
    flow_key_nh_ = static_cast<const NextHop *>(
            agent->nexthop_table()->FindActiveEntry(&key));
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
    table->AddMacVmBindingRoute(agent->mac_vm_binding_peer(),
                                vrf_->GetName(),
                                vm_mac_,
                                this);
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
                                   old_vrf->GetName(), vm_mac_,
                                   this);
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
                                     force_update||policy_change,
                                     old_vrf, old_dhcp_addr);
        }
        UpdateIpv4InstanceIp(force_update, policy_change, false,
                             old_ethernet_tag, old_vrf);
        UpdateFloatingIp(force_update, policy_change, false, old_ethernet_tag);
        UpdateAliasIp(force_update, policy_change);
        UpdateResolveRoute(old_ipv4_active, force_update, policy_change, 
                           old_vrf, old_subnet, old_subnet_plen);
    }
    if (ipv6_active_) {
        UpdateIpv6InstanceIp(force_update, policy_change, false, old_ethernet_tag);
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
    // Process following deletes only if any of old ipv4 or ipv6
    // was active
    if ((old_ipv4_active || old_ipv6_active)) {
        DeleteFloatingIp(false, old_ethernet_tag);
        DeleteAliasIp();
        DeleteServiceVlan();
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

    InterfaceNH::CreateL3VmInterfaceNH(GetUuid(),
                                       vm_mac_,
                                       vrf_->GetName(),
                                       learning_enabled_);
    InterfaceNHKey key1(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           GetUuid(), ""),
                        true, InterfaceNHFlags::INET4,
                        vm_mac_);
    l3_interface_nh_policy_ =
        static_cast<NextHop *>(agent->nexthop_table()->FindActiveEntry(&key1));
    InterfaceNHKey key2(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           GetUuid(), ""),
                        false, InterfaceNHFlags::INET4,
                        vm_mac_);
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

        InetUnicastAgentRouteTable::AddResolveRoute(peer_.get(), vrf_->GetName(),
                Address::GetIp4SubnetAddress(subnet_, subnet_plen_),
                subnet_plen_, vm_intf_key, vrf_->table_label(),
                policy_enabled_, vn_->GetName(), sg_id_list);
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
                                       bool l2,
                                       uint32_t old_ethernet_tag) {
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
        BridgeAgentRouteTable *l2_table = static_cast<BridgeAgentRouteTable *>(
                vrf_->GetRouteTable(Agent::BRIDGE));

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
                           old_layer3_forwarding, policy_change,
                           Ip4Address(), Ip6Address(),
                           vm_mac_,
                           Ip4Address(0));
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
                               Ip6Address(), old_ethernet_tag,
                               vm_mac_);
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
        InterfaceNH::CreateL2VmInterfaceNH(GetUuid(),
                                           vm_mac_,
                                           vrf_->GetName(),
                                           learning_enabled_, etree_leaf_,
                                           layer2_control_word_);
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           true, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_policy_ = static_cast<NextHop *>(agent->
                                  nexthop_table()->FindActiveEntry(&key));
    }
    if (l2_interface_nh_no_policy_.get() == NULL || force_update) {
        InterfaceNHKey key(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                              GetUuid(), ""),
                           false, InterfaceNHFlags::BRIDGE, vm_mac_);
        l2_interface_nh_no_policy_ = static_cast<NextHop *>(agent->
                                     nexthop_table()->FindActiveEntry(&key));
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
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(),
                             mac, new_ip_addr,
                             ethernet_tag_, NULL);
        table->ResyncVmRoute(peer_.get(), vrf_->GetName(),
                             mac, new_ip6_addr,
                             ethernet_tag_, NULL);
    }

    if (old_bridging && force_update == false)
        return;

    uint32_t label = l2_label_;
    if (pbb_interface()) {
        label = GetPbbLabel();
    }

    if (new_ip_addr.is_unspecified() || layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(),
                mac, this, new_ip_addr,
                label, vn_->GetName(), sg_id_list,
                path_preference, ethernet_tag_, etree_leaf_);
    }

    if (new_ip6_addr.is_unspecified() == false && layer3_forwarding_ == true) {
        table->AddLocalVmRoute(peer_.get(), vrf_->GetName(),
                mac, this, new_ip6_addr,
                label, vn_->GetName(), sg_id_list,
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
// VM Port active status related methods
/////////////////////////////////////////////////////////////////////////////
// Does the VMInterface need a physical device to be present
bool VmInterface::NeedDevice() const {
    bool ret = true;

    if (device_type_ == TOR)
        ret = false;

    if (device_type_ == VM_VLAN_ON_VMI)
        ret = false;

    if (subnet_.is_unspecified() == false) {
        ret = false;
    }

    if (transport_ != TRANSPORT_ETHERNET) {
        ret = false;
    }

    if (rx_vlan_id_ != VmInterface::kInvalidVlanId) {
        ret = false;
    } else {
        // Sanity check. rx_vlan_id is set, make sure tx_vlan_id is also set
        assert(tx_vlan_id_ == VmInterface::kInvalidVlanId);
    }

    return ret;
}

void VmInterface::GetOsParams(Agent *agent) {
    if (NeedDevice()) {
        Interface::GetOsParams(agent);
        return;
    }

    os_index_ = Interface::kInvalidIndex;
    mac_ = agent->vrrp_mac();
    os_oper_state_ = true;
}

// A VM Interface is L3 active under following conditions,
// - If interface is deleted, it is inactive
// - VN, VRF are set
// - If sub_interface VMIs, parent_ should be set
//   (We dont track parent_ and activate sub-interfaces. So, we only check 
//    parent_ is present and not necessarily active)
// - For non-VMWARE hypervisors,
//   The tap interface must be created. This is verified by os_index_
// - MAC address set for the interface
bool VmInterface::IsActive()  const {
    if (IsDeleted()) {
        return false;
    }

    if (!admin_state_) {
        return false;
    }

    // If sub_interface VMIs, parent_vmi_ should be set
    // (We dont track parent_ and activate sub-interfaces. So, we only check 
    //  paremt_vmi is present and not necessarily active)
    //  Check if parent is  not active set the SubInterface also not active
    const VmInterface *parentIntf = static_cast<const VmInterface *>(parent());
    if (device_type_ == VM_VLAN_ON_VMI) {
        if (parentIntf == NULL)
            return false;
        else if (parentIntf->IsActive() == false) {
            return false;
        }
    }

    if (device_type_ == REMOTE_VM_VLAN_ON_VMI) {
        if (tx_vlan_id_ == VmInterface::kInvalidVlanId ||
            rx_vlan_id_ == VmInterface::kInvalidVlanId ||
            parent_.get() == NULL)
            return false;
    }

    if ((vn_.get() == NULL) || (vrf_.get() == NULL)) {
        return false;
    }

    if (!vn_.get()->admin_state()) {
        return false;
    }

    if (NeedDevice() == false) {
        return true;
    }

    if (os_index_ == kInvalidIndex)
        return false;

    if (os_oper_state_ == false)
        return false;

    return mac_set_;
}

bool VmInterface::IsMetaDataIPActive() const {
    if (!layer3_forwarding()) {
        return false;
    }

    if (primary_ip6_addr_.is_unspecified()) {
        if (subnet_.is_unspecified() && primary_ip_addr_.to_ulong() == 0) {
            return false;
        }

        if (subnet_.is_unspecified() == false && parent_ == NULL) {
            return false;
        }
    }

    return IsActive();
}

bool VmInterface::IsMetaDataL2Active() const {
    if (!bridging()) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsIpv4Active() const {
    if (!layer3_forwarding()) {
        return false;
    }

    if (subnet_.is_unspecified() && primary_ip_addr_.to_ulong() == 0) {
        return false;
    }

    if (subnet_.is_unspecified() == false && parent_ == NULL) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsIpv6Active() const {
    if (!layer3_forwarding() || (primary_ip6_addr_.is_unspecified())) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsL2Active() const {
    if (!bridging()) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

//Check if interface transitioned from inactive to active layer 2 forwarding
bool VmInterface::L2Activated(bool old_l2_active) {
    if (old_l2_active == false && l2_active_ == true) {
        return true;
    }
    return false;
}

// check if interface transitioned from inactive to active for bridging
bool VmInterface::BridgingActivated(bool old_bridging) {
    if (old_bridging == false && bridging_ == true) {
        return true;
    }
    return false;
}

//Check if interface transitioned from inactive to active IP forwarding
bool VmInterface::Ipv4Activated(bool old_ipv4_active) {
    if (old_ipv4_active == false && ipv4_active_ == true) {
        return true;
    }
    return false;
}

bool VmInterface::Ipv6Activated(bool old_ipv6_active) {
    if (old_ipv6_active == false && ipv6_active_ == true) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active bridging to inactive state
bool VmInterface::L2Deactivated(bool old_l2_active) {
    if (old_l2_active == true && l2_active_ == false) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active bridging to inactive state
bool VmInterface::BridgingDeactivated(bool old_bridging) {
    if (old_bridging == true && bridging_ == false) {
        return true;
    }
    return false;
}

//Check if interface transitioned from active IP forwarding to inactive state
bool VmInterface::Ipv4Deactivated(bool old_ipv4_active) {
    if (old_ipv4_active == true && ipv4_active_ == false) {
        return true;
    }
    return false;
}

bool VmInterface::Ipv6Deactivated(bool old_ipv6_active) {
    if (old_ipv6_active == true && ipv6_active_ == false) {
        return true;
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Path preference utility methods
/////////////////////////////////////////////////////////////////////////////
// Set path-preference information for the route
void VmInterface::SetPathPreference(PathPreference *pref, bool ecmp,
                                    const IpAddress &dependent_ip) const {
    pref->set_ecmp(ecmp);
    if (local_preference_ != INVALID) {
        pref->set_static_preference(true);
    }
    if (local_preference_ == HIGH || ecmp == true) {
        pref->set_preference(PathPreference::HIGH);
    }
    pref->set_dependent_ip(dependent_ip);
    pref->set_vrf(vrf()->GetName());
}

void VmInterface::SetServiceVlanPathPreference(PathPreference *pref,
                                          const IpAddress &service_ip) const {

    bool ecmp_mode = false;
    IpAddress dependent_ip;

    //Logic for setting ecmp and tracking IP on Service chain route
    //Service vlan route can be active when interface is either
    //IPV4 active or IPV6 active, hence we have to consider both
    //IPV6 and IPV4 IP
    //If Service vlan is for Ipv4 route, then priority is as below
    //1> Service IP for v4
    //3> Primary IP for v4
    if (service_ip.is_v4()) {
        if (service_ip_ != Ip4Address(0)) {
            dependent_ip = service_ip_;
            ecmp_mode = service_ip_ecmp_;
        } else {
            dependent_ip = primary_ip_addr_;
            ecmp_mode = ecmp_;
        }
    }

    //If Service vlan is for Ipv6 route, then priority is as below
    //1> Service IP for v6
    //3> Primary IP for v6
    if (service_ip.is_v6()) {
        if (service_ip6_ != Ip6Address()) {
            dependent_ip = service_ip6_;
            ecmp_mode = service_ip_ecmp6_;
        } else {
            dependent_ip = primary_ip6_addr_;
            ecmp_mode = ecmp6_;
        }
    }

    pref->set_ecmp(ecmp_mode);
    if (local_preference_ != INVALID) {
        pref->set_static_preference(true);
    }
    if (local_preference_ == HIGH) {
        pref->set_preference(PathPreference::HIGH);
    } else {
        pref->set_preference(PathPreference::LOW);
    }

    pref->set_dependent_ip(dependent_ip);
    pref->set_vrf(vrf()->GetName());
}

void VmInterface::CopyEcmpLoadBalance(EcmpLoadBalance &ecmp_load_balance) {
    if (ecmp_load_balance_.use_global_vrouter() == false)
        return ecmp_load_balance.Copy(ecmp_load_balance_);
    return ecmp_load_balance.Copy(agent()->oper_db()->global_vrouter()->
                                  ecmp_load_balance());
}

/////////////////////////////////////////////////////////////////////////////
// Route utility methods
/////////////////////////////////////////////////////////////////////////////
//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmInterface::AddRoute(const std::string &vrf_name, const IpAddress &addr,
                           uint32_t plen, const std::string &dest_vn,
                           bool force_policy, bool ecmp, bool is_local,
                           bool is_health_check_service,
                           const IpAddress &service_ip,
                           const IpAddress &dependent_rt,
                           const CommunityList &communities, uint32_t label) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, ecmp, dependent_rt);

    VnListType vn_list;
    vn_list.insert(dest_vn);
    EcmpLoadBalance ecmp_load_balance;
    CopyEcmpLoadBalance(ecmp_load_balance);
    InetUnicastAgentRouteTable::AddLocalVmRoute(peer_.get(), vrf_name, addr,
                                                 plen, GetUuid(),
                                                 vn_list, label,
                                                 sg_id_list, communities,
                                                 force_policy,
                                                 path_preference, service_ip,
                                                 ecmp_load_balance, is_local,
                                                 is_health_check_service);
    return;
}

void VmInterface::ResolveRoute(const std::string &vrf_name, const Ip4Address &addr,
                               uint32_t plen, const std::string &dest_vn, bool policy) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);
    VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE, GetUuid(), "");

    InetUnicastAgentRouteTable::AddResolveRoute(peer_.get(), vrf_name,
            Address::GetIp4SubnetAddress(addr, plen),
            plen, vm_intf_key, vrf_->table_label(),
            policy, dest_vn, sg_id_list);
}

void VmInterface::DeleteRoute(const std::string &vrf_name,
                              const IpAddress &addr, uint32_t plen) {
    InetUnicastAgentRouteTable::Delete(peer_.get(), vrf_name, addr, plen);
    return;
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods
/////////////////////////////////////////////////////////////////////////////
// DHCP options applicable to the Interface
bool VmInterface::GetInterfaceDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options) const {
    if (oper_dhcp_options().are_dhcp_options_set()) {
        *options = oper_dhcp_options().dhcp_options();
        return true;
    }

    return false;
}

// DHCP options applicable to the Subnet to which the interface belongs
bool VmInterface::GetSubnetDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options,
                  bool ipv6) const {
    if (vn()) {
        const std::vector<VnIpam> &vn_ipam = vn()->GetVnIpam();
        uint32_t index;
        for (index = 0; index < vn_ipam.size(); ++index) {
            if (!ipv6 && vn_ipam[index].IsSubnetMember(primary_ip_addr())) {
                break;
            }
            if (ipv6 && vn_ipam[index].IsSubnetMember(primary_ip6_addr())) {
                break;
            }
        }
        if (index < vn_ipam.size() &&
            vn_ipam[index].oper_dhcp_options.are_dhcp_options_set()) {
            *options = vn_ipam[index].oper_dhcp_options.dhcp_options();
            return true;
        }
    }

    return false;
}

// DHCP options applicable to the Ipam to which the interface belongs
bool VmInterface::GetIpamDhcpOptions(
                  std::vector<autogen::DhcpOptionType> *options,
                  bool ipv6) const {
    if (vn()) {
        std::string ipam_name;
        autogen::IpamType ipam_type;
        if (!ipv6 &&
            vn()->GetIpamData(primary_ip_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
        if (ipv6 &&
            vn()->GetIpamData(primary_ip6_addr(), &ipam_name, &ipam_type)) {
            *options = ipam_type.dhcp_option_list.dhcp_option;
            return true;
        }
    }

    return false;
}

const MacAddress&
VmInterface::GetIpMac(const IpAddress &ip, uint8_t plen) const {
    AllowedAddressPairSet::const_iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        if (it->addr_ == ip && it->plen_ == plen &&
            it->mac_ != MacAddress::kZeroMac) {
            return it->mac_;
        }
        it++;
    }
    return vm_mac_;
}

bool VmInterface::WaitForTraffic() const {
    // do not continue if the interface is inactive or if the VRF is deleted
    if (IsActive() == false || vrf_->IsDeleted()) {
        return false;
    }

    //Get the instance ip route and its corresponding traffic seen status
    InetUnicastRouteKey rt_key(peer_.get(), vrf_->GetName(),
                               primary_ip_addr_, 32);
    const InetUnicastRouteEntry *rt =
        static_cast<const InetUnicastRouteEntry *>(
        vrf_->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
     if (!rt) {
         return false;
     }

     if (rt->FindPath(peer_.get()) == false) {
         return false;
     }

     return rt->FindPath(peer_.get())->path_preference().wait_for_traffic();
}

IpAddress VmInterface::GetServiceIp(const IpAddress &vm_ip) const {
    IpAddress ip;
    if (vm_ip.is_v4()) {
        ip = Ip4Address(0);
    } else if (vm_ip.is_v6()) {
        ip = Ip6Address();
    }
    if (vn_.get() == NULL) {
        return ip;
    }

    const VnIpam *ipam = NULL;
    if (subnet_.is_unspecified()) {
        ipam = vn_->GetIpam(vm_ip);
    } else {
        ipam = vn_->GetIpam(subnet_);
    }

    if (ipam) {
        if ((vm_ip.is_v4() && ipam->dns_server.is_v4()) ||
            (vm_ip.is_v6() && ipam->dns_server.is_v6())) {
            return ipam->dns_server;
        }
    }
    return ip;
}

// Copy the SG List for VM Interface. Used to add route for interface
void VmInterface::CopySgIdList(SecurityGroupList *sg_id_list) const {
    SecurityGroupEntrySet::const_iterator it;
    for (it = sg_list_.list_.begin(); it != sg_list_.list_.end(); ++it) {
        if (it->del_pending_)
            continue;
        if (it->sg_.get() == NULL)
            continue;
        sg_id_list->push_back(it->sg_->GetSgId());
    }
}

uint32_t VmInterface::GetServiceVlanLabel(const VrfEntry *vrf) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->vrf_.get() == vrf) {
            return it->label_;
        }
        it++;
    }
    return 0;
}

uint32_t VmInterface::GetServiceVlanTag(const VrfEntry *vrf) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->vrf_.get() == vrf) {
            return it->tag_;
        }
        it++;
    }
    return 0;
}

const VrfEntry* VmInterface::GetServiceVlanVrf(uint16_t vlan_tag) const {
    ServiceVlanSet::const_iterator it = service_vlan_list_.list_.begin();
    while (it != service_vlan_list_.list_.end()) {
        if (it->tag_ == vlan_tag) {
            return it->vrf_.get();
        }
        it++;
    }
    return NULL;
}

bool VmInterface::HasFloatingIp(Address::Family family) const {
    if (family == Address::INET) {
        return floating_ip_list_.v4_count_ > 0;
    } else {
        return floating_ip_list_.v6_count_ > 0;
    }
}

bool VmInterface::HasFloatingIp() const {
    return floating_ip_list_.list_.size() != 0;
}

bool VmInterface::IsFloatingIp(const IpAddress &ip) const {
    VmInterface::FloatingIpSet::const_iterator it =
        floating_ip_list_.list_.begin();
    while(it != floating_ip_list_.list_.end()) {
        if ((*it).floating_ip_ == ip) {
            return true;
        }
        it++;
    }
    return false;
}

VrfEntry *VmInterface::GetAliasIpVrf(const IpAddress &ip) const {
    // Look for matching Alias IP
    VmInterface::AliasIpSet::const_iterator it =
        alias_ip_list_.list_.begin();
    for (; it != alias_ip_list_.list_.end(); ++it) {
        if (it->vrf_.get() == NULL) {
            continue;
        }

        if (ip == it->alias_ip_) {
            return it->vrf_.get();
        }
    }
    return NULL;
}

void VmInterface::InsertHealthCheckInstance(HealthCheckInstance *hc_inst) {
    std::pair<HealthCheckInstanceSet::iterator, bool> ret;
    ret = hc_instance_set_.insert(hc_inst);
    assert(ret.second);
}

void VmInterface::DeleteHealthCheckInstance(HealthCheckInstance *hc_inst) {
    std::size_t ret = hc_instance_set_.erase(hc_inst);
    assert(ret != 0);
}

const VmInterface::HealthCheckInstanceSet &
VmInterface::hc_instance_set() const {
    return hc_instance_set_;
}

bool VmInterface::IsHealthCheckEnabled() const {
    return hc_instance_set_.size() != 0;
}

// Match the Health-Check instance for a packet from VM-Interface
// A packet from vmi is assumed to be response for health-check request from
// vhost0
const HealthCheckInstance *VmInterface::GetHealthCheckFromVmiFlow
(const IpAddress &sip, const IpAddress &dip, uint8_t proto,
 uint16_t sport) const {
    HealthCheckInstanceSet::const_iterator it = hc_instance_set_.begin();
    while (it != hc_instance_set_.end()) {
        const HealthCheckInstance *hc_instance = *it;
        it++;

        // Match ip-proto and health-check port
        const HealthCheckService *hc_service = hc_instance->service();
        if (hc_service == NULL)
            continue;

        if (hc_service->ip_proto() != proto)
            continue;

        if (hc_service->url_port() != sport)
            continue;

        // The source-ip and destination-ip can be matched from MetaDataIp
        // allocated for HealthCheck
        const MetaDataIp *mip = hc_instance->ip();
        if (mip == NULL)
            continue;

        if (mip->destination_ip() != sip)
            continue;

        if (mip->service_ip() != dip)
            continue;

        return hc_instance;
    }

    return NULL;
}
const string VmInterface::GetAnalyzer() const {
    if (mirror_entry()) {
        return mirror_entry()->GetAnalyzerName();
    } else {
        return std::string();
    }
}

const Peer *VmInterface::peer() const { 
    return peer_.get();
}

bool VmInterface::IsFatFlow(uint8_t protocol, uint16_t port) const {
    if (fat_flow_list_.list_.find(FatFlowEntry(protocol, port)) !=
                fat_flow_list_.list_.end()) {
        return true;
    }
    return false;
}

const MacAddress& VmInterface::GetVifMac(const Agent *agent) const {
    if (parent()) {
        if (device_type_ == VM_VLAN_ON_VMI) {
            const VmInterface *vmi =
                static_cast<const VmInterface *>(parent_.get());
            return vmi->GetVifMac(agent);
        }
        return parent()->mac();
    } else {
        return agent->vrrp_mac();
    }
}

bool VmInterface::InstallBridgeRoutes() const {
    return (l2_active_ & is_hc_active_);
}

// Policy is disabled only if user explicitly sets disable policy.
// If user changes to disable policy. only policy will be enabled in case of
// link local services & BGP as a service.
bool VmInterface::PolicyEnabled() const {
    if (disable_policy_) {
        return false;
    }
    return true;
}

// VN is in VXLAN mode if,
// - Tunnel type computed is VXLAN and
// - vxlan_id_ set in VN is non-zero
bool VmInterface::IsVxlanMode() const {
    if (TunnelType::ComputeType(TunnelType::AllType()) != TunnelType::VXLAN)
        return false;

    return vxlan_id_ != 0;
}

void VmInterface::BuildIpStringList(Address::Family family,
                                    std::vector<std::string> *vect) const {
    InstanceIpSet list;
    if (family == Address::INET) {
        list = instance_ipv4_list_.list_;
    } else {
        list = instance_ipv6_list_.list_;
    }
    InstanceIpSet::iterator it = list.begin();
    while (it != list.end()) {
        const VmInterface::InstanceIp &rt = *it;
        it++;
        vect->push_back(rt.ip_.to_string());
    }
}

uint32_t VmInterface::GetIsid() const {
    BridgeDomainEntrySet::const_iterator it = bridge_domain_list_.list_.begin();
    for (; it != bridge_domain_list_.list_.end(); it++) {
        return it->bridge_domain_->isid();
    }
    return kInvalidIsid;
}

uint32_t VmInterface::GetPbbVrf() const {
    BridgeDomainEntrySet::const_iterator it = bridge_domain_list_.list_.begin();
    for (; it != bridge_domain_list_.list_.end(); it++) {
        if (it->bridge_domain_->vrf()) {
            return it->bridge_domain_->vrf()->vrf_id();
        }
    }
    return VrfEntry::kInvalidIndex;
}

uint32_t VmInterface::GetPbbLabel() const {
    BridgeDomainEntrySet::const_iterator it = bridge_domain_list_.list_.begin();
    for (; it != bridge_domain_list_.list_.end(); it++) {
        if (it->bridge_domain_->vrf()) {
            return it->bridge_domain_->vrf()->table_label();
        }
    }
    return MplsTable::kInvalidLabel;
}

/////////////////////////////////////////////////////////////////////////////
// Metadata related routines
/////////////////////////////////////////////////////////////////////////////
Ip4Address VmInterface::mdata_ip_addr() const {
    if (mdata_ip_.get() == NULL) {
        return Ip4Address(0);
    }

    return mdata_ip_->GetLinkLocalIp();
}

MetaDataIp *VmInterface::GetMetaDataIp(const Ip4Address &ip) const {
    MetaDataIpMap::const_iterator it = metadata_ip_map_.find(ip);
    if (it != metadata_ip_map_.end()) {
        return it->second;
    }

    return NULL;
}

// Add meta-data route if linklocal_ip is needed
void VmInterface::UpdateMetadataRoute(bool old_metadata_ip_active,
                                      VrfEntry *old_vrf) {
    if (metadata_ip_active_ == false || old_metadata_ip_active == true)
        return;

    if (!need_linklocal_ip_) {
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    Agent *agent = table->agent();
    if (mdata_ip_.get() == NULL) {
        mdata_ip_.reset(new MetaDataIp(agent->metadata_ip_allocator(),
                                       this, id()));
    }
    //mdata_ip_->set_nat_src_ip(Ip4Address(METADATA_IP_ADDR));
    mdata_ip_->set_active(true);
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

// Delete MPLS Label for Layer3 routes
void VmInterface::DeleteL3MplsLabel() {
    if (label_ == MplsTable::kInvalidLabel) {
        return;
    }

    label_ = MplsTable::kInvalidLabel;
    UpdateMetaDataIpInfo();
}

void VmInterface::DeleteL3TunnelId() {
    if (!metadata_ip_active_) {
        DeleteL3MplsLabel();
    }
}

void VmInterface::InsertMetaDataIpInfo(MetaDataIp *mip) {
    std::pair<MetaDataIpMap::iterator, bool> ret;
    ret = metadata_ip_map_.insert(std::pair<Ip4Address, MetaDataIp*>
                                  (mip->GetLinkLocalIp(), mip));
    assert(ret.second);
}

void VmInterface::DeleteMetaDataIpInfo(MetaDataIp *mip) {
    std::size_t ret = metadata_ip_map_.erase(mip->GetLinkLocalIp());
    assert(ret != 0);
}

void VmInterface::UpdateMetaDataIpInfo() {
    MetaDataIpMap::iterator it = metadata_ip_map_.begin();
    while (it != metadata_ip_map_.end()) {
        it->second->UpdateInterfaceCb();
        it++;
    }
}

bool VmInterface::CopyIpAddress(Ip4Address &addr) {
    bool ret = false;
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());

    // Support DHCP relay for fabric-ports if IP address is not configured
    do_dhcp_relay_ = (fabric_port_ && addr.to_ulong() == 0 && vrf() &&
                      vrf()->GetName() == table->agent()->fabric_vrf_name());

    if (do_dhcp_relay_) {
        // Set config_seen flag on DHCP SNoop entry
        table->DhcpSnoopSetConfigSeen(name_);
        // IP Address not know. Get DHCP Snoop entry.
        // Also sets the config_seen_ flag for DHCP Snoop entry
        addr = table->GetDhcpSnoopEntry(name_);
        dhcp_addr_ = addr;
    }

    // Retain the old if new IP could not be got
    if (addr.to_ulong() == 0) {
        addr = primary_ip_addr_;
    }

    if (primary_ip_addr_ != addr) {
        primary_ip_addr_ = addr;
        ret = true;
    }

    return ret;
}

void VmInterface::SendTrace(const AgentDBTable *table, Trace event) const {
    InterfaceInfo intf_info;
    intf_info.set_name(name_);
    intf_info.set_index(id_);

    switch(event) {
    case ACTIVATED_IPV4:
        intf_info.set_op("IPV4 Activated");
        break;
    case DEACTIVATED_IPV4:
        intf_info.set_op("IPV4 Deactivated");
        break;
    case ACTIVATED_IPV6:
        intf_info.set_op("IPV6 Activated");
        break;
    case DEACTIVATED_IPV6:
        intf_info.set_op("IPV6 Deactivated");
        break;
    case ACTIVATED_L2:
        intf_info.set_op("L2 Activated");
        break;
    case DEACTIVATED_L2:
        intf_info.set_op("L2 Deactivated");
        break;
    case ADD:
        intf_info.set_op("Add");
        break;
    case DELETE:
        intf_info.set_op("Delete");
        break;

    case FLOATING_IP_CHANGE: {
        intf_info.set_op("Floating IP change");
        std::vector<FloatingIPInfo> fip_list;
        FloatingIpSet::iterator it = floating_ip_list_.list_.begin();
        while (it != floating_ip_list_.list_.end()) {
            const FloatingIp &ip = *it;
            FloatingIPInfo fip;
            fip.set_ip_address(ip.floating_ip_.to_string());
            fip.set_vrf_name(ip.vrf_->GetName());
            fip_list.push_back(fip);
            it++;
        }
        intf_info.set_fip(fip_list);
        break;
    }
    case SERVICE_CHANGE:
        break;
    }

    intf_info.set_ip_address(primary_ip_addr_.to_string());
    if (vm_) {
        intf_info.set_vm(UuidToString(vm_->GetUuid()));
    }
    if (vn_) {
        intf_info.set_vn(vn_->GetName());
    }
    if (vrf_) {
        intf_info.set_vrf(vrf_->GetName());
    }
    intf_info.set_vm_project(UuidToString(vm_project_uuid_));
    OPER_TRACE_ENTRY(Interface,
                     table,
                     intf_info);
}
