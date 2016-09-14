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

static bool ValidateMac(std::string &mac) {
    size_t pos = 0;
    int colon = 0;
    if (mac.empty()) {
        return false;
    }
    while ((pos = mac.find(':', pos)) != std::string::npos) {
        colon++;
        pos += 1;
    }
    if (colon != 5)
        return false;
    // NULL mac not expected.
    if (mac == MacAddress::ZeroMac().ToString())
        return false;
    return true;
}

void AddPortReq::HandleRequest() const {
    bool err = false;
    string resp_str;

    PortResp *resp = new PortResp();
    resp->set_context(context());

    uuid port_uuid = StringToUuid(get_port_uuid());
    uuid instance_uuid = StringToUuid(get_instance_uuid());
    uuid vn_uuid = StringToUuid(get_vn_uuid());
    uuid vm_project_uuid = StringToUuid(get_vm_project_uuid());
    string vm_name = get_vm_name();
    string tap_name = get_tap_name();
    uint16_t tx_vlan_id = get_tx_vlan_id();
    uint16_t rx_vlan_id = get_rx_vlan_id();
    int16_t port_type = get_port_type();
    CfgIntEntry::CfgIntType intf_type;

    boost::system::error_code ec, ec6;
    Ip4Address ip(Ip4Address::from_string(get_ip_address(), ec));
    Ip6Address ip6 = Ip6Address::from_string(get_ip6_address(), ec6);
    /* Return only if wrong IP address is passed in both IPv4 and IPv6 fields.
     * An IP address of all zeroes is not considered wrong/invalid */
    if ((ec != 0) && (ec6 != 0)) {
        resp_str += "Neither Ipv4 nor IPv6 address is correct, ";
        err = true;
    }
    string mac_address = get_mac_address();

    if (port_uuid == nil_uuid()) {
        resp_str += "Port uuid is not correct, ";
        err = true;
    }
    if (instance_uuid == nil_uuid()) {
        resp_str += "Instance uuid is not correct, ";
        err = true;
    }
    if (vn_uuid == nil_uuid()) {
        resp_str += "Vn uuid is not correct, ";
        err = true;
    }
    if (vm_project_uuid == nil_uuid()) {
        resp_str += "Vm project uuid is not correct, ";
        err = true;
    }
    if (!ValidateMac(mac_address)) {
        resp_str += "Invalid MAC, Use xx:xx:xx:xx:xx:xx format";
        err = true;
    }
    
    if (err) {
        resp->set_resp(resp_str);
        resp->Response();
        return;
    }

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new CfgIntKey(port_uuid));
    CfgIntData *cfg_int_data = new CfgIntData();
    intf_type = CfgIntEntry::CfgIntVMPort;
    if (port_type) {
        intf_type = CfgIntEntry::CfgIntNameSpacePort;
    }

    cfg_int_data->Init(instance_uuid, vn_uuid, vm_project_uuid,
                       tap_name, ip, ip6, mac_address,
                       vm_name, tx_vlan_id, rx_vlan_id,
                       intf_type, 0);
    req.data.reset(cfg_int_data);
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    ctable->Enqueue(&req);
    resp->set_resp(std::string("Success"));
    resp->Response();
    return;
}

void DeletePortReq::HandleRequest() const {
    PortResp *resp = new PortResp();
    resp->set_context(context());

    uuid port_uuid = StringToUuid(get_port_uuid());
    if (port_uuid == nil_uuid()) {
        resp->set_resp(std::string("Port uuid is not correct."));
        resp->Response();
        return;
    }

    CfgIntTable *ctable = Agent::GetInstance()->interface_config_table();
    assert(ctable);

    DBRequest req;
    req.key.reset(new CfgIntKey(port_uuid));
    req.oper = DBRequest::DB_ENTRY_DELETE;
    ctable->Enqueue(&req);
    resp->set_resp(std::string("Success"));
    resp->Response();
    return;
}
