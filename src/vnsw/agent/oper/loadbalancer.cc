/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "loadbalancer.h"

#include "ifmap/ifmap_node.h"
#include "oper/ifmap_dependency_manager.h"
#include "loadbalancer_properties.h"
#include <oper/agent_sandesh.h>
#include <oper/agent_types.h>

class LoadbalancerData : public AgentData {
  public:
    typedef LoadbalancerProperties Properties;

    LoadbalancerData(IFMapNode *node)
        : node_(node) {
    }

    LoadbalancerData(const Properties &properties)
         : node_(NULL), properties_(properties) {
    }

    IFMapNode *node() { return node_; }

    const Properties &properties() { return properties_; }

  private:
    IFMapNode *node_;
    Properties properties_;
};

Loadbalancer::Loadbalancer() {
}

Loadbalancer::~Loadbalancer() {
}



bool Loadbalancer::IsLess(const DBEntry &rhs) const {
    const Loadbalancer &lb = static_cast<const Loadbalancer &>(rhs);
    return uuid_ < lb.uuid_;
}

std::string Loadbalancer::ToString() const {
    return UuidToString(uuid_);
}

void Loadbalancer::SetKey(const DBRequestKey *key) {
    const LoadbalancerKey *lbkey = static_cast<const LoadbalancerKey *>(key);
    uuid_ = lbkey->instance_id();
}

DBEntryBase::KeyPtr Loadbalancer::GetDBRequestKey() const {
    LoadbalancerKey *key = new LoadbalancerKey(uuid_);
    return KeyPtr(key);
}

void Loadbalancer::set_properties(const Properties &properties) {
    properties_.reset(new Properties(properties));
}

const Loadbalancer::Properties *Loadbalancer::properties() const {
    return properties_.get();
}

static boost::uuids::uuid IdPermsGetUuid(const autogen::IdPermsType &id) {
    boost::uuids::uuid uuid;
    CfgUuidSet(id.uuid.uuid_mslong, id.uuid.uuid_lslong, uuid);
    return uuid;
}

static void PropertiesAddMember(
    IFMapNode *node, LoadbalancerProperties *properties) {
    autogen::LoadbalancerMember *member =
            static_cast<autogen::LoadbalancerMember *>(node->GetObject());
    properties->members()->insert(
        std::make_pair(IdPermsGetUuid(member->id_perms()),
                       member->properties()));
}

static void PropertiesAddHealthmonitor(
    IFMapNode *node, LoadbalancerProperties *properties) {
    autogen::LoadbalancerHealthmonitor *healthmon =
            static_cast<autogen::LoadbalancerHealthmonitor *>(
                node->GetObject());
    properties->healthmonitors()->insert(
        std::make_pair(IdPermsGetUuid(healthmon->id_perms()),
                       healthmon->properties()));
}


bool Loadbalancer::DBEntrySandesh(Sandesh *sresp, std::string &name) const {
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
    for (LoadbalancerProperties::MemberMap::const_iterator iter =
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

AgentSandeshPtr LoadbalancerTable::GetAgentSandesh
(const AgentSandeshArguments *args, const std::string &context) {
    return AgentSandeshPtr
        (new AgentLoadBalancerSandesh(context, args->GetString("name")));
}

/*
 * LoadbalancerTable class
 */
LoadbalancerTable::LoadbalancerTable(DB *db, const std::string &name)
        : AgentDBTable(db, name),
          graph_(NULL), dependency_manager_(NULL) {
}

LoadbalancerTable::~LoadbalancerTable() {
}

#if 0
void LoadbalancerTable::Clear() {

    DBEntry *db, *db_next;
    DBTablePartition *partition = static_cast<DBTablePartition *>(
                    GetTablePartition(0));
    for (db = partition->GetFirst(); db!= NULL; db = db_next) {
        db_next = partition->GetNext(db);
        if (db->IsDeleted()) {
            continue;
        }
        partition->Delete(db);
    }
}
#endif

std::auto_ptr<DBEntry> LoadbalancerTable::AllocEntry(
    const DBRequestKey *key) const {
    std::auto_ptr<DBEntry> entry(new Loadbalancer());
    entry->SetKey(key);
    return entry;
}

DBEntry *LoadbalancerTable::Add(const DBRequest *request) {
    Loadbalancer *loadbalancer = new Loadbalancer();
    loadbalancer->SetKey(request->key.get());
    LoadbalancerData *data =
            static_cast<LoadbalancerData *>(request->data.get());
    assert(dependency_manager_);
    loadbalancer->SetIFMapNodeState(
            dependency_manager_->SetState(data->node()));
    dependency_manager_->SetObject(data->node(), loadbalancer);

    assert(graph_);
    LoadbalancerProperties properties;
    CalculateProperties(graph_, data->node(), &properties);
    loadbalancer->set_properties(properties);
    return loadbalancer;
}

bool LoadbalancerTable::Delete(DBEntry *entry, const DBRequest *request) {
    Loadbalancer *loadbalancer  = static_cast<Loadbalancer *>(entry);
    assert(dependency_manager_);
    if (loadbalancer->ifmap_node()) {
        dependency_manager_->SetObject(loadbalancer->ifmap_node(), NULL);
        loadbalancer->SetIFMapNodeState(NULL);
    }
    return true;
}

bool LoadbalancerTable::OnChange(DBEntry *entry, const DBRequest *request) {
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(entry);

    LoadbalancerData *data = static_cast<LoadbalancerData *>(
        request->data.get());

    loadbalancer->SetKey(request->key.get());
    if (data->node() == NULL) {
        loadbalancer->set_properties(data->properties());
    } else {
        assert(graph_);
        LoadbalancerProperties properties;
        CalculateProperties(graph_, data->node(), &properties);
        loadbalancer->set_properties(properties);
    }
    return true;
}

void LoadbalancerTable::Initialize(
    DBGraph *graph, IFMapDependencyManager *dependency_manager) {

    graph_ = graph;
    dependency_manager_ = dependency_manager;

    dependency_manager_->Register(
        "loadbalancer-pool",
        boost::bind(&LoadbalancerTable::ChangeEventHandler, this, _1, _2));
}

bool LoadbalancerTable::IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u) {
    autogen::LoadbalancerPool *pool =
        static_cast<autogen::LoadbalancerPool *>(node->GetObject());
    const autogen::IdPermsType &id = pool->id_perms();
    u = IdPermsGetUuid(id);
    return true;
}

bool LoadbalancerTable::IFNodeToReq(IFMapNode *node, DBRequest &request,
        boost::uuids::uuid &id) {


    assert(boost::uuids::nil_uuid() != id);
    request.key.reset(new LoadbalancerKey(id));

    if (request.oper == DBRequest::DB_ENTRY_DELETE || node->IsDeleted()) {
        request.oper = DBRequest::DB_ENTRY_DELETE;
        return true;
    }

    request.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

    IFMapNodeState *state = dependency_manager_->IFMapNodeGet(node);
    const LoadbalancerProperties *current = NULL;
    LoadbalancerProperties properties;
    assert(graph_);
    CalculateProperties(graph_, node, &properties);
    Loadbalancer *loadbalancer = static_cast<Loadbalancer *>(state->object());
    if (!loadbalancer || loadbalancer->uuid() != id) {
        request.data.reset(new LoadbalancerData(node));
    } else {
        current = loadbalancer->properties();
        if (current) {
            if (properties.CompareTo(*current) == 0)
                return false;
        }

        LOG(DEBUG, "loadbalancer property change "
                            << properties.DiffString(current));
        request.data.reset(new LoadbalancerData(properties));
    }
    return true;
}

void LoadbalancerTable::CalculateProperties(DBGraph *graph, IFMapNode
        *node, LoadbalancerProperties *properties) {
    autogen::LoadbalancerPool *pool =
            static_cast<autogen::LoadbalancerPool *>(node->GetObject());
    properties->set_pool_properties(pool->properties());

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());

        const char *adjtype = adj->table()->Typename();
        if (strcmp(adjtype, "virtual-ip") == 0) {
            autogen::VirtualIp *vip =
                    static_cast<autogen::VirtualIp *>(adj->GetObject());
            properties->set_vip_uuid(IdPermsGetUuid(vip->id_perms()));
            properties->set_vip_properties(vip->properties());
        } else if (strcmp(adjtype, "loadbalancer-member") == 0) {
            PropertiesAddMember(adj, properties);
        } else if (strcmp(adjtype, "loadbalancer-healthmonitor") == 0) {
            PropertiesAddHealthmonitor(adj, properties);
        }
    }
}

void LoadbalancerTable::ChangeEventHandler(IFMapNode *node, DBEntry *entry) {

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

DBTableBase *LoadbalancerTable::CreateTable(
    DB *db, const std::string &name) {
    LoadbalancerTable *table = new LoadbalancerTable(db, name);
    table->Init();
    return table;
}
