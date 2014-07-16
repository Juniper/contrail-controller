/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/uuid/uuid_io.hpp>
#include <cmn/agent_cmn.h>

#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>
#include <agent_types.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_mirror.h>
#include <cfg/cfg_listener.h>

#include <oper/route_common.h>
#include <oper/interface_common.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/mirror_table.h>
#include <oper/agent_sandesh.h>
#include <oper/oper_dhcp_options.h>
#include <filter/acl.h>

using namespace autogen;
using namespace std;
using namespace boost;

VnTable *VnTable::vn_table_;

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
                              std::vector<OperDhcpOptions::Subnet> *routes) const {
    VnData::VnIpamDataMap::const_iterator it = vn_ipam_data_.find(ipam);
    if (it != vn_ipam_data_.end()) {
        *routes = it->second.oper_dhcp_options_.host_routes();
        return true;
    }
    return false;
}

bool VnEntry::GetIpamName(const Ip4Address &vm_addr,
                          std::string *ipam_name) const {
    for (unsigned int i = 0; i < ipam_.size(); i++) {
        if (ipam_[i].IsSubnetMember(vm_addr)) {
            *ipam_name = ipam_[i].ipam_name;
            return true;
        }
    }
    return false;
}

bool VnEntry::GetIpamData(const Ip4Address &vm_addr, std::string *ipam_name,
                          autogen::IpamType *ipam_type) const {
    // This will be executed from non DB context; task policy will ensure that
    // this is not run while DB task is updating the map
    if (!GetIpamName(vm_addr, ipam_name) ||
        !Agent::GetInstance()->domain_config_table()->GetIpam(*ipam_name, ipam_type))
        return false;

    return true;
}

bool VnEntry::GetIpamVdnsData(const Ip4Address &vm_addr, 
                              autogen::IpamType *ipam_type,
                              autogen::VirtualDnsType *vdns_type) const {
    std::string ipam_name;
    if (!GetIpamName(vm_addr, &ipam_name) ||
        !Agent::GetInstance()->domain_config_table()->GetIpam(ipam_name, ipam_type) ||
        ipam_type->ipam_dns_method != "virtual-dns-server")
        return false;
    
    if (!Agent::GetInstance()->domain_config_table()->GetVDns(
                ipam_type->ipam_dns_server.virtual_dns_server_name, vdns_type))
        return false;
        
    return true;
}

int VnEntry::GetVxLanId() const {
    if (Agent::GetInstance()->vxlan_network_identifier_mode() == 
        Agent::CONFIGURED) {
        return vxlan_id_;
    } else {
        return vnid_;
    }
}

void VnEntry::RebakeVxLan(int vxlan_id) {
    VxLanId::Create(vxlan_id, GetVrf()->GetName());
    VxLanId *vxlan_id_entry = NULL;
    VxLanIdKey vxlan_key(vxlan_id);
    vxlan_id_entry = static_cast<VxLanId *>(Agent::GetInstance()->
                            vxlan_table()->FindActiveEntry(&vxlan_key));
    vxlan_id_ref_ = vxlan_id_entry;
}

bool VnEntry::ReEvaluateVxlan(VrfEntry *old_vrf, int new_vxlan_id, int new_vnid,
                              bool new_layer2_forwarding, 
                              bool vxlan_network_identifier_mode_changed) {
    bool ret = false; 
    bool rebake_vxlan = false;

    //Two cases in which VXLAN needs to be rebaked.
    // - Firstly In case of global vxlan network identifier mode change
    // - Secondly in case of VN config change which can impact VXLAN.
    if (vxlan_network_identifier_mode_changed) {
        rebake_vxlan = true;
        ret = true;
    } else {
        if (old_vrf != GetVrf()) {
            rebake_vxlan = true;
        }

        if (new_layer2_forwarding != layer2_forwarding_) {
            layer2_forwarding_ = new_layer2_forwarding;
            rebake_vxlan = true;
        }

        if (new_vxlan_id != vxlan_id_) {
            //Ignore rebake if mode is not configured as user configured vxlan
            //is not in use.
            if (Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::CONFIGURED) {
                rebake_vxlan = true;
            }
            vxlan_id_ = new_vxlan_id; 
            ret = true;
        }

        if (new_vnid != vnid_) {
            //Ignore rebake if mode is not automatic as auto assigned vxlan
            //is not in use.
            if (Agent::GetInstance()->vxlan_network_identifier_mode() ==
                Agent::AUTOMATIC) {
                rebake_vxlan = true;
            }
            vnid_ = new_vnid;
            ret = true;
        }
    }

    if (!GetVrf()) {
        vxlan_id_ref_ = NULL;
        ret = true;
    } else {
        if (rebake_vxlan) {
            int active_vxlan_id = GetVxLanId();
            if (active_vxlan_id) {
                RebakeVxLan(active_vxlan_id);
            } else {
                vxlan_id_ref_ = NULL;
            }
            ret = true;
        }
    }
    return ret;
}

bool VnEntry::VxLanNetworkIdentifierChanged() {
    //No change in VN config. 
    //Need to pick vxlan based on config mode
    return ReEvaluateVxlan(NULL, 0, 0, true, true);
}

bool VnEntry::Resync() {
    return VxLanNetworkIdentifierChanged();
}

bool VnTable::Resync(DBEntry *entry, DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    bool ret = vn->Resync();
    return ret;
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
        Agent::GetInstance()->vn_table()->Enqueue(&req); 
    }
    return true;
}

void VnTable::VnEntryWalkDone(DBTableBase *partition) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

void VnTable::UpdateVxLanNetworkIdentifierMode() {
    DBTableWalker *walker = Agent::GetInstance()->db()->GetWalker();
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
    VnEntry *vn = new VnEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(vn));
}

DBEntry *VnTable::Add(const DBRequest *req) {
    VnKey *key = static_cast<VnKey *>(req->key.get());
    VnData *data = static_cast<VnData *>(req->data.get());
    VnEntry *vn = new VnEntry(key->uuid_);
    vn->name_ = data->name_;

    ChangeHandler(vn, req);
    vn->SendObjectLog(AgentLogEvent::ADD);
    return vn;
}

bool VnTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);
    VnEntry *vn = static_cast<VnEntry *>(entry);
    vn->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool VnTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    VnEntry *vn = static_cast<VnEntry *>(entry);
    VnData *data = static_cast<VnData *>(req->data.get());
    VrfEntry *old_vrf = vn->vrf_.get();

    AclKey key(data->acl_id_);
    AclDBEntry *acl = static_cast<AclDBEntry *>
        (Agent::GetInstance()->acl_table()->FindActiveEntry(&key));
    if (vn->acl_.get() != acl) {
        vn->acl_ = acl;
        ret = true;
    }

    AclKey mirror_key(data->mirror_acl_id_);
    AclDBEntry *mirror_acl = static_cast<AclDBEntry *>
        (Agent::GetInstance()->acl_table()->FindActiveEntry(&mirror_key));
    if (vn->mirror_acl_.get() != mirror_acl) {
        vn->mirror_acl_ = mirror_acl;
        ret = true;
    }

    AclKey mirror_cfg_acl_key(data->mirror_cfg_acl_id_);
    AclDBEntry *mirror_cfg_acl = static_cast<AclDBEntry *>
         (Agent::GetInstance()->acl_table()->FindActiveEntry(&mirror_cfg_acl_key));
    if (vn->mirror_cfg_acl_.get() != mirror_cfg_acl) {
        vn->mirror_cfg_acl_ = mirror_cfg_acl;
        ret = true;
    }

    VrfKey vrf_key(data->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetInstance()->vrf_table()->FindActiveEntry(&vrf_key));
    if (vrf != old_vrf) {
        if (!vrf)
            DeleteAllIpamRoutes(vn);
        vn->vrf_ = vrf;
        ret = true;
    }

    if (vn->ipv4_forwarding_ != data->ipv4_forwarding_) {
        vn->ipv4_forwarding_ = data->ipv4_forwarding_;
        ret = true;
    }

    //Ignore IPAM changes if layer3 is not enabled
    if (!vn->ipv4_forwarding_) {
        data->ipam_.clear();
        data->vn_ipam_data_.clear();
    }

    if (IpamChangeNotify(vn->ipam_, data->ipam_, vn)) {
        vn->ipam_ = data->ipam_;
        ret = true;
    }

    if (vn->vn_ipam_data_ != data->vn_ipam_data_) {
        vn->vn_ipam_data_ = data->vn_ipam_data_;
        ret = true;
    }

    ret |= vn->ReEvaluateVxlan(old_vrf, data->vxlan_id_, data->vnid_,
                              data->layer2_forwarding_, false);
    

    return ret;
}

void VnTable::Delete(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    DeleteAllIpamRoutes(vn);
    vn->SendObjectLog(AgentLogEvent::DELETE);
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

bool VnTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    VirtualNetwork *cfg = static_cast <VirtualNetwork *> (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    autogen::VirtualNetworkType properties = cfg->properties(); 
    int vnid = properties.network_id;
    int vxlan_id = properties.vxlan_network_identifier;
    bool layer2_forwarding = true;
    bool ipv4_forwarding = true;
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    if (properties.forwarding_mode == "l2") {
        ipv4_forwarding = false;
    }
    VnKey *key = new VnKey(u);
    VnData *data = NULL;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        uuid acl_uuid = nil_uuid();
        uuid mirror_cfg_acl_uuid = nil_uuid();
        string vrf_name = "";
        std::vector<VnIpam> vn_ipam;
        VnData::VnIpamDataMap vn_ipam_data;
        std::string ipam_name;

        // Find link with ACL / VRF adjacency
        IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
        for (DBGraphVertex::adjacency_iterator iter =
                node->begin(table->GetGraph()); 
                iter != node->end(table->GetGraph()); ++iter) {
            IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
            if (Agent::GetInstance()->cfg_listener()->SkipNode(adj_node)) {
                continue;
            }

            if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_acl_table()) {
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

            if ((adj_node->table() == Agent::GetInstance()->cfg()->cfg_vrf_table()) &&
              (!IsVRFServiceChainingInstance(node->name(), adj_node->name()))) {
                vrf_name = adj_node->name();
            }

            if (adj_node->table() == Agent::GetInstance()->cfg()->cfg_vn_network_ipam_table()) {
                if (IFMapNode *ipam_node = FindTarget(table, adj_node, 
                                                      "network-ipam")) {
                    ipam_name = ipam_node->name();

                    VirtualNetworkNetworkIpam *ipam = 
                        static_cast<VirtualNetworkNetworkIpam *>
                        (adj_node->GetObject());
                    assert(ipam);
                    const VnSubnetsType &subnets = ipam->data();
                    for (unsigned int i = 0; i < subnets.ipam_subnets.size(); ++i) {
                        vn_ipam.push_back(
                           VnIpam(subnets.ipam_subnets[i].subnet.ip_prefix,
                                  subnets.ipam_subnets[i].subnet.ip_prefix_len,
                                  subnets.ipam_subnets[i].default_gateway,
                                  subnets.ipam_subnets[i].enable_dhcp, ipam_name,
                                  subnets.ipam_subnets[i].dhcp_option_list.dhcp_option,
                                  subnets.ipam_subnets[i].host_routes.route));
                    }
                    VnIpamLinkData ipam_data;
                    ipam_data.oper_dhcp_options_.set_host_routes(subnets.host_routes.route);
                    vn_ipam_data.insert(VnData::VnIpamDataPair(ipam_name, ipam_data));
                }
            }
        }

        uuid mirror_acl_uuid = Agent::GetInstance()->mirror_cfg_table()->GetMirrorUuid(node->name());
        std::sort(vn_ipam.begin(), vn_ipam.end());
        data = new VnData(node->name(), acl_uuid, vrf_name, mirror_acl_uuid, 
                          mirror_cfg_acl_uuid, vn_ipam, vn_ipam_data,
                          vxlan_id, vnid, layer2_forwarding, ipv4_forwarding);
    }

    req.key.reset(key);
    req.data.reset(data);
    Agent::GetInstance()->vn_table()->Enqueue(&req);

    if (node->IsDeleted()) {
        return false;
    }
    // Change to ACL referernce can result in change of Policy flag 
    // on interfaces. Find all interfaces on this VN and RESYNC them.
    // This is also required to check changes to the enable_dhcp flag
    // in the VN subnets (VN Ipam).
    // TODO: Check if there is change in VRF
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    // Find link with VM-Port adjacency
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
            node->begin(table->GetGraph()); 
            iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->SkipNode
            (adj_node, Agent::GetInstance()->cfg()->cfg_vm_interface_table())) {
            continue;
        }

        if (adj_node->GetObject() == NULL) {
            continue;
        }
        if (Agent::GetInstance()->interface_table()->IFNodeToReq(adj_node, req)) {
            Agent::GetInstance()->interface_table()->Enqueue(&req);
        }
    }

    // Trigger Floating-IP resync
    VmInterface::FloatingIpVnSync(Agent::GetInstance()->interface_table(),
                                  node);
    VmInterface::VnSync(Agent::GetInstance()->interface_table(), node);

    return false;
}

void VnTable::AddVn(const uuid &vn_uuid, const string &name,
                    const uuid &acl_id, const string &vrf_name, 
                    const std::vector<VnIpam> &ipam,
                    const VnData::VnIpamDataMap &vn_ipam_data,
                    int vxlan_id) {
    DBRequest req;
    VnKey *key = new VnKey(vn_uuid);
    VnData *data = new VnData(name, acl_id, vrf_name, nil_uuid(), 
                              nil_uuid(), ipam, vn_ipam_data,
                              vxlan_id, vxlan_id, true, true);
 
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

void VnTable::IpamVnSync(IFMapNode *node) {
    if (node->IsDeleted()) {
        return;
    }

    IFMapAgentTable *table = static_cast<IFMapAgentTable *> (node->table());
    DBGraph *graph = table->GetGraph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (Agent::GetInstance()->cfg_listener()->SkipNode
            (adj_node, Agent::GetInstance()->cfg()->cfg_vn_table())) {
            continue;
        }

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        Agent::GetInstance()->vn_table()->IFNodeToReq(adj_node, req);
    }

    return;
}

void VnTable::UpdateSubnetGateway(const VnIpam &old_ipam, 
                                  const VnIpam &new_ipam,
                                  VnEntry *vn) {
    VrfEntry *vrf = vn->GetVrf();

    if (vrf && (vrf->GetName() != Agent::GetInstance()->
                linklocal_vrf_name())) {
        AddHostRouteForGw(vn, new_ipam);
        DelHostRouteForGw(vn, old_ipam);
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

            if ((*it_old).installed) {
                (*it_new).installed = true;
                // VNIPAM comparator does not check for gateway.
                // If gateway is changed then take appropriate actions.
                if (gateway_changed) {
                    UpdateSubnetGateway((*it_old), (*it_new), vn);
                }
            } else {
                AddIPAMRoutes(vn, *it_new);
                (*it_old).installed = (*it_new).installed;
            }

            if (gateway_changed) { 
                (*it_old).default_gw = (*it_new).default_gw;
            }

            // update DHCP options
            (*it_old).oper_dhcp_options = (*it_new).oper_dhcp_options;

            // check if dhcp enable flag changed
            if ((*it_old).dhcp_enable != (*it_new).dhcp_enable) {
                (*it_old).dhcp_enable = (*it_new).dhcp_enable;
                // Trigger to interface entries already is sent in config DB
                // processing, nothing else to do here
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
        if (vrf->GetName() == Agent::GetInstance()->linklocal_vrf_name()) {
            return;
        }
        AddHostRouteForGw(vn, ipam);
        AddSubnetRoute(vn, ipam);
        ipam.installed = true;
    }
}

void VnTable::DelIPAMRoutes(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf && ipam.installed) {
        DelHostRouteForGw(vn, ipam);
        DelSubnetRoute(vn, ipam);
        ipam.installed = false;
    }
}

// Add receive route for default gw
void VnTable::AddHostRouteForGw(VnEntry *vn, const VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    static_cast<Inet4UnicastAgentRouteTable *>(vrf->
        GetInet4UnicastRouteTable())->AddHostRoute(vrf->GetName(),
                                                   ipam.default_gw, 32,
                                                   vn->GetName());
}

// Del receive route for default gw
void VnTable::DelHostRouteForGw(VnEntry *vn, const VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    static_cast<Inet4UnicastAgentRouteTable *>
        (vrf->GetInet4UnicastRouteTable())->DeleteReq
        (Agent::GetInstance()->local_peer(), vrf->GetName(),
         ipam.default_gw, 32, NULL);
}

void VnTable::AddSubnetRoute(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    static_cast<Inet4UnicastAgentRouteTable *>(vrf->
        GetInet4UnicastRouteTable())->AddDropRoute
        (vrf->GetName(), ipam.GetSubnetAddress(), ipam.plen, vn->GetName(),
         true);
}

// Del receive route for default gw
void VnTable::DelSubnetRoute(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    static_cast<Inet4UnicastAgentRouteTable *>(vrf->
        GetInet4UnicastRouteTable())->DeleteReq
        (Agent::GetInstance()->local_peer(), vrf->GetName(),
         ipam.GetSubnetAddress(), ipam.plen, NULL);
}

bool VnEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    VnListResp *resp = static_cast<VnListResp *>(sresp);

    if (name.empty() || GetName() == name) {
        VnSandeshData data;
        data.set_name(GetName());
        data.set_uuid(UuidToString(GetUuid()));
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
            vn_subnet_sandesh_list.push_back(entry);
        } 
        data.set_ipam_data(vn_subnet_sandesh_list);

        std::vector<VnIpamHostRoutes> vn_ipam_host_routes_list;
        for (VnData::VnIpamDataMap::const_iterator it = vn_ipam_data_.begin();
             it != vn_ipam_data_.end(); ++it) {
            VnIpamHostRoutes vn_ipam_host_routes;
            vn_ipam_host_routes.ipam_name = it->first;
            const std::vector<OperDhcpOptions::Subnet> &host_routes =
                it->second.oper_dhcp_options_.host_routes();
            for (uint32_t i = 0; i < host_routes.size(); ++i) {
                vn_ipam_host_routes.host_routes.push_back(
                    host_routes[i].ToString());
            }
            vn_ipam_host_routes_list.push_back(vn_ipam_host_routes);
        }
        data.set_ipam_host_routes(vn_ipam_host_routes_list);
        data.set_ipv4_forwarding(Ipv4Forwarding());
        data.set_layer2_forwarding(layer2_forwarding());

        std::vector<VnSandeshData> &list =
            const_cast<std::vector<VnSandeshData>&>(resp->get_vn_list());
        list.push_back(data);
        return true;
    }

    return false;
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
        ipam_list.push_back(sandesh_ipam);
        ++it;
    }

    if (ipam_list.size()) {
        info.set_ipam_list(ipam_list);
    }
    info.set_layer2_forwarding(layer2_forwarding());
    info.set_ipv4_forwarding(Ipv4Forwarding());
    VN_OBJECT_LOG_LOG("AgentVn", SandeshLevel::SYS_INFO, info);
}

void VnListReq::HandleRequest() const {
    AgentVnSandesh *sand = new AgentVnSandesh(context(), get_name());
    sand->DoSandesh();
}

void DomainConfig::RegisterIpamCb(Callback cb) {
    ipam_callback_.push_back(cb);
}

void DomainConfig::RegisterVdnsCb(Callback cb) {
    vdns_callback_.push_back(cb);
}

void DomainConfig::IpamSync(IFMapNode *node) {
    autogen::NetworkIpam *network_ipam =
            static_cast <autogen::NetworkIpam *> (node->GetObject());
    assert(network_ipam);

    if (!node->IsDeleted()) {
        IpamDomainConfigMap::iterator it = ipam_config_.find(node->name());
        if (it != ipam_config_.end()) {
            it->second = network_ipam->mgmt();
        } else {
            ipam_config_.insert(IpamDomainConfigPair(node->name(),
                                                     network_ipam->mgmt()));
        }
        CallIpamCb(node);
    } else {
        CallIpamCb(node);
        ipam_config_.erase(node->name());
    }

}

void DomainConfig::VDnsSync(IFMapNode *node) {
    autogen::VirtualDns *virtual_dns =
            static_cast <autogen::VirtualDns *> (node->GetObject());
    assert(virtual_dns);

    if (!node->IsDeleted()) {
        VdnsDomainConfigMap::iterator it = vdns_config_.find(node->name());
        if (it != vdns_config_.end()) {
            it->second = virtual_dns->data();
        } else {
            vdns_config_.insert(VdnsDomainConfigPair(node->name(),
                                                     virtual_dns->data()));
        }
        CallVdnsCb(node);
    } else {
        CallVdnsCb(node);
        vdns_config_.erase(node->name());
    }
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
