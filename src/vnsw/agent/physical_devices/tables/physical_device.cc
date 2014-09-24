/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <ifmap/ifmap_node.h>

#include <cmn/agent_cmn.h>
#include <oper/agent_sandesh.h>
#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/device_manager.h>

#include <vector>
#include <string>

using AGENT::PhysicalDeviceEntry;
using AGENT::PhysicalDeviceTable;
using AGENT::PhysicalDeviceKey;
using AGENT::PhysicalDeviceData;
using std::string;

static string ToString(PhysicalDeviceEntry::ManagementProtocol proto) {
    switch (proto) {
    case PhysicalDeviceEntry::OVS:
        return "OVS";
        break;

    default:
        break;
    }

    return "INVALID";
}

static PhysicalDeviceEntry::ManagementProtocol FromString(const string &proto) {
    if (strcasecmp(proto.c_str(), "ovs"))
        return PhysicalDeviceEntry::OVS;

    return PhysicalDeviceEntry::INVALID;
}

bool PhysicalDeviceEntry::IsLess(const DBEntry &rhs) const {
    const PhysicalDeviceEntry &a =
        static_cast<const PhysicalDeviceEntry &>(rhs);
    return (uuid_ < a.uuid_);
}

string PhysicalDeviceEntry::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr PhysicalDeviceEntry::GetDBRequestKey() const {
    PhysicalDeviceKey *key = new PhysicalDeviceKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalDeviceEntry::SetKey(const DBRequestKey *key) {
    const PhysicalDeviceKey *k = static_cast<const PhysicalDeviceKey *>(key);
    uuid_ = k->uuid_;
}

bool PhysicalDeviceEntry::Copy(const PhysicalDeviceData *data) {
    bool ret = false;

    if (name_ == data->name_) {
        name_ = data->name_;
        ret = true;
    }

    if (vendor_ == data->vendor_) {
        vendor_ = data->vendor_;
        ret = true;
    }

    if (ip_ == data->ip_) {
        ip_ = data->ip_;
        ret = true;
    }

    ManagementProtocol proto = FromString(data->protocol_);
    if (protocol_ != proto) {
        protocol_ = proto;
        ret = true;
    }

    return ret;
}

std::auto_ptr<DBEntry> PhysicalDeviceTable::AllocEntry(const DBRequestKey *k)
    const {
    const PhysicalDeviceKey *key = static_cast<const PhysicalDeviceKey *>(k);
    PhysicalDeviceEntry *dev = new PhysicalDeviceEntry(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(dev));
}

DBEntry *PhysicalDeviceTable::Add(const DBRequest *req) {
    PhysicalDeviceKey *key = static_cast<PhysicalDeviceKey *>(req->key.get());
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    PhysicalDeviceEntry *dev = new PhysicalDeviceEntry(key->uuid_);
    dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::ADD);
    return dev;
}

bool PhysicalDeviceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    PhysicalDeviceEntry *dev = static_cast<PhysicalDeviceEntry *>(entry);
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    bool ret = dev->Copy(data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

void PhysicalDeviceTable::Delete(DBEntry *entry, const DBRequest *req) {
    PhysicalDeviceEntry *dev = static_cast<PhysicalDeviceEntry *>(entry);
    dev->SendObjectLog(AgentLogEvent::DELETE);
    return;
}

DBTableBase *PhysicalDeviceTable::CreateTable(DB *db, const std::string &name) {
    PhysicalDeviceTable *table = new PhysicalDeviceTable(db, name);
    table->Init();
    return table;
}

bool PhysicalDeviceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::VirtualMachine *cfg = static_cast <autogen::VirtualMachine *>
        (node->GetObject());
    assert(cfg);
    autogen::IdPermsType id_perms = cfg->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);

    PhysicalDeviceData *data = NULL;
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    } else {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        boost::system::error_code ec;
        IpAddress ip = IpAddress::from_string("1.1.1.1", ec);
        if (ec) {
            return false;
        }

        data = new PhysicalDeviceData(node->name(), "Juniper", ip, "ovs");
    }
    req.key.reset(new PhysicalDeviceKey(u));
    req.data.reset(data);
    return true;
}

PhysicalDeviceEntry *PhysicalDeviceTable::Find(const boost::uuids::uuid &u) {
    PhysicalDeviceKey key(u);
    return static_cast<PhysicalDeviceEntry *>(FindActiveEntry(&key));
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class DeviceSandesh : public AgentSandesh {
 public:
    DeviceSandesh(std::string context, const std::string &name)
        : AgentSandesh(context, name) {}

 private:
    DBTable *AgentGetTable() {
        return static_cast<DBTable *>
            (Agent::GetInstance()->device_manager()->device_table());
    }
    void Alloc() {
        resp_ = new SandeshDeviceListResp();
    }
};

static void SetDeviceSandeshData(const PhysicalDeviceEntry *entry,
                                      SandeshDevice *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    data->set_name(entry->name());
    data->set_vendor(entry->vendor());
    data->set_ip_address(entry->ip().to_string());
    data->set_management_protocol(ToString(entry->protocol()));
}

bool PhysicalDeviceEntry::DBEntrySandesh(Sandesh *resp, std::string &name)
    const {
    SandeshDeviceListResp *dev_resp =
        static_cast<SandeshDeviceListResp *> (resp);

    std::string str_uuid = UuidToString(uuid_);
    if (name.empty() || name_ == name) {
        SandeshDevice data;
        SetDeviceSandeshData(this, &data);
        std::vector<SandeshDevice> &list =
            const_cast<std::vector<SandeshDevice>&>
            (dev_resp->get_device_list());
        list.push_back(data);
        return true;
    }

    return false;
}

void SandeshDeviceReq::HandleRequest() const {
    DeviceSandesh *sand = new DeviceSandesh(context(), get_name());
    sand->DoSandesh();
}

void PhysicalDeviceEntry::SendObjectLog(AgentLogEvent::type event) const {
    DeviceObjectLogInfo info;

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
    info.set_vendor(vendor_);
    info.set_ip_address(ip_.to_string());
    info.set_management_protocol(::ToString(protocol_));
    info.set_ref_count(GetRefCount());
    DEVICE_OBJECT_LOG_LOG("Device", SandeshLevel::SYS_INFO, info);
}
