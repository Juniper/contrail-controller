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
#include <oper/vn.h>
#include <oper/ifmap_dependency_manager.h>
#include <oper/physical_device.h>
#include <oper/physical_device_vn.h>

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

    VnEntry *vn = table->agent()->vn_table()->Find(vn_uuid_);
    if (vn != vn_.get()) {
        vn_.reset(vn);
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
    entry->SendObjectLog(AgentLogEvent::CHANGE);
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

//////////////////////////////////////////////////////////////////////////////
// Config handling routines
//////////////////////////////////////////////////////////////////////////////
/*
 * There is no IFMapNode for PhysicalDeviceVn. We act on physical-router
 * notification to build the PhysicalDeviceVnTable.
 *
 * From a physical-router run iterate thru the links given below,
 * <physical-router> - <phyiscal-interface> - <logical-interface> -
 * <virtual-machine-interface> - <virtual-network>
 *
 * Since there is no node for physical-device-vn, we build a config-tree and
 * audit the tree based on version-number to identify deleted entries
 */
void PhysicalDeviceVnTable::IterateConfig(const Agent *agent, const char *type,
                                          IFMapNode *node, AgentKey *key,
                                          AgentData *data,
                                          const boost::uuids::uuid &dev_uuid) {
    CfgListener *cfg = agent->cfg_listener();
    if (strcmp(type, "physical-interface") == 0) {
        cfg->ForEachAdjacentIFMapNode
            (agent, node, "logical-interface", NULL, NULL,
             boost::bind(&PhysicalDeviceVnTable::IterateConfig, this, _1, _2,
                         _3, _4, _5, dev_uuid));
        return;
    }

    if (strcmp(type, "logical-interface") != 0) {
        return;
    }

    IFMapNode *adj_node = NULL;
    adj_node = cfg->FindAdjacentIFMapNode(agent, node,
                                          "virtual-machine-interface");
    if (adj_node == NULL)
        return;

    adj_node = cfg->FindAdjacentIFMapNode(agent, adj_node, "virtual-network");
    if (adj_node == NULL)
        return;

    autogen::VirtualNetwork *vn = static_cast<autogen::VirtualNetwork *>
        (adj_node->GetObject());
    assert(vn);
    autogen::IdPermsType id_perms = vn->id_perms();
    boost::uuids::uuid vn_uuid;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
               vn_uuid);

    PhysicalDeviceVnKey vn_key(dev_uuid, vn_uuid);
    config_tree_[vn_key] = config_version_;
    DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
    req.key.reset(new PhysicalDeviceVnKey(dev_uuid, vn_uuid));
    req.data.reset(new PhysicalDeviceVnData());
    Enqueue(&req);
    return;
}

void PhysicalDeviceVnTable::ConfigUpdate(IFMapNode *node) {
    config_version_++;

    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);
    autogen::IdPermsType id_perms = router->id_perms();
    boost::uuids::uuid router_uuid = nil_uuid();
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
               router_uuid);

    if (!node->IsDeleted()) {
        agent()->cfg_listener()->ForEachAdjacentIFMapNode
            (agent(), node, "physical-interface", NULL, NULL,
             boost::bind(&PhysicalDeviceVnTable::IterateConfig, this, _1, _2,
                         _3, _4, _5, router_uuid));
    }

    // Audit and delete entries with old version-number in config-tree
    ConfigIterator it = config_tree_.begin();
    while (it != config_tree_.end()){
        ConfigIterator del_it = it++;
        if (del_it->first.device_uuid_ != router_uuid) {
            // update version number and skip entry if it belongs to different
            // physical router/device
            del_it->second = config_version_;
            continue;
        }
        if (del_it->second < config_version_) {
            DBRequest req(DBRequest::DB_ENTRY_DELETE);
            req.key.reset(new PhysicalDeviceVnKey(del_it->first.device_uuid_,
                                                  del_it->first.vn_uuid_));
            Enqueue(&req);
            config_tree_.erase(del_it);
        }
    }
}

void PhysicalDeviceVnTable::ConfigEventHandler(DBEntry *entry) {
}

bool PhysicalDeviceVnTable::IFNodeToReq(IFMapNode *node, DBRequest &req) {
    return false;
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
            (agent->physical_device_vn_table());
    }
    void Alloc() {
        resp_ = new SandeshPhysicalDeviceVnListResp();
    }
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
}

bool PhysicalDeviceVn::DBEntrySandesh(Sandesh *resp, std::string &name)
    const {
    SandeshPhysicalDeviceVnListResp *port_resp =
        static_cast<SandeshPhysicalDeviceVnListResp *>(resp);

    if (name.empty() || UuidToString(device_uuid_) == name) {
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
        (resp->get_port_list());

    resp->set_config_version(table_->config_version());
    PhysicalDeviceVnTable::ConfigTree::const_iterator it =
        table_->config_tree().begin();
    while (it != table_->config_tree().end()){
        SandeshConfigPhysicalDeviceVn entry;
        entry.set_device_uuid(UuidToString(it->first.device_uuid_));
        entry.set_vn_uuid(UuidToString(it->first.vn_uuid_));
        entry.set_version(it->second);
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
