/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <oper/agent_sandesh.h>
#include <oper/vn.h>
#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_device_vn.h>

using AGENT::PhysicalDeviceVnEntry;
using AGENT::PhysicalDeviceVnTable;
using AGENT::PhysicalDeviceVnKey;
using AGENT::PhysicalDeviceVnData;

using std::string;

bool PhysicalDeviceVnEntry::IsLess(const DBEntry &rhs) const {
    const PhysicalDeviceVnEntry &a =
        static_cast<const PhysicalDeviceVnEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string PhysicalDeviceVnEntry::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr PhysicalDeviceVnEntry::GetDBRequestKey() const {
    PhysicalDeviceVnKey *key = new PhysicalDeviceVnKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalDeviceVnEntry::SetKey(const DBRequestKey *key) {
    const PhysicalDeviceVnKey *k =
        static_cast<const PhysicalDeviceVnKey *>(key);
    uuid_ = k->uuid_;
}

bool PhysicalDeviceVnEntry::Copy(const PhysicalDeviceVnData *data) {
    bool ret = false;

    PhysicalDeviceVnTable *table =
        static_cast<PhysicalDeviceVnTable*>(get_table());
    PhysicalDeviceEntry *dev = table->physical_device_table()->Find(data->device_);
    if (dev != device_.get()) {
        device_.reset(dev);
        ret = true;
    }

    VnEntry *vn = table->vn_table()->Find(data->vn_);
    if (vn != vn_.get()) {
        vn_.reset(vn);
        ret = true;
    }

    return ret;
}

std::auto_ptr<DBEntry> PhysicalDeviceVnTable::AllocEntry(const DBRequestKey *k)
    const {
    const PhysicalDeviceVnKey *key =
        static_cast<const PhysicalDeviceVnKey *>(k);
    PhysicalDeviceVnEntry *dev = new PhysicalDeviceVnEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(dev));
}

DBEntry *PhysicalDeviceVnTable::Add(const DBRequest *req) {
    PhysicalDeviceVnKey *key =
        static_cast<PhysicalDeviceVnKey *>(req->key.get());
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());
    PhysicalDeviceVnEntry *dev = new PhysicalDeviceVnEntry(key->uuid_);
    dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::ADD);
    return dev;
}

bool PhysicalDeviceVnTable::OnChange(DBEntry *entry, const DBRequest *req) {
    PhysicalDeviceVnEntry *dev = static_cast<PhysicalDeviceVnEntry *>(entry);
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());
    bool ret = dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

void PhysicalDeviceVnTable::Delete(DBEntry *entry, const DBRequest *req) {
    PhysicalDeviceVnEntry *dev = static_cast<PhysicalDeviceVnEntry *>(entry);
    dev->SendObjectLog(AgentLogEvent::DELETE);
    return;
}

DBTableBase *PhysicalDeviceVnTable::CreateTable(DB *db,
                                                const std::string &name) {
    PhysicalDeviceVnTable *table = new PhysicalDeviceVnTable(db, name);
    table->Init();
    return table;
}

void PhysicalDeviceVnTable::RegisterDBClients() {
    physical_device_table_ = agent()->device_manager()->device_table();
    vn_table_ = agent()->vn_table();
}

bool PhysicalDeviceVnTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::VirtualMachine *cfg = static_cast <autogen::VirtualMachine *>
        (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    PhysicalDeviceVnKey *key = new PhysicalDeviceVnKey(u);
    PhysicalDeviceVnData *data = NULL;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        boost::uuids::uuid dev;
        boost::uuids::uuid vn;
        data = new PhysicalDeviceVnData(dev, vn);
    }
    req.key.reset(key);
    req.data.reset(data);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class AgentPhysicalDeviceVnSandesh : public AgentSandesh {
 public:
    AgentPhysicalDeviceVnSandesh(std::string context, const std::string &name)
        : AgentSandesh(context, name) {}

 private:
    DBTable *AgentGetTable() {
        Agent *agent = Agent::GetInstance();
        return static_cast<DBTable *>
            (agent->device_manager()->physical_device_vn_table());
    }
    void Alloc() {
        resp_ = new SandeshPhysicalDeviceVnListResp();
    }
};

static void SetPhysicalDeviceVnSandeshData(const PhysicalDeviceVnEntry *entry,
                                           SandeshPhysicalDeviceVn *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    if (entry->device()) {
        data->set_device(entry->device()->name());
    } else {
        data->set_device("INVALID");
    }
    if (entry->vn()) {
        data->set_vn(UuidToString(entry->vn()->GetUuid()));
    } else {
        data->set_vn("INVALID");
    }
}

bool PhysicalDeviceVnEntry::DBEntrySandesh(Sandesh *resp, std::string &name)
    const {
    SandeshPhysicalDeviceVnListResp *port_resp =
        static_cast<SandeshPhysicalDeviceVnListResp *>(resp);

    if (name.empty() || UuidToString(uuid_) == name) {
        SandeshPhysicalDeviceVn data;
        SetPhysicalDeviceVnSandeshData(this, &data);
        std::vector<SandeshPhysicalDeviceVn> &list =
            const_cast<std::vector<SandeshPhysicalDeviceVn>&>
            (port_resp->get_port_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void SandeshPhysicalDeviceVnReq::HandleRequest() const {
    AgentPhysicalDeviceVnSandesh *sand =
        new AgentPhysicalDeviceVnSandesh(context(), get_device());
    sand->DoSandesh();
}

void PhysicalDeviceVnEntry::SendObjectLog(AgentLogEvent::type event) const {
    PhysicalDeviceVnObjectLogInfo info;

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
    if (device_.get()) {
        info.set_device(device_->name());
    } else {
        info.set_device("INVALID");
    }
    if (vn_) {
        info.set_device(device_->name());
    } else {
        info.set_device("INVALID");
    }
    info.set_ref_count(GetRefCount());
    PHYSICAL_DEVICE_VN_OBJECT_LOG_LOG("PhysicalDeviceVn",
                                      SandeshLevel::SYS_INFO, info);
}
