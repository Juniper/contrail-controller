/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/os.h>
#include <base/parse_object.h>
#include <base/util.h>
#include <base/address_util.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>

#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>
#include <oper/oper_dhcp_options.h>
#include <oper/physical_device_vn.h>
#include <oper/config_manager.h>
#include <oper/global_vrouter.h>
#include <oper/agent_route_resync.h>
#include <oper/qos_config.h>
#include <filter/acl.h>

using namespace autogen;
using namespace std;
using namespace boost;
using boost::assign::map_list_of;
using boost::assign::list_of;

VnTable *VnTable::vn_table_;

/////////////////////////////////////////////////////////////////////////////
// VnIpam routines
/////////////////////////////////////////////////////////////////////////////
VnIpam::VnIpam(const std::string& ip, uint32_t len, const std::string& gw,
               const std::string& dns, bool dhcp, const std::string &name,
               const std::vector<autogen::DhcpOptionType> &dhcp_options,
               const std::vector<autogen::RouteType> &host_routes,
               uint32_t alloc)
        : plen(len), installed(false), dhcp_enable(dhcp), ipam_name(name),
          alloc_unit(alloc) {
    boost::system::error_code ec;
    ip_prefix = IpAddress::from_string(ip, ec);
    default_gw = IpAddress::from_string(gw, ec);
    dns_server = IpAddress::from_string(dns, ec);
    oper_dhcp_options.set_options(dhcp_options);
    oper_dhcp_options.set_host_routes(host_routes);
}

Ip4Address VnIpam::GetSubnetAddress() const {
    if (ip_prefix.is_v4()) {
        return Address::GetIp4SubnetAddress(ip_prefix.to_v4(), plen);
    }
    return Ip4Address(0);
}

Ip6Address VnIpam::GetV6SubnetAddress() const {
    if (ip_prefix.is_v6()) {
        return Address::GetIp6SubnetAddress(ip_prefix.to_v6(), plen);
    }
    return Ip6Address();
}

bool VnIpam::IsSubnetMember(const IpAddress &ip) const {
    if (ip_prefix.is_v4() && ip.is_v4()) {
        return ((ip_prefix.to_v4().to_ulong() |
                 ~(0xFFFFFFFF << (32 - plen))) ==
                (ip.to_v4().to_ulong() | ~(0xFFFFFFFF << (32 - plen))));
    } else if (ip_prefix.is_v6() && ip.is_v6()) {
        return IsIp6SubnetMember(ip.to_v6(), ip_prefix.to_v6(), plen);
    }
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// VnEntry routines
/////////////////////////////////////////////////////////////////////////////
VnEntry::VnEntry(Agent *agent, boost::uuids::uuid id) :
    AgentOperDBEntry(), agent_(agent), uuid_(id), vrf_(NULL, this),
    vxlan_id_(0), vnid_(0), active_vxlan_id_(0), bridging_(true),
    layer3_forwarding_(true), admin_state_(true), table_label_(0),
    enable_rpf_(true), flood_unknown_unicast_(false),
    forwarding_mode_(Agent::L2_L3), mirror_destination_(false),
    underlay_forwarding_(false), vxlan_routing_vn_(false),
    logical_router_uuid_(), cfg_igmp_enable_(false), vn_max_flows_(0),
    lr_vrf_(NULL, this) {
}

VnEntry::~VnEntry() {
}

bool VnEntry::IsLess(const DBEntry &rhs) const {
    const VnEntry &a = static_cast<const VnEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string VnEntry::ToString() const {
    std::stringstream uuidstring;
    uuidstring << uuid_;
    return uuidstring.str();
}

DBEntryBase::KeyPtr VnEntry::GetDBRequestKey() const {
    return DBEntryBase::KeyPtr(new VnKey(uuid_));
}

void VnEntry::SetKey(const DBRequestKey *key) {
    const VnKey *k = static_cast<const VnKey *>(key);
    uuid_ = k->uuid_;
}

// Resync operation for VN. Invoked on change of forwarding-mode or global
// vxlan-config mode
bool VnEntry::Resync(Agent *agent) {
    // Evaluate rebake of vxlan
    bool ret = UpdateVxlan(agent, false);
    // Evaluate forwarding mode change
    ret |= UpdateForwardingMode(agent);
    // Update routes from Ipam
    ret |= ApplyAllIpam(agent, vrf_.get(), false);
    return ret;
}

bool VnEntry::ChangeHandler(Agent *agent, const DBRequest *req) {
    bool ret = false;
    VnData *data = static_cast<VnData *>(req->data.get());

    AclKey key(data->acl_id_);
    AclDBEntry *acl = static_cast<AclDBEntry *>
        (agent->acl_table()->FindActiveEntry(&key));
    if (acl_.get() != acl) {
        acl_ = acl;
        ret = true;
    }

    AclKey mirror_key(data->mirror_acl_id_);
    AclDBEntry *mirror_acl = static_cast<AclDBEntry *>
        (agent->acl_table()->FindActiveEntry(&mirror_key));
    if (mirror_acl_.get() != mirror_acl) {
        mirror_acl_ = mirror_acl;
        ret = true;
    }

    AclKey mirror_cfg_acl_key(data->mirror_cfg_acl_id_);
    AclDBEntry *mirror_cfg_acl = static_cast<AclDBEntry *>
         (agent->acl_table()->FindActiveEntry(&mirror_cfg_acl_key));
    if (mirror_cfg_acl_.get() != mirror_cfg_acl) {
        mirror_cfg_acl_ = mirror_cfg_acl;
        ret = true;
    }

    AgentQosConfigKey qos_config_key(data->qos_config_uuid_);
    AgentQosConfig *qos_config = static_cast<AgentQosConfig *>
        (agent->qos_config_table()->FindActiveEntry(&qos_config_key));
    if (qos_config_.get() != qos_config) {
        qos_config_ = qos_config;
        ret = true;
    }

    VrfKey vrf_key(data->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>
        (agent->vrf_table()->FindActiveEntry(&vrf_key));
    VrfEntryRef old_vrf = vrf_;
    bool rebake_vxlan = false;
    if (vrf != old_vrf.get()) {
        if (!vrf) {
            ApplyAllIpam(agent, old_vrf.get(), true);
        }
        vrf_ = vrf;
        rebake_vxlan = true;
        ret = true;
    }

    if (admin_state_ != data->admin_state_) {
        admin_state_ = data->admin_state_;
        ret = true;
    }

    if (forwarding_mode_ != data->forwarding_mode_) {
        forwarding_mode_ = data->forwarding_mode_;
        ret = true;
    }

    // Recompute the forwarding modes in VN
    // Must rebake the routes if any change in forwarding modes
    bool resync_routes = false;
    resync_routes = UpdateForwardingMode(agent);
    ret |= resync_routes;

    // Update the routes derived from IPAM
    ret |= UpdateIpam(agent, data->ipam_);

    if (vn_ipam_data_ != data->vn_ipam_data_) {
        vn_ipam_data_ = data->vn_ipam_data_;
        ret = true;
    }

    if (enable_rpf_ != data->enable_rpf_) {
        enable_rpf_ = data->enable_rpf_;
        ret = true;
    }

    if (vxlan_id_ != data->vxlan_id_) {
        vxlan_id_ = data->vxlan_id_;
        ret = true;
        if (agent->vxlan_network_identifier_mode() == Agent::CONFIGURED) {
            rebake_vxlan = true;
        }
    }

    if (vnid_ != data->vnid_) {
        vnid_ = data->vnid_;
        ret = true;
        if (agent->vxlan_network_identifier_mode() == Agent::AUTOMATIC) {
            rebake_vxlan = true;
        }
    }

    if (flood_unknown_unicast_ != data->flood_unknown_unicast_) {
        flood_unknown_unicast_ = data->flood_unknown_unicast_;
        rebake_vxlan = true;
        ret = true;
    }

    if (mirror_destination_ != data->mirror_destination_) {
        mirror_destination_ = data->mirror_destination_;
        rebake_vxlan = true;
        ret = true;
    }

    if (rebake_vxlan) {
        ret |= UpdateVxlan(agent, false);
    }

    if (resync_routes) {
        if (vrf_.get() != NULL) {
            AgentRouteResync *resync =
                (static_cast<AgentRouteResync *>(route_resync_walker_.get()));
            resync->UpdateRoutesInVrf(vrf_.get());
        }
    }

    if (pbb_evpn_enable_ != data->pbb_evpn_enable_) {
        pbb_evpn_enable_ = data->pbb_evpn_enable_;
        ret = true;
    }

    if (pbb_etree_enable_ != data->pbb_etree_enable_) {
        pbb_etree_enable_ = data->pbb_etree_enable_;
        ret = true;
    }

    if (layer2_control_word_ != data->layer2_control_word_) {
        layer2_control_word_ = data->layer2_control_word_;
        ret = true;
    }

    if (underlay_forwarding_ != data->underlay_forwarding_) {
        underlay_forwarding_ = data->underlay_forwarding_;
        ret = true;
    }

    if (slo_list_ != data->slo_list_) {
        slo_list_ = data->slo_list_;
    }

    if (vxlan_routing_vn_ != data->vxlan_routing_vn_) {
        vxlan_routing_vn_ = data->vxlan_routing_vn_;
        ret = true;
    }

    if (logical_router_uuid_ != data->logical_router_uuid_) {
        logical_router_uuid_ = data->logical_router_uuid_;
        ret = true;
    }

    if (mp_list_ != data->mp_list_) {
        mp_list_ = data->mp_list_;
    }

    if (cfg_igmp_enable_ != data->cfg_igmp_enable_) {
        cfg_igmp_enable_ = data->cfg_igmp_enable_;
    }

    if (vn_max_flows_ != data->vn_max_flows_) {
        vn_max_flows_ = data->vn_max_flows_;
        ret = true;
    }

    return ret;
}

// Rebake handles
// - vxlan-id change :
//   Deletes the config-entry for old-vxlan and adds config-entry for new-vxlan
//   Might result in change of vxlan_id_ref_ for the VN
//
//   If vxlan_id is 0, or vrf is NULL, its treated as delete of config-entry
// - Delete
//   Deletes the vxlan-config entry. Will reset the vxlan-id-ref to NULL
bool VnEntry::UpdateVxlan(Agent *agent, bool op_del) {
    int old_vxlan = active_vxlan_id_;
    int new_vxlan = GetVxLanId();

    if (op_del || old_vxlan != new_vxlan || vrf_.get() == NULL) {
        if (old_vxlan != 0) {
            agent->vxlan_table()->Delete(old_vxlan, uuid_);
            vxlan_id_ref_ = NULL;
            active_vxlan_id_ = 0;
        }
    }

    if (op_del == false && vrf_.get() != NULL && new_vxlan != 0) {
        vxlan_id_ref_ = agent->vxlan_table()->Locate
            (new_vxlan, uuid_, vrf_->GetName(), flood_unknown_unicast_,
             mirror_destination_, false, true);
        active_vxlan_id_ = new_vxlan;
    }

    return (old_vxlan != new_vxlan);
}

// Compute the layer3_forwarding_ and bridging mode for the VN.
// Forwarding mode is picked from VN if its defined. Else, picks forwarding
// mode from global-vrouter-config
bool VnEntry::UpdateForwardingMode(Agent *agent) {
    Agent::ForwardingMode forwarding_mode = forwarding_mode_;
    if (forwarding_mode == Agent::NONE) {
        forwarding_mode =
            agent->oper_db()->global_vrouter()->forwarding_mode();
    }

    bool ret = false;
    bool routing = (forwarding_mode == Agent::L2) ? false : true;
    if (routing != layer3_forwarding_) {
        layer3_forwarding_ = routing;
        ret = true;
    }

    bool bridging = (forwarding_mode == Agent::L3) ? false : true;
    if (bridging != bridging_) {
        bridging_ = bridging;
        ret = true;
    }

    return ret;
}

// Update all IPAM configurations. Typically invoked when forwarding-mode
// changes or VRF is set for the VN
bool VnEntry::ApplyAllIpam(Agent *agent, VrfEntry *old_vrf, bool del) {
    bool ret = false;
    for (unsigned int i = 0; i < ipam_.size(); ++i) {
        ret |= ApplyIpam(agent, &ipam_[i], old_vrf, del);
    }

    return ret;
}

// Add/Delete routes dervied from a single IPAM
bool VnEntry::ApplyIpam(Agent *agent, VnIpam *ipam, VrfEntry *old_vrf,
                        bool del) {
    if (CanInstallIpam(ipam) == false)
        del = true;

    bool old_installed = ipam->installed;
    if (del == false) {
        ipam->installed = AddIpamRoutes(agent, ipam);
    } else {
        DelIpamRoutes(agent, ipam, old_vrf);
        ipam->installed = false;
    }

    return old_installed != ipam->installed;
}

bool VnEntry::CanInstallIpam(const VnIpam *ipam) {
    if (vrf_.get() == NULL || layer3_forwarding_ == false)
        return false;
    return true;
}

// Check the old and new Ipam data and update receive routes for GW addresses
bool VnEntry::UpdateIpam(Agent *agent, std::vector<VnIpam> &new_ipam) {
    std::sort(new_ipam.begin(), new_ipam.end());

    std::vector<VnIpam>::iterator it_old = ipam_.begin();
    std::vector<VnIpam>::iterator it_new = new_ipam.begin();
    bool change = false;
    while (it_old != ipam_.end() && it_new != new_ipam.end()) {
        if (*it_old < *it_new) {
            // old entry is deleted
            ApplyIpam(agent, &(*it_old), vrf_.get(), true);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            ApplyIpam(agent, &(*it_new), vrf_.get(), false);
            change = true;
            it_new++;
        } else {
            change |= HandleIpamChange(agent, &(*it_old), &(*it_new));
            it_old++;
            it_new++;
        }
    }

    // delete remaining old entries
    for (; it_old != ipam_.end(); ++it_old) {
        ApplyIpam(agent, &(*it_old), vrf_.get(), true);
        change = true;
    }

    // add remaining new entries
    for (; it_new != new_ipam.end(); ++it_new) {
        ApplyIpam(agent, &(*it_new), vrf_.get(), false);
        change = true;
    }

    ipam_ = new_ipam;
    return change;
}

static bool IsGwHostRouteRequired(const Agent *agent) {
    return (!agent->tsn_enabled());
}

// Evaluate non key members of IPAM for changes. Key fields are not changed
// Handles changes to gateway-ip and service-ip in the IPAM
bool VnEntry::HandleIpamChange(Agent *agent, VnIpam *old_ipam,
                               VnIpam *new_ipam) {
    bool install = CanInstallIpam(new_ipam);

    if (install == false && old_ipam->installed == false)
        return false;

    if (install == false && old_ipam->installed == true) {
        ApplyIpam(agent, old_ipam, vrf_.get(), true);
        return true;
    }

    if (install == true && old_ipam->installed == false) {
        ApplyIpam(agent, new_ipam, vrf_.get(), false);
        return true;
    }

    new_ipam->installed = install;
    bool changed = false;
    bool policy = (agent->tsn_enabled()) ? false : true;
    // CEM-4646:don't delete route if is used by DNS/GW
    // Update: GW changed?NO, DNS changed?YES
    // action: check if old DNS address is same as GW address
    // Update: GW changed?YES, DNS changed?NO
    // action: check if old GW address is same as DNS address
    // Update: GW changed?YES, DNS changed?YES
    // action: check if new DNS server address is same as old GW address

    if (vrf_.get() && (vrf_->GetName() != agent->linklocal_vrf_name())) {
        if (old_ipam->default_gw != new_ipam->default_gw) {
            changed = true;
            if (IsGwHostRouteRequired(agent)) {
                if (old_ipam->default_gw != new_ipam->dns_server) {
                    UpdateHostRoute(agent, old_ipam->default_gw,
                            new_ipam->default_gw, policy);
                } else {
                    AddHostRoute(new_ipam->default_gw, policy);
                }
            }
        }
    }

    if (old_ipam->dns_server != new_ipam->dns_server) {
        if (old_ipam->dns_server != new_ipam->default_gw) {
            UpdateHostRoute(agent, old_ipam->dns_server,
                    new_ipam->dns_server, policy);
        } else {
            AddHostRoute(new_ipam->dns_server, policy);
        }
        changed = true;
    }

    // update DHCP options
    old_ipam->oper_dhcp_options = new_ipam->oper_dhcp_options;

    if (old_ipam->dhcp_enable != new_ipam->dhcp_enable) {
        changed = true;
    }

    return changed;
}

void VnEntry::UpdateHostRoute(Agent *agent, const IpAddress &old_address,
                              const IpAddress &new_address,
                              bool policy) {
    AddHostRoute(new_address, policy);
    DelHostRoute(old_address);
}

// Add all routes derived from IPAM. IPAM generates following routes,
// - Subnet route
// - Gateway route
// - Service-IP route
bool VnEntry::AddIpamRoutes(Agent *agent, VnIpam *ipam) {
    if (vrf_ == NULL)
        return false;

    // Do not let the gateway configuration overwrite the receive nh.
    if (vrf_->GetName() == agent->linklocal_vrf_name()) {
        return false;
    }

    // Allways policy will be enabled for default Gateway and
    // Dns server to create flows for BGP as service even
    // though explicit disable policy config form user.
    bool policy = (agent->tsn_enabled()) ? false : true;
    if (IsGwHostRouteRequired(agent))
        AddHostRoute(ipam->default_gw, policy);
    AddHostRoute(ipam->dns_server, policy);
    AddSubnetRoute(ipam);
    return true;
}

// Delete routes generated from an IPAM
void VnEntry::DelIpamRoutes(Agent *agent, VnIpam *ipam, VrfEntry *vrf) {
    if (ipam->installed == false)
        return;

    assert(vrf);
    if (IsGwHostRouteRequired(agent))
        DelHostRoute(ipam->default_gw);
    DelHostRoute(ipam->dns_server);
    DelSubnetRoute(ipam);
}

// Add host route for gateway-ip or service-ip
void VnEntry::AddHostRoute(const IpAddress &address, bool policy) {
    if (address.is_v4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet4UnicastRouteTable())->AddHostRoute(vrf_->GetName(),
                address.to_v4(), 32, GetName(), policy);
    } else if (address.is_v6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet6UnicastRouteTable())->AddHostRoute(vrf_->GetName(),
                address.to_v6(), 128, GetName(), policy);
    }
}

// Del host route for gateway-ip or service-ip
void VnEntry::DelHostRoute(const IpAddress &address) {
    Agent *agent = static_cast<VnTable *>(get_table())->agent();
    if (address.is_v4()) {
        static_cast<InetUnicastAgentRouteTable *>
            (vrf_->GetInet4UnicastRouteTable())->DeleteReq
            (agent->local_peer(), vrf_->GetName(), address.to_v4(), 32, NULL);
    } else if (address.is_v6()) {
        static_cast<InetUnicastAgentRouteTable *>
            (vrf_->GetInet6UnicastRouteTable())->DeleteReq
            (agent->local_peer(), vrf_->GetName(), address.to_v6(), 128,
             NULL);
    }
}

// Add subnet route for the IPAM
void VnEntry::AddSubnetRoute(VnIpam *ipam) {
    if (ipam->IsV4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet4UnicastRouteTable())->AddIpamSubnetRoute
            (vrf_->GetName(), ipam->GetSubnetAddress(), ipam->plen, GetName());
    } else if (ipam->IsV6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet6UnicastRouteTable())->AddIpamSubnetRoute
            (vrf_->GetName(), ipam->GetV6SubnetAddress(), ipam->plen,
             GetName());
    }
}

// Del subnet route for the IPAM
void VnEntry::DelSubnetRoute(VnIpam *ipam) {
    Agent *agent = static_cast<VnTable *>(get_table())->agent();
    if (ipam->IsV4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet4UnicastRouteTable())->DeleteReq
            (agent->local_peer(), vrf_->GetName(),
             ipam->GetSubnetAddress(), ipam->plen, NULL);
    } else if (ipam->IsV6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf_->
            GetInet6UnicastRouteTable())->DeleteReq
            (agent->local_peer(), vrf_->GetName(),
             ipam->GetV6SubnetAddress(), ipam->plen, NULL);
    }
}

void VnEntry::AllocWalker() {
    assert(route_resync_walker_.get() == NULL);
    route_resync_walker_ = new AgentRouteResync("VnRouteResyncWalker",
                                                agent_);
    agent_->oper_db()->agent_route_walk_manager()->
        RegisterWalker(static_cast<AgentRouteWalker *>
                       (route_resync_walker_.get()));
}

void VnEntry::ReleaseWalker() {
    if (route_resync_walker_.get() == NULL)
        return;
    agent_->oper_db()->agent_route_walk_manager()->
        ReleaseWalker(route_resync_walker_.get());
    route_resync_walker_.reset(NULL);
}

bool VnEntry::GetVnHostRoutes(const std::string &ipam,
                              std::vector<OperDhcpOptions::HostRoute> *routes) const {
    VnData::VnIpamDataMap::const_iterator it = vn_ipam_data_.find(ipam);
    if (it != vn_ipam_data_.end()) {
        *routes = it->second.oper_dhcp_options_.host_routes();
        return true;
    }
    return false;
}

bool VnEntry::GetIpamName(const IpAddress &vm_addr,
                          std::string *ipam_name) const {
    for (unsigned int i = 0; i < ipam_.size(); i++) {
        if (ipam_[i].IsSubnetMember(vm_addr)) {
            *ipam_name = ipam_[i].ipam_name;
            return true;
        }
    }
    return false;
}

bool VnEntry::GetIpamData(const IpAddress &vm_addr,
                          std::string *ipam_name,
                          autogen::IpamType *ipam) const {
    if (!GetIpamName(vm_addr, ipam_name) ||
        !agent_->domain_config_table()->GetIpam(*ipam_name, ipam))
        return false;

    return true;
}

const VnIpam *VnEntry::GetIpam(const IpAddress &ip) const {
    for (unsigned int i = 0; i < ipam_.size(); i++) {
        if (ipam_[i].IsSubnetMember(ip)) {
            return &ipam_[i];
        }
    }
    return NULL;
}

IpAddress VnEntry::GetGatewayFromIpam(const IpAddress &ip) const {
    const VnIpam *ipam = GetIpam(ip);
    if (ipam) {
        return ipam->default_gw;
    }
    return IpAddress();
}

IpAddress VnEntry::GetDnsFromIpam(const IpAddress &ip) const {
    const VnIpam *ipam = GetIpam(ip);
    if (ipam) {
        return ipam->dns_server;
    }
    return IpAddress();
}

uint32_t VnEntry::GetAllocUnitFromIpam(const IpAddress &ip) const {
    const VnIpam *ipam = GetIpam(ip);
    if (ipam) {
        return ipam->alloc_unit;
    }
    return 1;//Default value
}

bool VnEntry::GetIpamVdnsData(const IpAddress &vm_addr,
                              autogen::IpamType *ipam_type,
                              autogen::VirtualDnsType *vdns_type) const {
    std::string ipam_name;
    if (!GetIpamName(vm_addr, &ipam_name) ||
        !agent_->domain_config_table()->GetIpam(ipam_name, ipam_type) ||
        ipam_type->ipam_dns_method != "virtual-dns-server")
        return false;

    if (!agent_->domain_config_table()->GetVDns(
                ipam_type->ipam_dns_server.virtual_dns_server_name, vdns_type))
        return false;

    return true;
}

bool VnEntry::GetPrefix(const Ip6Address &ip, Ip6Address *prefix,
                        uint8_t *plen) const {
    for (uint32_t i = 0; i < ipam_.size(); ++i) {
        if (!ipam_[i].IsV6()) {
            continue;
        }
        if (IsIp6SubnetMember(ip, ipam_[i].ip_prefix.to_v6(),
                              ipam_[i].plen)) {
            *prefix = ipam_[i].ip_prefix.to_v6();
            *plen = (uint8_t)ipam_[i].plen;
            return true;
        }
    }
    return false;
}

std::string VnEntry::GetProject() const {
    // TODO: update to get the project name from project-vn link.
    // Currently, this info doesnt come to the agent
    std::string name(name_.c_str());
    char *saveptr;
    if (strtok_r(const_cast<char *>(name.c_str()), ":", &saveptr) == NULL)
        return "";
    char *project = strtok_r(NULL, ":", &saveptr);
    return (project == NULL) ? "" : std::string(project);
}

int VnEntry::GetVxLanId() const {
    if (agent_->vxlan_network_identifier_mode() == Agent::CONFIGURED) {
        return vxlan_id_;
    }
    if (vxlan_routing_vn_ && vxlan_id_) {
        return vxlan_id_;
    }
    return vnid_;
}

/////////////////////////////////////////////////////////////////////////////
// VnTable routines
/////////////////////////////////////////////////////////////////////////////
VnTable::VnTable(DB *db, const std::string &name) : AgentOperDBTable(db, name) {
    walk_ref_ = AllocWalker(boost::bind(&VnTable::VnEntryWalk, this, _1, _2),
                            boost::bind(&VnTable::VnEntryWalkDone, this, _1,
                                        _2));
}

VnTable::~VnTable() {
}

void VnTable::Clear() {
    ReleaseWalker(walk_ref_);
}

DBTableBase *VnTable::CreateTable(DB *db, const std::string &name) {
    vn_table_ = new VnTable(db, name);
    vn_table_->Init();
    return vn_table_;
};

VnEntry *VnTable::Find(const boost::uuids::uuid &u) {
    VnKey key(u);
    return static_cast<VnEntry *>(FindActiveEntry(&key));
}

std::auto_ptr<DBEntry> VnTable::AllocEntry(const DBRequestKey *k) const {
    const VnKey *key = static_cast<const VnKey *>(k);
    VnEntry *vn = new VnEntry(agent(), key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vn));
}

DBEntry *VnTable::OperDBAdd(const DBRequest *req) {
    VnKey *key = static_cast<VnKey *>(req->key.get());
    VnData *data = static_cast<VnData *>(req->data.get());
    VnEntry *vn = new VnEntry(agent(), key->uuid_);
    vn->name_ = data->name_;
    vn->AllocWalker();

    vn->ChangeHandler(agent(), req);
    vn->SendObjectLog(AgentLogEvent::ADD);

    if (vn->name_ == agent()->fabric_vn_name()) {
        //In case of distributes SNAT we want all
        //interfaces to revaluated upon receiving
        //the config
        agent()->set_fabric_vn_uuid(key->uuid_);
        agent()->cfg()->cfg_vm_interface_table()->NotifyAllEntries();
    }
    return vn;
}

bool VnTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    vn->ApplyAllIpam(agent(), vn->vrf_.get(), true);
    vn->UpdateVxlan(agent(), true);
    vn->ReleaseWalker();
    vn->SendObjectLog(AgentLogEvent::DEL);

    if (vn->name_ == agent()->fabric_vn_name()) {
        agent()->set_fabric_vn_uuid(boost::uuids::nil_uuid());
        agent()->cfg()->cfg_vm_interface_table()->NotifyAllEntries();
    }
    return true;
}

bool VnTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    bool ret = vn->ChangeHandler(agent(), req);
    vn->SendObjectLog(AgentLogEvent::CHANGE);
    if (ret) {
        VnData *data = static_cast<VnData *>(req->data.get());
        if (data && data->ifmap_node()) {
            agent()->oper_db()->dependency_manager()->PropogateNodeChange
                (data->ifmap_node());
        }
    }
    return ret;
}

bool VnTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    return vn->Resync(agent());
}

bool VnTable::VnEntryWalk(DBTablePartBase *partition, DBEntryBase *entry) {
    VnEntry *vn_entry = static_cast<VnEntry *>(entry);
    if (vn_entry->GetVrf()) {
        ResyncReq(vn_entry->GetUuid());
    }
    return true;
}

void VnTable::VnEntryWalkDone(DBTable::DBTableWalkRef walk_ref,
                              DBTableBase *partition) {
    agent()->interface_table()->GlobalVrouterConfigChanged();
    agent()->physical_device_vn_table()->
        UpdateVxLanNetworkIdentifierMode();
}

void VnTable::GlobalVrouterConfigChanged() {
    WalkAgain(walk_ref_);
}

/*
 * IsVRFServiceChainingInstance
 * Helper function to identify the service chain vrf.
 * In VN-VRF mapping config can send multiple VRF for same VN.
 * Since assumption had been made that VN to VRF is always 1:1 this
 * situation needs to be handled.
 * This is a temporary solution in which all VRF other than primary VRF
 * is ignored.
 * Example:
 * VN name: domain:vn1
 * VRF 1: domain:vn1:vn1 ----> Primary
 * VRF 2: domain:vn1:service-vn1_vn2 ----> Second vrf linked to vn1
 * Break the VN and VRF name into tokens based on ':'
 * So in primary vrf last and second-last token will be same and will be
 * equal to last token of VN name. Keep this VRF.
 * The second VRF last and second-last token are not same so it will be ignored.
 *
 */
bool IsVRFServiceChainingInstance(const string &vn_name, const string &vrf_name)
{
    vector<string> vn_token_result;
    vector<string> vrf_token_result;
    uint32_t vrf_token_result_size = 0;

    split(vn_token_result, vn_name, is_any_of(":"), token_compress_on);
    split(vrf_token_result, vrf_name, is_any_of(":"), token_compress_on);
    vrf_token_result_size = vrf_token_result.size();

    /*
     * This check is to handle test cases where vrf and vn name
     * are single word without discriminator ':'
     * e.g. vrf1 or vn1. In these cases we dont ignore and use
     * the VRF
     */
    if (vrf_token_result_size == 1) {
        return false;
    }
    if ((vrf_token_result.at(vrf_token_result_size - 1) ==
        vrf_token_result.at(vrf_token_result_size - 2)) &&
        (vn_token_result.at(vn_token_result.size() - 1) ==
         vrf_token_result.at(vrf_token_result_size - 1))) {
        return false;
    }
    return true;
}

IFMapNode *VnTable::FindTarget(IFMapAgentTable *table, IFMapNode *node,
                               std::string node_type) {
    for (DBGraphVertex::adjacency_iterator it = node->begin(table->GetGraph());
         it != node->end(table->GetGraph()); ++it) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(it.operator->());
        if (adj_node->table()->Typename() == node_type)
            return adj_node;
    }
    return NULL;
}

int VnTable::GetCfgVnId(VirtualNetwork *cfg_vn) {
    if (cfg_vn->IsPropertySet(autogen::VirtualNetwork::NETWORK_ID))
        return cfg_vn->network_id();
    else
        return cfg_vn->properties().network_id;
}

void VnTable::CfgForwardingFlags(IFMapNode *node,
                                 bool *rpf, bool *flood_unknown_unicast,
                                 Agent::ForwardingMode *forwarding_mode,
                                 bool *mirror_destination) {
    *rpf = true;

    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    autogen::VirtualNetworkType properties = cfg->properties();

    if (properties.rpf == "disable") {
        *rpf = false;
    }

    *flood_unknown_unicast = cfg->flood_unknown_unicast();
    *mirror_destination = properties.mirror_destination;
    *forwarding_mode =
        agent()->TranslateForwardingMode(properties.forwarding_mode);
 }

void
VnTable::BuildVnIpamData(const std::vector<autogen::IpamSubnetType> &subnets,
                         const std::string &ipam_name,
                         std::vector<VnIpam> *vn_ipam) {
    for (unsigned int i = 0; i < subnets.size(); ++i) {
        // if the DNS server address is not specified, set this
        // to be the same as the GW address
        std::string dns_server_address = subnets[i].dns_server_address;
        boost::system::error_code ec;
        IpAddress dns_server =
            IpAddress::from_string(dns_server_address, ec);
        if (ec.value() || dns_server.is_unspecified()) {
            dns_server_address = subnets[i].default_gateway;
        }

        vn_ipam->push_back
            (VnIpam(subnets[i].subnet.ip_prefix,
                    subnets[i].subnet.ip_prefix_len,
                    subnets[i].default_gateway,
                    dns_server_address,
                    subnets[i].enable_dhcp, ipam_name,
                    subnets[i].dhcp_option_list.dhcp_option,
                    subnets[i].host_routes.route,
                    subnets[i].alloc_unit));
    }
}

VnData *VnTable::BuildData(IFMapNode *node) {
    using boost::uuids::uuid;
    using boost::uuids::nil_uuid;
    boost::uuids::uuid mp_uuid = nil_uuid();
    UuidList mp_list;

    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    assert(cfg);

    uuid acl_uuid = nil_uuid();
    uuid mirror_cfg_acl_uuid = nil_uuid();
    uuid qos_config_uuid = nil_uuid();
    string vrf_name = "";
    std::vector<VnIpam> vn_ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    std::string ipam_name;
    UuidList slo_list;
    bool underlay_forwarding = false;
    bool vxlan_routing_vn = false;
    uuid logical_router_uuid = nil_uuid();

    // Find link with ACL / VRF adjacency
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(table->GetGraph()); ++iter) {

        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent()->config_manager()->SkipNode(adj_node)) {
            continue;
        }

        if (adj_node->table() == agent()->cfg()->cfg_acl_table()) {
            AccessControlList *acl_cfg = static_cast<AccessControlList *>
                (adj_node->GetObject());
            assert(acl_cfg);
            autogen::IdPermsType id_perms = acl_cfg->id_perms();
            if (acl_cfg->entries().dynamic) {
                CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                           mirror_cfg_acl_uuid);
            } else {
                CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                           acl_uuid);
            }
        }

        if ((adj_node->table() == agent()->cfg()->cfg_vrf_table()) &&
            (!IsVRFServiceChainingInstance(node->name(), adj_node->name()))) {
            vrf_name = adj_node->name();
        }

        if (adj_node->table() == agent()->cfg()->cfg_qos_table()) {
            autogen::QosConfig *qc =
                static_cast<autogen::QosConfig *>(adj_node->GetObject());
            autogen::IdPermsType id_perms = qc->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                        qos_config_uuid);
        }

        if (adj_node->table() == agent()->cfg()->cfg_slo_table()) {
            uuid slo_uuid = nil_uuid();
            autogen::SecurityLoggingObject *slo =
                static_cast<autogen::SecurityLoggingObject *>(adj_node->
                                                              GetObject());
            autogen::IdPermsType id_perms = slo->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                       slo_uuid);
            slo_list.push_back(slo_uuid);
        }

        if (adj_node->table() == agent()->cfg()->cfg_vn_network_ipam_table()) {
            if (IFMapNode *ipam_node = FindTarget(table, adj_node,
                                                  "network-ipam")) {
                ipam_name = ipam_node->name();
                VirtualNetworkNetworkIpam *vnni =
                    static_cast<VirtualNetworkNetworkIpam *>
                    (adj_node->GetObject());
                VnSubnetsType subnets;
                if (vnni)
                    subnets = vnni->data();

                autogen::NetworkIpam *network_ipam =
                    static_cast<autogen::NetworkIpam *>(ipam_node->GetObject());
                const std::string subnet_method =
                    boost::to_lower_copy(network_ipam->ipam_subnet_method());

                if (subnet_method == "flat-subnet") {
                    BuildVnIpamData(network_ipam->ipam_subnets(),
                                    ipam_name, &vn_ipam);
                } else {
                    BuildVnIpamData(subnets.ipam_subnets, ipam_name, &vn_ipam);
                }

                VnIpamLinkData ipam_data;
                ipam_data.oper_dhcp_options_.set_host_routes(subnets.host_routes.route);
                vn_ipam_data.insert(VnData::VnIpamDataPair(ipam_name, ipam_data));
            }
        }

        if (adj_node->table() == agent()->cfg()->cfg_vn_table()) {
            if (agent()->fabric_vn_name() == adj_node->name()) {
                underlay_forwarding = true;
            }
        }

        if (strcmp(adj_node->table()->Typename(),
                   "logical-router-virtual-network") == 0) {
            autogen::LogicalRouterVirtualNetwork *lr_vn_node =
                dynamic_cast<autogen::LogicalRouterVirtualNetwork *>
                (adj_node->GetObject());
            autogen::LogicalRouter *lr = NULL;
            IFMapNode *lr_adj_node =
                agent()->config_manager()->FindAdjacentIFMapNode(adj_node,
                                                                 "logical-router");
            if (lr_adj_node) {
                lr = dynamic_cast<autogen::LogicalRouter *>(lr_adj_node->
                                                            GetObject());
            }

            if (lr_vn_node && lr) {
                autogen::IdPermsType id_perms = lr->id_perms();
                CfgUuidSet(id_perms.uuid.uuid_mslong,
                           id_perms.uuid.uuid_lslong,
                           logical_router_uuid);
                autogen::LogicalRouterVirtualNetworkType data =
                    lr_vn_node->data();
                if (data.logical_router_virtual_network_type ==
                    "InternalVirtualNetwork")
                {
                    vxlan_routing_vn = true;
                }
            }
        }

        if (adj_node->table() ==
                            agent()->cfg()->cfg_multicast_policy_table()) {
            MulticastPolicy *mcast_group = static_cast<MulticastPolicy *>
                            (adj_node->GetObject());
            assert(mcast_group);
            autogen::IdPermsType id_perms = mcast_group->id_perms();
            CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                           mp_uuid);
            mp_list.push_back(mp_uuid);
        }
    }

    uuid mirror_acl_uuid = agent()->mirror_cfg_table()->GetMirrorUuid(node->name());
    std::sort(vn_ipam.begin(), vn_ipam.end());

    // Fetch VN forwarding Properties
    bool enable_rpf;
    bool flood_unknown_unicast;
    bool mirror_destination;
    bool pbb_evpn_enable = cfg->pbb_evpn_enable();
    bool pbb_etree_enable = cfg->pbb_etree_enable();
    bool layer2_control_word = cfg->layer2_control_word();
    bool cfg_igmp_enable = cfg->igmp_enable();
    uint32_t vn_max_flows = cfg->properties().max_flows;
    Agent::ForwardingMode forwarding_mode;
    CfgForwardingFlags(node, &enable_rpf, &flood_unknown_unicast,
                       &forwarding_mode, &mirror_destination);
    return new VnData(agent(), node, node->name(), acl_uuid, vrf_name,
                      mirror_acl_uuid, mirror_cfg_acl_uuid, vn_ipam,
                      vn_ipam_data, cfg->properties().vxlan_network_identifier,
                      GetCfgVnId(cfg), cfg->id_perms().enable, enable_rpf,
                      flood_unknown_unicast, forwarding_mode,
                      qos_config_uuid, mirror_destination, pbb_etree_enable,
                      pbb_evpn_enable, layer2_control_word, slo_list,
                      underlay_forwarding, vxlan_routing_vn,
                      logical_router_uuid, mp_list, cfg_igmp_enable,
                      vn_max_flows);
}

bool VnTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool VnTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {

    assert(!u.is_nil());

    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
        req.key.reset(new VnKey(u));
        req.oper = DBRequest::DB_ENTRY_DELETE;
        Enqueue(&req);
        return false;
    }

    agent()->config_manager()->AddVnNode(node);
    return false;
}

bool VnTable::ProcessConfig(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {

    if (node->IsDeleted()) {
        return false;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(new VnKey(u));
    req.data.reset(BuildData(node));
    Enqueue(&req);
    return false;
}

void VnTable::AddVn(const boost::uuids::uuid &vn_uuid, const string &name,
                    const boost::uuids::uuid &acl_id, const string &vrf_name,
                    const std::vector<VnIpam> &ipam,
                    const VnData::VnIpamDataMap &vn_ipam_data, int vn_id,
                    int vxlan_id, bool admin_state, bool enable_rpf,
                    bool flood_unknown_unicast, bool pbb_etree_enable,
                    bool pbb_evpn_enable, bool layer2_control_word) {
    using boost::uuids::nil_uuid;

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    UuidList empty_list;
    req.key.reset(new VnKey(vn_uuid));
    bool mirror_destination = false;
    VnData *data = new VnData(agent(), NULL, name, acl_id, vrf_name, nil_uuid(),
                              nil_uuid(), ipam, vn_ipam_data,
                              vxlan_id, vn_id, admin_state, enable_rpf,
                              flood_unknown_unicast, Agent::NONE, nil_uuid(),
                              mirror_destination, pbb_etree_enable,
                              pbb_evpn_enable, layer2_control_word, empty_list,
                              false, false, nil_uuid(), empty_list, false, 0);

    req.data.reset(data);
    Enqueue(&req);
}

void VnTable::DelVn(const boost::uuids::uuid &vn_uuid) {
    DBRequest req(DBRequest::DB_ENTRY_DELETE);
    req.key.reset(new VnKey(vn_uuid));
    req.data.reset(NULL);
    Enqueue(&req);
}

void VnTable::ResyncReq(const boost::uuids::uuid &vn) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    VnKey *key = new VnKey(vn);
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    req.data.reset(NULL);
    Enqueue(&req);
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
bool VnEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    VnListResp *resp = static_cast<VnListResp *>(sresp);

    VnSandeshData data;
    data.set_name(GetName());
    data.set_uuid(UuidToString(GetUuid()));
    data.set_vxlan_id(GetVxLanId());
    data.set_config_vxlan_id(vxlan_id_);
    data.set_vn_id(vnid_);
    if (GetAcl()) {
        data.set_acl_uuid(UuidToString(GetAcl()->GetUuid()));
    } else {
        data.set_acl_uuid("");
    }

    if (GetVrf()) {
        data.set_vrf_name(GetVrf()->GetName());
    } else {
        data.set_vrf_name("");
    }

    if (GetMirrorAcl()) {
        data.set_mirror_acl_uuid(UuidToString(GetMirrorAcl()->GetUuid()));
    } else {
        data.set_mirror_acl_uuid("");
    }

    if (GetMirrorCfgAcl()) {
        data.set_mirror_cfg_acl_uuid(UuidToString(GetMirrorCfgAcl()->GetUuid()));
    } else {
        data.set_mirror_cfg_acl_uuid("");
    }

    std::vector<VnIpamData> vn_subnet_sandesh_list;
    const std::vector<VnIpam> &vn_ipam = GetVnIpam();
    for (unsigned int i = 0; i < vn_ipam.size(); ++i) {
        VnIpamData entry;
        entry.set_ip_prefix(vn_ipam[i].ip_prefix.to_string());
        entry.set_prefix_len(vn_ipam[i].plen);
        entry.set_gateway(vn_ipam[i].default_gw.to_string());
        entry.set_ipam_name(vn_ipam[i].ipam_name);
        entry.set_dhcp_enable(vn_ipam[i].dhcp_enable ? "true" : "false");
        entry.set_dns_server(vn_ipam[i].dns_server.to_string());
        vn_subnet_sandesh_list.push_back(entry);
    }
    data.set_ipam_data(vn_subnet_sandesh_list);

    std::vector<VnIpamHostRoutes> vn_ipam_host_routes_list;
    for (VnData::VnIpamDataMap::const_iterator it = vn_ipam_data_.begin();
        it != vn_ipam_data_.end(); ++it) {
        VnIpamHostRoutes vn_ipam_host_routes;
        vn_ipam_host_routes.ipam_name = it->first;
        const std::vector<OperDhcpOptions::HostRoute> &host_routes =
            it->second.oper_dhcp_options_.host_routes();
        for (uint32_t i = 0; i < host_routes.size(); ++i) {
            vn_ipam_host_routes.host_routes.push_back(
                host_routes[i].ToString());
        }
        vn_ipam_host_routes_list.push_back(vn_ipam_host_routes);
    }
    data.set_ipam_host_routes(vn_ipam_host_routes_list);
    data.set_ipv4_forwarding(layer3_forwarding());
    data.set_layer2_forwarding(bridging());
    data.set_bridging(bridging());
    data.set_admin_state(admin_state());
    data.set_enable_rpf(enable_rpf());
    data.set_flood_unknown_unicast(flood_unknown_unicast());
    data.set_pbb_etree_enabled(pbb_etree_enable());
    data.set_layer2_control_word(layer2_control_word());
    std::vector<SecurityLoggingObjectLink> slo_list;
    UuidList::const_iterator sit = slo_list_.begin();
    while (sit != slo_list_.end()) {
        SecurityLoggingObjectLink slo_entry;
        slo_entry.set_slo_uuid(to_string(*sit));
        slo_list.push_back(slo_entry);
        ++sit;
    }
    data.set_slo_list(slo_list);
    std::vector<VnSandeshData> &list =
        const_cast<std::vector<VnSandeshData>&>(resp->get_vn_list());
    std::vector<MulticastPolicyLink> mp_list;
    UuidList::const_iterator mpit = mp_list_.begin();
    while (mpit != mp_list_.end()) {
        MulticastPolicyLink mp_entry;
        mp_entry.set_mp_uuid(to_string(*mpit));
        mp_list.push_back(mp_entry);
        ++mpit;
    }
    data.set_mp_list(mp_list);
    data.set_cfg_igmp_enable(cfg_igmp_enable());
    data.set_max_flows(vn_max_flows());
    list.push_back(data);
    return true;
}

void VnEntry::SendObjectLog(AgentLogEvent::type event) const {
    VnObjectLogInfo info;
    string str;
    string vn_uuid = UuidToString(GetUuid());
    const AclDBEntry *acl = GetAcl();
    const AclDBEntry *mirror_acl = GetMirrorAcl();
    const AclDBEntry *mirror_cfg_acl = GetMirrorCfgAcl();
    string acl_uuid;
    string mirror_acl_uuid;
    string mirror_cfg_acl_uuid;

    info.set_uuid(vn_uuid);
    info.set_name(GetName());
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DEL:
            str.assign("Deletion ");
            info.set_event(str);
            VN_OBJECT_LOG_LOG("AgentVn", SandeshLevel::SYS_INFO, info);
            return;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        default:
            str.assign("");
            break;
    }

    info.set_event(str);
    if (acl) {
        acl_uuid.assign(UuidToString(acl->GetUuid()));
        info.set_acl_uuid(acl_uuid);
    }
    if (mirror_acl) {
        mirror_acl_uuid.assign(UuidToString(mirror_acl->GetUuid()));
        info.set_mirror_acl_uuid(mirror_acl_uuid);
    }
    if (mirror_cfg_acl) {
        mirror_cfg_acl_uuid.assign(UuidToString(mirror_cfg_acl->GetUuid()));
        info.set_mirror_cfg_acl_uuid(mirror_cfg_acl_uuid);
    }
    VrfEntry *vrf = GetVrf();
    if (vrf) {
        info.set_vrf(vrf->GetName());
    }
    vector<VnObjectLogIpam> ipam_list;
    VnObjectLogIpam sandesh_ipam;
    vector<VnIpam>::const_iterator it = ipam_.begin();
    while (it != ipam_.end()) {
        VnIpam ipam = *it;
        sandesh_ipam.set_ip_prefix(ipam.ip_prefix.to_string());
        sandesh_ipam.set_prefix_len(ipam.plen);
        sandesh_ipam.set_gateway_ip(ipam.default_gw.to_string());
        sandesh_ipam.set_ipam_name(ipam.ipam_name);
        sandesh_ipam.set_dhcp_enable(ipam.dhcp_enable ? "true" : "false");
        sandesh_ipam.set_dns_server(ipam.dns_server.to_string());
        ipam_list.push_back(sandesh_ipam);
        ++it;
    }

    if (ipam_list.size()) {
        info.set_ipam_list(ipam_list);
    }
    info.set_bridging(bridging());
    info.set_ipv4_forwarding(layer3_forwarding());
    info.set_admin_state(admin_state());
    VN_OBJECT_LOG_LOG("AgentVn", SandeshLevel::SYS_INFO, info);
}

void VnListReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentVnSandesh(context(), get_name(), get_uuid(),
                                            get_vxlan_id(), get_ipam_name()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr VnTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                         const std::string &context) {
    return AgentSandeshPtr
        (new AgentVnSandesh(context, args->GetString("name"),
                            args->GetString("uuid"),
                            args->GetString("vxlan_id"),
                            args->GetString("ipam_name")));
}

/////////////////////////////////////////////////////////////////////////////
// DomainConfig routines
/////////////////////////////////////////////////////////////////////////////
DomainConfig::DomainConfig(Agent *agent) {
}

DomainConfig::~DomainConfig() {
}

void DomainConfig::Init() {
}

void DomainConfig::Terminate() {
}

void DomainConfig::RegisterIpamCb(Callback cb) {
    ipam_callback_.push_back(cb);
}

void DomainConfig::RegisterVdnsCb(Callback cb) {
    vdns_callback_.push_back(cb);
}

// Callback is invoked only if there is change in IPAM properties.
// In case of change in a link with IPAM, callback is not invoked.
void DomainConfig::IpamDelete(IFMapNode *node) {
    CallIpamCb(node);
    ipam_config_.erase(node->name());
    return;
}

void DomainConfig::IpamAddChange(IFMapNode *node) {
    autogen::NetworkIpam *network_ipam =
        static_cast <autogen::NetworkIpam *> (node->GetObject());
    assert(network_ipam);

    bool change = false;
    IpamDomainConfigMap::iterator it = ipam_config_.find(node->name());
    if (it != ipam_config_.end()) {
        if (IpamChanged(it->second, network_ipam->mgmt())) {
            it->second = network_ipam->mgmt();
            change = true;
        }
    } else {
        ipam_config_.insert(IpamDomainConfigPair(node->name(),
                                                 network_ipam->mgmt()));
        change = true;
    }
    if (change)
        CallIpamCb(node);
}

void DomainConfig::VDnsDelete(IFMapNode *node) {
    CallVdnsCb(node);
    vdns_config_.erase(node->name());
    return;
}

void DomainConfig::VDnsAddChange(IFMapNode *node) {
    autogen::VirtualDns *virtual_dns =
        static_cast <autogen::VirtualDns *> (node->GetObject());
    assert(virtual_dns);

    VdnsDomainConfigMap::iterator it = vdns_config_.find(node->name());
    if (it != vdns_config_.end()) {
        it->second = virtual_dns->data();
    } else {
        vdns_config_.insert(VdnsDomainConfigPair(node->name(),
                                                 virtual_dns->data()));
    }
    CallVdnsCb(node);
}

void DomainConfig::CallIpamCb(IFMapNode *node) {
    for (unsigned int i = 0; i < ipam_callback_.size(); ++i) {
        ipam_callback_[i](node);
    }
}

void DomainConfig::CallVdnsCb(IFMapNode *node) {
    for (unsigned int i = 0; i < vdns_callback_.size(); ++i) {
        vdns_callback_[i](node);
    }
}

bool DomainConfig::IpamChanged(const autogen::IpamType &old,
                               const autogen::IpamType &cur) const {
    if (old.ipam_method != cur.ipam_method ||
        old.ipam_dns_method != cur.ipam_dns_method)
        return true;

    if ((old.ipam_dns_server.virtual_dns_server_name !=
         cur.ipam_dns_server.virtual_dns_server_name) ||
        (old.ipam_dns_server.tenant_dns_server_address.ip_address !=
         cur.ipam_dns_server.tenant_dns_server_address.ip_address))
        return true;

    if (old.cidr_block.ip_prefix != cur.cidr_block.ip_prefix ||
        old.cidr_block.ip_prefix_len != cur.cidr_block.ip_prefix_len)
        return true;

    if (old.dhcp_option_list.dhcp_option.size() !=
        cur.dhcp_option_list.dhcp_option.size())
        return true;

    for (uint32_t i = 0; i < old.dhcp_option_list.dhcp_option.size(); i++) {
        if ((old.dhcp_option_list.dhcp_option[i].dhcp_option_name !=
             cur.dhcp_option_list.dhcp_option[i].dhcp_option_name) ||
            (old.dhcp_option_list.dhcp_option[i].dhcp_option_value !=
             cur.dhcp_option_list.dhcp_option[i].dhcp_option_value) ||
            (old.dhcp_option_list.dhcp_option[i].dhcp_option_value_bytes !=
             cur.dhcp_option_list.dhcp_option[i].dhcp_option_value_bytes))
            return true;
    }

    if (old.host_routes.route.size() != cur.host_routes.route.size())
        return true;

    for (uint32_t i = 0; i < old.host_routes.route.size(); i++) {
        if ((old.host_routes.route[i].prefix !=
             cur.host_routes.route[i].prefix) ||
            (old.host_routes.route[i].next_hop !=
             cur.host_routes.route[i].next_hop) ||
            (old.host_routes.route[i].next_hop_type !=
             cur.host_routes.route[i].next_hop_type) ||
            (old.host_routes.route[i].community_attributes.community_attribute !=
             cur.host_routes.route[i].community_attributes.community_attribute))
            return true;
    }

    return false;
}

bool DomainConfig::GetIpam(const std::string &name, autogen::IpamType *ipam) {
    IpamDomainConfigMap::iterator it = ipam_config_.find(name);
    if (it == ipam_config_.end())
        return false;
    *ipam = it->second;
    return true;
}

bool DomainConfig::GetVDns(const std::string &vdns,
                           autogen::VirtualDnsType *vdns_type) {
    VdnsDomainConfigMap::iterator it = vdns_config_.find(vdns);
    if (it == vdns_config_.end())
        return false;
    *vdns_type = it->second;
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// OperNetworkIpam routines
/////////////////////////////////////////////////////////////////////////////
OperNetworkIpam::OperNetworkIpam(Agent *agent, DomainConfig *domain_config) :
    OperIFMapTable(agent), domain_config_(domain_config) {
}

OperNetworkIpam::~OperNetworkIpam() {
}

void OperNetworkIpam::ConfigDelete(IFMapNode *node) {
    domain_config_->IpamDelete(node);
}

void OperNetworkIpam::ConfigAddChange(IFMapNode *node) {
    domain_config_->IpamAddChange(node);
}

void OperNetworkIpam::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddNetworkIpamNode(node);
}

OperVirtualDns::OperVirtualDns(Agent *agent, DomainConfig *domain_config) :
    OperIFMapTable(agent), domain_config_(domain_config) {
}

OperVirtualDns::~OperVirtualDns() {
}

void OperVirtualDns::ConfigDelete(IFMapNode *node) {
    domain_config_->VDnsDelete(node);
}

void OperVirtualDns::ConfigAddChange(IFMapNode *node) {
    domain_config_->VDnsAddChange(node);
}

void OperVirtualDns::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddVirtualDnsNode(node);
}
