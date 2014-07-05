/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/address.h>
#if defined(__GNUC__)
#include "base/compiler.h"
#if __GNUC_PREREQ(4, 5)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
#endif
#endif
#include <boost/uuid/random_generator.hpp>
#if defined(__GNUC__) && __GNUC_PREREQ(4, 6)
#pragma GCC diagnostic pop
#endif

#include <ifmap/ifmap_table.h>
#include <ifmap/ifmap_link.h>
#include <base/logging.h>

#include <cmn/agent_cmn.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>

#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <filter/acl_entry_spec.h>
#include <filter/acl.h>

void MirrorCfgTable::Shutdown() {
}

void MirrorCfgTable::Init() {
     return;
}

const char *MirrorCfgTable::Add(const MirrorCreateReq &cfg) {
    MirrorCfgKey key;
    key.handle = cfg.get_handle();
    if (key.handle.empty()) {
        return "Invalid Handle";
    }

    MirrorCfgTree::iterator it;
    it = mc_tree_.find(key);
    if (it != mc_tree_.end()) {
        return "Update not supported";
    }

    MirrorCfgEntry *entry = new MirrorCfgEntry;
    entry->key = key;

    entry->data.apply_vn = cfg.get_apply_vn();
    entry->data.src_vn = cfg.get_src_vn();
    entry->data.src_ip_prefix = cfg.get_src_ip_prefix();
    entry->data.src_ip_prefix_len = cfg.get_src_ip_prefix_len();

    entry->data.dst_vn = cfg.get_dst_vn();
    entry->data.dst_ip_prefix = cfg.get_dst_ip_prefix();
    entry->data.dst_ip_prefix_len = cfg.get_dst_ip_prefix_len();

    entry->data.start_src_port = cfg.get_start_src_port();
    entry->data.end_src_port = cfg.get_end_src_port();

    entry->data.start_dst_port = cfg.get_start_dst_port();
    entry->data.end_dst_port = cfg.get_end_dst_port();

    entry->data.protocol = cfg.get_protocol();

    entry->data.ip = cfg.get_ip();
    entry->data.udp_port = cfg.get_udp_port();

    entry->data.time_period = cfg.get_time_period();
    if (cfg.get_mirror_vrf().empty()) {
        entry->data.mirror_vrf = agent_cfg_->agent()->fabric_vrf_name();
    } else {
        entry->data.mirror_vrf = cfg.get_mirror_vrf();
    }

    // Send create request to Mirror Index table
    boost::system::error_code ec;
    Ip4Address dest_ip = Ip4Address::from_string(entry->data.ip, ec);
    if (ec.value() != 0) {
        delete entry;
        return "Invalid mirror destination address ";
    }

    if (entry->data.udp_port == 0) {
        delete entry;
        return "Invalid mirror destination port ";
    }

    Ip4Address sip;
    if (agent_cfg_->agent()->router_id() == dest_ip) {
        // If source IP and dest IP are same,
        // linux kernel will drop the packet. 
        // Hence we will use link local IP address as sip.
        sip = Ip4Address(METADATA_IP_ADDR);
    } else {
        sip = agent_cfg_->agent()->router_id();
    }
    
    MirrorTable::AddMirrorEntry(entry->key.handle,
                                entry->data.mirror_vrf, sip, 
                                agent_cfg_->agent()->mirror_port(), 
                                dest_ip, entry->data.udp_port);

    // Update ACL
    VnAclMap::iterator va_it;
    va_it = vn_acl_map_.find(entry->data.apply_vn);
    AclUuid dyn_acl_uuid;
    const char *str = NULL;
    int ace_id = 0;
    if (va_it == vn_acl_map_.end()) {
        dyn_acl_uuid = boost::uuids::random_generator() ();
        AclIdInfo acl_info;
        acl_info.id = dyn_acl_uuid;
        acl_info.num_of_entries++;
        ace_id = ++acl_info.ace_id_latest;
        vn_acl_map_.insert(std::pair<VnIdStr, AclIdInfo>(entry->data.apply_vn, acl_info));
        // Create ACL with given dyn_acl_uuid
        str = UpdateAclEntry(dyn_acl_uuid, true, entry, ace_id);
    } else {
        dyn_acl_uuid = va_it->second.id;
        va_it->second.num_of_entries++;
        ace_id = ++va_it->second.ace_id_latest;
        str = UpdateAclEntry(dyn_acl_uuid, false, entry, ace_id);
    }
    
    if (str != NULL) {
        delete entry;
        return str;
    } else {
        entry->ace_info.acl_id = dyn_acl_uuid;
        entry->ace_info.id = ace_id;
    }

    mc_tree_.insert(std::pair<MirrorCfgKey, MirrorCfgEntry *>(key, entry));


    IFMapNode *vn_node = agent_cfg_->cfg_vn_table()->FindNode(entry->data.apply_vn);
    if (vn_node && agent_cfg_->cfg_listener()->CanUseNode(vn_node)) {
        DBRequest req;
        assert(agent_cfg_->agent()->vn_table()->IFNodeToReq(vn_node, req) == false);
    }
    return NULL;
}

static inline IpAddress MaskToPrefix(int prefix_len) {
    if (prefix_len == 0 ) {
        return (IpAddress(Ip4Address((uint32_t)(~((int32_t) -1)))));
    }  else {
        return (IpAddress(Ip4Address((uint32_t)(~((1 << (32 - prefix_len)) - 1)))));
    }
}

const char *MirrorCfgTable::UpdateAclEntry (AclUuid &uuid, bool create,
                                            MirrorCfgEntry *entry,
                                            int ace_id) {
    AclSpec acl_spec;
    AclEntrySpec ace_spec;

    acl_spec.acl_id = uuid;
    ace_spec.terminal = false;
    ace_spec.id = ace_id;

    if (!(entry->data.src_ip_prefix.empty())) {
        ace_spec.src_addr_type = AddressMatch::IP_ADDR;
        boost::system::error_code ec;
        ace_spec.src_ip_addr = IpAddress::from_string(entry->data.src_ip_prefix.c_str(), ec);
        if (ec.value() != 0) {
            return "Invalid source-ip prefix";
        }
        if (!(ace_spec.src_ip_addr.is_v4())) {
            return "Invalid source-ip prefix";
        }
        ace_spec.src_ip_mask = MaskToPrefix(entry->data.src_ip_prefix_len);
    } else {
        ace_spec.src_addr_type = AddressMatch::NETWORK_ID;
        ace_spec.src_policy_id_str = entry->data.src_vn;
    }

    if (!(entry->data.dst_ip_prefix.empty())) {
        ace_spec.dst_addr_type = AddressMatch::IP_ADDR;
        boost::system::error_code ec;
        ace_spec.dst_ip_addr = IpAddress::from_string(entry->data.dst_ip_prefix.c_str(), ec);
        if (ec.value() != 0) {
            return "Invalid dest-ip prefix";
        }
        if (!(ace_spec.dst_ip_addr.is_v4())) {
            return "Invalid dest-ip prefix";
        }
        ace_spec.dst_ip_mask = MaskToPrefix(entry->data.dst_ip_prefix_len);
    } else {
        ace_spec.dst_addr_type = AddressMatch::NETWORK_ID;
        ace_spec.dst_policy_id_str = entry->data.dst_vn;
    }

    RangeSpec rs;
    rs.min = entry->data.start_src_port;
    rs.max = entry->data.end_src_port;
    if ((rs.min == (uint16_t)-1) && (rs.max == (uint16_t)-1)) {
        rs.min = 0;
    }
    ace_spec.src_port.push_back(rs);

    rs.min = entry->data.start_dst_port;
    rs.max = entry->data.end_dst_port;
    if ((rs.min == (uint16_t)-1) && (rs.max == (uint16_t)-1)) {
        rs.min = 0;
    }
    ace_spec.dst_port.push_back(rs);

    if (entry->data.protocol == -1) {
        rs.min = 0x0;
        rs.max = 0xff;
    } else {
        rs.min = rs.max = entry->data.protocol;
    }
    ace_spec.protocol.push_back(rs);

    // Fill action part later
    ActionSpec action_spec;
    action_spec.ta_type = TrafficAction::MIRROR_ACTION;
    boost::system::error_code ec;
    action_spec.ma.ip = Ip4Address::from_string(entry->data.ip, ec);
    if (ec.value() != 0) {
        return "Invalid mirror destination address ";
    }
    action_spec.ma.port = entry->data.udp_port;
    action_spec.ma.vrf_name = entry->data.mirror_vrf;
    action_spec.ma.analyzer_name = entry->key.handle;
    ace_spec.action_l.push_back(action_spec);
    // Add the ace to the acl
    acl_spec.acl_entry_specs_.push_back(ace_spec);

    // Enqueue ACL request
    DBRequest req;
    AclKey *key = new AclKey(uuid);
    AclData *data = new AclData(acl_spec);
    data->ace_add = true;
    LOG(DEBUG, "Ace add: " << data->ace_add << ", Ace spec id:" 
        << ace_spec.id);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(data);
    agent_cfg_->agent()->acl_table()->Enqueue(&req);

    return NULL;
}

void MirrorCfgTable::Delete(MirrorCfgKey &key) {
    MirrorCfgTree::iterator it;
    it = mc_tree_.find(key);
    if (it == mc_tree_.end()) {
        return;
    }

    MirrorCfgEntry *entry = it->second;
    MirrorTable::DelMirrorEntry(entry->key.handle);

    // Update ACL
    VnAclMap::iterator va_it;
    va_it = vn_acl_map_.find(entry->data.apply_vn);
    va_it->second.num_of_entries--;
    if (va_it->second.num_of_entries) {
        DBRequest areq;
        areq.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        AclKey *akey = new AclKey(va_it->second.id);
        AclData *adata = new AclData(entry->ace_info.id);
        areq.key.reset(akey);
        areq.data.reset(adata);
        agent_cfg_->agent()->acl_table()->Enqueue(&areq);
        
        // delete entry from mv map
        delete entry;
        mc_tree_.erase(it);
        return;
    }

    DBRequest areq;
    areq.oper = DBRequest::DB_ENTRY_DELETE;
    AclKey *akey = new AclKey(va_it->second.id);
    areq.key.reset(akey);
    areq.data.reset(NULL);
    agent_cfg_->agent()->acl_table()->Enqueue(&areq);

    mc_tree_.erase(it);

    // delete from vn_acl map
    vn_acl_map_.erase(va_it);

    IFMapNode *vn_node = agent_cfg_->cfg_vn_table()->FindNode(entry->data.apply_vn);
    if (vn_node && agent_cfg_->cfg_listener()->CanUseNode(vn_node)) {
        DBRequest req;
        assert(agent_cfg_->agent()->vn_table()->IFNodeToReq(vn_node, req) == false);
    }

    // delete entry from mv map
    delete entry;
    return;
}

const uuid MirrorCfgTable::GetMirrorUuid(const string &vn_name) const {
     VnAclMap::const_iterator va_it;
     va_it = vn_acl_map_.find(vn_name);
     if (va_it == vn_acl_map_.end()) {
         return nil_uuid();
     }

     return va_it->second.id;
}

void MirrorCfgTable::SetMirrorCfgSandeshData(std::string &handle, 
					     MirrorCfgDisplayResp &resp) {
    MirrorCfgTree::iterator it;

    std::vector<MirrorCfgSandesh> mc_l;
    for (it = mc_tree_.begin(); it != mc_tree_.end(); ++it) {
        MirrorCfgEntry *mc_entry = it->second;
        if (!handle.empty() && (handle != mc_entry->key.handle)) {
            continue;
        }
        MirrorCfgSandesh mc_s;
        mc_s.set_handle(mc_entry->key.handle);
        mc_s.set_apply_vn(mc_entry->data.apply_vn);
        mc_s.set_src_vn(mc_entry->data.src_vn);
        mc_s.set_src_ip_prefix(mc_entry->data.src_ip_prefix);
        mc_s.set_src_ip_prefix_len(mc_entry->data.src_ip_prefix_len);
        mc_s.set_dst_vn(mc_entry->data.dst_vn);
        mc_s.set_dst_ip_prefix(mc_entry->data.dst_ip_prefix);
        mc_s.set_dst_ip_prefix_len(mc_entry->data.dst_ip_prefix_len);
        mc_s.set_start_src_port(mc_entry->data.start_src_port);
        mc_s.set_end_src_port(mc_entry->data.end_src_port);
        mc_s.set_start_dst_port(mc_entry->data.start_dst_port);
        mc_s.set_end_dst_port(mc_entry->data.end_dst_port);
        mc_s.set_protocol(mc_entry->data.protocol);
        mc_s.set_ip(mc_entry->data.ip);
        mc_s.set_udp_port(mc_entry->data.udp_port);
        mc_s.set_time_period(mc_entry->data.time_period);
        mc_s.set_mirror_vrf(mc_entry->data.mirror_vrf);
        mc_l.push_back(mc_s);
    }
    resp.set_mcfg_l(mc_l);
}

void MirrorCfgTable::SetMirrorCfgVnSandeshData(std::string &vn_name,
					       MirrorCfgVnInfoResp &resp) {
    VnAclMap::iterator it;
    std::vector<VnAclInfo> vn_l;
    for (it = vn_acl_map_.begin(); it != vn_acl_map_.end(); ++it) {
        if (!vn_name.empty() && (vn_name != it->first)) {
            continue;
        }
        VnAclInfo vn_acl_info;
        vn_acl_info.set_vn_name(it->first);
        vn_acl_info.set_dyn_acl_uuid(UuidToString(it->second.id));
        vn_acl_info.set_num_of_entries(it->second.num_of_entries);
        vn_l.push_back(vn_acl_info);
    }
    resp.set_vn_acl_info_l(vn_l);
}

void MirrorCreateReq::HandleRequest() const {
    const char *str = Agent::GetInstance()->mirror_cfg_table()->Add(*this);
    MirrorCfgResp *resp = new MirrorCfgResp();
    resp->set_context(context());
    if (str == NULL) {
        str = "Success";
    } 
    resp->set_resp(str);
    resp->Response();
    return;
}

void MirrorDeleteReq::HandleRequest() const {
    MirrorCfgKey key;
    key.handle = get_handle();
    Agent::GetInstance()->mirror_cfg_table()->Delete(key);
    MirrorCfgResp *resp = new MirrorCfgResp();
    resp->set_context(context());
    resp->Response();
    return;
}

void MirrorCfgDisplayReq::HandleRequest() const {
    std::string handle = get_handle();
    MirrorCfgDisplayResp *resp = new MirrorCfgDisplayResp();
    Agent::GetInstance()->mirror_cfg_table()->SetMirrorCfgSandeshData(handle, *resp);
    resp->set_context(context());
    resp->Response();
    return;
}

void MirrorCfgVnInfoReq::HandleRequest() const {
    std::string vn_name = get_vn_name();
    MirrorCfgVnInfoResp *resp = new MirrorCfgVnInfoResp();
    Agent::GetInstance()->mirror_cfg_table()->SetMirrorCfgVnSandeshData(vn_name, *resp);
    resp->set_context(context());
    resp->Response();
    return;
}

void IntfMirrorCfgTable::Shutdown() {
}

void IntfMirrorCfgTable::Init() {
     return;
}

const char *IntfMirrorCfgTable::Add(const IntfMirrorCreateReq &intf_mirror) {
    MirrorCfgKey key;
    //const IntfMirrorCfgSandesh &intf_mirror = cfg.get_intf_mirr();
    key.handle = intf_mirror.get_handle();
    if (key.handle.empty()) {
        return "Invalid Handle";
    }
    
    IntfMirrorCfgTree::iterator it;
    it = intf_mc_tree_.find(key);
    if (it != intf_mc_tree_.end()) {
        return "Update not supported";
    }

    IntfMirrorCfgEntry *entry = new IntfMirrorCfgEntry;
    entry->key = key;
    entry->data.intf_id = StringToUuid(intf_mirror.get_intf_uuid());
    entry->data.intf_name = intf_mirror.get_intf_name();
    entry->data.mirror_dest.handle = intf_mirror.get_handle();
    boost::system::error_code ec;
    entry->data.mirror_dest.dip = Ip4Address::from_string(intf_mirror.get_ip(), ec);
    if (ec.value() != 0) {
        delete entry;
        return "Invalid mirror destination address ";
    }
    if (intf_mirror.get_udp_port() == 0) {
        delete entry;
        return "Invald mirror destination port ";
    }
    entry->data.mirror_dest.dport = intf_mirror.get_udp_port();
    if (agent_cfg_->agent()->router_id() == entry->data.mirror_dest.dip) {
        entry->data.mirror_dest.sip = Ip4Address(METADATA_IP_ADDR);
    } else {
        entry->data.mirror_dest.sip = agent_cfg_->agent()->router_id();
    }
    entry->data.mirror_dest.sport = agent_cfg_->agent()->mirror_port();
    entry->data.mirror_dest.time_period = intf_mirror.get_time_period();
    entry->data.mirror_dest.mirror_vrf = intf_mirror.get_mirror_vrf();

    MirrorTable::AddMirrorEntry(entry->key.handle,
                                entry->data.mirror_dest.mirror_vrf,
                                entry->data.mirror_dest.sip.to_v4(),
                                entry->data.mirror_dest.sport,
                                entry->data.mirror_dest.dip.to_v4(),
                                entry->data.mirror_dest.dport);
    intf_mc_tree_.insert(std::pair<MirrorCfgKey, IntfMirrorCfgEntry *>(key, entry));

    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                  entry->data.intf_id,
                                                  entry->data.intf_name);
    Interface *intf;
    intf = static_cast<Interface *>(agent_cfg_->agent()->interface_table()->FindActiveEntry(intf_key));
    if (intf) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(intf_key);
        VmInterfaceMirrorData *intf_data =
            new VmInterfaceMirrorData(true, entry->key.handle);
        req.data.reset(intf_data);
        agent_cfg_->agent()->interface_table()->Enqueue(&req);
    } else {
        delete intf_key;
    }

    return NULL;
}

void IntfMirrorCfgTable::Delete(MirrorCfgKey &key) {
    IntfMirrorCfgTree::iterator it;
    it = intf_mc_tree_.find(key);
    if (it == intf_mc_tree_.end()) {
        return;
    }
    IntfMirrorCfgEntry *entry = it->second;    
    MirrorTable::DelMirrorEntry(entry->key.handle);

    VmInterfaceKey *intf_key = new VmInterfaceKey(AgentKey::ADD_DEL_CHANGE,
                                                  entry->data.intf_id,
                                                  entry->data.intf_name);
    Interface *intf;
    intf = static_cast<Interface *>(agent_cfg_->agent()->interface_table()->FindActiveEntry(intf_key));
    if (intf) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        req.key.reset(intf_key);
        VmInterfaceMirrorData *intf_data =
            new VmInterfaceMirrorData(false, std::string());
        req.data.reset(intf_data);
        agent_cfg_->agent()->interface_table()->Enqueue(&req);
    } else {
        delete intf_key;
    }

    delete entry;
    intf_mc_tree_.erase(it);
}

void IntfMirrorCreateReq::HandleRequest() const {
    const char *str = Agent::GetInstance()->interface_mirror_cfg_table()->Add(*this);
    MirrorCfgResp *resp = new MirrorCfgResp();
    resp->set_context(context());
    if (str == NULL) {
        str = "Success";
    } 
    resp->set_resp(str);
    resp->Response();
    return;
}

void IntfMirrorDeleteReq::HandleRequest() const {
    MirrorCfgKey key;
    key.handle = get_handle();
    Agent::GetInstance()->interface_mirror_cfg_table()->Delete(key);
    MirrorCfgResp *resp = new MirrorCfgResp();
    resp->set_context(context());
    resp->set_resp("Success");
    resp->Response();
    return;
}

void IntfMirrorCfgTable::SetIntfMirrorCfgSandeshData(std::string &handle, 
                                            IntfMirrorCfgDisplayResp &resp) {
    IntfMirrorCfgTree::iterator it;
    std::vector<IntfMirrorCfgSandesh> mc_l;
    for (it = intf_mc_tree_.begin(); it != intf_mc_tree_.end(); ++it) {
        IntfMirrorCfgEntry *mc_entry = it->second;
        if (!handle.empty() && (handle != mc_entry->key.handle)) {
            continue;
        }
        IntfMirrorCfgSandesh mc_s;
        mc_s.set_handle(mc_entry->key.handle);
        mc_s.set_intf_uuid(UuidToString(mc_entry->data.intf_id));
        mc_s.set_intf_name(mc_entry->data.intf_name);
        mc_s.set_ip(mc_entry->data.mirror_dest.dip.to_string());
        mc_s.set_udp_port(mc_entry->data.mirror_dest.dport);
        mc_s.set_time_period(mc_entry->data.mirror_dest.time_period);
        mc_s.set_mirror_vrf(mc_entry->data.mirror_dest.mirror_vrf);
        mc_l.push_back(mc_s);
    }
    resp.set_imcfg_l(mc_l);
}

void IntfMirrorCfgDisplayReq::HandleRequest() const {
    std::string handle = get_handle();
    IntfMirrorCfgDisplayResp *resp = new IntfMirrorCfgDisplayResp();
    Agent::GetInstance()->interface_mirror_cfg_table()->SetIntfMirrorCfgSandeshData(handle, *resp);
    resp->set_context(context());
    resp->Response();
    return;
}


