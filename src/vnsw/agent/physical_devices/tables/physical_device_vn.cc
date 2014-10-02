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

#include <physical_devices/tables/physical_devices_types.h>
#include <physical_devices/tables/physical_device.h>
#include <physical_devices/tables/physical_device_vn.h>

using AGENT::PhysicalDeviceVnEntry;
using AGENT::PhysicalDeviceVnTable;
using AGENT::PhysicalDeviceVnKey;
using AGENT::PhysicalDeviceVnData;

using std::string;

//////////////////////////////////////////////////////////////////////////////
// PhysicalDeviceVnEntry routines
//////////////////////////////////////////////////////////////////////////////
bool PhysicalDeviceVnEntry::IsLess(const DBEntry &rhs) const {
    const PhysicalDeviceVnEntry &a =
        static_cast<const PhysicalDeviceVnEntry &>(rhs);
    if (device_uuid_ != a.device_uuid_) {
        return (device_uuid_ < a.device_uuid_);
    }
    return (vn_uuid_ < a.vn_uuid_);
}

string PhysicalDeviceVnEntry::ToString() const {
    return UuidToString(device_uuid_) + ":" + UuidToString(vn_uuid_);
}

DBEntryBase::KeyPtr PhysicalDeviceVnEntry::GetDBRequestKey() const {
    PhysicalDeviceVnKey *key = new PhysicalDeviceVnKey(device_uuid_, vn_uuid_);
    return DBEntryBase::KeyPtr(key);
}

void PhysicalDeviceVnEntry::SetKey(const DBRequestKey *k) {
    const PhysicalDeviceVnKey *key =
        static_cast<const PhysicalDeviceVnKey *>(k);

    device_uuid_ = key->device_uuid_;
    vn_uuid_ = key->vn_uuid_;
}

bool PhysicalDeviceVnEntry::Copy(PhysicalDeviceVnTable *table,
                                 const PhysicalDeviceVnData *data) {
    bool ret = false;

    PhysicalDeviceEntry *dev =
        table->physical_device_table()->Find(device_uuid_);
    if (dev != device_.get()) {
        device_.reset(dev);
        ret = true;
    }

    VnEntry *vn = table->vn_table()->Find(vn_uuid_);
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

    PhysicalDeviceVnEntry *entry = new PhysicalDeviceVnEntry(key->device_uuid_,
                                                             key->vn_uuid_);
    return std::auto_ptr<DBEntry>(static_cast<DBEntry *>(entry));
}

DBEntry *PhysicalDeviceVnTable::Add(const DBRequest *req) {
    PhysicalDeviceVnKey *key =
        static_cast<PhysicalDeviceVnKey *>(req->key.get());
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());

    PhysicalDeviceVnEntry *entry = new PhysicalDeviceVnEntry(key->device_uuid_,
                                                             key->vn_uuid_);
    entry->Copy(this, data);
    entry->SendObjectLog(AgentLogEvent::ADD);
    return entry;
}

bool PhysicalDeviceVnTable::OnChange(DBEntry *e, const DBRequest *req) {
    PhysicalDeviceVnEntry *entry = static_cast<PhysicalDeviceVnEntry *>(e);
    PhysicalDeviceVnData *data =
        static_cast<PhysicalDeviceVnData *>(req->data.get());
    bool ret = entry->Copy(this, data);
    entry->SendObjectLog(AgentLogEvent::CHANGE);
    return ret;
}

void PhysicalDeviceVnTable::Delete(DBEntry *e, const DBRequest *req) {
    PhysicalDeviceVnEntry *entry = static_cast<PhysicalDeviceVnEntry *>(e);
    entry->SendObjectLog(AgentLogEvent::DELETE);
    return;
}

DBTableBase *PhysicalDeviceVnTable::CreateTable(DB *db,
                                                const std::string &name) {
    PhysicalDeviceVnTable *table = new PhysicalDeviceVnTable(db, name);
    table->Init();
    return table;
}

void PhysicalDeviceVnTable::RegisterDBClients(IFMapDependencyManager *dep) {
    physical_device_table_ = agent()->device_manager()->device_table();
    vn_table_ = agent()->vn_table();
}

//////////////////////////////////////////////////////////////////////////////
// Config handling routines
//////////////////////////////////////////////////////////////////////////////
/*
 * There is no IFMapNode for PhysicalDeviceVnEntry. The entries are built from
 * link between physical-router and virtual-network.
 *
 * We act on physical-router notification to build the PhysicalDeviceVnTable.
 * From a physical-router run thru all the links to find the virtual-networks.
 *
 * We dont get notification of link deletion between physical-router and
 * virtual-network. Instead, we build a config-tree and audit the tree based
 * on version-number to identify deleted entries
 */
void PhysicalDeviceVnTable::ConfigUpdate(IFMapNode *node) {
    config_version_++;

    autogen::PhysicalRouter *router = static_cast <autogen::PhysicalRouter *>
        (node->GetObject());
    assert(router);
    autogen::IdPermsType id_perms = router->id_perms();
    boost::uuids::uuid router_uuid;
    CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
               router_uuid);

    // Go thru virtual-networks linked and add/update them in the config tree
    IFMapAgentTable *table = static_cast<IFMapAgentTable *>(node->table());
    for (DBGraphVertex::adjacency_iterator iter =
         node->begin(table->GetGraph());
         iter != node->end(table->GetGraph()); ++iter) {
        IFMapNode *adj_node = static_cast<IFMapNode *>(iter.operator->());
        if (agent()->cfg_listener()->SkipNode(adj_node)) {
            continue;
        }

        if (strcmp(adj_node->table()->Typename(), "virtual-network") != 0) {
            continue;
        }
        autogen::VirtualNetwork *vn = static_cast <autogen::VirtualNetwork *>
            (adj_node->GetObject());
        assert(vn);
        autogen::IdPermsType id_perms = vn->id_perms();
        boost::uuids::uuid vn_uuid;
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong,
                   vn_uuid);

        PhysicalDeviceVnKey key(router_uuid, vn_uuid);
        if (config_tree_.find(key) == config_tree_.end()) {
            DBRequest req(DBRequest::DB_ENTRY_ADD_CHANGE);
            req.key.reset(new PhysicalDeviceVnKey(router_uuid, vn_uuid));
            req.data.reset(new PhysicalDeviceVnData());
            Enqueue(&req);
        }
        config_tree_[key] = config_version_;
    }

    // Audit and delete entries with old version-number in config-tree
    ConfigIterator it = config_tree_.begin();
    while (it != config_tree_.end()){
        ConfigIterator del_it = it++;
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
            (agent->device_manager()->physical_device_vn_table());
    }
    void Alloc() {
        resp_ = new SandeshPhysicalDeviceVnListResp();
    }
};

static void SetPhysicalDeviceVnSandeshData(const PhysicalDeviceVnEntry *entry,
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

bool PhysicalDeviceVnEntry::DBEntrySandesh(Sandesh *resp, std::string &name)
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
        (agent, agent->device_manager()->physical_device_vn_table(),
         get_device(), context());
    agent->task_scheduler()->Enqueue(task);
}
