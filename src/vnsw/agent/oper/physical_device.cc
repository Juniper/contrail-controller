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
#include <oper/config_manager.h>
#include <oper/ifmap_dependency_manager.h>

#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>
#include <oper/tunnel_nh.h>

#include <vector>
#include <string>
#include <strings.h>
#include <multicast_types.h>

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

bool PhysicalDevice::Copy(PhysicalDeviceTable *table,
                          const PhysicalDeviceData *data) {
    bool ret = false;
    bool ip_updated = false;
    IpAddress old_ip;

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
        old_ip = ip_;
        ip_updated = true;
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

    if (ip_updated) {
        table->UpdateIpToDevMap(old_ip, ip_, this);
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
    PhysicalDeviceData *data = dynamic_cast<PhysicalDeviceData *>
        (req->data.get());
    assert(data);
    bool ret = dev->Copy(this, data);
    dev->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

bool PhysicalDeviceTable::OperDBResync(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    PhysicalDeviceTsnManagedData *data =
        dynamic_cast<PhysicalDeviceTsnManagedData *>(req->data.get());
    assert(data);
    if (dev->master() != data->master_) {
        dev->set_master(data->master_);
        return true;
    }
    return false;
}

bool PhysicalDeviceTable::OperDBDelete(DBEntry *entry, const DBRequest *req) {
    PhysicalDevice *dev = static_cast<PhysicalDevice *>(entry);
    DeleteIpToDevEntry(dev->ip());
    dev->SendObjectLog(AgentLogEvent::DEL);
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
}

static PhysicalDeviceKey *BuildKey(const autogen::PhysicalRouter
        *router, const boost::uuids::uuid &u) {
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

bool PhysicalDeviceTable::ProcessConfig(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);

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

bool PhysicalDeviceTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {
    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);

    assert(!u.is_nil());

    req.key.reset(BuildKey(router, u));
    if ((req.oper == DBRequest::DB_ENTRY_DELETE) || node->IsDeleted()) {
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
    data->set_master(entry->master());
}

bool PhysicalDevice::DBEntrySandesh(Sandesh *resp, std::string &name)
    const {
    SandeshDeviceListResp *dev_resp =
        static_cast<SandeshDeviceListResp *> (resp);

    std::string str_uuid = UuidToString(uuid_);
    if (name.empty() || name_.find(name) != string::npos) {
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
    sand->DoSandesh(sand);
}

AgentSandeshPtr PhysicalDeviceTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr(new DeviceSandesh(context, args->GetString("name")));
}

void PhysicalDevice::SendObjectLog(AgentLogEvent::type event) const {
    DeviceObjectLogInfo info;

    string str;
    switch (event) {
        case AgentLogEvent::ADD:
            str.assign("Addition ");
            break;
        case AgentLogEvent::DEL:
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

void PhysicalDeviceTable::UpdateIpToDevMap(IpAddress old_ip, IpAddress new_ip,
                                           PhysicalDevice *p) {
    DeleteIpToDevEntry(old_ip);
    if (!new_ip.is_unspecified()) {
        IpToDeviceMap::iterator it = ip_tree_.find(new_ip);
        if (it == ip_tree_.end()) {
            ip_tree_.insert(IpToDevicePair(new_ip, p));
        }
    }
}

void PhysicalDeviceTable::DeleteIpToDevEntry(IpAddress ip) {
    if (!ip.is_unspecified()) {
        IpToDeviceMap::iterator it = ip_tree_.find(ip);
        if (it != ip_tree_.end()) {
            ip_tree_.erase(it);
        }
    }
}

PhysicalDevice *PhysicalDeviceTable::IpToPhysicalDevice(IpAddress ip) {
    if (!ip.is_unspecified()) {
        IpToDeviceMap::iterator it = ip_tree_.find(ip);
        if (it != ip_tree_.end()) {
            return it->second;
        }
    }
    return NULL;
}

// Mastership changed for device, enqueue RESYNC to update master_ field if
// physical-device already present
void PhysicalDeviceTable::EnqueueDeviceChange(const boost::uuids::uuid &u,
                                              bool master) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalDeviceKey(u, AgentKey::RESYNC));

    req.data.reset(new PhysicalDeviceTsnManagedData(agent(), master));
    Enqueue(&req);
}

void PhysicalDeviceTable::AddDeviceToVrfEntry(const boost::uuids::uuid &u,
                                              const std::string &vrf) {
    DeviceVrfMap::iterator it = device2vrf_map_.find(u);
    if (it == device2vrf_map_.end()) {
        VrfSet vrf_set;
        vrf_set.insert(vrf);
        device2vrf_map_.insert(DeviceVrfPair(u, vrf_set));
        return;
    }
    VrfSet &vrf_set = it->second;
    VrfSet::iterator vit = vrf_set.find(vrf);
    if (vit == vrf_set.end()) {
        vrf_set.insert(vrf);
    }
}

/* Removes VRF from the vrf_list to which the device points. If the device does
 * not point to any more VRFs, then the device entry itself is removed. If the
 * device entry itself is removed or if the device is absent in the list, it
 * returns true. */
bool PhysicalDeviceTable::RemoveDeviceToVrfEntry(const boost::uuids::uuid &u,
                                                 const std::string &vrf) {
    DeviceVrfMap::iterator it = device2vrf_map_.find(u);
    if (it == device2vrf_map_.end()) {
        return true;
    }
    VrfSet &vrf_set = it->second;
    VrfSet::iterator vit = vrf_set.find(vrf);
    if (vit == vrf_set.end()) {
        return false;
    }
    /* If the VRF to be removed is the only vrf to which the device points,
     * then remove the device itself */
    if (vrf_set.size() == 1) {
        device2vrf_map_.erase(it);
        return true;
    }
    vrf_set.erase(vit);
    return false;
}

void PhysicalDeviceTable::ResetDeviceMastership(const boost::uuids::uuid &u,
                                                const std::string &vrf) {
    if (!RemoveDeviceToVrfEntry(u, vrf)) {
        /* If the device is pointing to any other vrfs apart from the
         * one passed to this API, then we still need to have
         * mastership as true for that device */
        return;
    }
    PhysicalDeviceSet::iterator dit = managed_pd_set_.find(u);
    if (dit != managed_pd_set_.end()) {
        /* Update mastership as false for the device */
        EnqueueDeviceChange(u, false);
        managed_pd_set_.erase(dit);
    }
}

void PhysicalDeviceTable::UpdateDeviceMastership(const std::string &vrf,
                                                 ComponentNHList clist,
                                                 bool del) {
    PhysicalDeviceSet new_set;

    if (del) {
        VrfDevicesMap::iterator it = vrf2devices_map_.find(vrf);
        if (it == vrf2devices_map_.end()) {
            return;
        }
        PhysicalDeviceSet dev_set = it->second;
        PhysicalDeviceSet::iterator pit = dev_set.begin();
        while (pit != dev_set.end()) {
            ResetDeviceMastership(*pit, vrf);
            ++pit;
        }
        vrf2devices_map_.erase(it);
        return;
    }

    ComponentNHList::const_iterator comp_nh_it = clist.begin();
    for(;comp_nh_it != clist.end(); comp_nh_it++) {
        if ((*comp_nh_it) == NULL) {
            continue;
        }

        if ((*comp_nh_it)->nh()->GetType() != NextHop::TUNNEL) {
            continue;
        }
        const TunnelNH *tnh = static_cast<const TunnelNH *>
            ((*comp_nh_it)->nh());

        PhysicalDevice *dev = IpToPhysicalDevice(*(tnh->GetDip()));
        if (dev == NULL) {
            continue;
        }
        AddDeviceToVrfEntry(dev->uuid(), vrf);
        /* Enqueue the change as true only if it was not earlier enqueued.
         * List of previously enqueued devices (with master as true) is
         * present in managed_pd_set_ */
        PhysicalDeviceSet::iterator pit = managed_pd_set_.find(dev->uuid());
        if (pit == managed_pd_set_.end()) {
            EnqueueDeviceChange(dev->uuid(), true);
            managed_pd_set_.insert(dev->uuid());
        }
        new_set.insert(dev->uuid());
    }

    /* Iterate through the old per vrf physical device list. If any of them are
     * not present in new list, enqueue change on those devices with master
     * as false */
    VrfDevicesMap::iterator it = vrf2devices_map_.find(vrf);
    if (it == vrf2devices_map_.end()) {
        vrf2devices_map_.insert(VrfDevicesPair(vrf, new_set));
        return;
    }
    PhysicalDeviceSet dev_set = it->second;
    PhysicalDeviceSet::iterator pit = dev_set.begin();
    while (pit != dev_set.end()) {
        const boost::uuids::uuid &u = *pit;
        ++pit;
        PhysicalDeviceSet::iterator dit = new_set.find(u);
        if (dit == new_set.end()) {
            /* This means that physical-device 'u' is removed from vrf passed
             * to this API. Reset the mastership only if the physical-device
             * is not present for any other VRFs */
            ResetDeviceMastership(u, vrf);
        }
    }
    //Update the devices_set for the vrf with new_set
    it->second = new_set;
}

void MasterPhysicalDevicesReq::HandleRequest() const {
    MasterPhysicalDevicesResp *resp = new MasterPhysicalDevicesResp();
    resp->set_context(context());

    Agent *agent = Agent::GetInstance();
    PhysicalDeviceTable *obj = agent->physical_device_table();
    const PhysicalDeviceTable::PhysicalDeviceSet &dev_list =
        obj->managed_pd_set();
    PhysicalDeviceTable::PhysicalDeviceSet::const_iterator it =
        dev_list.begin();
    std::vector<PDeviceData> list;
    while (it != dev_list.end()) {
        PDeviceData data;
        data.set_uuid(to_string(*it));
        list.push_back(data);
        ++it;
    }
    resp->set_dev_list(list);
    resp->set_more(false);
    resp->Response();
}
