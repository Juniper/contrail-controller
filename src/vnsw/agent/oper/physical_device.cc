/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <ifmap/ifmap_node.h>
#include <cmn/agent_cmn.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_listener.h>
#include <oper/agent_sandesh.h>
#include <oper/operdb_init.h>
#include <oper/config_manager.h>
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

DBEntry *PhysicalDeviceTable::OperDBAdd(const DBRequest *req) {
    PhysicalDeviceKey *key = static_cast<PhysicalDeviceKey *>(req->key.get());
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    PhysicalDevice *dev = new PhysicalDevice(key->uuid_);
    dev->Copy(this, data);
    dev->SendObjectLog(AgentLogEvent::ADD);
    return dev;
}

bool PhysicalDeviceTable::OperDBOnChange(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    PhysicalDeviceData *data = static_cast<PhysicalDeviceData *>
        (req->data.get());
    bool ret = dev->Copy(this, data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool PhysicalDeviceTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    dev->SendObjectLog(AgentLogEvent::DELETE);
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
void PhysicalDeviceTable::RegisterDBClients(IFMapDependencyManager *dep) {
    typedef IFMapDependencyTracker::PropagateList PropagateList;
    typedef IFMapDependencyTracker::ReactionMap ReactionMap;

    // physical_device is created from ConfigManager change-list. Its possbile
    // that physical-interface link is processed before physical-device is
    // created. Let dependency manager to trigger all child physical-interfaces
    // on create
    ReactionMap device_react = map_list_of<std::string, PropagateList>
        ("self", list_of("self")("physical-router-physical-interface"))
        ("physical-router-physical-interface", list_of("self"));
    dep->RegisterReactionMap("physical-router", device_react);

    dep->Register("physical-router",
                  boost::bind(&AgentOperDBTable::ConfigEventHandler, this,
                              _1));
    agent()->cfg()->Register("physical-router", this,
                             autogen::PhysicalRouter::ID_PERMS);
}

static PhysicalDeviceKey *BuildKey(const autogen::PhysicalRouter
        *router, boost::uuids::uuid &u) {
    return new PhysicalDeviceKey(u);
}

static PhysicalDeviceData *BuildData(Agent *agent, IFMapNode *node,
                                     const autogen::PhysicalRouter *router) {
    boost::system::error_code ec;
    IpAddress ip = IpAddress();
    IpAddress mip = IpAddress();
    ip = IpAddress::from_string(router->dataplane_ip(), ec);
    mip = IpAddress::from_string(router->management_ip(), ec);
    return new PhysicalDeviceData(agent, node->name(), router->display_name(),
                                  router->vendor_name(), ip, mip, "OVS", node);
}

bool PhysicalDeviceTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    autogen::IdPermsType id_perms = router->id_perms();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, u);
    return true;
}

bool PhysicalDeviceTable::ProcessConfig(IFMapNode *node, DBRequest &req) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);

    boost::uuids::uuid u;
    if (agent()->cfg_listener()->GetCfgDBStateUuid(node, u) == false)
        return false;

    req.key.reset(BuildKey(router, u));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.data.reset(BuildData(agent(), node, router));
    Enqueue(&req);

    return false;
}

bool PhysicalDeviceTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);

    boost::uuids::uuid u;
    if (agent()->cfg_listener()->GetCfgDBStateUuid(node, u) == false)
        return false;

    req.key.reset(BuildKey(router, u));
    if (node->IsDeleted()) {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    agent()->config_manager()->AddPhysicalDeviceNode(node);
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class DeviceSandesh : public AgentSandesh {
 public:
    DeviceSandesh(std::string context, const std::string &name)
        : AgentSandesh(context, name) {}
    void SetSandeshPageReq(Sandesh *sresp, const SandeshPageReq *req);

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
    AgentSandeshPtr sand(new DeviceSandesh(context(), get_name()));
    sand->DoSandesh(0, AgentSandesh::kEntriesPerPage);
}

void DeviceSandesh::SetSandeshPageReq(Sandesh *sresp,
                                      const SandeshPageReq *req) {
    SandeshDeviceListResp *resp = static_cast<SandeshDeviceListResp *>(sresp);
    resp->set_req(*req);
}

AgentSandesh *PhysicalDeviceTable::GetAgentSandesh(const std::string &context) {
    return new DeviceSandesh(context, "");
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
