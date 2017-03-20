/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/parse_object.h>
#include <base/util.h>
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
#include "net/address_util.h"

using namespace autogen;
using namespace std;
using namespace boost;
using boost::assign::map_list_of;
using boost::assign::list_of;

VnTable *VnTable::vn_table_;

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

Ip4Address VnIpam::GetBroadcastAddress() const {
    if (ip_prefix.is_v4()) {
        Ip4Address broadcast(ip_prefix.to_v4().to_ulong() | 
                             ~(0xFFFFFFFF << (32 - plen)));
        return broadcast;
    } 
    return Ip4Address(0);
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

VnEntry::VnEntry(Agent *agent, uuid id) :
    AgentOperDBEntry(), agent_(agent), uuid_(id), vrf_(NULL, this),
    vxlan_id_(0), vnid_(0), bridging_(true), layer3_forwarding_(true),
    admin_state_(true), table_label_(0), enable_rpf_(true),
    flood_unknown_unicast_(false), old_vxlan_id_(0),
    forwarding_mode_(Agent::L2_L3),
    route_resync_walker_(new AgentRouteResync(agent)), mirror_destination_(false)
{
    route_resync_walker_.get()->
        set_walkable_route_tables((1 << Agent::INET4_UNICAST) |
                                  (1 << Agent::INET6_UNICAST));
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
    VnKey *key = new VnKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void VnEntry::SetKey(const DBRequestKey *key) { 
    const VnKey *k = static_cast<const VnKey *>(key);
    uuid_ = k->uuid_;
}

AgentDBTable *VnEntry::DBToTable() const {
    return VnTable::GetInstance();
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
    } else {
        return vnid_;
    }
}

int VnEntry::ComputeEthernetTag() const {
    if (TunnelType::ComputeType(TunnelType::AllType()) != TunnelType::VXLAN) {
        return 0;
    }
    return GetVxLanId();
}

bool VnEntry::Resync() {
    VnTable *table = static_cast<VnTable *>(get_table());
    bool ret = false;

    //Evaluate rebake of vxlan
    ret |= table->RebakeVxlan(this, false);
    //Evaluate forwarding mode change
    ret |= table->EvaluateForwardingMode(this);

    return ret;
}

void VnEntry::ResyncRoutes() {
    if (vrf_.get() == NULL)
        return;
    route_resync_walker_.get()->UpdateRoutesInVrf(vrf_.get());
}

bool VnTable::GetLayer3ForwardingConfig
(Agent::ForwardingMode forwarding_mode) const {
    if (forwarding_mode == Agent::L2) {
        return false;
    }
    return true;
}

bool VnTable::GetBridgingConfig(Agent::ForwardingMode forwarding_mode) const {
    if (forwarding_mode == Agent::L3) {
        return false;
    }
    return true;
}

bool VnTable::EvaluateForwardingMode(VnEntry *vn) {
    bool ret = false;
    Agent::ForwardingMode forwarding_mode = vn->forwarding_mode();
    //Evaluate only if VN does not have specific forwarding mode change.
    if (forwarding_mode == Agent::NONE) {
        Agent::ForwardingMode global_forwarding_mode =
            agent()->oper_db()->global_vrouter()->forwarding_mode();
        bool layer3_forwarding =
            GetLayer3ForwardingConfig(global_forwarding_mode);
        if (layer3_forwarding != vn->layer3_forwarding()) {
            vn->set_layer3_forwarding(layer3_forwarding);
            ret |= true;
        }
        bool bridging = GetBridgingConfig(global_forwarding_mode);
        if (bridging != vn->bridging()) {
            vn->set_bridging(bridging);
            ret |= true;
        }
        //Evaluate IPAMs
        if (!vn->layer3_forwarding()) {
            DeleteAllIpamRoutes(vn);
        } else {
            AddAllIpamRoutes(vn);
        }
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
bool VnTable::RebakeVxlan(VnEntry *vn, bool op_del) {
    VxLanId *old_vxlan = vn->vxlan_id_ref_.get();

    uint32_t vxlan = 0;
    // Get vxlan if op is not DELETE and VRF is not NULL
    if (op_del == false && vn->vrf_.get() != NULL)
        vxlan = vn->GetVxLanId();

    // Delete config-entry if there is change in vxlan
    VxLanTable *table = agent()->vxlan_table();
    if (vxlan != vn->old_vxlan_id_) {
        if (vn->old_vxlan_id_) {
            table->Delete(vn->old_vxlan_id_, vn->uuid_);
            vn->vxlan_id_ref_ = NULL;
        }
    }

    // Add new config-entry
    if (vxlan) {
        vn->old_vxlan_id_ = vxlan;
        vn->vxlan_id_ref_ = table->Locate(vxlan, vn->uuid_, vn->vrf_->GetName(),
                                          vn->flood_unknown_unicast_,
                                          vn->mirror_destination_, false);
    }

    return (old_vxlan != vn->vxlan_id_ref_.get());
}

bool VnTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    bool ret = vn->Resync();
    return ret;
}

void VnTable::ResyncVxlan(const uuid &vn) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    VnKey *key = new VnKey(vn);
    key->sub_op_ = AgentKey::RESYNC;
    req.key.reset(key);
    req.data.reset(NULL);
    Enqueue(&req);
}

bool VnTable::VnEntryWalk(DBTablePartBase *partition, DBEntryBase *entry) {
    VnEntry *vn_entry = static_cast<VnEntry *>(entry);
    if (vn_entry->GetVrf()) {
        VnKey *key = new VnKey(vn_entry->GetUuid());
        key->sub_op_ = AgentKey::RESYNC;
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(key); 
        req.data.reset(NULL);
        Enqueue(&req);
    }
    return true;
}

void VnTable::VnEntryWalkDone(DBTableBase *partition) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    agent()->interface_table()->GlobalVrouterConfigChanged();
    agent()->physical_device_vn_table()->
        UpdateVxLanNetworkIdentifierMode();
}

void VnTable::GlobalVrouterConfigChanged() {
    DBTableWalker *walker = agent()->db()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        walker->WalkCancel(walkid_);
    }
    walkid_ = walker->WalkTable(VnTable::GetInstance(), NULL,
                      boost::bind(&VnTable::VnEntryWalk, 
                                  this, _1, _2),
                      boost::bind(&VnTable::VnEntryWalkDone, this, _1));
}

std::auto_ptr<DBEntry> VnTable::AllocEntry(const DBRequestKey *k) const {
    const VnKey *key = static_cast<const VnKey *>(k);
    VnEntry *vn = new VnEntry(agent(), key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vn));
}

extern IFMapNode *vn_test_node;
DBEntry *VnTable::OperDBAdd(const DBRequest *req) {
    VnKey *key = static_cast<VnKey *>(req->key.get());
    VnData *data = static_cast<VnData *>(req->data.get());
    VnEntry *vn = new VnEntry(agent(), key->uuid_);
    vn->name_ = data->name_;

    ChangeHandler(vn, req);
    vn->SendObjectLog(AgentLogEvent::ADD);
    return vn;
}

bool VnTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);
    VnEntry *vn = static_cast<VnEntry *>(entry);
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

bool VnTable::ForwardingModeChangeHandler(bool old_layer3_forwarding,
                                          bool old_bridging,
                                          bool *resync_routes,
                                          VnData *data,
                                          VnEntry *vn)
{
    bool ret = false;
    if (vn->layer3_forwarding_ != data->layer3_forwarding_) {
        vn->layer3_forwarding_ = data->layer3_forwarding_;
        *resync_routes = true;
        ret = true;
    }

    if (vn->bridging_ != data->bridging_) {
        vn->bridging_ = data->bridging_;
        *resync_routes = true;
        ret = true;
    }

    if (vn->layer3_forwarding_ && old_layer3_forwarding) {
        //Evaluate IPAM change only if there is no change in
        //layer3_forwarding.
        if (IpamChangeNotify(vn->ipam_, data->ipam_, vn)) {
            vn->ipam_ = data->ipam_;
            ret = true;
        }
    } else {
        if (vn->layer3_forwarding_) {
            //layer3_forwarding has been enabled.
            //Add all new ipam routes.
            vn->ipam_ = data->ipam_;
            if (!old_layer3_forwarding) {
                AddAllIpamRoutes(vn);
                ret = true;
            }
        } else {
            //layer3_forwarding has been disabled.
            //Delete all new ipam routes.
            if (old_layer3_forwarding) {
                DeleteAllIpamRoutes(vn);
                ret = true;
            }
            vn->ipam_ = data->ipam_;
        }
    }
    return ret;
}

bool VnTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    VnEntry *vn = static_cast<VnEntry *>(entry);
    VnData *data = static_cast<VnData *>(req->data.get());
    VrfEntry *old_vrf = vn->vrf_.get();
    bool old_layer3_forwarding = vn->layer3_forwarding_;
    bool old_bridging = vn->bridging_;
    bool rebake_vxlan = false;
    bool resync_routes = false;

    AclKey key(data->acl_id_);
    AclDBEntry *acl = static_cast<AclDBEntry *>
        (agent()->acl_table()->FindActiveEntry(&key));
    if (vn->acl_.get() != acl) {
        vn->acl_ = acl;
        ret = true;
    }

    AclKey mirror_key(data->mirror_acl_id_);
    AclDBEntry *mirror_acl = static_cast<AclDBEntry *>
        (agent()->acl_table()->FindActiveEntry(&mirror_key));
    if (vn->mirror_acl_.get() != mirror_acl) {
        vn->mirror_acl_ = mirror_acl;
        ret = true;
    }

    AclKey mirror_cfg_acl_key(data->mirror_cfg_acl_id_);
    AclDBEntry *mirror_cfg_acl = static_cast<AclDBEntry *>
         (agent()->acl_table()->FindActiveEntry(&mirror_cfg_acl_key));
    if (vn->mirror_cfg_acl_.get() != mirror_cfg_acl) {
        vn->mirror_cfg_acl_ = mirror_cfg_acl;
        ret = true;
    }

    AgentQosConfigKey qos_config_key(data->qos_config_uuid_);
    AgentQosConfig *qos_config = static_cast<AgentQosConfig *>
        (agent()->qos_config_table()->FindActiveEntry(&qos_config_key));
    if (vn->qos_config_.get() != qos_config) {
        vn->qos_config_ = qos_config;
        ret = true;
    }

    VrfKey vrf_key(data->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>
        (agent()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf != old_vrf) {
        if (!vrf) {
            DeleteAllIpamRoutes(vn);
        }
        vn->vrf_ = vrf;
        rebake_vxlan = true;
        ret = true;
    }

    if (vn->admin_state_ != data->admin_state_) {
        vn->admin_state_ = data->admin_state_;
        ret = true;
    }

    ret |= ForwardingModeChangeHandler(old_layer3_forwarding,
                                       old_bridging,
                                       &resync_routes,
                                       data,
                                       vn);

    if (vn->vn_ipam_data_ != data->vn_ipam_data_) {
        vn->vn_ipam_data_ = data->vn_ipam_data_;
        ret = true;
    }

    if (vn->enable_rpf_ != data->enable_rpf_) {
        vn->enable_rpf_ = data->enable_rpf_;
        ret = true;
    }

    if (vn->vxlan_id_ != data->vxlan_id_) {
        vn->vxlan_id_ = data->vxlan_id_;
        ret = true;
        if (agent()->vxlan_network_identifier_mode() == Agent::CONFIGURED) {
            rebake_vxlan = true;
        }
    }

    if (vn->vnid_ != data->vnid_) {
        vn->vnid_ = data->vnid_;
        ret = true;
        if (agent()->vxlan_network_identifier_mode() == Agent::AUTOMATIC) {
            rebake_vxlan = true;
        }
    }

    if (vn->flood_unknown_unicast_ != data->flood_unknown_unicast_) {
        vn->flood_unknown_unicast_ = data->flood_unknown_unicast_;
        rebake_vxlan = true;
        ret = true;
    }

    if (vn->forwarding_mode_ != data->forwarding_mode_) {
        vn->forwarding_mode_ = data->forwarding_mode_;
        ret = true;
    }

    if (vn->mirror_destination_ != data->mirror_destination_) {
        vn->mirror_destination_ = data->mirror_destination_;
        rebake_vxlan = true;
        ret = true;
    }

    if (rebake_vxlan) {
        ret |= RebakeVxlan(vn, false);
    }

    if (resync_routes) {
        vn->ResyncRoutes();
    }

    if (vn->pbb_evpn_enable_ != data->pbb_evpn_enable_) {
        vn->pbb_evpn_enable_ = data->pbb_evpn_enable_;
        ret = true;
    }

    if (vn->pbb_etree_enable_ != data->pbb_etree_enable_) {
        vn->pbb_etree_enable_ = data->pbb_etree_enable_;
        ret = true;
    }

    if (vn->layer2_control_word_ != data->layer2_control_word_) {
        vn->layer2_control_word_ = data->layer2_control_word_;
        ret = true;
    }
    return ret;
}

bool VnTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    DeleteAllIpamRoutes(vn);
    RebakeVxlan(vn, true);
    vn->SendObjectLog(AgentLogEvent::DELETE);
    return true;
}

VnEntry *VnTable::Find(const boost::uuids::uuid &u) {
    VnKey key(u);
    return static_cast<VnEntry *>(FindActiveEntry(&key));
}

DBTableBase *VnTable::CreateTable(DB *db, const std::string &name) {
    vn_table_ = new VnTable(db, name);
    vn_table_->Init();
    return vn_table_;
};

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

int VnTable::ComputeCfgVxlanId(IFMapNode *node) {
    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    if (Agent::GetInstance()->vxlan_network_identifier_mode() == 
        Agent::CONFIGURED) {
        return cfg->properties().vxlan_network_identifier;
    } else {
        return GetCfgVnId(cfg);
    }
}

void VnTable::CfgForwardingFlags(IFMapNode *node, bool *l2, bool *l3,
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
    //dervived forwarding mode is resultant of configured VN forwarding and global
    //configure forwarding mode. It is then used to setup the VN forwarding
    //mode.
    //In derivation, global configured forwarding mode is consulted only if VN
    //configured f/wing mode is not set.
    *forwarding_mode =
        agent()->TranslateForwardingMode(properties.forwarding_mode);
    Agent::ForwardingMode derived_forwarding_mode = *forwarding_mode;
    if (derived_forwarding_mode == Agent::NONE) {
        //Use global configured forwarding mode.
        derived_forwarding_mode = agent()->oper_db()->global_vrouter()->
            forwarding_mode();
    }

    //Set the forwarding mode in VN based on calculation above.
    //By default assume both bridging and layer3_forwarding is enabled.
    //Flap the mode if configured forwarding mode is not "l2_l3" i.e. l2 or l3.
    *l2 = GetBridgingConfig(derived_forwarding_mode);
    *l3 = GetLayer3ForwardingConfig(derived_forwarding_mode);
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
    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    assert(cfg);

    uuid acl_uuid = nil_uuid();
    uuid mirror_cfg_acl_uuid = nil_uuid();
    uuid qos_config_uuid = nil_uuid();
    string vrf_name = "";
    std::vector<VnIpam> vn_ipam;
    VnData::VnIpamDataMap vn_ipam_data;
    std::string ipam_name;

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
    }

    uuid mirror_acl_uuid = agent()->mirror_cfg_table()->GetMirrorUuid(node->name());
    std::sort(vn_ipam.begin(), vn_ipam.end());

    // Fetch VN forwarding Properties
    bool bridging;
    bool layer3_forwarding;
    bool enable_rpf;
    bool flood_unknown_unicast;
    bool mirror_destination;
    bool pbb_evpn_enable = cfg->pbb_evpn_enable();
    bool pbb_etree_enable = cfg->pbb_etree_enable();
    bool layer2_control_word = cfg->layer2_control_word();

    Agent::ForwardingMode forwarding_mode;
    CfgForwardingFlags(node, &bridging, &layer3_forwarding, &enable_rpf,
                       &flood_unknown_unicast, &forwarding_mode,
                       &mirror_destination);
    return new VnData(agent(), node, node->name(), acl_uuid, vrf_name,
                      mirror_acl_uuid, mirror_cfg_acl_uuid, vn_ipam,
                      vn_ipam_data, cfg->properties().vxlan_network_identifier,
                      GetCfgVnId(cfg), bridging, layer3_forwarding,
                      cfg->id_perms().enable, enable_rpf,
                      flood_unknown_unicast, forwarding_mode,
                      qos_config_uuid, mirror_destination, pbb_etree_enable,
                      pbb_evpn_enable, layer2_control_word);
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

void VnTable::AddVn(const uuid &vn_uuid, const string &name,
                    const uuid &acl_id, const string &vrf_name, 
                    const std::vector<VnIpam> &ipam,
                    const VnData::VnIpamDataMap &vn_ipam_data, int vn_id,
                    int vxlan_id, bool admin_state, bool enable_rpf,
                    bool flood_unknown_unicast, bool pbb_etree_enable,
                    bool pbb_evpn_enable, bool layer2_control_word) {
    bool mirror_destination = false;
    DBRequest req;
    VnKey *key = new VnKey(vn_uuid);
    VnData *data = new VnData(agent(), NULL, name, acl_id, vrf_name, nil_uuid(), 
                              nil_uuid(), ipam, vn_ipam_data,
                              vn_id, vxlan_id, true, true,
                              admin_state, enable_rpf,
                              flood_unknown_unicast, Agent::NONE, nil_uuid(),
                              mirror_destination, pbb_etree_enable, pbb_evpn_enable,
                              layer2_control_word);
 
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    Enqueue(&req);
}

void VnTable::DelVn(const uuid &vn_uuid) {
    DBRequest req;
    VnKey *key = new VnKey(vn_uuid);

    req.oper = DBRequest::DB_ENTRY_DELETE;
    req.key.reset(key);
    req.data.reset(NULL);
    Enqueue(&req);
}

void VnTable::UpdateHostRoute(const IpAddress &old_address, 
                              const IpAddress &new_address,
                              VnEntry *vn, bool relaxed_policy) {
    VrfEntry *vrf = vn->GetVrf();

    if (vrf && (vrf->GetName() != agent()->linklocal_vrf_name())) {
        AddHostRoute(vn, new_address, relaxed_policy);
        DelHostRoute(vn, old_address);
    }
}

// Check the old and new Ipam data and update receive routes for GW addresses
bool VnTable::IpamChangeNotify(std::vector<VnIpam> &old_ipam, 
                               std::vector<VnIpam> &new_ipam,
                               VnEntry *vn) {
    bool change = false;
    std::sort(old_ipam.begin(), old_ipam.end());
    std::sort(new_ipam.begin(), new_ipam.end());
    std::vector<VnIpam>::iterator it_old = old_ipam.begin();
    std::vector<VnIpam>::iterator it_new = new_ipam.begin();
    while (it_old != old_ipam.end() && it_new != new_ipam.end()) {
        if (*it_old < *it_new) {
            // old entry is deleted
            DelIPAMRoutes(vn, *it_old);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            AddIPAMRoutes(vn, *it_new);
            change = true;
            it_new++;
        } else {
            //Evaluate non key members of IPAM for changes.
            // no change in entry
            bool gateway_changed = ((*it_old).default_gw !=
                                    (*it_new).default_gw);
            bool service_address_changed = ((*it_old).dns_server != (*it_new).dns_server);

            if ((*it_old).installed) {
                (*it_new).installed = true;
                // VNIPAM comparator does not check for gateway.
                // If gateway is changed then take appropriate actions.
                IpAddress unspecified;
                if (gateway_changed) {
                    if (IsGwHostRouteRequired()) {
                        UpdateHostRoute((*it_old).default_gw,
                                        (*it_new).default_gw, vn, true);
                    }
                }
                if (service_address_changed) {
                    UpdateHostRoute((*it_old).dns_server,
                                    (*it_new).dns_server, vn, true);
                }
            } else {
                AddIPAMRoutes(vn, *it_new);
                (*it_old).installed = (*it_new).installed;
            }

            if (gateway_changed) {
                (*it_old).default_gw = (*it_new).default_gw;
            }
            if (service_address_changed) {
                (*it_old).dns_server = (*it_new).dns_server;
            }

            if (gateway_changed || service_address_changed) {
                // DHCP service would need to know in case this changes
                change = true;
            }

            // update DHCP options
            (*it_old).oper_dhcp_options = (*it_new).oper_dhcp_options;

            if ((*it_old).dhcp_enable != (*it_new).dhcp_enable) {
                (*it_old).dhcp_enable = (*it_new).dhcp_enable;
                change = true;
            }

            it_old++;
            it_new++;
        }
    }

    // delete remaining old entries
    for (; it_old != old_ipam.end(); ++it_old) {
        DelIPAMRoutes(vn, *it_old);
        change = true;
    }

    // add remaining new entries
    for (; it_new != new_ipam.end(); ++it_new) {
        AddIPAMRoutes(vn, *it_new);
        change = true;
    }

    std::sort(new_ipam.begin(), new_ipam.end());
    return change;
}

void VnTable::AddAllIpamRoutes(VnEntry *vn) {
    std::vector<VnIpam> &ipam = vn->ipam_;
    for (unsigned int i = 0; i < ipam.size(); ++i) {
        AddIPAMRoutes(vn, ipam[i]);
    }
}

void VnTable::DeleteAllIpamRoutes(VnEntry *vn) {
    std::vector<VnIpam> &ipam = vn->ipam_;
    for (unsigned int i = 0; i < ipam.size(); ++i) {
        DelIPAMRoutes(vn, ipam[i]);
    }
}

void VnTable::AddIPAMRoutes(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf) {
        // Do not let the gateway configuration overwrite the receive nh.
        if (vrf->GetName() == agent()->linklocal_vrf_name()) {
            return;
        }
        // Allways policy will be enabled for default Gateway and
        // Dns server to create flows for BGP as service even
        // though explicit disable policy config form user.
        if (IsGwHostRouteRequired())
            AddHostRoute(vn, ipam.default_gw, true);
        AddHostRoute(vn, ipam.dns_server, true);
        AddSubnetRoute(vn, ipam);
        ipam.installed = true;
    }
}

void VnTable::DelIPAMRoutes(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf && ipam.installed) {
        if (IsGwHostRouteRequired())
            DelHostRoute(vn, ipam.default_gw);
        DelHostRoute(vn, ipam.dns_server);
        DelSubnetRoute(vn, ipam);
        ipam.installed = false;
    }
}

bool VnTable::IsGwHostRouteRequired() {
    return (!agent()->tsn_enabled());
}

// Add receive route for default gw
void VnTable::AddHostRoute(VnEntry *vn, const IpAddress &address,
                           bool relaxed_policy) {
    VrfEntry *vrf = vn->GetVrf();
    if (address.is_v4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                address.to_v4(), 32, vn->GetName(), relaxed_policy);
    } else if (address.is_v6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet6UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                address.to_v6(), 128, vn->GetName(), relaxed_policy);
    }
}

// Del receive route for default gw
void VnTable::DelHostRoute(VnEntry *vn, const IpAddress &address) {
    VrfEntry *vrf = vn->GetVrf();
    if (address.is_v4()) {
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet4UnicastRouteTable())->DeleteReq
            (agent()->local_peer(), vrf->GetName(),
             address.to_v4(), 32, NULL);
    } else if (address.is_v6()) {
        static_cast<InetUnicastAgentRouteTable *>
            (vrf->GetInet6UnicastRouteTable())->DeleteReq
            (agent()->local_peer(), vrf->GetName(),
             address.to_v6(), 128, NULL);
    }
}

void VnTable::AddSubnetRoute(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (ipam.IsV4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->AddIpamSubnetRoute
            (vrf->GetName(), ipam.GetSubnetAddress(), ipam.plen, vn->GetName());
    } else if (ipam.IsV6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet6UnicastRouteTable())->AddIpamSubnetRoute
            (vrf->GetName(), ipam.GetV6SubnetAddress(), ipam.plen,
             vn->GetName());
    }
}

// Del receive route for default gw
void VnTable::DelSubnetRoute(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (ipam.IsV4()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet4UnicastRouteTable())->DeleteReq
            (agent()->local_peer(), vrf->GetName(),
             ipam.GetSubnetAddress(), ipam.plen, NULL);
    } else if (ipam.IsV6()) {
        static_cast<InetUnicastAgentRouteTable *>(vrf->
            GetInet6UnicastRouteTable())->DeleteReq
            (agent()->local_peer(), vrf->GetName(),
             ipam.GetV6SubnetAddress(), ipam.plen, NULL);
    }
}

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
    std::vector<VnSandeshData> &list =
        const_cast<std::vector<VnSandeshData>&>(resp->get_vn_list());
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
        case AgentLogEvent::DELETE:
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
    AgentSandeshPtr sand(new AgentVnSandesh(context(), get_name(),
                                       get_uuid(), get_vxlan_id(),
                                       get_ipam_name()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr VnTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                         const std::string &context) {
    return AgentSandeshPtr(new AgentVnSandesh(context,
                                              args->GetString("name"), args->GetString("uuid"),
                                              args->GetString("vxlan_id"), args->GetString("ipam_name")));
}

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
