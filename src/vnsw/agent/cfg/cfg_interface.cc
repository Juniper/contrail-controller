/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/agent.h>

#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>
#include <cfg/cfg_init.h>

using boost::uuids::uuid;

CfgIntEntry::CfgIntEntry() {
}

CfgIntEntry::CfgIntEntry(const boost::uuids::uuid &id) : port_id_(id) {
}

CfgIntEntry::~CfgIntEntry() {
}

// CfgIntData methods
void CfgIntData::Init (const uuid& vm_id, const uuid& vn_id,
                       const uuid& vm_project_id,
                       const std::string& tname, const Ip4Address& ip,
                       const Ip6Address& ip6, const std::string& mac,
                       const std::string& vm_name,
                       uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                       const CfgIntEntry::CfgIntType port_type,
                       const int32_t version) {
    vm_id_ = vm_id;
    vn_id_ = vn_id;
    vm_project_id_ = vm_project_id;
    tap_name_ = tname;
    ip_addr_ = ip;
    ip6_addr_ = ip6;
    mac_addr_ = mac;
    vm_name_ = vm_name;
    tx_vlan_id_ = tx_vlan_id;
    rx_vlan_id_ = rx_vlan_id;
    port_type_ = port_type;
    version_ = version;
}

// CfgIntEntry methods
void CfgIntEntry::Init(const CfgIntData& int_data) {
    vm_id_ = int_data.vm_id_;
    vn_id_ = int_data.vn_id_;
    tap_name_ = int_data.tap_name_;
    ip_addr_ = int_data.ip_addr_;
    ip6_addr_ = int_data.ip6_addr_;
    mac_addr_ = int_data.mac_addr_;
    vm_name_ = int_data.vm_name_;
    tx_vlan_id_ = int_data.tx_vlan_id_;
    rx_vlan_id_ = int_data.rx_vlan_id_;
    vm_project_id_ = int_data.vm_project_id_;
    port_type_ = int_data.port_type_;
    version_ = int_data.version_;
}

bool CfgIntEntry::IsLess(const DBEntry &rhs) const {
    const CfgIntEntry &a = static_cast<const CfgIntEntry &>(rhs);
    return port_id_ < a.port_id_;
}

DBEntryBase::KeyPtr CfgIntEntry::GetDBRequestKey() const {
    CfgIntKey *key = new CfgIntKey(port_id_);
    return DBEntryBase::KeyPtr(key);
}

void CfgIntEntry::SetKey(const DBRequestKey *key) { 
    const CfgIntKey *k = static_cast<const CfgIntKey *>(key);
    port_id_ = k->id_;
}

std::string CfgIntEntry::ToString() const {
    return "Interface Configuration";
}

std::string CfgIntEntry::CfgIntTypeToString(CfgIntEntry::CfgIntType type) {
    if (type == CfgIntEntry::CfgIntVMPort)
        return "CfgIntVMPort";
    if (type == CfgIntEntry::CfgIntNameSpacePort)
        return "CfgIntNameSpacePort";
    if (type == CfgIntEntry::CfgIntRemotePort)
        return "CfgIntRemotePort";
    return "CfgIntInvalid";
}

// CfgIntTable methods
std::auto_ptr<DBEntry> CfgIntTable::AllocEntry(const DBRequestKey *key) const {
    const CfgIntKey *k = static_cast<const CfgIntKey *>(key);
    CfgIntEntry *cfg_intf = new CfgIntEntry(k->id_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(cfg_intf));
}

bool CfgIntTable::OnChange(DBEntry *entry, const DBRequest *req) {
    bool ret = false;
    CfgIntEntry *cfg_int = static_cast<CfgIntEntry *>(entry);
    CfgIntData *data = static_cast<CfgIntData *>(req->data.get());

    // Handling only version change for now
    if (cfg_int->GetVersion() != data->version_) {
        cfg_int->SetVersion(data->version_);
        ret = true;
    }
    return ret;
}

DBEntry *CfgIntTable::Add(const DBRequest *req) {
    CfgIntKey *key = static_cast<CfgIntKey *>(req->key.get());
    CfgIntData *data = static_cast<CfgIntData *>(req->data.get());
    CfgIntEntry *cfg_int = new CfgIntEntry(key->id_);
    cfg_int->Init(*data);    
    CfgVnPortKey vn_port_key(cfg_int->GetVnUuid(), cfg_int->GetUuid());
    uuid_tree_[vn_port_key] = cfg_int;

    CFG_TRACE(IntfTrace, cfg_int->GetIfname(), 
              cfg_int->vm_name(), UuidToString(cfg_int->GetVmUuid()),
              UuidToString(cfg_int->GetVnUuid()),
              cfg_int->ip_addr().to_string(), "ADD", 
              cfg_int->GetVersion(), cfg_int->tx_vlan_id(),
              cfg_int->rx_vlan_id(),
              UuidToString(cfg_int->vm_project_uuid()),
              cfg_int->CfgIntTypeToString(cfg_int->port_type()),
              cfg_int->ip6_addr().to_string());
    return cfg_int;
}

bool CfgIntTable::Delete(DBEntry *entry, const DBRequest *req) {
    CfgIntEntry *cfg = static_cast<CfgIntEntry *>(entry);

    CFG_TRACE(IntfTrace, cfg->GetIfname(), 
              cfg->vm_name(), UuidToString(cfg->GetVmUuid()),
              UuidToString(cfg->GetVnUuid()),
              cfg->ip_addr().to_string(), "DELETE",
              cfg->GetVersion(), cfg->tx_vlan_id(),
              cfg->rx_vlan_id(),
              UuidToString(cfg->vm_project_uuid()),
              cfg->CfgIntTypeToString(cfg->port_type()),
              cfg->ip6_addr().to_string());

    CfgVnPortKey vn_port_key(cfg->GetVnUuid(), cfg->GetUuid());
    CfgVnPortTree::iterator it = uuid_tree_.find(vn_port_key);

    // Delete entry from UUID tree.
    if (it != uuid_tree_.end()) {
        // If duplicate delete, there will be no entry in UUID tree
        uuid_tree_.erase(it);
    }
    return true;
}

DBTableBase *CfgIntTable::CreateTable(DB *db, const std::string &name) {
    CfgIntTable *table = new CfgIntTable(db, name);
    table->Init();
    return table;
}
