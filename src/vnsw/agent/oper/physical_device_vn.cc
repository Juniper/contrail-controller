/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <cmn/agent_cmn.h>

#include <ifmap/ifmap_node.h>
#include <cfg/cfg_init.h>
#include <oper/agent_sandesh.h>
#include <oper/vn.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>
#include <oper/config_manager.h>

using std::string;
using boost::assign::map_list_of;
using boost::assign::list_of;

//////////////////////////////////////////////////////////////////////////////
// PhysicalDeviceVn routines
//////////////////////////////////////////////////////////////////////////////
bool PhysicalDeviceVn::IsLess(const DBEntry &rhs) const {
    const PhysicalDeviceVn &a =
        static_cast<const PhysicalDeviceVn &>(rhs);
    if (device_uuid_ != a.device_uuid_) {
        return (device_uuid_ < a.device_uuid_);
    }
    return (vn_uuid_ < a.vn_uuid_);
}

string PhysicalDeviceVn::ToString() const {
    return UuidToString(device_uuid_) + ":" + UuidToString(vn_uuid_);
}

DBEntryBase::KeyPtr PhysicalDeviceVn::GetDBRequestKey() const {
    PhysicalDeviceVnKey *key = new PhysicalDeviceVnKey(device_uuid_, vn_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalDeviceVn::SetKey(const DBRequestKey *k) {
    const PhysicalDeviceVnKey *key =
        static_cast<const PhysicalDeviceVnKey *>(k);

    device_uuid_ = key->device_uuid_;
    vn_uuid_ = key->vn_uuid_;
}

bool PhysicalDeviceVn::Copy(PhysicalDeviceVnTable *table,
                                 const PhysicalDeviceVnData *data) {
    bool ret = false;

    PhysicalDevice *dev =
        table->agent()->physical_device_table()->Find(device_uuid_);
    if (dev != device_.get()) {
        device_.reset(dev);
        ret = true;
    }

    if (dev && (dev->ip() != tor_ip_)) {
        tor_ip_ = dev->ip();
        ret = true;
    }

    if (dev && (dev->name() != device_display_name_)) {
        device_display_name_ = dev->name();
        ret = true;
    }

    VnEntry *vn = table->agent()->vn_table()->Find(vn_uuid_);
    if (vn != vn_.get()) {
        vn_.reset(vn);
        ret = true;
    }

    int vxlan_id = vn ? vn->GetVxLanId() : 0;
    if (vxlan_id != vxlan_id_) {
        vxlan_id_ = vxlan_id;
        ret = true;
    }

    return ret;
}

//////////////////////////////////////////////////////////////////////////////
// PhysicalDeviceVnTable routines
//////////////////////////////////////////////////////////////////////////////
std::auto_ptr<DBEntry> PhysicalDeviceVnTable::AllocEntry(const DBRequestKey *k)
    const {
    const PhysicalDeviceVnKey *key =
        static_cast<const PhysicalDeviceVnKey *>(k);

    PhysicalDeviceVn *entry = new PhysicalDeviceVn(key->device_uuid_,
                                                             key->vn_uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
}

DBEntry *PhysicalDeviceVnTable::Add(const DBRequest *req) {
    PhysicalDeviceVnKey *key =
        static_cast<PhysicalDeviceVnKey *>(req->key.get());
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());

    PhysicalDeviceVn *entry = new PhysicalDeviceVn(key->device_uuid_,
                                                             key->vn_uuid_);
    entry->Copy(this, data);
    entry->SendObjectLog(AgentLogEvent::ADD);
    return entry;
}

bool PhysicalDeviceVnTable::OnChange(DBEntry *e, const DBRequest *req) {
    PhysicalDeviceVn *entry = static_cast<PhysicalDeviceVn *>(e);
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());
    bool ret = entry->Copy(this, data);
    return ret;
}

bool PhysicalDeviceVnTable::Resync(DBEntry *e, const DBRequest *req) {
    PhysicalDeviceVn *entry = static_cast<PhysicalDeviceVn *>(e);
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());
    bool ret = entry->Copy(this, data);
    return ret;
}

bool PhysicalDeviceVnTable::Delete(DBEntry *e, const DBRequest *req) {
    PhysicalDeviceVn *entry = static_cast<PhysicalDeviceVn *>(e);
    entry->SendObjectLog(AgentLogEvent::DELETE);
    return true;
}

DBTableBase *PhysicalDeviceVnTable::CreateTable(DB *db,
                                                const std::string &name) {
    PhysicalDeviceVnTable *table = new PhysicalDeviceVnTable(db, name);
    table->Init();
    return table;
}

bool PhysicalDeviceVnTable::DeviceVnWalk(DBTablePartBase *partition,
                                         DBEntryBase *entry) {
    PhysicalDeviceVn *dev_vn = static_cast<PhysicalDeviceVn *>(entry);
    DBEntryBase::KeyPtr db_key = dev_vn->GetDBRequestKey();
    PhysicalDeviceVnKey *key =
        static_cast<PhysicalDeviceVnKey *>(db_key.release());
    key->sub_op_ = AgentKey::RESYNC;
    DBRequest req;
    req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    req.key.reset(key);
    req.data.reset(NULL);
    Process(req);
    return true;
}

void PhysicalDeviceVnTable::DeviceVnWalkDone(DBTableBase *part) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

void PhysicalDeviceVnTable::UpdateVxLanNetworkIdentifierMode() {
    DBTableWalker *walker = agent()->db()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        walker->WalkCancel(walkid_);
    }
    walkid_ = walker->WalkTable(this, NULL,
                          boost::bind(&PhysicalDeviceVnTable::DeviceVnWalk,
                                      this, _1, _2),
                          boost::bind(&PhysicalDeviceVnTable::DeviceVnWalkDone,
                                      this, _1));
}

//////////////////////////////////////////////////////////////////////////////
// Vmi Config handling routines
//////////////////////////////////////////////////////////////////////////////
void PhysicalDeviceVnTable::ProcessConfig(const boost::uuids::uuid &dev,
                                          const boost::uuids::uuid &vn) {
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalDeviceVnKey(dev, vn));
    req.data.reset(new PhysicalDeviceVnData());
    Enqueue(&req);
    return;
}

bool PhysicalDeviceVnTable::AddConfigEntry(const boost::uuids::uuid &vmi,
                                           const boost::uuids::uuid &dev,
                                           const boost::uuids::uuid &vn) {
    // Sanity checks. Needed since VMInterface is not checking for nil_uuid
    if (vmi == nil_uuid() || dev == nil_uuid() || vn == nil_uuid())
        return false;

    config_tree_.insert(PhysicalDeviceVnToVmi(dev, vn, vmi));
    agent()->config_manager()->AddPhysicalDeviceVn(dev, vn);
    return true;
}

bool PhysicalDeviceVnTable::DeleteConfigEntry(const boost::uuids::uuid &vmi,
                                              const boost::uuids::uuid &dev,
                                              const boost::uuids::uuid &vn) {

    // Sanity checks. Needed since VMInterface is not checking for nil_uuid
    if (vmi == nil_uuid() || dev == nil_uuid() || vn == nil_uuid())
        return false;

    config_tree_.erase(PhysicalDeviceVnToVmi(dev, vn, vmi));
    // Dont delete physical-device-vn entry if there are more entries in 
    // config-tree with given dev and vn
    ConfigTree::iterator it =
        config_tree_.upper_bound(PhysicalDeviceVnToVmi(dev, vn, nil_uuid()));
    bool del_entry = false;
    if (it == config_tree_.end())
        del_entry = true;
    else if (it->dev_ != dev)
        del_entry = true;
    else if (it->vn_ != vn)
        del_entry = true;
       
    if (del_entry) {
        agent()->config_manager()->DelPhysicalDeviceVn(dev, vn);
        DBRequest req(DBRequest::DB_ENTRY_DELETE);
        req.key.reset(new PhysicalDeviceVnKey(dev, vn));
        Enqueue(&req);
    }

    return del_entry;
}

bool PhysicalDeviceVnTable::IFNodeToReq(IFMapNode *node, DBRequest &req,
        const boost::uuids::uuid &u) {
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines
/////////////////////////////////////////////////////////////////////////////
class AgentPhysicalDeviceVnSandesh : public AgentSandesh {
 public:
    AgentPhysicalDeviceVnSandesh(const std::string &context,
                                 const std::string &dev,
                                 const std::string &vn)
        : AgentSandesh(context, ""), dev_str_(dev), vn_str_(vn) {
        dev_uuid_ = StringToUuid(dev);
        vn_uuid_ = StringToUuid(vn);
    }

    virtual bool Filter(const DBEntryBase *entry) {
        const PhysicalDeviceVn *dev_vn =
            static_cast<const PhysicalDeviceVn *>(entry);
        if (dev_str_.empty() == false) {
            if (dev_vn->device_uuid() != dev_uuid_)
                return false;
        }

        if (vn_str_.empty() == false) {
            if (dev_vn->vn_uuid() != vn_uuid_)
                return false;
        }

        return true;
    }

    virtual bool FilterToArgs(AgentSandeshArguments *args) {
        args->Add("device", dev_str_);
        args->Add("vn", vn_str_);
        return true;
    }

 private:
    DBTable *AgentGetTable() {
        Agent *agent = Agent::GetInstance();
        return static_cast<DBTable *>
            (agent->physical_device_vn_table());
    }
    void Alloc() {
        resp_ = new SandeshPhysicalDeviceVnListResp();
    }

    std::string dev_str_;
    boost::uuids::uuid dev_uuid_;
    std::string vn_str_;
    boost::uuids::uuid vn_uuid_;
};

static void SetPhysicalDeviceVnSandeshData(const PhysicalDeviceVn *entry,
                                           SandeshPhysicalDeviceVn *data) {
    data->set_device_uuid(UuidToString(entry->device_uuid()));
    if (entry->device()) {
        data->set_device(entry->device()->name());
    } else {
        data->set_device("INVALID");
    }
    data->set_vn_uuid(UuidToString(entry->vn_uuid()));
    if (entry->vn()) {
        data->set_vn(entry->vn()->GetName());
    } else {
        data->set_vn("INVALID");
    }
    data->set_vxlan_id(entry->vxlan_id());
}

bool PhysicalDeviceVn::DBEntrySandesh(Sandesh *resp, std::string &name)
    const {
    SandeshPhysicalDeviceVnListResp *port_resp =
        static_cast<SandeshPhysicalDeviceVnListResp *>(resp);

    SandeshPhysicalDeviceVn data;
    SetPhysicalDeviceVnSandeshData(this, &data);
    std::vector<SandeshPhysicalDeviceVn> &list =
        const_cast<std::vector<SandeshPhysicalDeviceVn>&>
        (port_resp->get_port_list());
    list.push_back(data);
    return true;
}

void SandeshPhysicalDeviceVnReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentPhysicalDeviceVnSandesh(context(),
                                                          get_device(),
                                                          get_vn()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr PhysicalDeviceVnTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context){
    return AgentSandeshPtr
        (new AgentPhysicalDeviceVnSandesh(context, args->GetString("device"),
                                          args->GetString("vn")));
}

void PhysicalDeviceVn::SendObjectLog(AgentLogEvent::type event) const {
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

    info.set_device_uuid(UuidToString(device_uuid_));
    if (device_.get()) {
        info.set_device(device_->name());
    } else {
        info.set_device("INVALID");
    }
    info.set_vn_uuid(UuidToString(vn_uuid_));
    if (vn_) {
        info.set_vn(vn_->GetName());
    } else {
        info.set_vn("INVALID");
    }
    info.set_ref_count(GetRefCount());
    PHYSICAL_DEVICE_VN_OBJECT_LOG_LOG("PhysicalDeviceVn",
                                      SandeshLevel::SYS_INFO, info);
}

/////////////////////////////////////////////////////////////////////////////
// Sandesh routines to dump config tree
/////////////////////////////////////////////////////////////////////////////
class ConfigPhysicalDeviceVnSandesh : public Task {
 public:
    ConfigPhysicalDeviceVnSandesh(Agent *agent, PhysicalDeviceVnTable *table,
                                  const string &key, const string &context) :
        Task(agent->task_scheduler()->GetTaskId("db::DBTable"), 0),
        table_(table), key_(key), context_(context) { }
    ~ConfigPhysicalDeviceVnSandesh() { }
    virtual bool Run();
    std::string Description() const { return "ConfigPhysicalDeviceVnSandesh"; }

 private:
    PhysicalDeviceVnTable *table_;
    string key_;
    string context_;
    DISALLOW_COPY_AND_ASSIGN(ConfigPhysicalDeviceVnSandesh);
};

bool ConfigPhysicalDeviceVnSandesh::Run() {
    SandeshConfigPhysicalDeviceVnListResp *resp =
        new SandeshConfigPhysicalDeviceVnListResp();
    std::vector<SandeshConfigPhysicalDeviceVn> &list =
        const_cast<std::vector<SandeshConfigPhysicalDeviceVn>&>
        (resp->get_device_vn_list());

    PhysicalDeviceVnTable::ConfigTree::const_iterator it =
        table_->config_tree().begin();
    while (it != table_->config_tree().end()){
        SandeshConfigPhysicalDeviceVn entry;
        entry.set_device_uuid(UuidToString(it->dev_));
        entry.set_vn_uuid(UuidToString(it->vn_));
        entry.set_vmi_uuid(UuidToString(it->vmi_));
        list.push_back(entry);
        it++;
    }
    resp->set_context(context_);
    resp->set_more(false);
    resp->Response();
    return true;
}

void SandeshConfigPhysicalDeviceVnReq::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    ConfigPhysicalDeviceVnSandesh *task =
        new ConfigPhysicalDeviceVnSandesh
        (agent, agent->physical_device_vn_table(),
         get_device(), context());
    agent->task_scheduler()->Enqueue(task);
}
