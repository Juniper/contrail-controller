/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <base/address_util.h>
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

#include <filter/acl.h>
#include <port_ipc/port_ipc_handler.h>
#include <port_ipc/port_subscribe_table.h>
#include <resource_manager/resource_manager.h>
#include <resource_manager/resource_table.h>
#include <resource_manager/mpls_index.h>

#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/vnswif_listener_base.h>

using namespace std;
using namespace boost::uuids;
using namespace autogen;

static IpAddress ipv4_empty_addr = IpAddress::from_string("0.0.0.0");
static IpAddress ipv6_empty_addr = IpAddress::from_string("0::0");
static IpAddress ipv4_max_addr = IpAddress::from_string("255.255.255.255");
static IpAddress ipv6_max_addr =
            IpAddress::from_string("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF");

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
// VM Port active status related methods
/////////////////////////////////////////////////////////////////////////////
// Does the VMInterface need a physical device to be present
bool VmInterface::NeedDevice() const {
    bool ret = true;

    if (device_type_ == TOR)
        ret = false;

    if (device_type_ == VM_VLAN_ON_VMI)
        ret = false;

    if (vmi_type_ != VHOST && subnet_.is_unspecified() == false) {
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

bool VmInterface::NeedOsStateWithoutDevice() const {
    /* For TRANSPORT_PMD (in dpdk mode) interfaces, the link state is updated
     * as part of netlink message sent by vrouter to agent. This state is
     * updated in os_oper_state_ field */
    if (transport_ == TRANSPORT_PMD) {
        return true;
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    if (table) {
        return NeedDefaultOsOperStateDisabled(table->agent());
    }
    return false;
}

void VmInterface::GetOsParams(Agent *agent) {
    if (NeedDevice() || NeedOsStateWithoutDevice()) {
        Interface::GetOsParams(agent);
        return;
    }

    os_params_.os_index_ = Interface::kInvalidIndex;
    os_params_.mac_ = agent->vrrp_mac();
    /* In VmWare mode, the default os_oper_state of VMI is false. It needs to be
     * explicitly enabled via REST request to agent. This needs to be done
     * only for interfaces which are not of transport_ TRANSPORT_ETHERNET */
    if (!NeedDefaultOsOperStateDisabled(agent)) {
        os_params_.os_oper_state_ = true;
    }
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

    if (vmi_type_ == VHOST) {
        //In case of vhost interface, upon interface
        //addition we dont have corresponding VN, hence
        //ignore VN name check for ip active
        if (vrf_.get() == NULL) {
            return false;
        }
    } else if ((vn_.get() == NULL) || (vrf_.get() == NULL)) {
        return false;
    }

    if (vn_.get() && !vn_.get()->admin_state()) {
        return false;
    }

    if (NeedOsStateWithoutDevice() && os_oper_state() == false) {
        VnswInterfaceListener *vnswif =
            agent()->ksync()->vnsw_interface_listner();
            bool link_status = vnswif->IsHostLinkStateUp(name());
            if ((transport_ == TRANSPORT_PMD) && link_status &&
                (os_index() != Interface::kInvalidIndex) && (!agent()->test_mode())) {
                DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
                req.key.reset(new VmInterfaceKey(AgentKey::RESYNC, GetUuid(),
                                     name()));
                req.data.reset(new VmInterfaceOsOperStateData(link_status));
                agent()->interface_table()->Enqueue(&req);
                return true;
            }
        return false;
    }

    if (NeedDevice() == false) {
        return true;
    }

    if (os_index() == kInvalidIndex)
        return false;

    if (os_oper_state() == false)
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
    if (vmi_type_ == VHOST) {
        return IsActive();
    }

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
    if (vmi_type_ == VHOST) {
        return IsActive();
    }

    if (!layer3_forwarding() || (primary_ip6_addr_.is_unspecified())) {
        return false;
    }

    if (!is_hc_active_) {
        return false;
    }

    return IsActive();
}

bool VmInterface::IsL2Active() const {
    if (vmi_type_ == VHOST) {
        return IsActive();
    }

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
    if (local_preference_ != 0) {
        pref->set_static_preference(true);
        pref->set_preference(local_preference_);
    } else if (ecmp == true) {
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
    if (local_preference_ != 0) {
        pref->set_static_preference(true);
        pref->set_preference(local_preference_);
    } else if (ecmp_mode == true) {
        pref->set_preference(PathPreference::HIGH);
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

// Add EVPN route of type <ip, mac>
void VmInterface::AddL2InterfaceRoute(const IpAddress &ip,
                                      const MacAddress &mac,
                                      const IpAddress &dependent_ip) const {
    assert(peer_.get());
    EvpnAgentRouteTable *table =
        static_cast<EvpnAgentRouteTable *>(vrf_->GetEvpnRouteTable());

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    TagList tag_list;
    CopyTagIdList(&tag_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, false, dependent_ip);

    uint32_t label = l2_label_;
    if (pbb_interface()) {
        label = GetPbbLabel();
    }

    std::string vn_name = Agent::NullString();
    if (vn() != NULL) {
        vn_name = vn()->GetName();
    }

    table->AddLocalVmRoute(peer_.get(), vrf_->GetName(), mac, this, ip,
                           label, vn_name, sg_id_list,
                           tag_list, path_preference,
                           ethernet_tag_, etree_leaf_, name());
}

// Delete EVPN route
void VmInterface::DeleteL2InterfaceRoute(const VrfEntry *vrf,
                                         uint32_t ethernet_tag,
                                         const IpAddress &ip,
                                         const MacAddress &mac) const {
    EvpnAgentRouteTable *table =
        static_cast<EvpnAgentRouteTable *>(vrf->GetEvpnRouteTable());
    if (table == NULL)
        return;

    table->DelLocalVmRoute(peer_.get(), vrf->GetName(), mac, this,
                           ip, ethernet_tag);
}

//Add a route for VM port
//If ECMP route, add new composite NH and mpls label for same
void VmInterface::AddRoute(const std::string &vrf_name, const IpAddress &addr,
                           uint32_t plen, const std::string &dest_vn,
                           bool force_policy, bool ecmp, bool is_local,
                           bool is_health_check_service,
                           const IpAddress &service_ip,
                           const IpAddress &dependent_rt,
                           const CommunityList &communities, uint32_t label,
                           const std::string &intf_route_type) {

    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    TagList tag_list;
    CopyTagIdList(&tag_list);

    PathPreference path_preference;
    SetPathPreference(&path_preference, ecmp, dependent_rt);

    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    bool native_encap = false;
    VrfEntry *vrf = table->FindVrfRef(vrf_name);
    if (addr.is_v4() &&
        vrf && vrf->forwarding_vrf() == table->agent()->fabric_vrf()) {
        native_encap = true;
    }

    VnListType vn_list;
    vn_list.insert(dest_vn);
    EcmpLoadBalance ecmp_load_balance;
    CopyEcmpLoadBalance(ecmp_load_balance);
    InetUnicastAgentRouteTable::AddLocalVmRoute
        (peer_.get(), vrf_name, addr, plen, GetUuid(), vn_list, label,
         sg_id_list, tag_list, communities, force_policy, path_preference,
         service_ip, ecmp_load_balance, is_local, is_health_check_service,
         name(), native_encap, intf_route_type);
    return;
}

void VmInterface::ResolveRoute(const std::string &vrf_name,
                               const Ip4Address &addr, uint32_t plen,
                               const std::string &dest_vn, bool policy) {
    SecurityGroupList sg_id_list;
    CopySgIdList(&sg_id_list);

    TagList tag_list;
    CopyTagIdList(&tag_list);

    VmInterfaceKey vm_intf_key(AgentKey::ADD_DEL_CHANGE, GetUuid(), "");

    InetUnicastAgentRouteTable::AddResolveRoute
        (peer_.get(), vrf_name, Address::GetIp4SubnetAddress(addr, plen), plen,
         vm_intf_key, vrf_->table_label(), policy, dest_vn, sg_id_list,
         tag_list);
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
bool VmInterface::GetInterfaceDhcpOptions
(std::vector<autogen::DhcpOptionType> *options) const {
    if (oper_dhcp_options().are_dhcp_options_set()) {
        *options = oper_dhcp_options().dhcp_options();
        return true;
    }

    return false;
}

// DHCP options applicable to the Subnet to which the interface belongs
bool VmInterface::GetSubnetDhcpOptions
(std::vector<autogen::DhcpOptionType> *options, bool ipv6) const {
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
bool VmInterface::GetIpamDhcpOptions
(std::vector<autogen::DhcpOptionType> *options, bool ipv6) const {
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

/*
 * Check if the passed IP is in the AAP IP list on this interface;
 * If so, return true, else return false
 */
bool
VmInterface::MatchAapIp(const IpAddress &ip, uint8_t plen) const {
    AllowedAddressPairSet::const_iterator it =
        allowed_address_pair_list_.list_.begin();
    while (it != allowed_address_pair_list_.list_.end()) {
        if (it->addr_ == ip && it->plen_ == plen) {
            return true;
        }
        it++;
    }
    return false;
}

bool VmInterface::WaitForTraffic() const {
    // do not continue if the interface is inactive or if the VRF is deleted
    if (IsActive() == false || vrf_->IsDeleted()) {
        return false;
    }

    //Get the instance ip route and its corresponding traffic seen status
    InetUnicastRouteKey rt_key(peer_.get(), vrf_->GetName(),
                               primary_ip_addr_, 32);
    const InetUnicastRouteEntry *rt = static_cast<const InetUnicastRouteEntry *>
        (vrf_->GetInet4UnicastRouteTable()->FindActiveEntry(&rt_key));
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

IpAddress VmInterface::GetGatewayIp(const IpAddress &vm_ip) const {
    IpAddress ip;
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
        if ((vm_ip.is_v4() && ipam->default_gw.is_v4()) ||
            (vm_ip.is_v6() && ipam->default_gw.is_v6())) {
            return ipam->default_gw;
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

void VmInterface::InsertHealthCheckInstance(HealthCheckInstanceBase *hc_inst) {
    std::pair<HealthCheckInstanceSet::iterator, bool> ret;
    ret = hc_instance_set_.insert(hc_inst);
    assert(ret.second);
}

void VmInterface::DeleteHealthCheckInstance(HealthCheckInstanceBase *hc_inst) {
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
const HealthCheckInstanceBase *VmInterface::GetHealthCheckFromVmiFlow
(const IpAddress &sip, const IpAddress &dip, uint8_t proto,
 uint16_t sport) const {
    HealthCheckInstanceSet::const_iterator it = hc_instance_set_.begin();
    while (it != hc_instance_set_.end()) {
        const HealthCheckInstanceBase *hc_instance = *it;
        it++;

        // Match ip-proto and health-check port
        const HealthCheckService *hc_service = hc_instance->service();
        if (hc_service == NULL)
            continue;

        if (hc_service->ip_proto() != proto)
            continue;

        if (hc_service->url_port() != sport)
            continue;

        // The source-ip and destination-ip can be matched with the address
        // allocated for HealthCheck
        if (hc_instance->destination_ip() != sip)
            continue;

        if (hc_instance->source_ip() != dip)
            continue;

        return hc_instance;
    }

    return NULL;
}

// Check if the interface requires to resync the health check service instance
// Resync will be applied for healthcheck types BFD or Segment.
void VmInterface::UpdateInterfaceHealthCheckService()
{
    if (!IsHealthCheckEnabled() || !is_hc_active()) {
        return;
    }

    HealthCheckInstanceSet::const_iterator it = hc_instance_set_.begin();
    while (it != hc_instance_set_.end()) {
        const HealthCheckInstanceBase *hc_instance = *it;
        it++;
        HealthCheckService *hc_service = hc_instance->service();
        if (hc_service == NULL)
            continue;

        // resync will be applied healthcheck type is either BFD or Segment.
        HealthCheckService::HealthCheckType type =
                                     hc_service->health_check_type();
        if (type != HealthCheckService::BFD &&
            type != HealthCheckService::SEGMENT) {
            continue;
        }

        // if either source_ip or destination_ip unspecified,
        // resync the healtcheckservice
        if ((hc_instance->destination_ip().is_unspecified() == false) &&
                (hc_instance->get_source_ip().is_unspecified() == false)) {
            continue;
        }
        hc_service->ResyncHealthCheckInterface(hc_service, this);
    }
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

bool VmInterface::IsFatFlowPortBased(uint8_t protocol, uint16_t port,
                                     FatFlowIgnoreAddressType *ignore_addr) const {

    FatFlowEntrySet::iterator start = fat_flow_list_.list_.lower_bound(
                                         FatFlowEntry(protocol, port,
                                                      IGNORE_ADDRESS_MIN_VAL,
                                                      AGGREGATE_PREFIX_MIN_VAL));
    FatFlowEntrySet::iterator end = fat_flow_list_.list_.upper_bound(
                                         FatFlowEntry(protocol, port,
                                                      IGNORE_ADDRESS_MAX_VAL,
                                                      AGGREGATE_PREFIX_MIN_VAL));

    if (start != fat_flow_list_.list_.end()) {
        while (start != end) {
            if ((start->protocol == protocol) && (start->port == port) &&
                (start->prefix_aggregate == AGGREGATE_NONE)) {
                *ignore_addr = start->ignore_address;
                return true;
            }
            start++;
        }
    }
    return false;
}

bool VmInterface::MatchSrcPrefixPort(uint8_t protocol, uint16_t port,
                                     IpAddress *src_ip,
                                     FatFlowIgnoreAddressType *ignore_addr) const
{
    FatFlowPrefixAggregateType prefix_type, max_prefix_type;
    Ip4Address Ipv4_subnet, Ipv4_ret_subnet;
    Ip6Address Ipv6_subnet, Ipv6_ret_subnet;
    uint8_t max_mask, max_plen;
    const FatFlowEntry *match_ffe = NULL;

    *ignore_addr = VmInterface::IGNORE_NONE;

    if (src_ip->is_v4()) {
        prefix_type = AGGREGATE_SRC_IPV4;
        // set max_prefixtype to src_dst_ipv4 so that we can do LPM on src prefix
        max_prefix_type = AGGREGATE_SRC_DST_IPV4;
        Ipv4_subnet = Address::GetIp4SubnetAddress(src_ip->to_v4(),
                                                   FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 32;
        max_plen = 32;
    } else {
        prefix_type = AGGREGATE_SRC_IPV6;
        max_prefix_type = AGGREGATE_SRC_DST_IPV6;
        Ipv6_subnet = Address::GetIp6SubnetAddress(src_ip->to_v6(),
                                                   FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 128;
        max_plen = 128;
    }

    const FatFlowEntry lbffe = FatFlowEntry(protocol, port, "destination",
                                            prefix_type,
                                           (src_ip->is_v4()?
                                              IpAddress(Ipv4_subnet):
                                              IpAddress(Ipv6_subnet)),
                                            FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                                            FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                                           (src_ip->is_v4()?
                                             ipv4_empty_addr: ipv6_empty_addr
                                           ),
                                           0, 0);

    FatFlowEntrySet::iterator start = fat_flow_list_.list_.lower_bound(lbffe);

    const FatFlowEntry ubffe =
             FatFlowEntry(protocol, port, "none",
                          max_prefix_type,
                          *src_ip, max_mask, max_plen,
                          (src_ip->is_v4()?
                              ipv4_max_addr: ipv6_max_addr
                          ),
                          max_mask, max_plen);

    FatFlowEntrySet::iterator end = fat_flow_list_.list_.upper_bound(ubffe);

    if ((start != fat_flow_list_.list_.end()) &&
        (start->protocol == protocol) && (start->port == port)) {
        while (start != end) {
            if (src_ip->is_v4() && start->src_prefix.is_v4() &&
                IsIp4SubnetMember(src_ip->to_v4(), start->src_prefix.to_v4(),
                                  start->src_prefix_mask) &&
                (start->protocol == protocol) && (start->port == port)) {
                if ((!match_ffe) ||
                    (start->src_prefix_mask > match_ffe->src_prefix_mask)) {
                    match_ffe = &(*start);
                }
            } else if (src_ip->is_v6() && start->src_prefix.is_v6() &&
                       IsIp6SubnetMember(src_ip->to_v6(), start->src_prefix.to_v6(),
                                         start->src_prefix_mask) &&
                       (start->protocol == protocol) && (start->port == port)) {
                if ((!match_ffe) ||
                    (start->src_prefix_mask > match_ffe->src_prefix_mask)) {
                    match_ffe = &(*start);
                }
            }
            start++;
        }
    }
    if (match_ffe && (match_ffe->prefix_aggregate == prefix_type)) {
        if (src_ip->is_v4()) {
            Ipv4_ret_subnet = Address::GetIp4SubnetAddress(src_ip->to_v4(),
                                                   match_ffe->src_aggregate_plen);
            *src_ip = IpAddress(Ipv4_ret_subnet);
            *ignore_addr = match_ffe->ignore_address;
        } else {
            Ipv6_ret_subnet = Address::GetIp6SubnetAddress(src_ip->to_v6(),
                                                  match_ffe->src_aggregate_plen);
            *src_ip = IpAddress(Ipv6_ret_subnet);
            *ignore_addr = match_ffe->ignore_address;
        }
        return true;
    }
    return false;
}

bool VmInterface::MatchSrcPrefixRule(uint8_t protocol, uint16_t *sport,
                                     uint16_t *dport, bool *same_port_num,
                                     IpAddress *SrcIP,
                                     FatFlowIgnoreAddressType *ignore_addr) const
{
    if ((*sport) < (*dport)) {
        if (MatchSrcPrefixPort(protocol, *sport, SrcIP, ignore_addr)) {
            *dport = 0;
            return true;
        }
        if (MatchSrcPrefixPort(protocol, *dport, SrcIP, ignore_addr)) {
            *sport = 0;
            return true;
        }
    } else {
        if (MatchSrcPrefixPort(protocol, *dport, SrcIP, ignore_addr)) {
            if ((*sport) == (*dport)) {
                *same_port_num = true;
            }
            *sport = 0;
            return true;
        }
        if (MatchSrcPrefixPort(protocol, *sport, SrcIP, ignore_addr)) {
            *dport = 0;
            return true;
        }
    }
    if (MatchSrcPrefixPort(protocol, 0, SrcIP, ignore_addr)) {
        *sport = *dport = 0;
        return true;
    }
    return false;
}

bool VmInterface::MatchDstPrefixPort(uint8_t protocol, uint16_t port,
                                     IpAddress *dst_ip,
                                     FatFlowIgnoreAddressType *ignore_addr) const
{
    FatFlowPrefixAggregateType prefix_type;
    Ip4Address Ipv4_subnet, Ipv4_ret_subnet;
    Ip6Address Ipv6_subnet, Ipv6_ret_subnet;
    uint8_t max_mask, max_plen;
    const FatFlowEntry *match_ffe = NULL;

    *ignore_addr = VmInterface::IGNORE_NONE;

    if (dst_ip->is_v4()) {
        prefix_type = AGGREGATE_DST_IPV4;
        Ipv4_subnet = Address::GetIp4SubnetAddress(dst_ip->to_v4(),
                                                   FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 32;
        max_plen = 32;
    } else {
        prefix_type = AGGREGATE_DST_IPV6;
        Ipv6_subnet = Address::GetIp6SubnetAddress(dst_ip->to_v6(),
                                                   FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 128;
        max_plen = 128;
    }

    const FatFlowEntry lbffe =
                  FatFlowEntry(protocol, port, "source", prefix_type,
                               (dst_ip->is_v4()?
                                  ipv4_empty_addr:
                                  ipv6_empty_addr
                               ), 0, 0,
                               (dst_ip->is_v4()?
                                  IpAddress(Ipv4_subnet):
                                  IpAddress(Ipv6_subnet)),
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);

    FatFlowEntrySet::iterator start = fat_flow_list_.list_.lower_bound(lbffe);

    const FatFlowEntry ubffe = FatFlowEntry(protocol, port, "none",
                                            prefix_type,
                                           (dst_ip->is_v4()?
                                             ipv4_empty_addr:
                                             ipv6_empty_addr
                                           ), 0, 0,
                                           *dst_ip, max_mask, max_plen);

    FatFlowEntrySet::iterator end = fat_flow_list_.list_.upper_bound(ubffe);

    if ((start != fat_flow_list_.list_.end()) &&
        (start->protocol == protocol) && (start->port == port)) {
        while (start != end) {
            if (dst_ip->is_v4() && start->dst_prefix.is_v4() &&
                IsIp4SubnetMember(dst_ip->to_v4(), start->dst_prefix.to_v4(),
                                  start->dst_prefix_mask) &&
                (start->protocol == protocol) && (start->port == port)) {
                if ((!match_ffe) ||
                    (start->dst_prefix_mask > match_ffe->dst_prefix_mask)) {
                    match_ffe = &(*start);
                }
            } else if (dst_ip->is_v6() && start->dst_prefix.is_v6() &&
                       IsIp6SubnetMember(dst_ip->to_v6(), start->dst_prefix.to_v6(),
                                         start->dst_prefix_mask) &&
                       (start->protocol == protocol) && (start->port == port)) {
                if ((!match_ffe) ||
                    (start->dst_prefix_mask > match_ffe->dst_prefix_mask)) {
                    match_ffe = &(*start);
                }
            }
            start++;
        }
    }
    if (match_ffe && (match_ffe->prefix_aggregate == prefix_type)) {
        if (dst_ip->is_v4()) {
            Ipv4_ret_subnet = Address::GetIp4SubnetAddress(dst_ip->to_v4(),
                                             match_ffe->dst_aggregate_plen);
            *dst_ip = IpAddress(Ipv4_ret_subnet);
            *ignore_addr = match_ffe->ignore_address;
        } else {
            Ipv6_ret_subnet = Address::GetIp6SubnetAddress(dst_ip->to_v6(),
                                             match_ffe->dst_aggregate_plen);
            *dst_ip = IpAddress(Ipv6_ret_subnet);
            *ignore_addr = match_ffe->ignore_address;
        }
        return true;
    }
    return false;
}


bool VmInterface::MatchDstPrefixRule(uint8_t protocol, uint16_t *sport,
                                     uint16_t *dport, bool *same_port_num,
                                     IpAddress *DstIP,
                                     FatFlowIgnoreAddressType *ignore_addr) const
{
    if ((*sport) < (*dport)) {
        if (MatchDstPrefixPort(protocol, *sport, DstIP, ignore_addr)) {
            *dport = 0;
            return true;
        }
        if (MatchDstPrefixPort(protocol, *dport, DstIP, ignore_addr)) {
            *sport = 0;
            return true;
        }
    } else {
        if (MatchDstPrefixPort(protocol, *dport, DstIP, ignore_addr)) {
            if ((*sport) == (*dport)) {
                *same_port_num = true;
            }
            *sport = 0;
            return true;
        }
        if (MatchDstPrefixPort(protocol, *sport, DstIP, ignore_addr)) {
            *dport = 0;
            return true;
        }
    }
    if (MatchDstPrefixPort(protocol, 0, DstIP, ignore_addr)) {
        *sport = *dport = 0;
        return true;
    }
    return false;
}


bool VmInterface::MatchSrcDstPrefixPort(uint8_t protocol, uint16_t port,
                                        IpAddress *src_ip,
                                        IpAddress *dst_ip) const
{
    FatFlowPrefixAggregateType prefix_type;
    Ip4Address Ipv4_src_subnet, Ipv4_src_ret_subnet;
    Ip4Address Ipv4_dst_subnet, Ipv4_dst_ret_subnet;
    Ip6Address Ipv6_src_subnet, Ipv6_src_ret_subnet;
    Ip6Address Ipv6_dst_subnet, Ipv6_dst_ret_subnet;
    uint8_t max_mask, max_plen;
    const FatFlowEntry *match_ffe = NULL;

    if (src_ip->is_v4()) {
        prefix_type = AGGREGATE_SRC_DST_IPV4;
        Ipv4_src_subnet = Address::GetIp4SubnetAddress(src_ip->to_v4(),
                                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        Ipv4_dst_subnet = Address::GetIp4SubnetAddress(dst_ip->to_v4(),
                                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 32;
        max_plen = 32;
    } else {
        prefix_type = AGGREGATE_SRC_DST_IPV6;
        Ipv6_src_subnet = Address::GetIp6SubnetAddress(src_ip->to_v6(),
                                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        Ipv6_dst_subnet = Address::GetIp6SubnetAddress(dst_ip->to_v6(),
                                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);
        max_mask = 128;
        max_plen = 128;
    }

    const FatFlowEntry lbffe =
                  FatFlowEntry(protocol, port, "none",
                               prefix_type,
                               (src_ip->is_v4()?
                                  IpAddress(Ipv4_src_subnet):
                                  IpAddress(Ipv6_src_subnet)),
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                               (dst_ip->is_v4()?
                                  IpAddress(Ipv4_dst_subnet):
                                  IpAddress(Ipv6_dst_subnet)),
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN,
                               FAT_FLOW_ENTRY_MIN_PREFIX_LEN);

    FatFlowEntrySet::iterator start = fat_flow_list_.list_.lower_bound(lbffe);

    const FatFlowEntry ubffe = FatFlowEntry(protocol, port, "none", prefix_type,
                               *src_ip, max_mask, max_plen,
                               *dst_ip, max_mask, max_plen);

    FatFlowEntrySet::iterator end = fat_flow_list_.list_.upper_bound(ubffe);

    if ((start != fat_flow_list_.list_.end()) &&
        (start->protocol == protocol) && (start->port == port)) {
        while (start != end) {
            if (src_ip->is_v4() && start->src_prefix.is_v4() &&
                start->dst_prefix.is_v4() &&
                IsIp4SubnetMember(src_ip->to_v4(), start->src_prefix.to_v4(),
                                  start->src_prefix_mask) &&
                IsIp4SubnetMember(dst_ip->to_v4(), start->dst_prefix.to_v4(),
                                  start->dst_prefix_mask) &&
                (start->protocol == protocol) && (start->port == port)) {
                match_ffe = &(*start);
            } else if (src_ip->is_v6() && start->src_prefix.is_v6() &&
                       start->dst_prefix.is_v6() &&
                       IsIp6SubnetMember(src_ip->to_v6(), start->src_prefix.to_v6(),
                                         start->src_prefix_mask) &&
                       IsIp6SubnetMember(dst_ip->to_v6(), start->dst_prefix.to_v6(),
                                         start->dst_prefix_mask) &&
                       (start->protocol == protocol) && (start->port == port)) {
                match_ffe = &(*start);
            }
            start++;
        }
    }
    if (match_ffe && (match_ffe->prefix_aggregate == prefix_type)) {
        if (src_ip->is_v4()) {
            Ipv4_src_ret_subnet =
                       Address::GetIp4SubnetAddress(src_ip->to_v4(),
                                                    match_ffe->src_aggregate_plen);
            Ipv4_dst_ret_subnet =
                       Address::GetIp4SubnetAddress(dst_ip->to_v4(),
                                                    match_ffe->dst_aggregate_plen);
            *src_ip = IpAddress(Ipv4_src_ret_subnet);
            *dst_ip = IpAddress(Ipv4_dst_ret_subnet);
        } else {
            Ipv6_src_ret_subnet =
                      Address::GetIp6SubnetAddress(src_ip->to_v6(),
                                                   match_ffe->src_aggregate_plen);
            Ipv6_dst_ret_subnet =
                      Address::GetIp6SubnetAddress(dst_ip->to_v6(),
                                                   match_ffe->dst_aggregate_plen);
            *src_ip = IpAddress(Ipv6_src_ret_subnet);
            *dst_ip = IpAddress(Ipv6_dst_ret_subnet);
        }
        return true;
    }
    return false;
}


bool VmInterface::MatchSrcDstPrefixRule(uint8_t protocol, uint16_t *sport,
                                        uint16_t *dport, bool *same_port_num,
                                        IpAddress *SrcIP, IpAddress *DstIP) const
{
    if ((*sport) < (*dport)) {
        if (MatchSrcDstPrefixPort(protocol, *sport, SrcIP, DstIP)) {
            *dport = 0;
            return true;
        }
        if (MatchSrcDstPrefixPort(protocol, *dport, SrcIP, DstIP)) {
            *sport = 0;
            return true;
        }
    } else {
        if (MatchSrcDstPrefixPort(protocol, *dport, SrcIP, DstIP)) {
            if ((*sport) == (*dport)) {
                *same_port_num = true;
            }
            *sport = 0;
            return true;
        }
        if (MatchSrcDstPrefixPort(protocol, *sport, SrcIP, DstIP)) {
            *dport = 0;
            return true;
        }
    }
    if (MatchSrcDstPrefixPort(protocol, 0, SrcIP, DstIP)) {
        *sport = *dport = 0;
        return true;
    }
    return false;
}


bool VmInterface::IsFatFlowPrefixAggregation(bool ingress, uint8_t protocol,
                                             uint16_t *sport, uint16_t *dport,
                                             bool *same_port_num,
                                             IpAddress *SrcIP, IpAddress *DstIP,
                                             bool *is_fat_flow_src_prefix,
                                             bool *is_fat_flow_dst_prefix,
                                             FatFlowIgnoreAddressType
                                                  *ignore_addr) const
{
    uint16_t *src_port = sport,
             *dst_port = dport;
    IpAddress *src_ip = (ingress? SrcIP: DstIP),
              *dst_ip = (ingress? DstIP: SrcIP);
    bool *is_src_prefix = (ingress? is_fat_flow_src_prefix : is_fat_flow_dst_prefix);
    bool *is_dst_prefix = (ingress? is_fat_flow_dst_prefix : is_fat_flow_src_prefix);
    bool ret;
    FatFlowIgnoreAddressType ign_addr;

    *is_src_prefix = false;
    *is_dst_prefix = false;

    /* Match Src prefix rule first */
    if (MatchSrcPrefixRule(protocol, src_port, dst_port, same_port_num,
                           src_ip, &ign_addr)) {
        *is_src_prefix = true;
        *ignore_addr = ign_addr;
        return true;
    }
    /* Match Dst prefix rule */
    if (MatchDstPrefixRule(protocol, src_port, dst_port, same_port_num,
                           dst_ip, &ign_addr)) {
        *is_dst_prefix = true;
        *ignore_addr = ign_addr;
        return true;
    }
    /* Match Src+Dst prefix rule */
    ret = MatchSrcDstPrefixRule(protocol, src_port, dst_port, same_port_num,
                                src_ip, dst_ip);
    if (ret) {
        *is_src_prefix = true;
        *is_dst_prefix = true;
    }

    return ret;
}


bool VmInterface::ExcludeFromFatFlow(Address::Family family,
                                     const IpAddress &sip,
                                     const IpAddress &dip) const {
    if (family == Address::INET) {
        IpAddress gw_ip = GetGatewayIp(primary_ip_addr_);
        if ((sip == gw_ip) || (dip == gw_ip)) {
            return true;
        }
        IpAddress service_ip = GetServiceIp(primary_ip_addr_);
        if ((sip == service_ip) || (dip == service_ip)) {
            return true;
        }
        boost::system::error_code ec;
        Ip4Address ll_subnet = Ip4Address::from_string
            (agent()->v4_link_local_subnet(), ec);
        if (IsIp4SubnetMember(sip.to_v4(), ll_subnet,
                              agent()->v4_link_local_plen())) {
            return true;
        }
        if (IsIp4SubnetMember(dip.to_v4(), ll_subnet,
                              agent()->v4_link_local_plen())) {
            return true;
        }
    } else if (family == Address::INET6){
        IpAddress gw_ip = GetGatewayIp(primary_ip6_addr_);
        if ((sip == gw_ip) || (dip == gw_ip)) {
            return true;
        }
        IpAddress service_ip = GetServiceIp(primary_ip6_addr_);
        if ((sip == service_ip) || (dip == service_ip)) {
            return true;
        }
        boost::system::error_code ec;
        Ip6Address ll6_subnet = Ip6Address::from_string
            (agent()->v6_link_local_subnet(), ec);
        if (IsIp6SubnetMember(sip.to_v6(), ll6_subnet,
                              agent()->v6_link_local_plen())) {
            return true;
        }
        if (IsIp6SubnetMember(dip.to_v6(), ll6_subnet,
                              agent()->v6_link_local_plen())) {
            return true;
        }
    }
    return false;
}

void VmInterface::FillV4ExcludeIp(uint64_t plen, const Ip4Address &ip,
                                  VmInterface::FatFlowExcludeList *list) const {
    uint64_t value = 0;
    if (!ip.is_unspecified()) {
        value = plen | htonl(ip.to_ulong());
        list->fat_flow_v4_exclude_list_.push_back(value);
    }
}

void VmInterface::FillV6ExcludeIp(uint16_t plen, const IpAddress &ip,
                                  VmInterface::FatFlowExcludeList *list) const {
    if (ip.is_v6() && !ip.is_unspecified()) {
        uint64_t ip_arr[2];
        Ip6AddressToU64Array(ip.to_v6(), ip_arr, 2);
        list->fat_flow_v6_exclude_upper_list_.push_back(ip_arr[0]);
        list->fat_flow_v6_exclude_lower_list_.push_back(ip_arr[1]);
        list->fat_flow_v6_exclude_plen_list_.push_back(plen);
    }
}

void VmInterface::BuildFatFlowExcludeList
(VmInterface::FatFlowExcludeList *list) const {
    /* Build the list afresh each time. So clear the list */
    list->Clear();

    /* Build IPv4 exclude-list */
    IpAddress gw_ip = GetGatewayIp(primary_ip_addr_);
    uint64_t plen = (uint64_t(Address::kMaxV4PrefixLen) << 32);
    FillV4ExcludeIp(plen, gw_ip.to_v4(), list);
    IpAddress service_ip = GetServiceIp(primary_ip_addr_);
    FillV4ExcludeIp(plen, service_ip.to_v4(), list);
    boost::system::error_code ec;
    Ip4Address ll_subnet = Ip4Address::from_string
        (agent()->v4_link_local_subnet(), ec);
    plen = (uint64_t(agent()->v4_link_local_plen()) << 32);
    FillV4ExcludeIp(plen, ll_subnet, list);

    /* Build IPv6 exclude-list */
    IpAddress gw6_ip = GetGatewayIp(primary_ip6_addr_);
    FillV6ExcludeIp(128, gw6_ip, list);
    IpAddress service6_ip = GetServiceIp(primary_ip6_addr_);
    FillV6ExcludeIp(128, service6_ip, list);
    Ip6Address ll6_subnet = Ip6Address::from_string
        (agent()->v6_link_local_subnet(), ec);
    FillV6ExcludeIp(agent()->v6_link_local_plen(), ll6_subnet, list);
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

    /* For a l2-only VN's VMI, policy should be implicitly disabled */
    if (layer3_forwarding_ == false) {
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
MetaDataIp *VmInterface::GetMetaDataIp(const Ip4Address &ip) const {
    MetaDataIpMap::const_iterator it = metadata_ip_map_.find(ip);
    if (it != metadata_ip_map_.end()) {
        return it->second;
    }

    return NULL;
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
        table->DhcpSnoopSetConfigSeen(name());
        // IP Address not know. Get DHCP Snoop entry.
        // Also sets the config_seen_ flag for DHCP Snoop entry
        addr = table->GetDhcpSnoopEntry(name());
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

VmInterface * VmInterface::PortTuplePairedInterface() const {
    if (si_other_end_vmi_ == nil_uuid()) {
        return NULL;
    }
    InterfaceTable *table = static_cast<InterfaceTable *>(get_table());
    VmInterfaceKey key(AgentKey::ADD_DEL_CHANGE, si_other_end_vmi_, "");
    VmInterface *intf = static_cast<VmInterface *>(table->Find(&key, false));
    return intf;
}

void VmInterface::SendTrace(const AgentDBTable *table, Trace event) const {
    InterfaceInfo intf_info;
    intf_info.set_name(name());
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
    case DEL:
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
