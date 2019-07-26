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
#include <oper/tag.h>

#include <filter/acl.h>
#include <filter/policy_set.h>
#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;
VmInterface::IgnoreAddressMap VmInterface::fatflow_ignore_addr_map_ =
    InitIgnoreAddressMap();

const char *VmInterface::kInterface = "interface";
const char *VmInterface::kServiceInterface = "service-interface";
const char *VmInterface::kInterfaceStatic = "interface-static";

/////////////////////////////////////////////////////////////////////////////
// VM-Interface entry routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         bool os_oper_state,
                         const boost::uuids::uuid &logical_router_uuid) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL, os_oper_state,
              logical_router_uuid),
    vm_(NULL, this), vn_(NULL), primary_ip_addr_(0), subnet_bcast_addr_(0),
    primary_ip6_addr_(), vm_mac_(MacAddress::kZeroMac), policy_enabled_(false),
    mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX), cfg_name_(""),
    fabric_port_(true), need_linklocal_ip_(false), drop_new_flows_(false),
    dhcp_enable_(true), do_dhcp_relay_(false), proxy_arp_mode_(PROXY_ARP_NONE),
    vm_name_(), vm_project_uuid_(nil_uuid()), vxlan_id_(0), bridging_(false),
    layer3_forwarding_(true), flood_unknown_unicast_(false),
    mac_set_(false), ecmp_(false), ecmp6_(false), disable_policy_(false),
    tx_vlan_id_(kInvalidVlanId), rx_vlan_id_(kInvalidVlanId), parent_(NULL, this),
    local_preference_(0), oper_dhcp_options_(),
    cfg_igmp_enable_(false), igmp_enabled_(false), max_flows_(0),
    mac_vm_binding_state_(new MacVmBindingState()),
    nexthop_state_(new NextHopState()),
    vrf_table_label_state_(new VrfTableLabelState()),
    metadata_ip_state_(new MetaDataIpState()),
    resolve_route_state_(new ResolveRouteState()),
    interface_route_state_(new VmiRouteState()),
    sg_list_(), tag_list_(), floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    instance_ipv4_list_(true), instance_ipv6_list_(false), fat_flow_list_(),
    vrf_assign_rule_list_(), vm_ip_service_addr_(0),
    device_type_(VmInterface::DEVICE_TYPE_INVALID),
    vmi_type_(VmInterface::VMI_TYPE_INVALID),
    configurer_(0), subnet_(0), subnet_plen_(0), ethernet_tag_(0),
    logical_interface_(nil_uuid()), nova_ip_addr_(0), nova_ip6_addr_(),
    dhcp_addr_(0), metadata_ip_map_(), hc_instance_set_(),
    ecmp_load_balance_(), service_health_check_ip_(), is_vn_qos_config_(false),
    learning_enabled_(false), etree_leaf_(false), layer2_control_word_(false),
    slo_list_(), forwarding_vrf_(NULL), vhostuser_mode_(vHostUserClient),
    is_left_si_(false),
    service_mode_(VmInterface::SERVICE_MODE_ERROR),
    service_intf_type_("") {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
    flow_count_ = 0;
}

VmInterface::VmInterface(const boost::uuids::uuid &uuid,
                         const std::string &name,
                         const Ip4Address &addr, const MacAddress &mac,
                         const std::string &vm_name,
                         const boost::uuids::uuid &vm_project_uuid,
                         uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                         Interface *parent, const Ip6Address &a6,
                         DeviceType device_type, VmiType vmi_type,
                         uint8_t vhostuser_mode, bool os_oper_state,
                         const boost::uuids::uuid &logical_router_uuid) :
    Interface(Interface::VM_INTERFACE, uuid, name, NULL, os_oper_state,
              logical_router_uuid),
    vm_(NULL, this), vn_(NULL), primary_ip_addr_(addr), subnet_bcast_addr_(0),
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
    cfg_igmp_enable_(false), igmp_enabled_(false), max_flows_(0),
    mac_vm_binding_state_(new MacVmBindingState()),
    nexthop_state_(new NextHopState()),
    vrf_table_label_state_(new VrfTableLabelState()),
    metadata_ip_state_(new MetaDataIpState()),
    resolve_route_state_(new ResolveRouteState()),
    interface_route_state_(new VmiRouteState()),
    sg_list_(), tag_list_(),
    floating_ip_list_(), alias_ip_list_(), service_vlan_list_(),
    static_route_list_(), allowed_address_pair_list_(),
    instance_ipv4_list_(true), instance_ipv6_list_(false), fat_flow_list_(),
    vrf_assign_rule_list_(), device_type_(device_type),
    vmi_type_(vmi_type), configurer_(0), subnet_(0),
    subnet_plen_(0), ethernet_tag_(0), logical_interface_(nil_uuid()),
    nova_ip_addr_(0), nova_ip6_addr_(), dhcp_addr_(0), metadata_ip_map_(),
    hc_instance_set_(), service_health_check_ip_(), is_vn_qos_config_(false),
    learning_enabled_(false), etree_leaf_(false), layer2_control_word_(false),
    slo_list_(), forwarding_vrf_(NULL), vhostuser_mode_(vhostuser_mode),
    is_left_si_(false),
    service_mode_(VmInterface::SERVICE_MODE_ERROR),
    service_intf_type_("") {
    metadata_ip_active_ = false;
    metadata_l2_active_ = false;
    ipv4_active_ = false;
    ipv6_active_ = false;
    l2_active_ = false;
    flow_count_ = 0;
}

VmInterface::~VmInterface() {
    // Release metadata first to ensure metadata_ip_map_ is empty
    metadata_ip_state_.reset(NULL);
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
    if (uuid_ == nil_uuid() && intf.uuid_ == nil_uuid()) {
        return name() < intf.name();
    }

    return uuid_ < intf.uuid_;
}

string VmInterface::ToString() const {
    return "VM-PORT <" + name() + ">";
}

DBEntryBase::KeyPtr VmInterface::GetDBRequestKey() const {
    InterfaceKey *key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, uuid_, name());
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
    table->DeleteDhcpSnoopEntry(name());
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
    bool force_update = false;
    Ip4Address old_subnet = subnet_;
    uint8_t  old_subnet_plen = subnet_plen_;
    bool old_metadata_ip_active = metadata_ip_active_;
    bool old_metadata_l2_active = metadata_l2_active_;

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

    //Update DHCP and DNS flag in Interface Class.
    if (dhcp_enable_) {
        dhcp_enabled_ = true;
        dns_enabled_ = true;
    } else {
        dhcp_enabled_ = false;
        dns_enabled_ = false;
    }

    // Compute service-ip for the interface
    vm_ip_service_addr_ = GetServiceIp(primary_ip_addr()).to_v4();

    // Add/Update L2
    if (l2_active_) {
        UpdateL2();
    }

    // Apply config based on old and new values
    ApplyConfig(old_ipv4_active, old_l2_active, old_ipv6_active,
                old_subnet, old_subnet_plen);

    // Check if Healthcheck service resync is required
    UpdateInterfaceHealthCheckService();
    return ret;
}


// Apply the latest configuration
void VmInterface::ApplyConfig(bool old_ipv4_active, bool old_l2_active,
                              bool old_ipv6_active,
                              const Ip4Address &old_subnet,
                              uint8_t old_subnet_plen) {
    VmInterfaceState::Op l2_force_op = VmInterfaceState::INVALID;
    VmInterfaceState::Op l3_force_op = VmInterfaceState::INVALID;

    // For following intf type we dont generate any things like l2 routes,
    // l3 routes etc
    // VM_SRIOV, VMI_ON_LR
    if ((device_type_ == VmInterface::VM_SRIOV) ||
        (device_type_ == VmInterface::VMI_ON_LR)) {
        l2_force_op = VmInterfaceState::DEL;
        l3_force_op = VmInterfaceState::DEL;
    }

    /////////////////////////////////////////////////////////////////////////
    // PHASE-1 Updates follows.
    //
    // Changes independnt of any state
    // NOTE: Dont move updates below across PHASE-1 block
    /////////////////////////////////////////////////////////////////////////

    // DHCP MAC IP binding
    UpdateState(mac_vm_binding_state_.get(), l2_force_op, l3_force_op);

    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    //Update security group and tag list first so that route can
    //build the tag list and security group list
    sg_list_.UpdateList(agent, this, l2_force_op, l3_force_op);
    tag_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    // Fat flow configuration
    fat_flow_list_.UpdateList(agent, this);

    // Bridge Domain configuration
    bridge_domain_list_.Update(agent, this);
    if (bridge_domain_list_.list_.size() == 0) {
        pbb_interface_ = false;
    }

    // NOTE : The updates are independnt of any state and agent-mode. They
    // must not move beyond this part

    // Need not apply config for TOR VMI as it is more of an inidicative
    // interface. No route addition or NH addition happens for this interface.
    // Also, when parent is not updated for a non-Nova interface, device type
    // remains invalid.
    if ((device_type_ == VmInterface::TOR ||
         device_type_ == VmInterface::DEVICE_TYPE_INVALID) &&
         (old_subnet.is_unspecified() && old_subnet_plen == 0)) {
        // TODO : Should force_op be set to VmInterfaceState::DEL instead?
        return;
    }

    /////////////////////////////////////////////////////////////////////////
    // PHASE-2 Updates follows.
    //
    // Appy changes independent of L2/L3 modes first
    // NOTE: Dont move updates below across PHASE-2 block
    /////////////////////////////////////////////////////////////////////////

    // Update VRF Table Label
    UpdateState(vrf_table_label_state_.get(), l2_force_op, l3_force_op);

    // Update NextHop parameters
    UpdateState(nexthop_state_.get(), l2_force_op, l3_force_op);
    GetNextHopInfo();

    // Add/Update L3 Metadata
    UpdateState(metadata_ip_state_.get(), l2_force_op, l3_force_op);

    /////////////////////////////////////////////////////////////////////////
    // PHASE-3 Updates follows.
    //
    // Updates dependent on l2-active state and healtch-check-state
    // NOTE : Dont move updates below across PHASE-3
    /////////////////////////////////////////////////////////////////////////

    // Dont add any l2-states if l2 is not active
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID) {
        l2_force_op = VmInterfaceState::DEL;
    }
    if (l2_active_ == false || is_hc_active_ == false) {
        l2_force_op = VmInterfaceState::DEL;
    }

    // Add EVPN and L3 route for do_dhcp_relay_ before instance-ip routes
    UpdateState(interface_route_state_.get(), l2_force_op, l3_force_op);

    // Add/Update L3/L2 routes routes resulting from instance-ip
    instance_ipv4_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    // Add/Update L3/L2 routes routes resulting from instance-ip
    instance_ipv6_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    // Update floating-ip related configuration
    floating_ip_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    alias_ip_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    UpdateState(resolve_route_state_.get(), l2_force_op, l3_force_op);

    static_route_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    service_vlan_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    vrf_assign_rule_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    allowed_address_pair_list_.UpdateList(agent, this, l2_force_op,
                                          l3_force_op);
    receive_route_list_.UpdateList(agent, this, l2_force_op, l3_force_op);

    /////////////////////////////////////////////////////////////////////////
    // PHASE-3 Updates follows.
    // Only cleanup and deletes follow below
    //
    // NOTE : Dont move updates below across PHASE-3
    /////////////////////////////////////////////////////////////////////////

    // Del L3 Metadata after deleting L3 information
    DeleteState(metadata_ip_state_.get());

    // Delete NextHop if inactive
    DeleteState(nexthop_state_.get());
    GetNextHopInfo();

    // Remove floating-ip entries marked for deletion
    CleanupFloatingIpList();

    // Remove Alias-ip entries marked for deletion
    CleanupAliasIpList();

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

void VmInterface::UpdateL2() {
    if (device_type() == VmInterface::TOR ||
        device_type() == VmInterface::DEVICE_TYPE_INVALID)
        return;

    int new_vxlan_id = vn_.get() ? vn_->GetVxLanId() : 0;
    if (l2_active_ && ((vxlan_id_ == 0) ||
                       (vxlan_id_ != new_vxlan_id))) {
        vxlan_id_ = new_vxlan_id;
    }
    ethernet_tag_ = IsVxlanMode() ? vxlan_id_ : 0;
}

////////////////////////////////////////////////////////////////////////////
// VmInterface attribute methods
////////////////////////////////////////////////////////////////////////////
bool VmInterface::UpdateState(const VmInterfaceState *state,
                              VmInterfaceState::Op l2_force_op,
                              VmInterfaceState::Op l3_force_op) {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    return state->Update(agent, this, l2_force_op, l3_force_op);
}

bool VmInterface::DeleteState(VmInterfaceState *state) {
    Agent *agent = static_cast<InterfaceTable *>(get_table())->agent();
    bool ret = true;
    if (state->l2_installed_) {
        if (state->DeleteL2(agent, this))
            state->l2_installed_ = false;
        else
            ret = false;
    }

    if (state->l3_installed_) {
        if (state->DeleteL3(agent, this))
            state->l3_installed_ = false;
        else
            ret = false;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// VmInterfaceAttr basic routines
/////////////////////////////////////////////////////////////////////////////
VmInterfaceState::Op VmInterfaceState::RecomputeOp(Op old_op, Op new_op) {
    return (new_op > old_op) ? new_op : old_op;
}

bool VmInterfaceState::Update(const Agent *agent, VmInterface *vmi,
                              Op l2_force_op, Op l3_force_op) const {
    Op l2_op = RecomputeOp(l2_force_op, GetOpL2(agent, vmi));
    Op l3_op = RecomputeOp(l3_force_op, GetOpL3(agent, vmi));

    if ((l2_op == DEL || l2_op == DEL_ADD) && l2_installed_) {
        DeleteL2(agent, vmi);
        l2_installed_ = false;
    }
    if ((l3_op == DEL || l3_op == DEL_ADD) && l3_installed_) {
        DeleteL3(agent, vmi);
        l3_installed_ = false;
    }

    Copy(agent, vmi);

    if (l3_op == ADD || l3_op == DEL_ADD) {
        if (AddL3(agent, vmi))
            l3_installed_ = true;
    }
    if (l2_op == ADD || l2_op == DEL_ADD) {
        if (AddL2(agent, vmi))
            l2_installed_ = true;
    }

    return true;
}

// Force operation to be DEL if del_pending_ is set
VmInterfaceState::Op VmInterface::ListEntry::GetOp(VmInterfaceState::Op op)
    const {
    if (del_pending_ == false)
        return op;

    return VmInterfaceState::RecomputeOp(op, VmInterfaceState::DEL);
}

static bool GetIpActiveState(const IpAddress &ip, const VmInterface *vmi) {
    if (ip.is_v6())
        return vmi->ipv6_active();

    return vmi->ipv4_active();
}

////////////////////////////////////////////////////////////////////////////
// MacVmBinding attribute method
// Adds a path to MacVmBinding is responsible to update flood-dhcp flag
////////////////////////////////////////////////////////////////////////////
MacVmBindingState::MacVmBindingState() :
    VmInterfaceState(), vrf_(NULL), dhcp_enabled_(false) {
}

MacVmBindingState::~MacVmBindingState() {
}

VmInterfaceState::Op MacVmBindingState::GetOpL3(const Agent *agent,
                                                const VmInterface *vmi) const {
    if (vmi->IsActive() == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool MacVmBindingState::DeleteL3(const Agent *agent, VmInterface *vmi) const {
    BridgeAgentRouteTable *table =
            static_cast<BridgeAgentRouteTable *>(vrf_->GetBridgeRouteTable());
    // return if table is already marked for deletion
    if (table == NULL)
        return true;
    table->DeleteMacVmBindingRoute(agent->mac_vm_binding_peer(),
                                   vrf_->GetName(), vmi->vm_mac(), vmi);
    return true;
}

void MacVmBindingState::Copy(const Agent *agent, const VmInterface *vmi) const {
    vrf_ = vmi->vrf();
    dhcp_enabled_ = vmi->dhcp_enable_config();
}

bool MacVmBindingState::AddL3(const Agent *agent, VmInterface *vmi) const {
    BridgeAgentRouteTable *table =
        static_cast<BridgeAgentRouteTable *>(vrf_->GetBridgeRouteTable());
    // flood_dhcp must be set in route if dhcp is disabled for interface
    bool flood_dhcp = !vmi->dhcp_enable_config();
    table->AddMacVmBindingRoute(agent->mac_vm_binding_peer(), vrf_->GetName(),
                                vmi->vm_mac(), vmi, flood_dhcp);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// SecurityGroup routines
// Does not generate any new state. Only holds reference to SG for interface
/////////////////////////////////////////////////////////////////////////////
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

bool VmInterface::SecurityGroupEntryList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    SecurityGroupEntrySet::iterator it = list_.begin();
    while (it != list_.end()) {
        SecurityGroupEntrySet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        if (prev->del_pending()) {
            list_.erase(prev);
        }
    }

    return true;
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry() :
    ListEntry(), VmInterfaceState(), uuid_(nil_uuid()) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry
(const SecurityGroupEntry &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    uuid_(rhs.uuid_) {
}

VmInterface::SecurityGroupEntry::SecurityGroupEntry(const uuid &u) :
    ListEntry(), VmInterfaceState(), uuid_(u) {
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

// Remove reference to SG when list entry is deleted. Note, the SG states are
// not dependent on any interface active state
VmInterfaceState::Op VmInterface::SecurityGroupEntry::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    if (del_pending_)
        return VmInterfaceState::INVALID;

    return VmInterfaceState::ADD;
}

bool VmInterface::SecurityGroupEntry::AddL3(const Agent *agent,
                                            VmInterface *vmi) const {
    if (sg_.get() != NULL)
        return false;

    SgKey sg_key(uuid_);
    sg_ = static_cast<SgEntry *>(agent->sg_table()->FindActiveEntry(&sg_key));
    return true;
}

bool VmInterface::SecurityGroupEntry::DeleteL3(const Agent *agent,
                                               VmInterface *vmi) const {
    sg_ = NULL;
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Fat Flow list routines
// Only updates fat-flow configuraion in interface
// NOTE: Its not derived from VmInterfaceState and also does not generate
// any new states
/////////////////////////////////////////////////////////////////////////////
VmInterface::FatFlowEntry::FatFlowEntry(const uint8_t proto, const uint16_t p,
                                        std::string ignore_addr_value, 
                                        FatFlowPrefixAggregateType in_prefix_aggregate,
                                        IpAddress in_src_prefix, uint8_t in_src_prefix_mask,
                                        uint8_t in_src_aggregate_plen, 
                                        IpAddress in_dst_prefix, uint8_t in_dst_prefix_mask,
                                        uint8_t in_dst_aggregate_plen) :
    protocol(proto), port(p) {
    ignore_address = fatflow_ignore_addr_map_.find(ignore_addr_value)->second;
    prefix_aggregate = in_prefix_aggregate;
    src_prefix = in_src_prefix;
    src_prefix_mask = in_src_prefix_mask;
    src_aggregate_plen = in_src_aggregate_plen;
    dst_prefix = in_dst_prefix;
    dst_prefix_mask = in_dst_prefix_mask;
    dst_aggregate_plen = in_dst_aggregate_plen;
}


VmInterface::FatFlowEntry
VmInterface::FatFlowEntry::MakeFatFlowEntry(const std::string &proto, const int &port,
                                            const std::string &ignore_addr_str,
                                            const std::string &in_src_prefix_str, const int &in_src_prefix_mask,
                                            const int &in_src_aggregate_plen,
                                            const std::string &in_dst_prefix_str, const int &in_dst_prefix_mask,
                                            const int &in_dst_aggregate_plen) {
    uint8_t protocol = (uint8_t) Agent::ProtocolStringToInt(proto);
    IpAddress src_prefix;
    uint8_t src_prefix_mask = 0, src_aggregate_plen = 0;
    IpAddress dst_prefix, empty_prefix, empty_prefix_v6 = IpAddress::from_string("0::0");
    uint8_t dst_prefix_mask = 0, dst_aggregate_plen = 0;
    FatFlowPrefixAggregateType prefix_aggregate = AGGREGATE_NONE;
    FatFlowIgnoreAddressType ignore_address =
                                   fatflow_ignore_addr_map_.find(ignore_addr_str)->second;
    int port_num = port;

    /*
     * Protocol is taken as 1 for both IPv4 & IPv6 and Port no should be 0 for ICMP/ICMPv6,
     * override if we get something else from config
     */
    if ((protocol == IPPROTO_ICMP) || (protocol == IPPROTO_ICMPV6)) {
        protocol = IPPROTO_ICMP;
        port_num = 0;
    }

    if (in_src_prefix_str.length() > 0) {

        src_prefix = IpAddress::from_string(in_src_prefix_str);
        src_prefix_mask = in_src_prefix_mask;
        src_aggregate_plen = in_src_aggregate_plen;
        if (src_prefix.is_v4()) {
            // convert to prefix
            src_prefix = IpAddress(Address::GetIp4SubnetAddress(src_prefix.to_v4(),
                                   src_prefix_mask));
            prefix_aggregate = AGGREGATE_SRC_IPV4;
        } else {
            src_prefix = IpAddress(Address::GetIp6SubnetAddress(src_prefix.to_v6(),
                                   src_prefix_mask));
            prefix_aggregate = AGGREGATE_SRC_IPV6;
        }
    }
    if (in_dst_prefix_str.length() > 0) {
        dst_prefix = IpAddress::from_string(in_dst_prefix_str);
        dst_prefix_mask = in_dst_prefix_mask;
        dst_aggregate_plen = in_dst_aggregate_plen;
        if (dst_prefix.is_v4()) {
            dst_prefix = IpAddress(Address::GetIp4SubnetAddress(dst_prefix.to_v4(),
                                   dst_prefix_mask));
        } else {
            dst_prefix = IpAddress(Address::GetIp6SubnetAddress(dst_prefix.to_v6(),
                                   dst_prefix_mask));
        }

        if (prefix_aggregate == AGGREGATE_NONE) {
            if (dst_prefix.is_v4()) {
                prefix_aggregate = AGGREGATE_DST_IPV4;
            } else {
                prefix_aggregate = AGGREGATE_DST_IPV6;
            }
        } else {
            if (dst_prefix.is_v4()) {
                prefix_aggregate = AGGREGATE_SRC_DST_IPV4;
            } else {
                prefix_aggregate = AGGREGATE_SRC_DST_IPV6;
            }
        }
    }
    if (ignore_address == IGNORE_SOURCE) {
        if ((prefix_aggregate == AGGREGATE_SRC_IPV4) || (prefix_aggregate == AGGREGATE_SRC_IPV6)) {
             src_prefix = empty_prefix;
             src_prefix_mask = 0;
             src_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_NONE;
        } else if (prefix_aggregate == AGGREGATE_SRC_DST_IPV4) {
             src_prefix = empty_prefix;
             src_prefix_mask = 0;
             src_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_DST_IPV4;
        } else if (prefix_aggregate == AGGREGATE_SRC_DST_IPV6) {
             src_prefix = empty_prefix_v6;
             src_prefix_mask = 0;
             src_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_DST_IPV6;
        }
    } else if (ignore_address == IGNORE_DESTINATION) {
        if ((prefix_aggregate == AGGREGATE_DST_IPV4) || (prefix_aggregate == AGGREGATE_DST_IPV6)) {
             dst_prefix = empty_prefix;
             dst_prefix_mask = 0;
             dst_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_NONE;
        } else if (prefix_aggregate == AGGREGATE_SRC_DST_IPV4) {
             dst_prefix = empty_prefix;
             dst_prefix_mask = 0;
             dst_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_SRC_IPV4;
        } else if (prefix_aggregate == AGGREGATE_SRC_DST_IPV6) {
             dst_prefix = empty_prefix_v6;
             dst_prefix_mask = 0;
             dst_aggregate_plen = 0;
             prefix_aggregate = AGGREGATE_SRC_IPV6;
        }
    }

    if ((in_src_prefix_str.length() == 0) && (prefix_aggregate == AGGREGATE_DST_IPV6)) {
         src_prefix = empty_prefix_v6;
    }
    if ((in_dst_prefix_str.length() == 0) && (prefix_aggregate == AGGREGATE_SRC_IPV6)) {
         dst_prefix = empty_prefix_v6;
    }

    VmInterface::FatFlowEntry entry(protocol, port_num,
                                    ignore_addr_str, prefix_aggregate, src_prefix, src_prefix_mask,
                                    src_aggregate_plen, dst_prefix, dst_prefix_mask, dst_aggregate_plen);
    return entry;
}

void VmInterface::FatFlowEntry::print(void) const {
    LOG(ERROR, "Protocol:" << (int) protocol << " Port:" << port << " IgnoreAddr:" << ignore_address
        << " PrefixAggr:" << prefix_aggregate << " SrcPrefix:" << src_prefix.to_string() << "/" << (int) src_prefix_mask
        << " SrcAggrPlen:" << (int) src_aggregate_plen << " DstPrefix:" << dst_prefix.to_string() << "/" << (int) dst_prefix_mask
        << " DstAggrPlen:" << (int) dst_aggregate_plen);
}

void VmInterface::FatFlowList::Insert(const FatFlowEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::FatFlowList::Update(const FatFlowEntry *lhs,
                                      const FatFlowEntry *rhs) {
    lhs->ignore_address = rhs->ignore_address;
    lhs->prefix_aggregate = rhs->prefix_aggregate;
    lhs->src_prefix = rhs->src_prefix;
    lhs->src_prefix_mask = rhs->src_prefix_mask;
    lhs->src_aggregate_plen = rhs->src_aggregate_plen;
    lhs->dst_prefix = rhs->dst_prefix;
    lhs->dst_prefix_mask = rhs->dst_prefix_mask;
    lhs->dst_aggregate_plen = rhs->dst_aggregate_plen;
}

void VmInterface::FatFlowList::Remove(FatFlowEntrySet::iterator &it) {
    it->set_del_pending(true);
}

bool VmInterface::FatFlowList::UpdateList(const Agent *agent,
                                          VmInterface *vmi) {
    FatFlowEntrySet::iterator it = list_.begin();
    while (it != list_.end()) {
        FatFlowEntrySet::iterator prev = it++;
        if (prev->del_pending_) {
            list_.erase(prev);
        }
    }
    return true;
}

void VmInterface::FatFlowList::DumpList(void) const {
    LOG(ERROR, "Dumping FatFlowList:\n");
    for (FatFlowEntrySet::iterator it = list_.begin(); it != list_.end(); it++) {
         it->print();
    }
}

////////////////////////////////////////////////////////////////////////////
// Bridge Domain List
// NOTE: Its not derived from VmInterfaceState and also does not generate
// any new states
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

bool VmInterface::BridgeDomainList::Update(const Agent *agent,
                                           VmInterface *vmi) {
    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    BridgeDomainEntrySet::iterator it = list_.begin();
    while (it != list_.end()) {
        BridgeDomainEntrySet::iterator prev = it++;
        if (prev->del_pending_ == false) {
            BridgeDomainKey key(prev->uuid_);
            prev->bridge_domain_ = static_cast<const BridgeDomainEntry *>
                (table->agent()->bridge_domain_table()->FindActiveEntry(&key));
            // Ignore bridge domain without VRF
            // Interface will get config update again when VRF is created
            if (prev->bridge_domain_->vrf() == NULL) {
                prev->del_pending_ = true;
            }
        }

        if (prev->del_pending_) {
            list_.erase(prev);
        }
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////
// VRF Table label
// Create VRF-NextHop based on interface-type. The NextHop is not explicitly
// deleted. It gets deleted when reference count drops to 0
////////////////////////////////////////////////////////////////////////////
VrfTableLabelState::VrfTableLabelState() : VmInterfaceState() {
}

VrfTableLabelState::~VrfTableLabelState() {
}

VmInterfaceState::Op VrfTableLabelState::GetOpL3(const Agent *agent,
                                                 const VmInterface *vmi) const {
    return VmInterfaceState::ADD;
}

// Take care of only adding VRF Table label. The label will be freed when
// VRF is deleted
bool VrfTableLabelState::AddL3(const Agent *agent, VmInterface *vmi) const {
    if (vmi->vrf() == NULL || vmi->vmi_type() != VmInterface::GATEWAY) {
        return false;
    }

    vmi->vrf()->CreateTableLabel(false, false, false, false);
    return false;
}

////////////////////////////////////////////////////////////////////////////
// NextHop attribute
//
// L2 nexthops:
// These L2 nexthop are used by multicast and bridge. Presence of multicast
// forces it to be present in ipv4 mode(l3-only).
//
// L3 nexthops:
// Also creates L3 interface NH, if layer3_forwarding is set.
// It does not depend on oper state of ip forwarding.
// Needed as health check can disable oper ip_active and will result in flow
// key pointing to L2 interface NH. This has to be avoided as interface still
// points to l3 nh and flow should use same. For this fix it is also required
// that l3 i/f nh is also created on seeing config and not oper state of l3.
// Reason being if vmi(irrespective of health check) is coming up and
// transitioning from ipv4_inactive to ip4_active and during this transition
// a flow is added then flow_key in vmi will return null because l3 config is
// set and interface nh not created yet.
////////////////////////////////////////////////////////////////////////////
NextHopState::NextHopState() : VmInterfaceState() {
}

NextHopState::~NextHopState() {
}

// The nexthops are deleted after all update processing by explicitly
// calling delete. So, dont return DEL operation from here
VmInterfaceState::Op NextHopState::GetOpL2(const Agent *agent,
                                           const VmInterface *vmi) const {
    if (vmi->IsActive() == false)
        return VmInterfaceState::INVALID;
    return VmInterfaceState::ADD;
}

// The nexthops must be deleted after all update processing. So, dont return
// DEL operation from here
VmInterfaceState::Op NextHopState::GetOpL3(const Agent *agent,
                                           const VmInterface *vmi) const {
    if ((vmi->IsActive() == false) || (vmi->layer3_forwarding() == false))
        return VmInterfaceState::INVALID;

    return VmInterfaceState::ADD;
}

bool NextHopState::AddL2(const Agent *agent, VmInterface *vmi) const {
    InterfaceNH::CreateL2VmInterfaceNH(vmi->GetUuid(), vmi->vm_mac(),
                                       vmi->forwarding_vrf()->GetName(),
                                       vmi->learning_enabled(),
                                       vmi->etree_leaf(),
                                       vmi->layer2_control_word(),
                                       vmi->name());

    InterfaceNHKey key1(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           vmi->GetUuid(), vmi->name()),
                        true, InterfaceNHFlags::BRIDGE, vmi->vm_mac());
    l2_nh_policy_ = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key1));

    InterfaceNHKey key2(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           vmi->GetUuid(), vmi->name()),
                        false, InterfaceNHFlags::BRIDGE, vmi->vm_mac());
    l2_nh_no_policy_ = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key2));

    // Update L2 mpls label from nh entry
    if (vmi->policy_enabled()) {
        l2_label_ = l2_nh_policy_->mpls_label()->label();
    } else {
        l2_label_ = l2_nh_no_policy_->mpls_label()->label();
    }

    return true;
}

bool NextHopState::DeleteL2(const Agent *agent, VmInterface *vmi) const {
    // Delete is called independent of interface active-state.
    // Dont delete NH if interface is still active
    if (vmi->IsActive())
        return false;

    l2_nh_policy_.reset();
    l2_nh_no_policy_.reset();
    l2_label_ = MplsTable::kInvalidLabel;
    InterfaceNH::DeleteL2InterfaceNH(vmi->GetUuid(), vmi->vm_mac(),
                                     vmi->name());
    return true;
}

bool NextHopState::AddL3(const Agent *agent, VmInterface *vmi) const {
    InterfaceNH::CreateL3VmInterfaceNH(vmi->GetUuid(), vmi->vm_mac(),
                                       vmi->forwarding_vrf()->GetName(),
                                       vmi->learning_enabled(),
                                       vmi->name());

    InterfaceNH::CreateMulticastVmInterfaceNH(vmi->GetUuid(), vmi->vm_mac(),
                                       vmi->forwarding_vrf()->GetName(),
                                       vmi->name());

    InterfaceNHKey key1(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           vmi->GetUuid(), vmi->name()),
                        true, InterfaceNHFlags::INET4, vmi->vm_mac());
    l3_nh_policy_ = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key1));

    InterfaceNHKey key2(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                           vmi->GetUuid(), vmi->name()),
                        false, InterfaceNHFlags::INET4, vmi->vm_mac());
    l3_nh_no_policy_ = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key2));

    InterfaceNHKey key3(new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                    vmi->GetUuid(), vmi->name()), false,
                                    (InterfaceNHFlags::INET4|
                                     InterfaceNHFlags::MULTICAST),
                                    vmi->vm_mac());
    l3_mcast_nh_no_policy_ = static_cast<NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key3));

    // Update L3 mpls label from nh entry
    if (vmi->policy_enabled()) {
        l3_label_ = l3_nh_policy_->mpls_label()->label();
    } else {
        l3_label_ = l3_nh_no_policy_->mpls_label()->label();
    }

    return true;
}

bool NextHopState::DeleteL3(const Agent *agent, VmInterface *vmi) const {
    // Delete is called independent of interface active-state.
    // Keep NH as long as interface is active and layer3-forwarding is enabled
    // TODO : It should be ok to keep the NH even if l3-forwarding is disabled?
    if (vmi->IsActive() && vmi->layer3_forwarding())
        return false;

    l3_mcast_nh_no_policy_.reset();
    l3_nh_policy_.reset();
    l3_nh_no_policy_.reset();
    l3_label_ = MplsTable::kInvalidLabel;
    InterfaceNH::DeleteMulticastVmInterfaceNH(vmi->GetUuid(), vmi->vm_mac(),
                                    vmi->name());
    InterfaceNH::DeleteL3InterfaceNH(vmi->GetUuid(), vmi->vm_mac(), vmi->name());
    return true;
}

const NextHop* VmInterface::l3_interface_nh_no_policy() const {
    return nexthop_state_->l3_nh_no_policy_.get();
}

const NextHop* VmInterface::l2_interface_nh_no_policy() const {
    return nexthop_state_->l2_nh_no_policy_.get();
}

const NextHop* VmInterface::l2_interface_nh_policy() const {
    return nexthop_state_->l2_nh_policy_.get();
}

void VmInterface::GetNextHopInfo() {
    l2_label_ = nexthop_state_->l2_label();
    label_ = nexthop_state_->l3_label();
    // If Layer3 forwarding is configured irrespective of ipv4/v6 status,
    // flow_key_nh should be l3 based.
    if (layer3_forwarding()) {
        flow_key_nh_ = nexthop_state_->l3_nh_policy_;
    } else {
        flow_key_nh_ = nexthop_state_->l2_nh_policy_;
    }
}

////////////////////////////////////////////////////////////////////////////
// MetaData Ip Routines
////////////////////////////////////////////////////////////////////////////
MetaDataIpState::MetaDataIpState() : VmInterfaceState(), mdata_ip_() {
}

MetaDataIpState::~MetaDataIpState() {
    mdata_ip_.reset(NULL);
}

// metadata-ip is deleted after all update processing by explicitly
// calling delete. So, dont return DEL operation from here
VmInterfaceState::Op MetaDataIpState::GetOpL3(const Agent *agent,
                                              const VmInterface *vmi) const {
    if (vmi->need_linklocal_ip() == false ||
        vmi->metadata_ip_active() == false)
        return VmInterfaceState::INVALID;

    return VmInterfaceState::ADD;
}

bool MetaDataIpState::AddL3(const Agent *agent, VmInterface *vmi) const {
    if (mdata_ip_.get() == NULL) {
        mdata_ip_.reset(new MetaDataIp(agent->metadata_ip_allocator(), vmi,
                                       vmi->id()));
    }
    mdata_ip_->set_active(true);
    vmi->UpdateMetaDataIpInfo();
    return true;
}

// Delete meta-data route
bool MetaDataIpState::DeleteL3(const Agent *agent, VmInterface *vmi) const {
    if (vmi->metadata_ip_active() && vmi->need_linklocal_ip())
        return false;

    if (mdata_ip_.get() == NULL) {
        return true;
    }

    // Call UpdateMetaDataIpInfo before setting mdata_ip_ state to inactive
    vmi->UpdateMetaDataIpInfo();
    mdata_ip_->set_active(false);
    return true;
}

Ip4Address VmInterface::mdata_ip_addr() const {
    if (metadata_ip_state_.get() == NULL)
        return Ip4Address(0);

    if (metadata_ip_state_->mdata_ip_.get() == NULL) {
        return Ip4Address(0);
    }

    return metadata_ip_state_->mdata_ip_->GetLinkLocalIp();
}

////////////////////////////////////////////////////////////////////////////
// ResolveRoute Attribute Routines
////////////////////////////////////////////////////////////////////////////
ResolveRouteState::ResolveRouteState() :
    VmInterfaceState(), vrf_(NULL), subnet_(), plen_(0) {
}

ResolveRouteState::~ResolveRouteState() {
}

void ResolveRouteState::Copy(const Agent *agent, const VmInterface *vmi) const {
    vrf_ = vmi->forwarding_vrf();
    subnet_ = vmi->subnet();
    plen_ = vmi->subnet_plen();
}

VmInterfaceState::Op ResolveRouteState::GetOpL3(const Agent *agent,
                                                const VmInterface *vmi) const {
    if (vmi->ipv4_active() == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->forwarding_vrf() || subnet_ != vmi->subnet() ||
        plen_ != vmi->subnet_plen())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool ResolveRouteState::DeleteL3(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    vmi->DeleteRoute(vrf_->GetName(), subnet_, plen_);
    return true;
}

bool ResolveRouteState::AddL3(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL || subnet_.is_unspecified())
        return false;

    if (vmi->vmi_type() != VmInterface::VHOST) {
        if (vmi->vn() == NULL) {
            return false;
        }
    }

    SecurityGroupList sg_id_list;
    vmi->CopySgIdList(&sg_id_list);

    TagList tag_id_list;
    vmi->CopyTagIdList(&tag_id_list);

    std::string vn_name;
    if (vmi->vn() != NULL) {
        vn_name = vmi->vn()->GetName();
    }

    bool policy = vmi->policy_enabled();
    if (vmi->vmi_type() == VmInterface::VHOST) {
        policy = false;
    }

    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, vmi->GetUuid(), vmi->name());
    InetUnicastAgentRouteTable::AddResolveRoute
        (vmi->peer(), vrf_->GetName(),
         Address::GetIp4SubnetAddress(subnet_, plen_), plen_, key,
         vrf_->table_label(), policy, vn_name,
         sg_id_list, tag_id_list);
    return true;
}

// If the interface is Gateway we need to add a receive route,
// such the packet gets routed. Bridging on gateway
// interface is not supported
VmInterfaceState::Op ResolveRouteState::GetOpL2(const Agent *agent,
                                                const VmInterface *vmi) const {
    if ((vmi->vmi_type() != VmInterface::GATEWAY &&
         vmi->vmi_type() != VmInterface::REMOTE_VM))
        return VmInterfaceState::DEL;

    if (vmi->bridging() == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->forwarding_vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool ResolveRouteState::DeleteL2(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    BridgeAgentRouteTable::Delete(vmi->peer(), vrf_->GetName(),
                                  vmi->GetVifMac(agent), 0);
    return true;
}

bool ResolveRouteState::AddL2(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL || vmi->vn() == NULL)
        return false;

    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetRouteTable(Agent::BRIDGE));
    table->AddBridgeReceiveRoute(vmi->peer(), vrf_->GetName(), 0,
                                 vmi->GetVifMac(agent), vmi->vn()->GetName());
    return true;
}

////////////////////////////////////////////////////////////////////////////
// VmiRouteState Routines
// This is responsible to manage following routes,
// - Manages the <0.0.0.0, MAC> EVPN route for the interface
// - Manage the L3 route for dhcp-relay when do_dhcp_relay_ is enabled
//   ip_ here is set to primary-ip when do_dhcp_relay_ is enabled
//   When do_dhcp_relay_ is enabled, adds route for ip_ along with service-ip.
//
//   Note, instance-ip can also potentially add/delete route for same ip-address
////////////////////////////////////////////////////////////////////////////
VmiRouteState::VmiRouteState() :
    VmInterfaceState(), vrf_(NULL), ip_(), ethernet_tag_(0),
    do_dhcp_relay_(false) {
}

VmiRouteState::~VmiRouteState() {
}

void VmiRouteState::Copy(const Agent *agent, const VmInterface *vmi) const {
    vrf_ = vmi->vrf();
    ethernet_tag_ = vmi->ethernet_tag();
    do_dhcp_relay_ = vmi->do_dhcp_relay();
    ip_ = do_dhcp_relay_ ?  ip_ = vmi->dhcp_addr() : Ip4Address(0);
}

VmInterfaceState::Op VmiRouteState::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    if (vmi->ipv4_active() == false)
        return VmInterfaceState::DEL;

    // On do_dhcp_relay_ change from enable to disbale, delete the route
    // Normally, we should not look at old value of do_dhcp_relay_ here, but
    // ignoring it will result in delete and add of route for primary-ip
    // Check old value of do_dhcp_relay_ to avoid this
    //
    // Even on transition, if instance-ip is present matching the primary-ip
    // the route will added again due to instance-ip later
    if (do_dhcp_relay_ && vmi->do_dhcp_relay() == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    if (ip_ != vmi->dhcp_addr())
        return VmInterfaceState::DEL_ADD;

    // Dont have to add L3 route if DHCP Relay not enabled
    if (vmi->do_dhcp_relay() == false)
        return VmInterfaceState::INVALID;

    return VmInterfaceState::ADD;
}

bool VmiRouteState::DeleteL3(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL || ip_.is_unspecified())
        return false;

    vmi->DeleteRoute(vrf_->GetName(), ip_, 32);
    return true;
}

bool VmiRouteState::AddL3(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL || vmi->vn() == NULL || ip_.is_unspecified())
        return false;

    /* expected only for instance IP */
    vmi->AddRoute(vrf_->GetName(), ip_, 32, vmi->vn()->GetName(), false,
                  vmi->ecmp(), false, false, vmi->vm_ip_service_addr(),
                  Ip4Address(0), CommunityList(), vmi->label(),
                  VmInterface::kInterface);
    return true;
}

VmInterfaceState::Op VmiRouteState::GetOpL2
(const Agent *agent, const VmInterface *vmi) const {
    if (vmi->l2_active() == false || vmi->is_hc_active() == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    if (ethernet_tag_ != vmi->ethernet_tag())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmiRouteState::DeleteL2(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    vmi->DeleteL2InterfaceRoute(vrf_, ethernet_tag_, Ip4Address(0),
                                vmi->vm_mac());
    return true;
}

bool VmiRouteState::AddL2(const Agent *agent, VmInterface *vmi) const {
    if (vrf_ == NULL || vmi->vn() == NULL)
        return false;

    vmi->AddL2InterfaceRoute(Ip4Address(), vmi->vm_mac(), Ip4Address(0));
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// InstanceIp routines. Manages following,
// - Manages the L3 and L2 routes derived from the instance-ip.
/////////////////////////////////////////////////////////////////////////////
void VmInterface::InstanceIpList::Insert(const InstanceIp *rhs) {
    list_.insert(*rhs);
}

void VmInterface::InstanceIpList::Update(const InstanceIp *lhs,
                                         const InstanceIp *rhs) {
    lhs->ecmp_ = rhs->ecmp_;
    lhs->is_service_ip_ = rhs->is_service_ip_;
    lhs->is_service_health_check_ip_ = rhs->is_service_health_check_ip_;
    lhs->is_local_ = rhs->is_local_;
    lhs->tracking_ip_ = rhs->tracking_ip_;

    lhs->set_del_pending(false);
}

void VmInterface::InstanceIpList::Remove(InstanceIpSet::iterator &it) {
    it->set_del_pending(true);
}

bool VmInterface::InstanceIpList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    InstanceIpSet::iterator it = list_.begin();
    // Apply the instance-ip configured for interface
    while (it != list_.end()) {
        InstanceIpSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        if (prev->del_pending() == false)
            prev->SetPrefixForAllocUnitIpam(vmi);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        if (prev->del_pending()) {
            list_.erase(prev);
        }
    }

    return true;
}

VmInterface::InstanceIp::InstanceIp() :
    ListEntry(), VmInterfaceState(), ip_(), plen_(), ecmp_(false),
    is_primary_(false), is_service_ip_(false),
    is_service_health_check_ip_(false), is_local_(false),
    tracking_ip_(), vrf_(NULL), ethernet_tag_(0) {
}

VmInterface::InstanceIp::InstanceIp(const InstanceIp &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    ip_(rhs.ip_), plen_(rhs.plen_), ecmp_(rhs.ecmp_),
    is_primary_(rhs.is_primary_), is_service_ip_(rhs.is_service_ip_),
    is_service_health_check_ip_(rhs.is_service_health_check_ip_),
    is_local_(rhs.is_local_), tracking_ip_(rhs.tracking_ip_),
    vrf_(NULL), ethernet_tag_(0) {
}

VmInterface::InstanceIp::InstanceIp(const IpAddress &addr, uint8_t plen,
                                    bool ecmp, bool is_primary,
                                    bool is_service_ip,
                                    bool is_service_health_check_ip,
                                    bool is_local,
                                    const IpAddress &tracking_ip) :
    ListEntry(), VmInterfaceState(), ip_(addr), plen_(plen), ecmp_(ecmp),
    is_primary_(is_primary), is_service_ip_(is_service_ip),
    is_service_health_check_ip_(is_service_health_check_ip),
    is_local_(is_local), tracking_ip_(tracking_ip), vrf_(NULL),
    ethernet_tag_(0) {
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

void VmInterface::InstanceIp::SetPrefixForAllocUnitIpam(VmInterface *vmi) const{
    if (vmi->vn() == NULL)
        return;

    uint32_t alloc_unit = vmi->vn()->GetAllocUnitFromIpam(ip_);

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

static bool GetInstanceIpActiveState(const VmInterface::InstanceIp *instance_ip,
                                     const VmInterface *vmi) {
    if (instance_ip->is_service_health_check_ip_) {
        // for service health check instance ip keep the route based on
        // metadata-active state
        return vmi->metadata_ip_active();
    }

    if (instance_ip->ip_.is_v6()) {
        return vmi->ipv6_active();
    }

    return vmi->ipv4_active();
}

VmInterfaceState::Op VmInterface::InstanceIp::GetOpL2
(const Agent *agent, const VmInterface *vmi) const {
    if (GetInstanceIpActiveState(this, vmi) == false)
        return VmInterfaceState::DEL;

    if (!is_service_ip_ && vmi->vmi_type() != VmInterface::VHOST) {
        // Add route only when vn IPAM exists for the IP
        if (vmi->vn() && vmi->vn()->GetIpam(ip_) == false)
            return VmInterfaceState::DEL;
    }

    if (IsL3Only())
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    if (ethernet_tag_ != vmi->ethernet_tag())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::InstanceIp::AddL2(const Agent *agent,
                                    VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    vmi->AddL2InterfaceRoute(ip_, vmi->vm_mac(), tracking_ip_);
    return true;
}

bool VmInterface::InstanceIp::DeleteL2(const Agent *agent,
                                       VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    vmi->DeleteL2InterfaceRoute(vrf_, ethernet_tag_, ip_, vmi->vm_mac());
    return true;
}

VmInterfaceState::Op VmInterface::InstanceIp::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    // TODO : Should be check health-check state here?
    if (GetInstanceIpActiveState(this, vmi) == false)
        return VmInterfaceState::DEL;

    if (!is_service_ip_ && vmi->vmi_type() != VmInterface::VHOST) {
        // Add route only when vn IPAM exists for the IP
        if (vmi->vn() && vmi->vn()->GetIpam(ip_) == false)
            return VmInterfaceState::DEL;
    }

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::InstanceIp::AddL3(const Agent *agent,
                                    VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;
    assert(ip_.is_unspecified() == false);
    std::string vn_name;
    if (vmi->vn()) {
        vn_name = vmi->vn()->GetName();
    } else if (vmi->vmi_type() == VHOST) {
        vn_name  = agent->fabric_vn_name();
    }

    vmi->AddRoute(vmi->vrf()->GetName(), ip_, plen_, vn_name,
                  is_force_policy(), ecmp_,is_local_,
                  is_service_health_check_ip_, vmi->GetServiceIp(ip_),
                  tracking_ip_, CommunityList(), vmi->label(),
                  is_service_ip_ ? kServiceInterface : kInterface);
    return true;
}

bool VmInterface::InstanceIp::DeleteL3(const Agent *agent,
                                       VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;
    vmi->DeleteRoute(vrf_->GetName(), ip_, plen_);
    return true;
}

void VmInterface::InstanceIp::Copy(const Agent *agent,
                                   const VmInterface *vmi) const {
    vrf_ = vmi->vrf();
    ethernet_tag_ = vmi->ethernet_tag();
}

/////////////////////////////////////////////////////////////////////////////
// FloatingIp routines
/////////////////////////////////////////////////////////////////////////////
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

bool VmInterface::FloatingIpList::UpdateList(const Agent *agent,
                                             VmInterface *vmi,
                                             VmInterfaceState::Op l2_force_op,
                                             VmInterfaceState::Op l3_force_op) {
    FloatingIpSet::iterator it = list_.begin();
    while (it != list_.end()) {
        FloatingIpSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
    }
    return true;
}

VmInterface::FloatingIp::FloatingIp() :
    ListEntry(), VmInterfaceState(), floating_ip_(), vn_(NULL),
    vrf_(NULL, this), vrf_name_(""), vn_uuid_(),
    fixed_ip_(), direction_(DIRECTION_BOTH), port_map_enabled_(false),
    src_port_map_(), dst_port_map_(), ethernet_tag_(0), port_nat_(false) {
}

VmInterface::FloatingIp::FloatingIp(const FloatingIp &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    floating_ip_(rhs.floating_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_), fixed_ip_(rhs.fixed_ip_),
    direction_(rhs.direction_), port_map_enabled_(rhs.port_map_enabled_),
    src_port_map_(rhs.src_port_map_), dst_port_map_(rhs.dst_port_map_),
    ethernet_tag_(rhs.ethernet_tag_), port_nat_(rhs.port_nat_) {
}

VmInterface::FloatingIp::FloatingIp(const IpAddress &addr,
                                    const std::string &vrf,
                                    const boost::uuids::uuid &vn_uuid,
                                    const IpAddress &fixed_ip,
                                    Direction direction,
                                    bool port_map_enabled,
                                    const PortMap &src_port_map,
                                    const PortMap &dst_port_map,
                                    bool port_nat) :
    ListEntry(), VmInterfaceState(),floating_ip_(addr), vn_(NULL),
    vrf_(NULL, this), vrf_name_(vrf), vn_uuid_(vn_uuid), fixed_ip_(fixed_ip),
    direction_(direction), port_map_enabled_(port_map_enabled),
    src_port_map_(src_port_map), dst_port_map_(dst_port_map), ethernet_tag_(0),
    port_nat_(port_nat) {
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

const IpAddress
VmInterface::FloatingIp::GetFixedIp(const VmInterface *vmi) const {
    if (fixed_ip_.is_unspecified()) {
        if (floating_ip_.is_v4() == true) {
            return vmi->primary_ip_addr();
        } else {
            return vmi->primary_ip6_addr();
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

bool VmInterface::FloatingIp::port_nat() const {
    return port_nat_;
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

void VmInterface::FloatingIp::Copy(const Agent *agent,
                                   const VmInterface *vmi) const {
    // Delete for FloatingIp happens in CleanupFloatingIpList(). So, dont
    // update vn_ and vrf_ fields in case of delete
    if (del_pending_) {
        vn_ = NULL;
        vrf_ = NULL;
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    if (vn_.get() == NULL)
        vn_ = table->FindVnRef(vn_uuid_);

    if (vrf_.get() == NULL)
        vrf_ = table->FindVrfRef(vrf_name_);
    ethernet_tag_ = vmi->ethernet_tag();

    return;
}

VmInterfaceState::Op VmInterface::FloatingIp::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    if (GetIpActiveState(floating_ip_, vmi) == false)
        return VmInterfaceState::DEL;

    if (vmi->vrf() == NULL || vmi->vn() == NULL)
        return VmInterfaceState::DEL;

    return VmInterfaceState::ADD;
}

bool VmInterface::FloatingIp::AddL3(const Agent *agent,
                                    VmInterface *vmi) const {
    if (vrf_.get() == NULL || vn_.get() == NULL || port_nat_)
        return false;

    fixed_ip_ = GetFixedIp(vmi);
    uint8_t plen = floating_ip_.is_v4() ?
        Address::kMaxV4PrefixLen : Address::kMaxV6PrefixLen;
    IpAddress service_ip = Ip4Address();
    if (floating_ip_.is_v6())
        service_ip = Ip6Address();

    bool ecmp = floating_ip_.is_v4() ? vmi->ecmp() : vmi->ecmp6();
    vmi->AddRoute(vrf_.get()->GetName(), floating_ip_, plen, vn_->GetName(),
                  false, ecmp, false, false, service_ip, fixed_ip_,
                  CommunityList(), vmi->label(), kInterface);

    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    if (floating_ip_.is_v4() && table->update_floatingip_cb().empty()==false) {
        table->update_floatingip_cb()(vmi, vn_.get(), floating_ip_.to_v4(),
                                      false);
        //TODO:: callback for DNS handling
    }

    return true;
}

bool VmInterface::FloatingIp::DeleteL3(const Agent *agent,
                                       VmInterface *vmi) const {
    if (vrf_.get() == NULL)
        return false;
    uint8_t plen = floating_ip_.is_v4() ?
        Address::kMaxV4PrefixLen : Address::kMaxV6PrefixLen;

    vmi->DeleteRoute(vrf_.get()->GetName(), floating_ip_, plen);

    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    if (floating_ip_.is_v4() && table->update_floatingip_cb().empty()==false) {
        table->update_floatingip_cb()(vmi, vn_.get(), floating_ip_.to_v4(),
                                      true);
        //TODO:: callback for DNS handling
    }
    return true;
}

VmInterfaceState::Op VmInterface::FloatingIp::GetOpL2
(const Agent *agent, const VmInterface *vmi) const {
    if (GetIpActiveState(floating_ip_, vmi) == false)
        return VmInterfaceState::DEL;

    if (vmi->vrf() == NULL || vmi->vn() == NULL)
        return VmInterfaceState::DEL;

    if (ethernet_tag_ != vmi->ethernet_tag())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::FloatingIp::AddL2(const Agent *agent,
                                    VmInterface *vmi) const {
    if (vrf_.get() == NULL || vn_.get() == NULL || port_nat_)
        return false;

    SecurityGroupList sg_id_list;
    vmi->CopySgIdList(&sg_id_list);

    TagList tag_id_list;
    vmi->CopyTagIdList(&tag_id_list);

    PathPreference path_preference;
    vmi->SetPathPreference(&path_preference, false, GetFixedIp(vmi));

    EvpnAgentRouteTable *evpn_table = static_cast<EvpnAgentRouteTable *>
        (vrf_->GetEvpnRouteTable());

    std::string vn_name;
    if (vmi->vn()) {
        vn_name = vmi->vn()->GetName();
    }

    evpn_table->AddReceiveRoute(vmi->peer_.get(), vrf_->GetName(),
                                vmi->l2_label(), vmi->vm_mac(), floating_ip_,
                                ethernet_tag_, vn_name,
                                path_preference);
    return true;
}

bool VmInterface::FloatingIp::DeleteL2(const Agent *agent,
                                       VmInterface *vmi) const {
    if (vrf_.get() == NULL)
        return false;

    EvpnAgentRouteTable *evpn_table =
        static_cast<EvpnAgentRouteTable *>(vrf_->GetEvpnRouteTable());
    if (evpn_table == NULL) {
        return false;
    }

    evpn_table->DelLocalVmRoute(vmi->peer_.get(), vrf_->GetName(),
                                vmi->vm_mac(), vmi, floating_ip_,
                                ethernet_tag_);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// AliasIp routines
/////////////////////////////////////////////////////////////////////////////
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

void VmInterface::AliasIpList::Update(const AliasIp *lhs, const AliasIp *rhs) {
    lhs->set_del_pending(false);
}

void VmInterface::AliasIpList::Remove(AliasIpSet::iterator &it) {
    it->set_del_pending(true);
}

bool VmInterface::AliasIpList::UpdateList(const Agent *agent, VmInterface *vmi,
                                          VmInterfaceState::Op l2_force_op,
                                          VmInterfaceState::Op l3_force_op) {
    AliasIpSet::iterator it = list_.begin();
    while (it != list_.end()) {
        AliasIpSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
    }
    return true;
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

VmInterface::AliasIp::AliasIp() :
    ListEntry(), VmInterfaceState(), alias_ip_(), vn_(NULL), vrf_(NULL, this),
    vrf_name_(""), vn_uuid_() {
}

VmInterface::AliasIp::AliasIp(const AliasIp &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    alias_ip_(rhs.alias_ip_), vn_(rhs.vn_), vrf_(rhs.vrf_, this),
    vrf_name_(rhs.vrf_name_), vn_uuid_(rhs.vn_uuid_) {
}

VmInterface::AliasIp::AliasIp(const IpAddress &addr, const std::string &vrf,
                              const boost::uuids::uuid &vn_uuid) :
    ListEntry(), VmInterfaceState(), alias_ip_(addr), vn_(NULL),
    vrf_(NULL, this), vrf_name_(vrf), vn_uuid_(vn_uuid) {
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

VmInterfaceState::Op VmInterface::AliasIp::GetOpL3(const Agent *agent,
                                                   const VmInterface *vmi) const {
    if (GetIpActiveState(alias_ip_, vmi) == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

void VmInterface::AliasIp::Copy(const Agent *agent,
                                const VmInterface *vmi) const {
    if (del_pending_) {
        vrf_ = NULL;
        vn_ = NULL;
        return;
    }

    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    if (vn_.get() == NULL) {
        vn_ = table->FindVnRef(vn_uuid_);
        assert(vn_.get());
    }

    if (vrf_.get() == NULL) {
        vrf_ = table->FindVrfRef(vrf_name_);
        assert(vrf_.get());
    }
    return;
}

bool VmInterface::AliasIp::AddL3(const Agent *agent, VmInterface *vmi) const {
    if (vrf_.get() == NULL || vn_.get() == NULL)
       return false;

    uint8_t plen = alias_ip_.is_v4() ?
        Address::kMaxV4PrefixLen : Address::kMaxV6PrefixLen;
    IpAddress service_ip = Ip4Address();
    if (alias_ip_.is_v6())
        service_ip = Ip6Address();
    vmi->AddRoute(vrf_->GetName(), alias_ip_, plen, vn_->GetName(), false,
                  vmi->ecmp(), false, false, service_ip, service_ip,
                  CommunityList(), vmi->label(), kInterface);
    return true;
}

bool VmInterface::AliasIp::DeleteL3(const Agent *agent,
                                    VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;
    uint8_t plen = alias_ip_.is_v4() ?
        Address::kMaxV4PrefixLen : Address::kMaxV6PrefixLen;
    vmi->DeleteRoute(vrf_.get()->GetName(), alias_ip_, plen);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// StaticRoute routines
/////////////////////////////////////////////////////////////////////////////
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

bool VmInterface::StaticRouteList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    StaticRouteSet::iterator it = list_.begin();
    while (it != list_.end()) {
        StaticRouteSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        if (prev->del_pending_) {
            list_.erase(prev);
        }
    }
    return true;
}

VmInterface::StaticRoute::StaticRoute() :
    ListEntry(), VmInterfaceState(), vrf_(), addr_(), plen_(0), gw_(),
    communities_() {
}

VmInterface::StaticRoute::StaticRoute(const StaticRoute &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    vrf_(rhs.vrf_), addr_(rhs.addr_), plen_(rhs.plen_), gw_(rhs.gw_),
    communities_(rhs.communities_) {
}

VmInterface::StaticRoute::StaticRoute(const IpAddress &addr,
                                      uint32_t plen, const IpAddress &gw,
                                      const CommunityList &communities) :
    ListEntry(), VmInterfaceState(), vrf_(), addr_(addr), plen_(plen), gw_(gw),
    communities_(communities) {
}

VmInterface::StaticRoute::~StaticRoute() {
}

bool VmInterface::StaticRoute::operator() (const StaticRoute &lhs,
                                           const StaticRoute &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::StaticRoute::IsLess(const StaticRoute *rhs) const {
    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return gw_ < rhs->gw_;
}

void VmInterface::StaticRoute::Copy(const Agent *agent,
                                    const VmInterface *vmi) const {
    if (vmi->vmi_type() == VmInterface::VHOST) {
        vrf_ = vmi->forwarding_vrf();
    } else {
        vrf_ = vmi->vrf();
    }
}

VmInterfaceState::Op VmInterface::StaticRoute::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    if (GetIpActiveState(addr_, vmi) == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::StaticRoute::AddL3(const Agent *agent,
                                     VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;
    std::string vn_name;
    if (vmi->vn()) {
        vn_name = vmi->vn()->GetName();
    } else if (vmi->vmi_type() == VHOST) {
        vn_name  = agent->fabric_vn_name();
    }

    if (gw_.is_v4() && addr_.is_v4() && gw_.to_v4() != Ip4Address(0)) {
        SecurityGroupList sg_id_list;
        vmi->CopySgIdList(&sg_id_list);

        TagList tag_id_list;
        vmi->CopyTagIdList(&tag_id_list);

        VnListType vn_list;
        if (vmi->vn()) {
            vn_list.insert(vn_name);
        }

        bool native_encap = false;
        const Peer *peer = vmi->peer_.get();
        if (vrf_->GetName() == agent->fabric_vrf_name()) {
            vn_list.insert(agent->fabric_vn_name());
            native_encap = true;
            peer = agent->local_peer();
        }

        InetUnicastAgentRouteTable::AddGatewayRoute
            (peer, vrf_->GetName(), addr_.to_v4(), plen_,
             gw_.to_v4(), vn_list, vmi->vrf_->table_label(),
             sg_id_list, tag_id_list, communities_, native_encap);
    } else {
        IpAddress dependent_ip;
        bool ecmp = false;
        if (addr_.is_v4()) {
            dependent_ip = vmi->primary_ip_addr();
            ecmp = vmi->ecmp();
        } else if (addr_.is_v6()) {
            dependent_ip = vmi->primary_ip6_addr();
            ecmp = vmi->ecmp6();
        }
        vmi->AddRoute(vrf_->GetName(), addr_, plen_, vn_name,
                      false, ecmp, false, false, vmi->GetServiceIp(addr_),
                      dependent_ip, communities_, vmi->label(),
                      kInterfaceStatic);
    }
    return true;
}

bool VmInterface::StaticRoute::DeleteL3(const Agent *agent,
                                        VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    const Peer *peer = vmi->peer();
    if (vmi->vmi_type() == VHOST) {
        peer = agent->local_peer();
    }
    InetUnicastAgentRouteTable::Delete(peer, vrf_->GetName(),
                                       addr_, plen_);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//Allowed addresss pair route
///////////////////////////////////////////////////////////////////////////////
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

bool VmInterface::AllowedAddressPairList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    AllowedAddressPairSet::iterator it = list_.begin();
    while (it != list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        if (!prev->del_pending_)
            continue;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        list_.erase(prev);
    }
    it = list_.begin();
    while (it != list_.end()) {
        AllowedAddressPairSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
    }
    return true;
}

VmInterface::AllowedAddressPair::AllowedAddressPair() :
    ListEntry(), VmInterfaceState(), addr_(), plen_(0), ecmp_(false), mac_(),
    ecmp_config_changed_(false),
    service_ip_(), label_(MplsTable::kInvalidLabel), policy_enabled_nh_(NULL),
    policy_disabled_nh_(NULL), vrf_(NULL), ethernet_tag_(0) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(
    const AllowedAddressPair &rhs) : ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    addr_(rhs.addr_), plen_(rhs.plen_), ecmp_(rhs.ecmp_),
    mac_(rhs.mac_),
    ecmp_config_changed_(rhs.ecmp_config_changed_),
    service_ip_(rhs.service_ip_), label_(rhs.label_),
    policy_enabled_nh_(rhs.policy_enabled_nh_),
    policy_disabled_nh_(rhs.policy_disabled_nh_),
    vrf_(rhs.vrf_), ethernet_tag_(rhs.ethernet_tag_) {
}

VmInterface::AllowedAddressPair::AllowedAddressPair(const IpAddress &addr,
                                                    uint32_t plen, bool ecmp,
                                                    const MacAddress &mac) :
    ListEntry(), VmInterfaceState(), addr_(addr), plen_(plen), ecmp_(ecmp),
    mac_(mac), ecmp_config_changed_(false),
    label_(MplsTable::kInvalidLabel), policy_enabled_nh_(NULL),
    policy_disabled_nh_(NULL), vrf_(NULL), ethernet_tag_(0) {
}

VmInterface::AllowedAddressPair::~AllowedAddressPair() {
}

bool VmInterface::AllowedAddressPair::operator() (const AllowedAddressPair &lhs,
                                                  const AllowedAddressPair &rhs)
                                                  const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::AllowedAddressPair::IsLess(const AllowedAddressPair *rhs)
    const {
    if (addr_ != rhs->addr_)
        return addr_ < rhs->addr_;

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return mac_ < rhs->mac_;
}

void VmInterface::AllowedAddressPair::Copy(const Agent *agent,
                                           const VmInterface *vmi) const {
    vrf_ = vmi->vrf();
    ethernet_tag_ = vmi->ethernet_tag();
}

VmInterfaceState::Op VmInterface::AllowedAddressPair::GetOpL2
(const Agent *agent, const VmInterface *vmi) const {
    if (vmi->bridging() == false)
        return VmInterfaceState::DEL;

    if (ethernet_tag_ != vmi->ethernet_tag())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::AllowedAddressPair::AddL2(const Agent *agent,
                                            VmInterface *vmi) const {
    if (vrf_ == NULL || mac_ == MacAddress::kZeroMac)
        return false;

    IpAddress dependent_rt;
    IpAddress route_addr;
    if (addr_.is_v4()) {
        if (plen_ == 32) {
            route_addr = addr_;
        } else {
            route_addr = Ip4Address(0);
        }
        dependent_rt = Ip4Address(0);
    } else if (addr_.is_v6()) {
        if (plen_ == 128) {
            route_addr = addr_;
        } else {
            route_addr = Ip6Address();
        }
        dependent_rt = Ip6Address();
    }
    vmi->AddL2InterfaceRoute(route_addr, mac_, dependent_rt);
    return true;
}

bool VmInterface::AllowedAddressPair::DeleteL2(const Agent *agent,
                                               VmInterface *vmi) const {
    if (vrf_ == NULL || mac_ == MacAddress::kZeroMac)
        return false;

    IpAddress route_addr;
    if (addr_.is_v4()) {
        if (plen_ == 32) {
            route_addr = addr_;
        } else {
            route_addr = Ip4Address(0);
        }
    } else if (addr_.is_v6()) {
        if (plen_ == 128) {
            route_addr = addr_;
        } else {
            route_addr = Ip6Address();
        }
    }
    vmi->DeleteL2InterfaceRoute(vrf_, ethernet_tag_, route_addr, mac_);
    return true;
}

VmInterfaceState::Op VmInterface::AllowedAddressPair::GetOpL3
(const Agent *agent, const VmInterface *vmi) const {
    if (GetIpActiveState(addr_, vmi) == false)
        return VmInterfaceState::DEL;

    return VmInterfaceState::ADD;
}

bool VmInterface::AllowedAddressPair::DeleteL3(const Agent *agent,
                                               VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    vmi->DeleteRoute(vrf_->GetName(), addr_, plen_);
    if (label_ != MplsTable::kInvalidLabel) {
        label_ = MplsTable::kInvalidLabel;
    }
    policy_enabled_nh_ = NULL;
    policy_disabled_nh_ = NULL;
    return true;
}

bool VmInterface::AllowedAddressPair::AddL3(const Agent *agent,
                                            VmInterface *vmi) const {
    if (vrf_ == NULL || vmi->vn_ == NULL)
        return false;

    service_ip_ = vmi->GetServiceIp(addr_);
    if (mac_ == MacAddress::kZeroMac || mac_ == vmi->vm_mac_) {
        vmi->AddRoute(vrf_->GetName(), addr_, plen_, vmi->vn_->GetName(),
                      false, ecmp_, false, false, service_ip_, Ip4Address(0),
                      CommunityList(), vmi->label(), kInterface);
        return true;
    }

    InterfaceNH::CreateL3VmInterfaceNH
        (vmi->GetUuid(), mac_, vmi->vrf_->GetName(), vmi->learning_enabled_,
         vmi->name());

    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, vmi->GetUuid(),
                           vmi->name());

    // Get policy disabled nh first
    InterfaceNHKey key(vmi_key.Clone(), false, InterfaceNHFlags::INET4, mac_);
    InterfaceNH *nh =static_cast<InterfaceNH *>
        (agent->nexthop_table()->FindActiveEntry(&key));
    policy_disabled_nh_ = nh;
    // Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);

    // Get policy enabled nh first
    InterfaceNHKey key1(vmi_key.Clone(), true, InterfaceNHFlags::INET4, mac_);
    nh = static_cast<InterfaceNH *>(agent->
                                    nexthop_table()->FindActiveEntry(&key1));
    // Ensure nexthop to be deleted upon refcount falling to 0
    nh->set_delete_on_zero_refcount(true);
    policy_enabled_nh_ = nh;

    // Update AAP mpls label from nh entry
    if (vmi->policy_enabled()) {
        label_ = policy_enabled_nh_->mpls_label()->label();
    } else {
        label_ = policy_disabled_nh_->mpls_label()->label();
    }

    vmi->AddRoute(vrf_->GetName(), addr_, plen_, vmi->vn_->GetName(),
                  false, ecmp_, false, false, service_ip_, Ip6Address(),
                  CommunityList(), label_, kInterface);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// ServiceVlan routines
/////////////////////////////////////////////////////////////////////////////
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

bool VmInterface::ServiceVlanList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    ServiceVlanSet::iterator it = list_.begin();
    while (it != list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        if (prev->del_pending_ || prev->del_add_) {
            prev->Update(agent, vmi);
            if (prev->del_pending_) {
                list_.erase(prev);
            }
        }
    }

    it = list_.begin();
    while (it != list_.end()) {
        ServiceVlanSet::iterator prev = it++;
        prev->Update(agent, vmi);
    }

    return true;
}

/////////////////////////////////////////////////////////////////////////////
// TagGroup routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::TagEntry::TagEntry() :
    ListEntry(), VmInterfaceState() , type_(0xFFFFFFFF), uuid_(nil_uuid()) {
}

VmInterface::TagEntry::TagEntry(const TagEntry &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    type_(rhs.type_), uuid_(rhs.uuid_) {
}

VmInterface::TagEntry::TagEntry(uint32_t type, const uuid &u) :
    ListEntry(), VmInterfaceState(), type_(type), uuid_(u) {
}

VmInterface::TagEntry::~TagEntry() {
}

bool VmInterface::TagEntry::operator ==(const TagEntry &rhs) const {
    return uuid_ == rhs.uuid_;
}

bool VmInterface::TagEntry::operator()(const TagEntry &lhs,
                                       const TagEntry &rhs) const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::TagEntry::IsLess(const TagEntry *rhs) const {
    if (type_ != rhs->type_) {
        return type_ < rhs->type_;
    }

    //We can only have duplicate of tag of type Label
    //Rest of tags would be unique
    if (type_ != TagTable::LABEL) {
        return false;
    }
    return uuid_ < rhs->uuid_;
}

bool VmInterface::TagEntry::AddL3(const Agent *agent,
                                  VmInterface *intrface) const {
    if (tag_.get() && tag_->tag_uuid() == uuid_) {
        return false;
    }

    TagKey tag_key(uuid_);
    typedef ::TagEntry GlobalTagEntry;
    tag_ = static_cast<GlobalTagEntry *>(agent->tag_table()->
                                             FindActiveEntry(&tag_key));
    return true;
}

bool VmInterface::TagEntry::DeleteL3(const Agent *agent,
                                     VmInterface *intrface) const {
    tag_.reset();
    return true;
}

VmInterfaceState::Op
VmInterface::TagEntry::GetOpL3(const Agent *agent,
                               const VmInterface *vmi) const {
    if (del_pending_)
        return VmInterfaceState::INVALID;

    return VmInterfaceState::ADD;
}

void VmInterface::TagEntryList::Insert(const TagEntry *rhs) {
    list_.insert(*rhs);
}

void VmInterface::TagEntryList::Update(const TagEntry *lhs,
                                       const TagEntry *rhs) {
    if (lhs->uuid_ != rhs->uuid_) {
        lhs->uuid_ = rhs->uuid_;
    }
}

void VmInterface::TagEntryList::Remove(TagEntrySet::iterator &it) {
    it->set_del_pending(true);
}

bool VmInterface::TagEntryList::UpdateList(const Agent *agent,
                                           VmInterface *vmi,
                                           VmInterfaceState::Op l2_force_op,
                                           VmInterfaceState::Op l3_force_op) {
    TagEntrySet::iterator it = list_.begin();
    while (it != list_.end()) {
        TagEntrySet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        if (prev->del_pending()) {
            list_.erase(prev);
        }
    }

    vmi->UpdatePolicySet(agent);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// ServiceVlan routines
/////////////////////////////////////////////////////////////////////////////
VmInterface::ServiceVlan::ServiceVlan() :
    ListEntry(), tag_(0), vrf_name_(""), addr_(0), old_addr_(0),
    addr6_(), old_addr6_(), smac_(), dmac_(), vrf_(NULL, this),
    label_(MplsTable::kInvalidLabel), v4_rt_installed_(false),
    v6_rt_installed_(false), del_add_(false) {
}

VmInterface::ServiceVlan::ServiceVlan(const ServiceVlan &rhs) :
    ListEntry(rhs.del_pending_), tag_(rhs.tag_),
    vrf_name_(rhs.vrf_name_), addr_(rhs.addr_), old_addr_(rhs.old_addr_),
    addr6_(rhs.addr6_), old_addr6_(rhs.old_addr6_),
    vrf_(rhs.vrf_, this), label_(rhs.label_),
    v4_rt_installed_(rhs.v4_rt_installed_),
    v6_rt_installed_(rhs.v6_rt_installed_), del_add_(false) {
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
    smac_(smac), dmac_(dmac), vrf_(NULL, this),
    label_(MplsTable::kInvalidLabel) , v4_rt_installed_(false),
    v6_rt_installed_(false), del_add_(false) {
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

void VmInterface::ServiceVlan::Update(const Agent *agent,
                                      VmInterface *vmi) const {
    InterfaceTable *table = static_cast<InterfaceTable *>(vmi->get_table());
    VrfEntry *vrf = table->FindVrfRef(vrf_name_);

    if (del_pending_ || vrf_ != vrf || vmi->ipv4_active() == false || del_add_) {
        if (v4_rt_installed_) {
            InetUnicastAgentRouteTable::Delete(vmi->peer(), vrf_->GetName(),
                                               old_addr_,
                                               Address::kMaxV4PrefixLen);
            v4_rt_installed_ = false;
        }
    }

    if (del_pending_ || vrf_ != vrf || vmi->ipv6_active() == false || del_add_) {
        if (v6_rt_installed_) {
            InetUnicastAgentRouteTable::Delete(vmi->peer(), vrf_->GetName(),
                                               old_addr6_,
                                               Address::kMaxV6PrefixLen);
            v6_rt_installed_ = false;
        }
    }

    if (del_pending_ || vrf_ != vrf || del_add_) {
        DeleteCommon(vmi);
    }

    vrf_ = vrf;
    old_addr_ = addr_;
    old_addr6_ = addr6_;
    bool old_del_add = del_add_;
    del_add_ = false;

    if (del_pending_ || vrf_ == NULL || old_del_add)
        return;

    if (vmi->ipv4_active() == false && vmi->ipv6_active() == false)
        return;

    if (label_ == MplsTable::kInvalidLabel) {
        AddCommon(agent, vmi);
    }

    SecurityGroupList sg_id_list;
    vmi->CopySgIdList(&sg_id_list);

    TagList tag_id_list;
    vmi->CopyTagIdList(&tag_id_list);

    VnListType vn_list;
    vn_list.insert(vmi->vn()->GetName());

    // Add both ipv4 *and* ipv6 service vlan route if one of v4 or v6 is active
    // TODO : Shouldnt route be added only if corresponding state is active?
    if (addr_.is_unspecified() == false) {
        PathPreference pref;
        vmi->SetServiceVlanPathPreference(&pref, addr_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (vmi->peer(), vrf_->GetName(), addr_, Address::kMaxV4PrefixLen,
             vmi->GetUuid(), tag_, label_, vn_list, sg_id_list,
             tag_id_list, pref);
        v4_rt_installed_ = true;
    }

    if (addr6_.is_unspecified() == false) {
        PathPreference pref;
        vmi->SetServiceVlanPathPreference(&pref, addr6_);

        InetUnicastAgentRouteTable::AddVlanNHRoute
            (vmi->peer(), vrf_->GetName(), addr6_, Address::kMaxV6PrefixLen,
             vmi->GetUuid(), tag_, label_, vn_list, sg_id_list,
             tag_id_list, pref);
        v6_rt_installed_ = true;
    }

    return;
}

void VmInterface::ServiceVlan::DeleteCommon(const VmInterface *vmi) const {
    if (label_ == MplsTable::kInvalidLabel)
        return;

    // Delete the L2 Recive routes added for smac_ and dmac_
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    if (table) {
        table->Delete(vmi->peer(), vrf_->GetName(), dmac_, 0);
        table->Delete(vmi->peer(), vrf_->GetName(), smac_, 0);
    }

    VrfAssignTable::DeleteVlan(vmi->GetUuid(), tag_);
    VlanNH::Delete(vmi->GetUuid(), tag_);
    label_ = MplsTable::kInvalidLabel;
}

void VmInterface::ServiceVlan::AddCommon(const Agent *agent,
                                         const VmInterface *vmi) const {
    if (label_ != MplsTable::kInvalidLabel)
        return;

    assert(vrf_);
    VlanNH::Create(vmi->GetUuid(), tag_, vrf_name_, smac_, dmac_);
    VrfAssignTable::CreateVlan(vmi->GetUuid(), vrf_name_, tag_);
    // Assign label_ from vlan NH db entry
    VlanNHKey key(vmi->GetUuid(), tag_);
    const NextHop *nh = static_cast<const NextHop *>
        (agent->nexthop_table()->FindActiveEntry(&key));
    label_ = nh->mpls_label()->label();

    // With IRB model, add L2 Receive route for SMAC and DMAC to ensure
    // packets from service vm go thru routing
    BridgeAgentRouteTable *table = static_cast<BridgeAgentRouteTable *>
        (vrf_->GetBridgeRouteTable());
    table->AddBridgeReceiveRoute(vmi->peer(), vrf_->GetName(), 0, dmac_,
                                 vmi->vn()->GetName());
    table->AddBridgeReceiveRoute(vmi->peer(), vrf_->GetName(), 0, smac_,
                                 vmi->vn()->GetName());
}

////////////////////////////////////////////////////////////////////////////
// VRF assign rule routines
////////////////////////////////////////////////////////////////////////////
VmInterface::VrfAssignRule::VrfAssignRule():
    ListEntry(), id_(0), vrf_name_(" "), ignore_acl_(false) {
}

VmInterface::VrfAssignRule::VrfAssignRule(const VrfAssignRule &rhs):
    ListEntry(rhs.del_pending_), id_(rhs.id_),
    vrf_name_(rhs.vrf_name_), ignore_acl_(rhs.ignore_acl_),
    match_condition_(rhs.match_condition_) {
}

VmInterface::VrfAssignRule::VrfAssignRule
(uint32_t id, const autogen::MatchConditionType &match_condition,
 const std::string &vrf_name, bool ignore_acl):
    ListEntry(), id_(id), vrf_name_(vrf_name), ignore_acl_(ignore_acl),
    match_condition_(match_condition) {
}

VmInterface::VrfAssignRule::~VrfAssignRule() {
}

bool VmInterface::VrfAssignRule::operator()(const VrfAssignRule &lhs,
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

bool VmInterface::VrfAssignRuleList::UpdateList
(const Agent *agent, VmInterface *vmi, VmInterfaceState::Op l2_force_op,
 VmInterfaceState::Op l3_force_op) {
    // Erase all delete marked entry
    VrfAssignRuleSet::iterator it = list_.begin();
    while (it != list_.end()) {
        VrfAssignRuleSet::iterator prev = it++;
        if (prev->del_pending_) {
            list_.erase(prev);
        }
    }

    // Delete vrf_assign_acl_ if there are no more ACE entries
    if (list_.size() == 0 || vmi->IsActive() == false) {
        if (vrf_assign_acl_.get() != NULL) {
            vrf_assign_acl_ = NULL;
            DBRequest req(DBRequest::DB_ENTRY_DELETE);
            req.key.reset(new AclKey(vmi->GetUuid()));
            req.data.reset(NULL);
            agent->acl_table()->Process(req);
        }
        return true;
    }

    // One or more ace-entries present, create corresponding ACL
    AclSpec acl_spec;
    acl_spec.acl_id = vmi->GetUuid();
    uint32_t id = 0;
    for (it = list_.begin(); it != list_.end(); it++) {
        AclEntrySpec ace_spec;
        ace_spec.id = id++;

        if (ace_spec.Populate(&(it->match_condition_)) == false) {
            continue;
        }

        // Add both v4 and v6 rules regardless of whether interface is
        //ipv4_active_/ipv6_active_
        ActionSpec vrf_translate_spec;
        vrf_translate_spec.ta_type = TrafficAction::VRF_TRANSLATE_ACTION;
        vrf_translate_spec.simple_action = TrafficAction::VRF_TRANSLATE;
        vrf_translate_spec.vrf_translate.set_vrf_name(it->vrf_name_);
        vrf_translate_spec.vrf_translate.set_ignore_acl(it->ignore_acl_);
        ace_spec.action_l.push_back(vrf_translate_spec);
        acl_spec.acl_entry_specs_.push_back(ace_spec);
    }

    // ACL entries populated, add the DBEntry
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new AclKey(acl_spec.acl_id));
    Agent *agent1 = static_cast<InterfaceTable *>(vmi->get_table())->agent();
    req.data.reset(new AclData(agent1, NULL, acl_spec));
    agent->acl_table()->Process(req);

    // Query the ACL entry and store it
    AclKey entry_key(vmi->GetUuid());
    vrf_assign_acl_ = static_cast<AclDBEntry *>
        (agent->acl_table()->FindActiveEntry(&entry_key));
    assert(vrf_assign_acl_);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// RecevieRoute routines
/////////////////////////////////////////////////////////////////////////////
void VmInterface::VmiReceiveRouteList::Insert(const VmiReceiveRoute *rhs) {
    list_.insert(*rhs);
}

void VmInterface::VmiReceiveRouteList::Update(const VmiReceiveRoute *lhs,
                                           const VmiReceiveRoute *rhs) {
    lhs->set_del_pending(false);
}

void VmInterface::VmiReceiveRouteList::Remove(VmiReceiveRouteSet::iterator &it) {
    it->set_del_pending(true);
}

bool VmInterface::VmiReceiveRouteList::UpdateList(const Agent *agent,
                                               VmInterface *vmi,
                                               VmInterfaceState::Op l2_force_op,
                                               VmInterfaceState::Op l3_force_op) {
    VmiReceiveRouteSet::iterator it = list_.begin();
    while (it != list_.end()) {
        VmiReceiveRouteSet::iterator prev = it++;
        VmInterfaceState::Op l2_op = prev->GetOp(l2_force_op);
        VmInterfaceState::Op l3_op = prev->GetOp(l3_force_op);
        vmi->UpdateState(&(*prev), l2_op, l3_op);
        if (prev->del_pending_) {
            list_.erase(prev);
        }
    }
    return true;
}

VmInterface::VmiReceiveRoute::VmiReceiveRoute() :
    ListEntry(), VmInterfaceState(), addr_(), plen_(0), add_l2_(false) {
}

VmInterface::VmiReceiveRoute::VmiReceiveRoute(const VmiReceiveRoute &rhs) :
    ListEntry(rhs.del_pending_),
    VmInterfaceState(rhs.l2_installed_, rhs.l3_installed_),
    addr_(rhs.addr_), plen_(rhs.plen_), add_l2_(rhs.add_l2_) {
}

VmInterface::VmiReceiveRoute::VmiReceiveRoute(const IpAddress &addr,
                                              uint32_t plen,
                                              bool add_l2):
    ListEntry(), VmInterfaceState(), addr_(addr), plen_(plen), add_l2_(add_l2) {
}

bool VmInterface::VmiReceiveRoute::operator() (const VmiReceiveRoute &lhs,
                                               const VmiReceiveRoute &rhs)
                                               const {
    return lhs.IsLess(&rhs);
}

bool VmInterface::VmiReceiveRoute::IsLess(const VmiReceiveRoute *rhs) const {
    if (addr_ != rhs->addr_) {
        return addr_ < rhs->addr_;
    }

    if (plen_ != rhs->plen_) {
        return plen_ < rhs->plen_;
    }

    return add_l2_ < rhs->add_l2_;
}

void VmInterface::VmiReceiveRoute::Copy(const Agent *agent,
                                        const VmInterface *vmi) const {
    vrf_ = vmi->forwarding_vrf();
}

VmInterfaceState::Op
VmInterface::VmiReceiveRoute::GetOpL3(const Agent *agent,
                                      const VmInterface *vmi) const {
    if (GetIpActiveState(addr_, vmi) == false)
        return VmInterfaceState::DEL;

    if (vrf_ != vmi->forwarding_vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

VmInterfaceState::Op
VmInterface::VmiReceiveRoute::GetOpL2(const Agent *agent,
                                      const VmInterface *vmi) const {
    if (add_l2_ == false) {
        return VmInterfaceState::INVALID;
    }

    if (vrf_ != vmi->forwarding_vrf())
        return VmInterfaceState::DEL_ADD;

    return VmInterfaceState::ADD;
}

bool VmInterface::VmiReceiveRoute::AddL3(const Agent *agent,
                                         VmInterface *vmi) const {
    if (vrf_ == NULL) {
        return false;
    }

    ReceiveNH::Create(agent->nexthop_table(), vmi, vmi->policy_enabled());
    ReceiveNH::Create(agent->nexthop_table(), vmi, false);
    InetUnicastAgentRouteTable *table = vrf_->GetInet4UnicastRouteTable();
    VmInterfaceKey vmi_key(AgentKey::ADD_DEL_CHANGE, vmi->GetUuid(),
                           vmi->name());
    if (addr_.is_v6()) {
        table = vrf_->GetInet6UnicastRouteTable();
    }

    table->AddVHostRecvRoute(agent->local_peer(), vrf_->GetName(), vmi_key,
                             addr_, plen_, agent->fabric_vn_name(),
                             vmi->policy_enabled(), true);
    return true;
}

bool VmInterface::VmiReceiveRoute::DeleteL3(const Agent *agent,
                                            VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    InetUnicastAgentRouteTable::Delete(agent->local_peer(), vrf_->GetName(),
                                       addr_, plen_);
    return true;
}

bool VmInterface::VmiReceiveRoute::AddL2(const Agent *agent,
                                         VmInterface *vmi) const {
    if (vrf_ == NULL) {
        return false;
    }

    BridgeAgentRouteTable *table =
        static_cast<BridgeAgentRouteTable *>(vrf_->GetRouteTable(Agent::BRIDGE));
    table->AddBridgeReceiveRoute(agent->local_peer(), vrf_->GetName(), 0,
                                 vmi->GetVifMac(agent), agent->fabric_vn_name());
    return true;
}

bool VmInterface::VmiReceiveRoute::DeleteL2(const Agent *agent,
                                            VmInterface *vmi) const {
    if (vrf_ == NULL)
        return false;

    BridgeAgentRouteTable::Delete(agent->local_peer(), vrf_->GetName(),
                                  vmi->GetVifMac(agent), 0);
    return true;
}
//Build ACL list to be applied on VMI
//ACL list build on two criteria
//1> global-application-policy set.
//2> application-policy-set attached via application tag
bool VmInterface::UpdatePolicySet(const Agent *agent) {
    bool ret = false;
    FirewallPolicyList new_firewall_policy_list;
    FirewallPolicyList new_fwaas_firewall_policy_list;

    PolicySet *gps = agent->policy_set_table()->global_policy_set();
    if (gps) {
        new_firewall_policy_list = gps->fw_policy_list();
    }

    TagEntrySet::const_iterator it = tag_list_.list_.begin();
    for(; it != tag_list_.list_.end(); it++) {
        if (it->tag_ == NULL) {
            continue;
        }

        ::TagEntry::PolicySetList::const_iterator ps_it =
            it->tag_->policy_set_list().begin();
        for(; ps_it != it->tag_->policy_set_list().end(); ps_it++) {
            FirewallPolicyList &tag_fp_list = ps_it->get()->fw_policy_list();
            FirewallPolicyList::iterator fw_policy_it = tag_fp_list.begin();
            for (; fw_policy_it != tag_fp_list.end(); fw_policy_it++) {
	        if (it->tag_->IsNeutronFwaasTag())
                    new_fwaas_firewall_policy_list.push_back(*fw_policy_it);
                else
                    new_firewall_policy_list.push_back(*fw_policy_it);
            }
        }
    }

    if (fw_policy_list_ != new_firewall_policy_list) {
        fw_policy_list_ = new_firewall_policy_list;
        ret = true;
    }

    if (fwaas_fw_policy_list_ != new_fwaas_firewall_policy_list) {
        fwaas_fw_policy_list_ = new_fwaas_firewall_policy_list;
        ret = true;
    }

    return ret;
}

void VmInterface::CopyTagIdList(TagList *tag_id_list) const {
    TagEntrySet::const_iterator it;
    for (it = tag_list_.list_.begin(); it != tag_list_.list_.end(); ++it) {
        if (it->del_pending_)
            continue;
        if (it->tag_.get() == NULL)
            continue;
        tag_id_list->push_back(it->tag_->tag_id());
    }
    std::sort(tag_id_list->begin(), tag_id_list->end());
}

void VmInterface::update_flow_count(int val) const {
    int max_flows = max_flows_;
    int new_flow_count = flow_count_.fetch_and_add(val);

    if (max_flows == 0) {
        // max_flows are not configured,
        // disable drop new flows and return
        SetInterfacesDropNewFlows(false);
        return;
    }

    if (val < 0) {
        assert(new_flow_count >= val);
        if ((new_flow_count + val) <
            ((max_flows * (Agent::kDropNewFlowsRecoveryThreshold))/100)) {
            SetInterfacesDropNewFlows(false);
        }
    } else {
        if ((new_flow_count + val) >= max_flows) {
            SetInterfacesDropNewFlows(true);
        }
    }
}

void VmInterface::SetInterfacesDropNewFlows(bool drop_new_flows) const {
    if (drop_new_flows_vmi_ == drop_new_flows) {
        return;
    }
    drop_new_flows_vmi_ = drop_new_flows;
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VmInterfaceKey(AgentKey::RESYNC,
                                     GetUuid(), ""));
    req.data.reset(new VmInterfaceNewFlowDropData(drop_new_flows));
    agent()->interface_table()->Enqueue(&req);
}
