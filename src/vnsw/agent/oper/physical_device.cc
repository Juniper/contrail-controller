/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <oper/agent_sandesh.h>
#include <oper/operdb_init.h>
#include <oper/ifmap_dependency_manager.h>

#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>

#include <vector>
#include <string>

using std::string;
using boost::assign::map_list_of;
using boost::assign::list_of;

/////////////////////////////////////////////////////////////////////////////
// PhysicalDevice routines
/////////////////////////////////////////////////////////////////////////////
static string ToString(PhysicalDevice::ManagementProtocol proto) {
    switch (proto) {
    case PhysicalDevice::OVS:
        return "OVS";
        break;

    default:
        break;
    }

    return "INVALID";
}

static PhysicalDevice::ManagementProtocol FromString(const string &proto) {
    if (strcasecmp(proto.c_str(), "ovs"))
        return PhysicalDevice::OVS;

    return PhysicalDevice::INVALID;
}

bool PhysicalDevice::IsLess(const DBEntry &rhs) const {
    const PhysicalDevice &a =
        static_cast<const PhysicalDevice &>(rhs);
    return (uuid_ < a.uuid_);
}

string PhysicalDevice::ToString() const {
    return UuidToString(uuid_);
}

DBEntryBase::KeyPtr PhysicalDevice::GetDBRequestKey() const {
    PhysicalDeviceKey *key = new PhysicalDeviceKey(uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalDevice::SetKey(const DBRequestKey *key) {
    const PhysicalDeviceKey *k = static_cast<const PhysicalDeviceKey *>(key);
    uuid_ = k->uuid_;
}

bool PhysicalDevice::Copy(const PhysicalDeviceTable *table,
                               const PhysicalDeviceData *data) {
    bool ret = false;

    if (fq_name_ != data->fq_name_) {
        fq_name_ = data->fq_name_;
        ret = true;
    }

    if (name_ != data->name_) {
        name_ = data->name_;
        ret = true;
    }

    if (vendor_ != data->vendor_) {
        vendor_ = data->vendor_;
        ret = true;
    }

    if (ip_ != data->ip_) {
        ip_ = data->ip_;
        ret = true;
    }

    if (management_ip_ != data->management_ip_) {
        management_ip_ = data->management_ip_;
        ret = true;
    }

    if (management_ip_ != data->management_ip_) {
        management_ip_ = data->management_ip_;
        ret = true;
    }

    ManagementProtocol proto = FromString(data->protocol_);
    if (protocol_ != proto) {
        protocol_ = proto;
        ret = true;
    }

    if (ifmap_node_ != data->ifmap_node_) {
        OperDB *oper = table->agent()->oper_db();
        oper->dependency_manager()->SetObject(data->ifmap_node_, this);
        if (ifmap_node_)
            oper->dependency_manager()->ResetObject(ifmap_node_);
        ifmap_node_ = data->ifmap_node_;
    }

    return ret;
}

/////////////////////////////////////////////////////////////////////////////
// PhysicalDeviceTable routines
/////////////////////////////////////////////////////////////////////////////
std::auto_ptr<DBEntry> PhysicalDeviceTable::AllocEntry(const DBRequestKey *k)
    const {
    const PhysicalDeviceKey *key = static_cast<const PhysicalDeviceKey *>(k);
    PhysicalDevice *dev = new PhysicalDevice(key->uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(dev));
}

DBEntry *PhysicalDeviceTable::Add(const DBRequest *req) {
    PhysicalDeviceKey *key = static_cast<PhysicalDeviceKey *>(req->key.get());
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    PhysicalDevice *dev = new PhysicalDevice(key->uuid_);
    dev->Copy(this, data);
    dev->SendObjectLog(AgentLogEvent::ADD);
    return dev;
}

bool PhysicalDeviceTable::OnChange(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    bool ret = dev->Copy(this, data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool PhysicalDeviceTable::Delete(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    dev->SendObjectLog(AgentLogEvent::DELETE);
    if (dev->ifmap_node_)
        agent()->oper_db()->dependency_manager()->ResetObject(dev->ifmap_node_);
    return true;
}

PhysicalDevice *PhysicalDeviceTable::Find(const boost::uuids::uuid &u) {
    PhysicalDeviceKey key(u);
    return static_cast<PhysicalDevice *>(FindActiveEntry(&key));
}

DBTableBase *PhysicalDeviceTable::CreateTable(DB *db, const std::string &name) {
    PhysicalDeviceTable *table = new PhysicalDeviceTable(db, name);
    table->Init();
    return table;
}

/////////////////////////////////////////////////////////////////////////////
// Config handling
/////////////////////////////////////////////////////////////////////////////
void PhysicalDeviceTable::ConfigEventHandler(DBEntry *entry) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    DBRequest req;
    if (IFNodeToReq(dev->ifmap_node_, req) == true) {
        Enqueue(&req);
    }
}

void PhysicalDeviceTable::RegisterDBClients(IFMapDependencyManager *dep) {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    ReactionMap device_react = map_list_of<std::string, PropagateList>
        ("self", list_of("self"))
        ("physical-router-physical-interface", list_of("self"));
    dep->RegisterReactionMap("physical-router", device_react);

    dep->Register("physical-router",
                  boost::bind(&PhysicalDeviceTable::ConfigEventHandler, this,
                              _1));
    agent()->cfg()->Register("physical-router", this,
                             autogen::PhysicalRouter::ID_PERMS);
}

static PhysicalDeviceKey *BuildKey(const autogen::PhysicalRouter *router) {
    autogen::IdPermsType id_perms = router->id_perms();
    boost::uuids::uuid u;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return new PhysicalDeviceKey(u);
}

static PhysicalDeviceData *BuildData(IFMapNode *node,
                                     const autogen::PhysicalRouter *router) {
    boost::system::error_code ec;
    IpAddress ip = IpAddress();
    IpAddress mip = IpAddress();
    ip = IpAddress::from_string(router->dataplane_ip(), ec);
    mip = IpAddress::from_string(router->management_ip(), ec);
    return new PhysicalDeviceData(node->name(), router->display_name(),
                                  router->vendor_name(), ip, mip, "OVS", node);
}

bool PhysicalDeviceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);

    req.key.reset(BuildKey(router));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        agent()->physical_device_vn_table()->ConfigUpdate(node);
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(node, router));
    // Enqueue request for physical-router before DBRequest for
    // physical-router vn entry is processed below
    Enqueue(&req);

    agent()->physical_device_vn_table()->ConfigUpdate(node);
    // Return false since DBRequest already enqueued above.
    return false;
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
            (Agent::GetInstance()->physical_device_table());
    }
    void Alloc() {
        resp_ = new SandeshDeviceListResp();
    }
};

static void SetDeviceSandeshData(const PhysicalDevice *entry,
                                      SandeshDevice *data) {
    data->set_uuid(UuidToString(entry->uuid()));
    data->set_fq_name(entry->fq_name());
    data->set_name(entry->name());
    data->set_vendor(entry->vendor());
    data->set_ip_address(entry->ip().to_string());
    data->set_management_protocol(ToString(entry->protocol()));
}

bool PhysicalDevice::DBEntrySandesh(Sandesh *resp, std::string &name)
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

void PhysicalDevice::SendObjectLog(AgentLogEvent::type event) const {
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
    info.set_fq_name(fq_name_);
    info.set_name(name_);
    info.set_vendor(vendor_);
    info.set_ip_address(ip_.to_string());
    info.set_management_protocol(::ToString(protocol_));
    info.set_ref_count(GetRefCount());
    DEVICE_OBJECT_LOG_LOG("Device", SandeshLevel::SYS_INFO, info);
}
