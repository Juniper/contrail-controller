/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "loadbalancer_pool.h"

#include "ifmap/ifmap_node.h"
#include "oper/ifmap_dependency_manager.h"
#include "loadbalancer_pool_info.h"
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>

class LoadbalancerPoolData : public AgentData {
  public:
    LoadbalancerPoolData(IFMapNode *node)
        : node_(node) {
    }

    IFMapNode *node() { return node_; }

  private:
    IFMapNode *node_;
};

LoadbalancerPool::LoadbalancerPool() {
}

LoadbalancerPool::~LoadbalancerPool() {
}



bool LoadbalancerPool::IsLess(const DBEntry &rhs) const {
    const LoadbalancerPool &lb = static_cast<const LoadbalancerPool &>(rhs);
    return uuid_ < lb.uuid_;
}

std::string LoadbalancerPool::ToString() const {
    return UuidToString(uuid_);
}

void LoadbalancerPool::SetKey(const DBRequestKey *key) {
    const LoadbalancerPoolKey *lbkey = static_cast<const LoadbalancerPoolKey *>
        (key);
    uuid_ = lbkey->instance_id();
}

DBEntryBase::KeyPtr LoadbalancerPool::GetDBRequestKey() const {
    LoadbalancerPoolKey *key = new LoadbalancerPoolKey(uuid_);
    return KeyPtr(key);
}

void LoadbalancerPool::set_properties(const Properties &properties) {
    properties_.reset(new Properties(properties));
}

const LoadbalancerPool::Properties *LoadbalancerPool::properties() const {
    return properties_.get();
}

static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static void PropertiesAddMember(
    IFMapNode *node, LoadBalancerPoolInfo *properties) {
    autogen::LoadbalancerMember *member =
            static_cast<autogen::LoadbalancerMember *>(node->GetObject());
    properties->members()->insert(
        std::make_pair(IdPermsGetUuid(member->id_perms()),
                       member->properties()));
}

static void PropertiesAddHealthmonitor(
    IFMapNode *node, LoadBalancerPoolInfo *properties) {
    autogen::LoadbalancerHealthmonitor *healthmon =
            static_cast<autogen::LoadbalancerHealthmonitor *>(
                node->GetObject());
    properties->healthmonitors()->insert(
        std::make_pair(IdPermsGetUuid(healthmon->id_perms()),
                       healthmon->properties()));
}

bool LoadbalancerPool::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
    LoadBalancerResp *resp = static_cast<LoadBalancerResp*> (sresp);

    std::string str_uuid = UuidToString(uuid_);
    if (! name.empty() && str_uuid != name) {
        return false;
    }

    LoadBalancerSandeshData data;
    data.set_uuid(str_uuid);
    const autogen::VirtualIpType &vip =
        properties_.get()->vip_properties();
    data.set_vip_address(vip.address);
    data.set_port(vip.protocol_port);

    const autogen::LoadbalancerPoolType &pool = properties()->pool_properties();
    data.set_mode(pool.protocol);
    data.set_balance(pool.loadbalancer_method);

    if (properties()->healthmonitors().size()) {
         const autogen::LoadbalancerHealthmonitorType &hm =
                 properties()->healthmonitors().begin()->second;
        data.set_inter(hm.timeout * 1000);
        data.set_fall(hm.max_retries);
        data.set_rise(1);
        if (!hm.expected_codes.empty()) {
            data.set_expected_codes(hm.expected_codes);
            if (!hm.http_method.empty()) {
                data.set_http_method(hm.http_method);
            }
            if (!hm.url_path.empty()) {
                data.set_url_path(hm.url_path);
            }
        }
    }

    std::vector<std::string> members;
    for (LoadBalancerPoolInfo::MemberMap::const_iterator iter =
                 properties()->members().begin();
         iter != properties()->members().end(); ++iter) {
        autogen::LoadbalancerMemberType member = iter->second;
        members.push_back(member.address);
    }
    data.set_member_list(members);

    std::vector<LoadBalancerSandeshData> &list =
            const_cast<std::vector<LoadBalancerSandeshData>&>
            (resp->get_load_balancer_list());
    list.push_back(data);

    return true;
}

void LoadBalancerReq::HandleRequest() const {
    AgentSandeshPtr sand(new AgentLoadBalancerSandesh(context(),get_uuid()));
    sand->DoSandesh(sand);
}

AgentSandeshPtr LoadbalancerPoolTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr
        (new AgentLoadBalancerSandesh(context, args->GetString("name")));
}

/*
 * LoadbalancerPoolTable class
 */
LoadbalancerPoolTable::LoadbalancerPoolTable(DB *db, const std::string &name)
        : AgentDBTable(db, name),
          graph_(NULL), dependency_manager_(NULL) {
}

LoadbalancerPoolTable::~LoadbalancerPoolTable() {
}

std::auto_ptr<DBEntry> LoadbalancerPoolTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new LoadbalancerPool());
    entry->SetKey(key);
    return entry;
}

DBEntry *LoadbalancerPoolTable::Add(const DBRequest *request) {
    LoadbalancerPool *loadbalancer = new LoadbalancerPool();
    LoadbalancerPool::Type type;
    loadbalancer->SetKey(request->key.get());
    LoadbalancerPoolData *data =
            static_cast<LoadbalancerPoolData *>(request->data.get());
    assert(dependency_manager_);
    loadbalancer->SetIFMapNodeState(
            dependency_manager_->SetState(data->node()));
    dependency_manager_->SetObject(data->node(), loadbalancer);

    assert(graph_);
    LoadBalancerPoolInfo properties;
    CalculateProperties(graph_, data->node(), &properties, &type);
    loadbalancer->set_properties(properties);
    loadbalancer->set_type(type);
    return loadbalancer;
}

bool LoadbalancerPoolTable::Delete(DBEntry *entry, const DBRequest *request) {
    LoadbalancerPool *loadbalancer  = static_cast<LoadbalancerPool *>(entry);
    assert(dependency_manager_);
    if (loadbalancer->ifmap_node()) {
        dependency_manager_->SetObject(loadbalancer->ifmap_node(), NULL);
        loadbalancer->SetIFMapNodeState(NULL);
    }
    return true;
}

bool LoadbalancerPoolTable::OnChange(DBEntry *entry, const DBRequest *request) {
    LoadbalancerPool *loadbalancer = static_cast<LoadbalancerPool *>(entry);
    LoadbalancerPool::Type type;
    bool ret = false;

    LoadbalancerPoolData *data = static_cast<LoadbalancerPoolData *>(
        request->data.get());

    assert(graph_);
    LoadBalancerPoolInfo properties;
    CalculateProperties(graph_, data->node(), &properties, &type);

    loadbalancer->SetKey(request->key.get());
    const LoadBalancerPoolInfo *current = loadbalancer->properties();
    if (current) {
        if (properties.CompareTo(*current) != 0) {
            loadbalancer->set_properties(properties);
            ret = true;
        }
    } else {
        loadbalancer->set_properties(properties);
        ret = true;
    }
    if (loadbalancer->type() != type) {
        loadbalancer->set_type(type);
        ret = true;
    }
    return ret;
}

void LoadbalancerPoolTable::Initialize(
    DBGraph *graph, IFMapDependencyManager *dependency_manager) {

    graph_ = graph;
    dependency_manager_ = dependency_manager;

    dependency_manager_->Register(
        "loadbalancer-pool",
        boost::bind(&LoadbalancerPoolTable::ChangeEventHandler, this, _1, _2));
}

bool LoadbalancerPoolTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::LoadbalancerPool *pool =
        static_cast<autogen::LoadbalancerPool *>(node->GetObject());
    const autogen::IdPermsType &id = pool->id_perms();
    u = IdPermsGetUuid(id);
    return true;
}

bool LoadbalancerPoolTable::IFNodeToReq(IFMapNode *node, DBRequest &request,
        const boost::uuids::uuid &id) {


    assert(!id.is_nil());
    request.key.reset(new LoadbalancerPoolKey(id));

    if (request.oper == DBRequest::DB_ENTRY_DELETE || node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    request.data.reset(new LoadbalancerPoolData(node));
    return true;
}

void LoadbalancerPoolTable::CalculateProperties(DBGraph *graph, IFMapNode
        *node, LoadBalancerPoolInfo *properties,
        LoadbalancerPool::Type *type) {
    autogen::LoadbalancerPool *pool =
            static_cast<autogen::LoadbalancerPool *>(node->GetObject());
    properties->set_pool_properties(pool->properties());
    properties->set_custom_attributes(pool->custom_attributes());
    *type = LoadbalancerPool::INVALID;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        const char *adjtype = adj->table()->Typename();
        if (strcmp(adjtype, "virtual-ip") == 0) {
            autogen::VirtualIp *vip =
                    static_cast<autogen::VirtualIp *>(adj->GetObject());
            properties->set_vip_uuid(IdPermsGetUuid(vip->id_perms()));
            properties->set_vip_properties(vip->properties());
            *type = LoadbalancerPool::LBAAS_V1;
        } else if (strcmp(adjtype, "loadbalancer-listener") == 0) {
            *type = LoadbalancerPool::LBAAS_V2;
        } else if (strcmp(adjtype, "loadbalancer-member") == 0) {
            PropertiesAddMember(adj, properties);
        } else if (strcmp(adjtype, "loadbalancer-healthmonitor") == 0) {
            PropertiesAddHealthmonitor(adj, properties);
        }
    }
}

void LoadbalancerPoolTable::ChangeEventHandler(IFMapNode *node, DBEntry *entry) {

    DBRequest req;
    boost::uuids::uuid new_uuid;
    IFNodeToUuid(node, new_uuid);
    IFMapNodeState *state = dependency_manager_->IFMapNodeGet(node);
    boost::uuids::uuid old_uuid = state->uuid();

    if (!node->IsDeleted()) {
        if (entry) {
            if ((old_uuid != new_uuid)) {
                if (old_uuid != boost::uuids::nil_uuid()) {
                    req.oper = DBRequest::DB_ENTRY_DELETE;
                    if (IFNodeToReq(node, req, old_uuid) == true) {
                        assert(req.oper == DBRequest::DB_ENTRY_DELETE);
                        Enqueue(&req);
                    }
                }
            }
        }
        state->set_uuid(new_uuid);
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
        new_uuid = old_uuid;
    }

    if (IFNodeToReq(node, req, new_uuid) == true) {
        Enqueue(&req);
    }
}

DBTableBase *LoadbalancerPoolTable::CreateTable(
    DB *db, const std::string &name) {
    LoadbalancerPoolTable *table = new LoadbalancerPoolTable(db, name);
    table->Init();
    return table;
}
