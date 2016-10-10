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
