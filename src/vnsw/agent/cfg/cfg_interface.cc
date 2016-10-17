/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <base/time_util.h>
#include <base/string_util.h>
#include <cmn/agent_cmn.h>
#include <cmn/agent.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>
#include <cfg/cfg_init.h>
#include <oper/agent_sandesh.h>

using namespace std;

/////////////////////////////////////////////////////////////////////////////
// InterfaceConfigEntry related routines
/////////////////////////////////////////////////////////////////////////////
void InterfaceConfigEntry::Set(const InterfaceConfigData *data) {
    tap_name_ = data->tap_name_;
    last_update_time_ = UTCUsecToString(UTCTimestampUsec());
    do_subscribe_ = data->do_subscribe_;
    version_ = data->version_;
}

bool InterfaceConfigEntry::IsLess(const DBEntry &rhs) const {
    const InterfaceConfigEntry &port =
        static_cast<const InterfaceConfigEntry &>(rhs);
    if (key_type_ != port.key_type_)
        return key_type_ < port.key_type_;

    return Compare(port);
}

void InterfaceConfigEntry::SetKey(const InterfaceConfigKey *k) {
    key_type_ = k->key_type_;
}

bool InterfaceConfigEntry::Change(const InterfaceConfigData *rhs) {
    bool ret = false;
    if (version_ != rhs->version_) {
        version_ = rhs->version_;
        ret = true;
    }

    return ret;
}

static string key_type_names[] = {
    "UUID_KEY",
    "LABELS_KEY"
};

const string InterfaceConfigEntry::TypeToString(KeyType type) {
    if (type >= KEY_TYPE_INVALID || type < VMI_UUID)
        type = KEY_TYPE_INVALID;

    return key_type_names[type];
}

bool InterfaceConfigKey::IsLess(const InterfaceConfigKey *rhs) const {
    if (key_type_ != rhs->key_type_)
        return key_type_ < rhs->key_type_;
    return Compare(rhs);
}

/////////////////////////////////////////////////////////////////////////////
// InterfaceConfigVmiEntry related routines
/////////////////////////////////////////////////////////////////////////////
bool InterfaceConfigVmiEntry::Compare(const InterfaceConfigEntry &rhs) const {
    const InterfaceConfigVmiEntry &port =
        static_cast<const InterfaceConfigVmiEntry &>(rhs);
    return vmi_uuid_ < port.vmi_uuid_;
}

bool InterfaceConfigVmiEntry::OnAdd(const InterfaceConfigData *d) {
    InterfaceConfigEntry::Set(d);
    const InterfaceConfigVmiData *data =
        static_cast<const InterfaceConfigVmiData *>(d);
    MacAddress mac(data->mac_addr_);
    if (mac.IsZero()) {
        CFG_TRACE(IntfErrorTrace, "Invalid MAC address. Ignoring add message");
        return false;
    }
    vmi_type_ = data->vmi_type_;
    vm_uuid_ = data->vm_uuid_;
    vn_uuid_ = data->vn_uuid_;
    project_uuid_ = data->project_uuid_;
    ip4_addr_ = data->ip4_addr_;
    ip6_addr_ = data->ip6_addr_;
    mac_addr_ = data->mac_addr_;
    vm_name_ = data->vm_name_;
    tx_vlan_id_ = data->tx_vlan_id_;
    rx_vlan_id_ = data->rx_vlan_id_;

    CFG_TRACE(IntfTrace, tap_name(), vm_name_, UuidToString(vmi_uuid_),
              UuidToString(vn_uuid_), ip4_addr().to_string(), "ADD",
              version(), tx_vlan_id_, rx_vlan_id_, UuidToString(project_uuid_),
              VmiTypeToString(vmi_type()), ip6_addr_.to_string());
    return true;
}

bool InterfaceConfigVmiEntry::OnChange(const InterfaceConfigData *rhs) {
    return InterfaceConfigEntry::Change(rhs);
}

bool InterfaceConfigVmiEntry::OnDelete() {
    return true;
}

DBEntryBase::KeyPtr InterfaceConfigVmiEntry::GetDBRequestKey() const {
    InterfaceConfigVmiKey *key = new InterfaceConfigVmiKey(vmi_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void InterfaceConfigVmiEntry::SetKey(const DBRequestKey *k) {
    const InterfaceConfigVmiKey *key =
        static_cast<const InterfaceConfigVmiKey *> (k);
    InterfaceConfigEntry::SetKey(key);
    vmi_uuid_ = key->vmi_uuid_;
}

std::string InterfaceConfigVmiEntry::ToString() const {
    return "InterfaceConfigVmiEntry<" + UuidToString(vm_uuid_) + ">";
}

bool InterfaceConfigVmiEntry::DBEntrySandesh(Sandesh *sresp,
                                             std::string &name) const {
    SandeshInterfaceConfigResp *resp =
        static_cast<SandeshInterfaceConfigResp *> (sresp);
    SandeshInterfaceConfigEntry entry;
    entry.set_type("VMI-UUID-Based");
    entry.set_vmi_uuid(UuidToString(vmi_uuid_));
    entry.set_tap_name(tap_name());
    entry.set_last_update_time(last_update_time());
    entry.set_version(version());
    entry.set_vmi_type(VmiTypeToString(vmi_type_));
    entry.set_vm_uuid(UuidToString(vm_uuid_));
    entry.set_vn_uuid(UuidToString(vn_uuid_));
    entry.set_project_uuid(UuidToString(project_uuid_));
    entry.set_ip4_address(ip4_addr_.to_string());
    entry.set_ip6_address(ip6_addr_.to_string());
    entry.set_mac_address(mac_addr_);
    entry.set_vm_name(vm_name_);
    entry.set_rx_vlan_id(rx_vlan_id_);
    entry.set_tx_vlan_id(tx_vlan_id_);

    std::vector<SandeshInterfaceConfigEntry> &list =
        const_cast<std::vector<SandeshInterfaceConfigEntry>&>
        (resp->get_entries());
    list.push_back(entry);
    return true;
}

InterfaceConfigEntry *InterfaceConfigVmiKey::AllocEntry() const {
    return new InterfaceConfigVmiEntry(vmi_uuid_);
}

bool InterfaceConfigVmiKey::Compare(const InterfaceConfigKey *rhs) const {
    const InterfaceConfigVmiKey *key =
        static_cast<const InterfaceConfigVmiKey *>(rhs);
    return vmi_uuid_ < key->vmi_uuid_;
}

static string vmi_type_names[] = {
    "VM Interface",
    "Namespace",
    "Remote Port",
    "TYPE_INVALID"
};

string InterfaceConfigVmiEntry::VmiTypeToString(VmiType type) {
    if (type >= TYPE_INVALID || type < VM_INTERFACE)
        type = TYPE_INVALID;

    return vmi_type_names[type];
}

/////////////////////////////////////////////////////////////////////////////
// InterfaceConfigLabelEntry related routines
/////////////////////////////////////////////////////////////////////////////
bool InterfaceConfigLabelEntry::Compare(const InterfaceConfigEntry &rhs) const {
    const InterfaceConfigLabelEntry &port =
        static_cast<const InterfaceConfigLabelEntry &>(rhs);
    if (vm_label_ != port.vm_label_)
        return vm_label_ < port.vm_label_;

    return network_label_ < port.network_label_;
}

bool InterfaceConfigLabelEntry::OnAdd(const InterfaceConfigData *d) {
    InterfaceConfigEntry::Set(d);
    const InterfaceConfigLabelData *data =
        static_cast<const InterfaceConfigLabelData *>(d);
    vm_namespace_ = data->vm_namespace_;
    vm_ifname_ = data->vm_ifname_;
    return true;
}

bool InterfaceConfigLabelEntry::OnChange(const InterfaceConfigData *rhs) {
    return InterfaceConfigEntry::Change(rhs);
}

bool InterfaceConfigLabelEntry::OnDelete() {
    return true;
}

DBEntryBase::KeyPtr InterfaceConfigLabelEntry::GetDBRequestKey() const {
    InterfaceConfigLabelKey *key =
        new InterfaceConfigLabelKey(vm_label_, network_label_);
    return DBEntryBase::KeyPtr(key);
}

void InterfaceConfigLabelEntry::SetKey(const DBRequestKey *k) {
    const InterfaceConfigLabelKey *key =
        static_cast<const InterfaceConfigLabelKey *> (k);
    InterfaceConfigEntry::SetKey(key);
    vm_label_ = key->vm_label_;
    network_label_ = key->network_label_;
}

bool InterfaceConfigLabelKey::Compare(const InterfaceConfigKey *rhs) const {
    const InterfaceConfigLabelKey *key =
        static_cast<const InterfaceConfigLabelKey *>(rhs);
    if (vm_label_ != key->vm_label_)
        return vm_label_ < key->vm_label_;
    return network_label_ < key->network_label_;
}

std::string InterfaceConfigLabelEntry::ToString() const {
    return "InterfaceConfigLabelEntry<" + vm_label_ + ", " + network_label_ + ">";
}

bool InterfaceConfigLabelEntry::DBEntrySandesh(Sandesh *sresp,
                                               std::string &name) const {
    SandeshInterfaceConfigResp *resp =
        static_cast<SandeshInterfaceConfigResp *> (sresp);
    SandeshInterfaceConfigEntry entry;
    entry.set_type("Label-Based");
    entry.set_tap_name(tap_name());
    entry.set_last_update_time(last_update_time());
    entry.set_version(version());
    entry.set_vm_label(vm_label());
    entry.set_vn_label(network_label());
    entry.set_network_namespace(vm_namespace());
    entry.set_vm_ifname(vm_ifname());

    std::vector<SandeshInterfaceConfigEntry> &list =
        const_cast<std::vector<SandeshInterfaceConfigEntry>&>
        (resp->get_entries());
    list.push_back(entry);
    return true;
}

InterfaceConfigEntry *InterfaceConfigLabelKey::AllocEntry() const {
    return new InterfaceConfigLabelEntry(vm_label_, network_label_);
}

/////////////////////////////////////////////////////////////////////////////
// InterfaceConfigTable related routines
/////////////////////////////////////////////////////////////////////////////
DBTableBase *InterfaceConfigTable::CreateTable(DB *db, const string &name) {
    InterfaceConfigTable *table = new InterfaceConfigTable(db, name);
    table->Init();
    return table;
}

std::auto_ptr<DBEntry> InterfaceConfigTable::AllocEntry(const DBRequestKey *k)
    const {
    const InterfaceConfigKey *key = static_cast<const InterfaceConfigKey *>(k);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(key->AllocEntry()));
}

DBEntry *InterfaceConfigTable::Add(const DBRequest *req) {
    const InterfaceConfigKey *key =
        static_cast<const InterfaceConfigKey *>(req->key.get());
    const InterfaceConfigData *data =
        static_cast<const InterfaceConfigData *>(req->data.get());
    InterfaceConfigEntry *entry = key->AllocEntry();
    if (entry->OnAdd(data) == false) {
        delete entry;
        return NULL;
    }
    return entry;
}

bool InterfaceConfigTable::OnChange(DBEntry *entry, const DBRequest *req) {
    const InterfaceConfigData *data =
        static_cast<const InterfaceConfigData *>(req->data.get());
    InterfaceConfigEntry *port = static_cast<InterfaceConfigEntry *>(entry);
    return port->OnChange(data);
}

bool InterfaceConfigTable::Delete(DBEntry *entry, const DBRequest *req) {
    InterfaceConfigEntry *port = static_cast<InterfaceConfigEntry *>(entry);
    return port->OnDelete();
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////

// Sandesh support for InterfaceConfig table
class AgentSandeshInterfaceConfig : public AgentSandesh {
public:
    AgentSandeshInterfaceConfig(const std::string &context,
                                const std::string &tap_name,
                                const std::string &vmi_uuid,
                                const std::string &vm_label,
                                const std::string &vn_label);
    ~AgentSandeshInterfaceConfig() {}
    virtual bool Filter(const DBEntryBase *entry);
    virtual bool FilterToArgs(AgentSandeshArguments *args);

private:
    DBTable *AgentGetTable();
    void Alloc();
    std::string tap_name_;
    std::string vmi_uuid_;
    std::string vm_label_;
    std::string vn_label_;
};

AgentSandeshInterfaceConfig::AgentSandeshInterfaceConfig
(const std::string &context, const std::string &tap_name,
 const std::string &vmi_uuid, const std::string &vm_label,
 const std::string &vn_label) :
    AgentSandesh(context, ""), tap_name_(tap_name), vmi_uuid_(vmi_uuid),
    vm_label_(vm_label), vn_label_(vn_label) {
}

DBTable *AgentSandeshInterfaceConfig::AgentGetTable() {
    return Agent::GetInstance()->interface_config_table();
}

void AgentSandeshInterfaceConfig::Alloc() {
    resp_ = new SandeshInterfaceConfigResp();
}

static bool MatchSubString(const string &str, const string &sub_str) {
    if (sub_str.empty())
        return true;

    return (str.find(sub_str) != string::npos);
}

bool AgentSandeshInterfaceConfig::Filter(const DBEntryBase *e) {
    const InterfaceConfigEntry *entry =
        dynamic_cast<const InterfaceConfigEntry *>(e);
    const InterfaceConfigVmiEntry *vmi =
        dynamic_cast<const InterfaceConfigVmiEntry *>(e);
    const InterfaceConfigLabelEntry *label =
        dynamic_cast<const InterfaceConfigLabelEntry *>(e);
    assert(entry);

    if (MatchSubString(entry->tap_name(), tap_name_) == false)
        return false;

    if (vmi &&
        (MatchSubString(UuidToString(vmi->vmi_uuid()), vmi_uuid_) == false))
        return false;

    if (label && MatchSubString(label->vm_label(), vm_label_) == false)
        return false;

    if (label && MatchSubString(label->network_label(), vn_label_) == false)
        return false;

    return true;
}

bool AgentSandeshInterfaceConfig::FilterToArgs(AgentSandeshArguments *args) {
    args->Add("vmi_uuid", vmi_uuid_);
    args->Add("tap_name", tap_name_);
    args->Add("vm_label", vm_label_);
    args->Add("vn_label", vn_label_);
    return true;
}

AgentSandeshPtr
InterfaceConfigTable::GetAgentSandesh(const AgentSandeshArguments *args,
                                      const std::string &context) {
    AgentSandeshInterfaceConfig *entry;
    entry = new AgentSandeshInterfaceConfig(context,
                                            args->GetString("vmi_uuid"),
                                            args->GetString("tap_name"),
                                            args->GetString("vm_label"),
                                            args->GetString("vn_label"));
    return AgentSandeshPtr(entry);
}

void SandeshInterfaceConfigReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentSandeshInterfaceConfig(context(),
                                                         get_tap_name(),
                                                         get_vmi_uuid(),
                                                         get_vm_label(),
                                                         get_vn_label()));
    sand->DoSandesh(sand);
    return;
}
