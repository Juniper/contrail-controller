/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <oper/agent_sandesh.h>

#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_port.h>
#include <physical_devices/tables/logical_port.h>

#include <vector>
#include <string>

using AGENT::LogicalPortKey;
using AGENT::LogicalPortData;
using AGENT::LogicalPortEntry;
using AGENT::LogicalPortTable;

using AGENT::DefaultLogicalPortKey;
using AGENT::DefaultLogicalPortEntry;

using AGENT::VlanLogicalPortKey;
using AGENT::VlanLogicalPortEntry;

using std::string;
using std::auto_ptr;
using boost::uuids::uuid;

bool LogicalPortEntry::IsLess(const DBEntry &rhs) const {
    const LogicalPortEntry &a = static_cast<const LogicalPortEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string LogicalPortEntry::ToString() const {
    return UuidToString(uuid_);
}

void LogicalPortEntry::SetKey(const DBRequestKey *key) {
    const LogicalPortKey *k = static_cast<const LogicalPortKey *>(key);
    uuid_ = k->uuid_;
}

bool LogicalPortEntry::CopyBase(const LogicalPortData *data) {
    bool ret = false;

    if (name_ == data->name_) {
        name_ = data->name_;
        ret = true;
    }

    LogicalPortTable *table = static_cast<LogicalPortTable*>(get_table());
    PhysicalPortEntry *phy_port =
        table->physical_port_table()->Find(data->physical_port_);
    if (phy_port != physical_port_.get()) {
        physical_port_.reset(phy_port);
        ret = true;
    }

    if (Copy(data) == true) {
        ret = true;
    }

    return ret;
}

std::auto_ptr<DBEntry> LogicalPortTable::AllocEntry(const DBRequestKey *k)
    const {
    const LogicalPortKey *key = static_cast<const LogicalPortKey *>(k);
    return auto_ptr<DBEntry>(static_cast<DBEntry *>(key->AllocEntry(this)));
}

DBEntry *LogicalPortTable::Add(const DBRequest *req) {
    LogicalPortKey *key = static_cast<LogicalPortKey *>(req->key.get());
    LogicalPortData *data = static_cast<LogicalPortData *>(req->data.get());
    LogicalPortEntry *entry = key->AllocEntry(this);
    entry->CopyBase(data);
    entry->SendObjectLog(AgentLogEvent::ADD);
    return entry;
}

bool LogicalPortTable::OnChange(DBEntry *e, const DBRequest *req) {
    LogicalPortEntry *entry = static_cast<LogicalPortEntry *>(e);
    LogicalPortData *data = static_cast<LogicalPortData *>(req->data.get());
    bool ret = entry->CopyBase(data);
    entry->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

void LogicalPortTable::Delete(DBEntry *e, const DBRequest *req) {
    LogicalPortEntry *entry = static_cast<LogicalPortEntry *>(e);
    entry->SendObjectLog(AgentLogEvent::DELETE);
    return;
}

DBTableBase *LogicalPortTable::CreateTable(DB *db, const std::string &name) {
    LogicalPortTable *table = new LogicalPortTable(db, name);
    table->Init();
    return table;
}

void LogicalPortTable::RegisterDBClients() {
    physical_port_table_ = agent()->device_manager()->physical_port_table();
}

bool LogicalPortTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::VirtualMachine *cfg = static_cast <autogen::VirtualMachine *>
        (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    LogicalPortKey *key = NULL;
    LogicalPortData *data = NULL;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        boost::uuids::uuid physical_port;
        boost::uuids::uuid vif;
        data = new LogicalPortData(node->name(), physical_port, vif);
    }
    req.key.reset(key);
    req.data.reset(data);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class LogicalPortSandesh : public AgentSandesh {
 public:
    LogicalPortSandesh(const std::string &context, const std::string &name)
        : AgentSandesh(context, name) {}

 private:
    DBTable *AgentGetTable() {
        return static_cast<DBTable *>
            (Agent::GetInstance()->device_manager()->physical_port_table());
    }
    void Alloc() {
        resp_ = new SandeshLogicalPortListResp();
    }
};

static void SetLogicalPortSandeshData(const LogicalPortEntry *entry,
                                       SandeshLogicalPort *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    data->set_name(entry->name());
    if (entry->physical_port()) {
        data->set_physical_port(entry->physical_port()->name());
    } else {
        data->set_physical_port("INVALID");
    }
}

bool LogicalPortEntry::DBEntrySandesh(Sandesh *resp, std::string &name) const {
    SandeshLogicalPortListResp *port_resp =
        static_cast<SandeshLogicalPortListResp *>(resp);

    if (name.empty() || name_ == name) {
        SandeshLogicalPort data;
        SetLogicalPortSandeshData(this, &data);
        std::vector<SandeshLogicalPort> &list =
            const_cast<std::vector<SandeshLogicalPort>&>
            (port_resp->get_port_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void SandeshLogicalPortReq::HandleRequest() const {
    LogicalPortSandesh *sand = new LogicalPortSandesh(context(), get_name());
    sand->DoSandesh();
}

void LogicalPortEntry::SendObjectLog(AgentLogEvent::type event) const {
    LogicalPortObjectLogInfo info;

    string str;
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DELETE:
            str.assign("Deletion ");
            break;
        case AgentLogEvent::CHANGE:
            str.assign("Modification ");
            break;
        default:
            str.assign("INVALID");
            break;
    }
    info.set_event(str);

    info.set_uuid(UuidToString(uuid_));
    info.set_name(name_);
    if (physical_port_) {
        info.set_physical_port(physical_port_->name());
    } else {
        info.set_physical_port("INVALID");
    }
    info.set_ref_count(GetRefCount());
    LOGICAL_PORT_OBJECT_LOG_LOG("LogicalPort", SandeshLevel::SYS_INFO, info);
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////
LogicalPortEntry *VlanLogicalPortKey::AllocEntry(const LogicalPortTable *table)
    const {
    return new VlanLogicalPortEntry(uuid_);
}

DBEntryBase::KeyPtr VlanLogicalPortEntry::GetDBRequestKey() const {
    LogicalPortKey *key = static_cast<LogicalPortKey *>
        (new VlanLogicalPortKey(uuid()));
    return DBEntryBase::KeyPtr(key);
}

bool VlanLogicalPortEntry::Copy(const LogicalPortData *d) {
    bool ret = false;
    const VlanLogicalPortData *data =
        static_cast<const VlanLogicalPortData *>(d);

    if (vlan_ != data->vlan_) {
        vlan_ = data->vlan_;
        ret = true;
    }

    return ret;
}

//////////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////////
LogicalPortEntry *DefaultLogicalPortKey::AllocEntry
    (const LogicalPortTable *table) const {
    return new DefaultLogicalPortEntry(uuid_);
}

DBEntryBase::KeyPtr DefaultLogicalPortEntry::GetDBRequestKey() const {
    LogicalPortKey *key = static_cast<LogicalPortKey *>
        (new DefaultLogicalPortKey(uuid()));
    return DBEntryBase::KeyPtr(key);
}

bool DefaultLogicalPortEntry::Copy(const LogicalPortData *data) {
    return false;
}
