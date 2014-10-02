/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <oper/agent_sandesh.h>
#include <oper/ifmap_dependency_manager.h>

#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/device_manager.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_port.h>

#include <vector>
#include <string>

using AGENT::PhysicalPortEntry;
using AGENT::PhysicalPortTable;
using AGENT::PhysicalPortKey;
using AGENT::PhysicalPortData;

using std::string;

/////////////////////////////////////////////////////////////////////////////
// PhysicalPortEntry routines
/////////////////////////////////////////////////////////////////////////////
bool PhysicalPortEntry::IsLess(const DBEntry &rhs) const {
    const PhysicalPortEntry &a = static_cast<const PhysicalPortEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string PhysicalPortEntry::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr PhysicalPortEntry::GetDBRequestKey() const {
    PhysicalPortKey *key = new PhysicalPortKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalPortEntry::SetKey(const DBRequestKey *key) {
    const PhysicalPortKey *k = static_cast<const PhysicalPortKey *>(key);
    uuid_ = k->uuid_;
}

bool PhysicalPortEntry::Copy(const PhysicalPortData *data) {
    bool ret = false;

    if (name_ == data->name_) {
        name_ = data->name_;
        ret = true;
    }

    PhysicalPortTable *table = static_cast<PhysicalPortTable*>(get_table());
    PhysicalDeviceEntry *dev = table->device_table()->Find(data->device_);
    if (dev != device_.get()) {
        device_.reset(dev);
        ret = true;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// PhysicalPortTable routines
/////////////////////////////////////////////////////////////////////////////
std::auto_ptr<DBEntry> PhysicalPortTable::AllocEntry(const DBRequestKey *k)
    const {
    const PhysicalPortKey *key = static_cast<const PhysicalPortKey *>(k);
    PhysicalPortEntry *dev = new PhysicalPortEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(dev));
}

DBEntry *PhysicalPortTable::Add(const DBRequest *req) {
    PhysicalPortKey *key = static_cast<PhysicalPortKey *>(req->key.get());
    PhysicalPortData *data = static_cast<PhysicalPortData *>(req->data.get());
    PhysicalPortEntry *dev = new PhysicalPortEntry(key->uuid_);
    dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::ADD);
    return dev;
}

bool PhysicalPortTable::OnChange(DBEntry *entry, const DBRequest *req) {
    PhysicalPortEntry *dev = static_cast<PhysicalPortEntry *>(entry);
    PhysicalPortData *data = static_cast<PhysicalPortData *>(req->data.get());
    bool ret = dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

void PhysicalPortTable::Delete(DBEntry *entry, const DBRequest *req) {
    PhysicalPortEntry *dev = static_cast<PhysicalPortEntry *>(entry);
    dev->SendObjectLog(AgentLogEvent::DELETE);
    return;
}

PhysicalPortEntry *PhysicalPortTable::Find(const boost::uuids::uuid &u) {
    PhysicalPortKey key(u);
    return static_cast<PhysicalPortEntry *>(FindActiveEntry(&key));
}

DBTableBase *PhysicalPortTable::CreateTable(DB *db, const std::string &name) {
    PhysicalPortTable *table = new PhysicalPortTable(db, name);
    table->Init();
    return table;
}

/////////////////////////////////////////////////////////////////////////////
// Config handling routines
/////////////////////////////////////////////////////////////////////////////
void PhysicalPortTable::ConfigEventHandler(DBEntry *entry) {
}

void PhysicalPortTable::RegisterDBClients(IFMapDependencyManager *dep) {
    device_table_ = agent()->device_manager()->device_table();
    dep->Register("physical-interface",
                  boost::bind(&PhysicalPortTable::ConfigEventHandler, this,
                              _1));
    agent()->cfg()->Register("physical-interface", this,
                             autogen::PhysicalInterface::ID_PERMS);
}

static PhysicalPortKey *BuildKey(const autogen::PhysicalInterface *port) {
    autogen::IdPermsType id_perms = port->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return new PhysicalPortKey(u);
}

static PhysicalPortData *BuildData(const Agent *agent, IFMapNode *node,
                                   const autogen::PhysicalInterface *port) {
    boost::uuids::uuid dev_uuid;
    // Find link with physical-router adjacency
    IFMapNode *adj_node = NULL;
    adj_node = agent->cfg_listener()->FindAdjacentIFMapNode(agent, node,
                                                            "physical-router");
    if (adj_node) {
        autogen::PhysicalRouter *router =
            static_cast<autogen::PhysicalRouter *>(adj_node->GetObject());
        autogen::IdPermsType id_perms = router->id_perms();
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   dev_uuid);
    }

    return new PhysicalPortData(node->name(), dev_uuid);
}

bool PhysicalPortTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::PhysicalInterface *port =
        static_cast <autogen::PhysicalInterface *>(node->GetObject());
    assert(port);

    req.key.reset(BuildKey(port));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, port));
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class AgentPhysicalPortSandesh : public AgentSandesh {
 public:
    AgentPhysicalPortSandesh(std::string context, const std::string &name)
        : AgentSandesh(context, name) {}

 private:
    DBTable *AgentGetTable() {
        return static_cast<DBTable *>
            (Agent::GetInstance()->device_manager()->physical_port_table());
    }
    void Alloc() {
        resp_ = new SandeshPhysicalPortListResp();
    }
};

static void SetPhysicalPortSandeshData(const PhysicalPortEntry *entry,
                                       SandeshPhysicalPort *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    data->set_name(entry->name());
    if (entry->device()) {
        data->set_device(entry->device()->name());
    } else {
        data->set_device("INVALID");
    }
}

bool PhysicalPortEntry::DBEntrySandesh(Sandesh *resp, std::string &name) const {
    SandeshPhysicalPortListResp *port_resp =
        static_cast<SandeshPhysicalPortListResp *>(resp);

    if (name.empty() || name_ == name) {
        SandeshPhysicalPort data;
        SetPhysicalPortSandeshData(this, &data);
        std::vector<SandeshPhysicalPort> &list =
            const_cast<std::vector<SandeshPhysicalPort>&>
            (port_resp->get_port_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void SandeshPhysicalPortReq::HandleRequest() const {
    AgentPhysicalPortSandesh *sand = new AgentPhysicalPortSandesh(context(),
                                                                  get_name());
    sand->DoSandesh();
}

void PhysicalPortEntry::SendObjectLog(AgentLogEvent::type event) const {
    PhysicalPortObjectLogInfo info;

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
    if (device_) {
        info.set_device(device_->name());
    } else {
        info.set_device("INVALID");
    }
    info.set_ref_count(GetRefCount());
    PHYSICAL_PORT_OBJECT_LOG_LOG("PhysicalPort", SandeshLevel::SYS_INFO, info);
}
