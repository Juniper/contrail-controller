/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <base/parse_object.h>
#include <ifmap/ifmap_link.h>
#include <ifmap/ifmap_table.h>
#include <vnc_cfg_types.h>

#include <cmn/agent_cmn.h>

#include <oper/interface.h>
#include <oper/vn.h>
#include <oper/nexthop.h>
#include <oper/mpls.h>
#include <oper/inet4_route.h>
#include <oper/inet4_ucroute.h>
#include <oper/inet4_mcroute.h>
#include <oper/mirror_table.h>

#include <cfg/init_config.h>
#include <cfg/interface_cfg.h>
#include <cfg/mirror_cfg.h>
#include <oper/agent_sandesh.h>

static VnTable *vn_table_;
DomainConfig::DomainConfigMap DomainConfig::ipam_config_;
DomainConfig::DomainConfigMap DomainConfig::vdns_config_;

using namespace autogen;
using namespace std;
using namespace boost;

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
    return vn_table_;
}

bool VnEntry::GetIpamData(const VmPortInterface *vmitf, 
                          autogen::IpamType &ipam_type) const {
    // This will be executed from non DB context; task policy will ensure that
    // this is not run while DB task is updating the map
    if (!vmitf)
        return false;

    Ip4Address vm_addr = vmitf->GetIpAddr();
    unsigned int i;
    for (i = 0; i < ipam_.size(); i++) {
        if (ipam_[i].IsSubnetMember(vm_addr))
            break;
    }
    if (i == ipam_.size() ||
        !DomainConfig::GetIpam(ipam_[i].ipam_name, ipam_type))
        return false;

    return true;
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
    return vn;
}

bool VnTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = ChangeHandler(entry, req);
    return ret;
}

bool VnTable::ChangeHandler(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    VnEntry *vn = static_cast<VnEntry *>(entry);
    VnData *data = static_cast<VnData *>(req->data.get());

    AclKey key(data->acl_id_);
    AclDBEntry *acl = static_cast<AclDBEntry *>
        (Agent::GetAclTable()->FindActiveEntry(&key));
    if (vn->acl_.get() != acl) {
        vn->acl_ = acl;
        ret = true;
    }

    AclKey mirror_key(data->mirror_acl_id_);
    AclDBEntry *mirror_acl = static_cast<AclDBEntry *>
        (Agent::GetAclTable()->FindActiveEntry(&mirror_key));
    if (vn->mirror_acl_.get() != mirror_acl) {
        vn->mirror_acl_ = mirror_acl;
        ret = true;
    }

    AclKey mirror_cfg_acl_key(data->mirror_cfg_acl_id_);
    AclDBEntry *mirror_cfg_acl = static_cast<AclDBEntry *>
         (Agent::GetAclTable()->FindActiveEntry(&mirror_cfg_acl_key));
    if (vn->mirror_cfg_acl_.get() != mirror_cfg_acl) {
        vn->mirror_cfg_acl_ = mirror_cfg_acl;
        ret = true;
    }

    VrfKey vrf_key(data->vrf_name_);
    VrfEntry *vrf = static_cast<VrfEntry *>
        (Agent::GetVrfTable()->FindActiveEntry(&vrf_key));
    if (vrf != vn->vrf_.get()) {
        if (!vrf)
            DeleteIpamHostRoutes(vn);
        vn->vrf_ = vrf;
        ret = true;
    }

    if (IpamChangeNotify(vn->ipam_, data->ipam_, vn)) {
        vn->ipam_ = data->ipam_;
        ret = true;
    }

    return ret;
}

void VnTable::Delete(DBEntry *entry, const DBRequest *req) {
    VnEntry *vn = static_cast<VnEntry *>(entry);
    DeleteIpamHostRoutes(vn);
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
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

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
        std::string ipam_name;

        // Find link with ACL / VRF adjacency
        IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
        for (DBGraphVertex::adjacency_iterator iter =
                node->begin(table->GetGraph()); 
                iter != node->end(table->GetGraph()); ++iter) {
            IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
            if (CfgListener::CanUseNode(adj_node) == false) {
                continue;
            }

            if (adj_node->table() == AgentConfig::GetAclTable()) {
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

            if ((adj_node->table() == AgentConfig::GetVrfTable()) &&
              (!IsVRFServiceChainingInstance(node->name(), adj_node->name()))) {
                vrf_name = adj_node->name();
            }

            if (adj_node->table() == AgentConfig::GetVnNetworkIpamTable()) {
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
                                  subnets.ipam_subnets[i].default_gateway, ipam_name));
                    }
                }
            }
        }

        uuid mirror_acl_uuid = Agent::GetMirrorCfgTable()->GetMirrorUuid(node->name());
        std::sort(vn_ipam.begin(), vn_ipam.end());
        data = new VnData(node->name(), acl_uuid, vrf_name, mirror_acl_uuid, 
                          mirror_cfg_acl_uuid, vn_ipam);
    }

    req.key.reset(key);
    req.data.reset(data);
    Agent::GetVnTable()->Enqueue(&req);

    if (node->IsDeleted()) {
        return false;
    }
    // Change to ACL referernce can result in change of Policy flag 
    // on interfaces. Find all interfaces on this VN and RESYNC them
    // TODO: Check if there is change in VRF
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    // Find link with VM-Port adjacency
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
            node->begin(table->GetGraph()); 
            iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (CfgListener::CanUseNode
            (adj_node, AgentConfig::GetVmInterfaceTable()) == false) {
            continue;
        }

        if (adj_node->GetObject() == NULL) {
            continue;
        }
        if (Agent::GetInterfaceTable()->IFNodeToReq(adj_node, req)) {
            Agent::GetInterfaceTable()->Enqueue(&req);
        }
    }

    // Trigger Floating-IP resync
    VmPortInterface::FloatingIpVnSync(node);

    return false;
}

void VnTable::AddVn(const uuid &vn_uuid, const string &name,
                    const uuid &acl_id, const string &vrf_name, 
                    const std::vector<VnIpam> &ipam) {
    DBRequest req;
    VnKey *key = new VnKey(vn_uuid);
    VnData *data = new VnData(name, acl_id, vrf_name, nil_uuid(), nil_uuid(), ipam);
 
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
        if (CfgListener::CanUseNode
            (adj_node, AgentConfig::GetVnTable()) == false) {
            continue;
        }

        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        Agent::GetVnTable()->IFNodeToReq(adj_node, req);
    }

    return;
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
            DelHostRouteForGw(vn, *it_old);
            change = true;
            it_old++;
        } else if (*it_new < *it_old) {
            // new entry
            AddHostRouteForGw(vn, *it_new);
            change = true;
            it_new++;
        } else {
            // no change in entry
            if ((*it_old).installed)
                (*it_new).installed = true;
            else {
                AddHostRouteForGw(vn, *it_new);
                (*it_old).installed = (*it_new).installed;
            }
            it_old++;
            it_new++;
        }
    }

    // delete remaining old entries
    for (; it_old != old_ipam.end(); ++it_old) {
        DelHostRouteForGw(vn, *it_old);
        change = true;
    }

    // add remaining new entries
    for (; it_new != new_ipam.end(); ++it_new) {
        AddHostRouteForGw(vn, *it_new);
        change = true;
    }

    std::sort(new_ipam.begin(), new_ipam.end());
    return change;
}

void VnTable::DeleteIpamHostRoutes(VnEntry *vn) {
    std::vector<VnIpam> &ipam = vn->ipam_;
    for (unsigned int i = 0; i < ipam.size(); ++i)
        DelHostRouteForGw(vn, ipam[i]);
}

// Add receive route for default gw
void VnTable::AddHostRouteForGw(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf) {
        // Do not let the gateway configuration overwrite the receive nh.
        if (vrf->GetName() == Agent::GetLinkLocalVrfName()) {
            return;
        }
        vrf->GetInet4UcRouteTable()->AddHostRoute(vrf->GetName(), 
                                                  ipam.default_gw, 32, 
                                                  vn->GetName());
        ipam.installed = true;
    }
}

// Del receive route for default gw
void VnTable::DelHostRouteForGw(VnEntry *vn, VnIpam &ipam) {
    VrfEntry *vrf = vn->GetVrf();
    if (vrf && ipam.installed) {
        vrf->GetInet4UcRouteTable()->DeleteReq(Agent::GetLocalPeer(),
                                               vrf->GetName(), 
                                               ipam.default_gw, 32);
        ipam.installed = false;
    }
}

bool VnEntry::DBEntrySandesh(Sandesh *sresp, std::string &name)  const {
    VnListResp *resp = static_cast<VnListResp *>(sresp);

    if (GetName().find(name) != std::string::npos) {
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

        std::vector<VnIpamData> vn_ipam_data;
        const std::vector<VnIpam> &vn_ipam = GetVnIpam();
        for (unsigned int i = 0; i < vn_ipam.size(); ++i) {
            VnIpamData entry;
            entry.set_ip_prefix(vn_ipam[i].ip_prefix.to_string());
            entry.set_prefix_len(vn_ipam[i].plen);
            entry.set_gateway(vn_ipam[i].default_gw.to_string());
            entry.set_ipam_name(vn_ipam[i].ipam_name);
            vn_ipam_data.push_back(entry);
        } 
        data.set_ipam_data(vn_ipam_data);

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
    string vn_uuid = boost::lexical_cast<string>(GetUuid());
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
        acl_uuid.assign(boost::lexical_cast<string>(acl->GetUuid()));
        info.set_acl_uuid(acl_uuid);
    }
    if (mirror_acl) {
        mirror_acl_uuid.assign(boost::lexical_cast<string>(mirror_acl->GetUuid()));
        info.set_mirror_acl_uuid(mirror_acl_uuid);
    }
    if (mirror_cfg_acl) {
        mirror_cfg_acl_uuid.assign(boost::lexical_cast<string>(mirror_cfg_acl->GetUuid()));
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
    VN_OBJECT_LOG_LOG("AgentVn", SandeshLevel::SYS_INFO, info);
}

void VnListReq::HandleRequest() const {
    AgentVnSandesh *sand = new AgentVnSandesh(context(), get_name());
    sand->DoSandesh();
}

void DomainConfig::IpamSync(IFMapNode *node) {
    if (!node->IsDeleted())
        ipam_config_.insert(DomainConfigPair(node->name(), node));
    else {
        ipam_config_.erase(node->name());
    }
}

void DomainConfig::VDnsSync(IFMapNode *node) {
    if (!node->IsDeleted())
        vdns_config_.insert(DomainConfigPair(node->name(), node));
    else
        vdns_config_.erase(node->name());
}

bool DomainConfig::GetIpam(const std::string &name, autogen::IpamType &ipam) {
    DomainConfigMap::iterator it = ipam_config_.find(name);
    if (it == ipam_config_.end())
        return false;
    autogen::NetworkIpam *network_ipam = 
            static_cast <autogen::NetworkIpam *> (it->second->GetObject());
    assert(network_ipam);

    ipam = network_ipam->mgmt();
    return true;
}

bool DomainConfig::GetVDns(const std::string &vdns, 
                           autogen::VirtualDnsType &vdns_type) {
    DomainConfigMap::iterator it = vdns_config_.find(vdns);
    if (it == vdns_config_.end())
        return false;
    autogen::VirtualDns *virtual_dns = 
            static_cast <autogen::VirtualDns *> (it->second->GetObject());
    assert(virtual_dns);

    vdns_type = virtual_dns->data();
    return true;
}
